#define _POSIX_C_SOURCE 200809L

#include "sf.h"
#include "sfcolor.h"
#include "sfansi.h"
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
		dst[0] = '\0';
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
find_builtin_logo_text_for_template(enum shitfetch_template template, const char *name)
{
	size_t i;

	if (template == SHITFETCH_TEMPLATE_MINI) {
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
find_builtin_logo_text(const struct shitfetch_settings *settings, const char *name)
{
	return find_builtin_logo_text_for_template(settings->template, name);
}

static const char *
find_logo_text(const struct shitfetch_settings *settings, const char *name, char **file_buf)
{
	const char *builtin;
	char path[SHITFETCH_MAX_PATH];
	FILE *fp;

	if (file_buf)
		*file_buf = NULL;
	if (name[0] == '\0')
		return NULL;

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

/* Picks the art for this run and reports the name it was found under, so the palette follows
   the logo that is actually drawn. A --logo naming something unknown falls back to the
   running distribution instead of a generic penguin; if that art is missing too the caller
   gets nothing and prints no logo at all. */
static const char *
resolve_logo_text(const struct shitfetch_settings *settings, const char *os_id,
	char *name, size_t name_cap, char **file_buf)
{
	char distro[64];
	const char *text;

	if (strcmp(settings->logo, "auto") == 0)
		normalize_logo_name(os_id, name, name_cap);
	else
		normalize_logo_name(settings->logo, name, name_cap);

	text = find_logo_text(settings, name, file_buf);
	if (text != NULL)
		return text;

	normalize_logo_name(os_id, distro, sizeof(distro));
	if (strcmp(distro, name) == 0)
		return NULL;
	text = find_logo_text(settings, distro, file_buf);
	if (text != NULL)
		snprintf(name, name_cap, "%s", distro);
	return text;
}

static void
palette_for_logo(const struct shitfetch_settings *settings, const char *logo_name, const char *out[9])
{
	(void)settings;
	sfcolor_palette_for_os(logo_name, out);
}

/* Resolves the palette for a logo and applies the logo_color_spec override, which
   collapses all nine slots onto a single color. */
static void
resolve_palette(const struct shitfetch_settings *settings, const char *logo_name,
	char out[9][32])
{
	const char *logo_colors[9];
	char logo_color[32];
	bool overridden;
	size_t i;

	palette_for_logo(settings, logo_name, logo_colors);
	overridden = settings->logo_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->logo_color_spec, logo_colors[0], logo_color, sizeof(logo_color));
	for (i = 0; i < 9; i++) {
		const char *src = overridden ? logo_color : logo_colors[i];

		snprintf(out[i], 32, "%s", src != NULL ? src : "39");
	}
}

static size_t
utf8_decode(const char *s, unsigned int *cp)
{
	const unsigned char *p = (const unsigned char *)s;
	unsigned int c = p[0];
	size_t len;
	size_t i;

	if (c < 0x80) {
		*cp = c;
		return 1;
	}
	if ((c & 0xE0) == 0xC0) {
		len = 2;
		c &= 0x1Fu;
	} else if ((c & 0xF0) == 0xE0) {
		len = 3;
		c &= 0x0Fu;
	} else if ((c & 0xF8) == 0xF0) {
		len = 4;
		c &= 0x07u;
	} else {
		*cp = 0xFFFDu;
		return 1;
	}
	for (i = 1; i < len; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*cp = 0xFFFDu;
			return 1;
		}
		c = (c << 6) | (p[i] & 0x3Fu);
	}
	*cp = c;
	return len;
}

/* Walks the raw logo text once. With fill == false it only measures rows/cols so the
   caller can size the allocation; with fill == true it writes cells. */
static void
grid_scan(const char *text, struct shitfetch_logo_grid *grid, bool fill)
{
	const char *p = text;
	size_t row = 0;
	size_t col = 0;
	unsigned char color = 0;

	while (*p != '\0' && row < SHITFETCH_MAX_LOGO_LINES) {
		unsigned int cp;
		size_t adv;

		if (*p == '\n') {
			if (col > grid->cols)
				grid->cols = col;
			row++;
			col = 0;
			color = 0;
			p++;
			continue;
		}
		if (*p == '\r') {
			p++;
			continue;
		}
		if (*p == '\033') {
			p++;
			if (*p == '[') {
				p++;
				while (*p != '\0' && !((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
					p++;
				if (*p != '\0')
					p++;
			}
			continue;
		}
		if (*p == '$' && p[1] >= '1' && p[1] <= '9') {
			color = (unsigned char)(p[1] - '1');
			p += 2;
			continue;
		}
		if (*p == '$' && p[1] == '$') {
			cp = '$';
			adv = 2;
		} else {
			adv = utf8_decode(p, &cp);
		}
		p += adv;
		if (col < SHITFETCH_MAX_LOGO_COLS) {
			if (fill && cp != ' ') {
				struct shitfetch_logo_cell *cell = &grid->cells[row * grid->cols + col];

				cell->cp = cp;
				cell->color_idx = color;
			}
			col++;
		}
	}
	if (col > 0) {
		if (col > grid->cols)
			grid->cols = col;
		row++;
	}
	if (!fill)
		grid->rows = row;
}

int
shitfetch_load_logo_grid(const struct shitfetch_settings *settings, const char *os_id,
	struct shitfetch_logo_grid *out)
{
	const char *logo_text;
	char *file_buf = NULL;
	char logo_name[64];

	memset(out, 0, sizeof(*out));
	if (!settings->show_logo)
		return -1;

	logo_text = resolve_logo_text(settings, os_id, logo_name, sizeof(logo_name), &file_buf);
	if (logo_text == NULL)
		return -1;

	resolve_palette(settings, logo_name, out->palette);
	grid_scan(logo_text, out, false);
	if (out->rows == 0 || out->cols == 0) {
		free(file_buf);
		return -1;
	}
	out->cells = calloc(out->rows * out->cols, sizeof(*out->cells));
	if (out->cells == NULL) {
		free(file_buf);
		return -1;
	}
	grid_scan(logo_text, out, true);
	free(file_buf);
	return 0;
}

void
shitfetch_logo_grid_free(struct shitfetch_logo_grid *grid)
{
	if (grid == NULL)
		return;
	free(grid->cells);
	grid->cells = NULL;
	grid->rows = 0;
	grid->cols = 0;
}

size_t
shitfetch_load_logo(const struct shitfetch_settings *settings, const char *os_id,
	struct shitfetch_logo_line *lines, size_t cap)
{
	const char *logo_text;
	char *file_buf = NULL;
	char logo_name[64];
	char palette[9][32];
	const char *logo_colors[9];
	const char *p;
	size_t count = 0;
	size_t i;

	if (!settings->show_logo)
		return 0;

	logo_text = resolve_logo_text(settings, os_id, logo_name, sizeof(logo_name), &file_buf);
	if (logo_text == NULL)
		return 0;

	resolve_palette(settings, logo_name, palette);
	for (i = 0; i < 9; i++)
		logo_colors[i] = palette[i];
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
	const char *logo_colors[9];
	const char *chosen;

	if (out_cap == 0)
		return;
	out[0] = '\0';

	if (!settings->show_logo) {
		snprintf(out, out_cap, "%d", settings->key_color);
		return;
	}

	if (resolve_logo_text(settings, os_id, logo_name, sizeof(logo_name), &file_buf) == NULL)
		normalize_logo_name(os_id, logo_name, sizeof(logo_name));
	free(file_buf);

	palette_for_logo(settings, logo_name, logo_colors);
	chosen = logo_colors[0];

	if (settings->logo_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->logo_color_spec, chosen, out, out_cap))
		return;

	snprintf(out, out_cap, "%s", chosen ? chosen : "36");
}
