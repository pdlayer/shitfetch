#include "sfansi.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void
append_bytes(char *dst, size_t cap, const char *src, size_t n)
{
	size_t have;

	if (cap == 0)
		return;
	have = strlen(dst);
	if (have >= cap - 1)
		return;
	if (n > cap - have - 1)
		n = cap - have - 1;
	memcpy(dst + have, src, n);
	dst[have + n] = '\0';
}

static void
append_cstr(char *dst, size_t cap, const char *src)
{
	append_bytes(dst, cap, src, strlen(src));
}

static size_t
utf8_seq_len(const char *s)
{
	unsigned char c = (unsigned char)s[0];
	size_t len;
	size_t i;

	if ((c & 0xE0u) == 0xC0u)
		len = 2;
	else if ((c & 0xF0u) == 0xE0u)
		len = 3;
	else if ((c & 0xF8u) == 0xF0u)
		len = 4;
	else
		return 1;
	for (i = 1; i < len; i++) {
		if (((unsigned char)s[i] & 0xC0u) != 0x80u)
			return i;
	}
	return len;
}

/* Appends one whole character or nothing at all, so a line that runs out of room still ends
   on a complete UTF-8 sequence. Returns false when the cell was dropped, which keeps the
   reported width in step with what actually landed in the buffer. */
static bool
append_cell(char *dst, size_t cap, const char *src, size_t n)
{
	size_t have;

	if (cap == 0)
		return false;
	have = strlen(dst);
	if (have + n > cap - 1)
		return false;
	memcpy(dst + have, src, n);
	dst[have + n] = '\0';
	return true;
}

void
sfansi_parse_logo_line(const char *input, const char *palette[9],
	char *output, size_t output_cap, size_t *visible_len)
{
	const char *p = input;
	const char esc = '\033';
	size_t seq;

	if (output_cap == 0)
		return;
	output[0] = '\0';
	if (visible_len != NULL)
		*visible_len = 0;

	append_cstr(output, output_cap, "\033[");
	append_cstr(output, output_cap, palette[0]);
	append_cstr(output, output_cap, "m");

	while (*p != '\0') {
		if (*p == '\n' || *p == '\r') {
			p++;
			continue;
		}

		if (*p == esc) {
			const char *start = p;
			p++;
			if (*p == '[') {
				p++;
				while (*p != '\0') {
					if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
						p++;
						break;
					}
					p++;
				}
				append_bytes(output, output_cap, start, (size_t)(p - start));
				continue;
			}
			append_bytes(output, output_cap, start, 1);
			continue;
		}

		if (*p == '$') {
			char n = *(p + 1);

			if (n == '$') {
				if (append_cell(output, output_cap, "$", 1) && visible_len != NULL)
					(*visible_len)++;
				p += 2;
				continue;
			}
			if (n >= '1' && n <= '9') {
				int idx = n - '1';

				append_cstr(output, output_cap, "\033[");
				append_cstr(output, output_cap, palette[idx]);
				append_cstr(output, output_cap, "m");
				p += 2;
				continue;
			}
		}

		seq = utf8_seq_len(p);
		if (append_cell(output, output_cap, p, seq) && visible_len != NULL)
			(*visible_len)++;
		p += seq;
	}

	append_cstr(output, output_cap, "\033[0m");
}
