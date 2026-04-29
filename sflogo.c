#define _POSIX_C_SOURCE 200809L

#include "sf.h"
#include "sfcolor.h"
#include "sfansi.h"
#include "sfminiansi.h"
#include "sfutil.h"
#include "sfascii.h"
#include "sfminiascii.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void
normalize_logo_name(const char *src, char *dst, size_t cap)
{
	size_t i;

	if (src == NULL || src[0] == '\0') {
		snprintf(dst, cap, "linux");
		return;
	}
	snprintf(dst, cap, "%s", src);
	shitfetch_strlower(dst);
	for (i = 0; dst[i] != '\0'; i++) {
		if (dst[i] == '-' || dst[i] == ' ')
			dst[i] = '_';
	}
}

static const char *
find_builtin_logo_text(const struct shitfetch_settings *settings, const char *name)
{
	size_t i;

	if (settings->template == SHITFETCH_TEMPLATE_MINI) {
		for (i = 0; i < sfmini_ascii_entries_count; i++) {
			if (strcmp(sfmini_ascii_entries[i].name, name) == 0)
				return sfmini_ascii_entries[i].text;
		}
	} else {
		for (i = 0; i < sfascii_entries_count; i++) {
			if (strcmp(sfascii_entries[i].name, name) == 0)
				return sfascii_entries[i].text;
		}
	}

	return NULL;
}

static const char *
find_logo_text(const struct shitfetch_settings *settings, const char *name, char **file_buf)
{
	const char *builtin;
	char path[SHITFETCH_MAX_PATH];
	FILE *fp;

	if (file_buf)
		*file_buf = NULL;

	builtin = find_builtin_logo_text(settings, name);
	if (builtin != NULL)
		return builtin;

	snprintf(path, sizeof(path), "%s/%s", settings->ascii_dir, name);
	fp = fopen(path, "r");
	if (fp) {
		fseek(fp, 0, SEEK_END);
		long len = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (len > 0 && file_buf) {
			*file_buf = malloc(len + 1);
			if (*file_buf) {
				size_t n = fread(*file_buf, 1, len, fp);
				(*file_buf)[n] = '\0';
				fclose(fp);
				return *file_buf;
			}
		}
		fclose(fp);
	}

	return NULL;
}

static void
palette_for_logo(const struct shitfetch_settings *settings, const char *logo_name, const char *out[9])
{
	if (settings->template == SHITFETCH_TEMPLATE_MINI)
		sfmini_palette_for_os(logo_name, out);
	else
		sfcolor_palette_for_os(logo_name, out);
}

static const char *
logo_main_color_from_text(const char *logo_text, const char *logo_colors[9])
{
	size_t counts[9] = {0};
	int current = 0;
	int best = 0;
	const char *p;
	int i;

	if (logo_text == NULL)
		return logo_colors[0];

	p = logo_text;
	while (*p != '\0') {
		if (*p == '\n' || *p == '\r') {
			p++;
			continue;
		}

		if (*p == '\033') {
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
				continue;
			}
			continue;
		}

		if (*p == '$') {
			char n = *(p + 1);

			if (n == '$') {
				counts[current]++;
				p += 2;
				continue;
			}
			if (n >= '1' && n <= '9') {
				current = n - '1';
				p += 2;
				continue;
			}
		}

		counts[current]++;
		p++;
	}

	for (i = 1; i < 9; i++) {
		if (counts[i] > counts[best])
			best = i;
	}

	return logo_colors[best];
}

size_t
shitfetch_load_logo(const struct shitfetch_settings *settings, const char *os_id,
	struct shitfetch_logo_line *lines, size_t cap)
{
	const char *logo_text;
	char *file_buf = NULL;
	char logo_name[64];
	const char *logo_colors[9];
	const char *logo_override[9];
	char logo_color[32];
	const char *p;
	size_t count = 0;
	size_t i;

	if (!settings->show_logo)
		return 0;

	if (strcmp(settings->logo, "auto") == 0)
		normalize_logo_name(os_id, logo_name, sizeof(logo_name));
	else
		normalize_logo_name(settings->logo, logo_name, sizeof(logo_name));

	logo_text = find_logo_text(settings, logo_name, &file_buf);
	if (logo_text == NULL)
		logo_text = find_logo_text(settings, "linux", &file_buf);
	if (logo_text == NULL)
		return 0;

	palette_for_logo(settings, logo_name, logo_colors);
	if (settings->logo_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->logo_color_spec, logo_colors[0], logo_color, sizeof(logo_color))) {
		for (i = 0; i < 9; i++)
			logo_override[i] = logo_color;
		logo_colors[0] = logo_override[0];
		logo_colors[1] = logo_override[1];
		logo_colors[2] = logo_override[2];
		logo_colors[3] = logo_override[3];
		logo_colors[4] = logo_override[4];
		logo_colors[5] = logo_override[5];
		logo_colors[6] = logo_override[6];
		logo_colors[7] = logo_override[7];
		logo_colors[8] = logo_override[8];
	}
	p = logo_text;

	while (*p != '\0' && count < cap) {
		struct shitfetch_logo_line *line = &lines[count];
		char raw[SHITFETCH_MAX_LINE];
		size_t n = 0;

		while (*p != '\0' && *p != '\n' && n < sizeof(raw) - 1)
			raw[n++] = *p++;
		if (*p == '\n')
			p++;
		raw[n] = '\0';
		sfansi_parse_logo_line(raw, logo_colors, line->text, sizeof(line->text), &line->visible_len);
		count++;
	}
	if (file_buf) free(file_buf);
	return count;
}

void
shitfetch_logo_main_color(const struct shitfetch_settings *settings, const char *os_id,
	char *out, size_t out_cap)
{
	char logo_name[64];
	char *file_buf = NULL;
	const char *logo_text;
	const char *logo_colors[9];

	if (out_cap == 0)
		return;
	out[0] = '\0';

	if (!settings->show_logo) {
		snprintf(out, out_cap, "%d", settings->key_color);
		return;
	}

	if (strcmp(settings->logo, "auto") == 0)
		normalize_logo_name(os_id, logo_name, sizeof(logo_name));
	else
		normalize_logo_name(settings->logo, logo_name, sizeof(logo_name));

	logo_text = find_logo_text(settings, logo_name, &file_buf);
	palette_for_logo(settings, logo_name, logo_colors);

	const char *chosen = logo_main_color_from_text(logo_text, logo_colors);

	if (settings->logo_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->logo_color_spec, chosen, out, out_cap)) {
		if (file_buf) free(file_buf);
		return;
	}

	snprintf(out, out_cap, "%s", chosen ? chosen : "36");
	if (file_buf) free(file_buf);
}
