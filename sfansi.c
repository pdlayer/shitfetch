#include "sfansi.h"

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

void
sfansi_parse_logo_line(const char *input, const char *palette[9],
	char *output, size_t output_cap, size_t *visible_len)
{
	const char *p = input;
	const char esc = '\033';

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
				append_cstr(output, output_cap, "$");
				if (visible_len != NULL)
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

		append_bytes(output, output_cap, p, 1);
		if (visible_len != NULL)
			(*visible_len)++;
		p++;
	}

	append_cstr(output, output_cap, "\033[0m");
}
