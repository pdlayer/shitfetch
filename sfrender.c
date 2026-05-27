#define _POSIX_C_SOURCE 200809L

#include "sf.h"
#include "sfcolor.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
build_identity(char *user_out, size_t user_cap, char *host_out, size_t host_cap)
{
	const char *user = getenv("USER");
	char host[128];
	char *dot;

	if (user_cap == 0 || host_cap == 0)
		return;
	if (user == NULL || user[0] == '\0')
		user = "user";
	if (gethostname(host, sizeof(host)) != 0) {
		snprintf(host, sizeof(host), "host");
	} else {
		host[sizeof(host) - 1] = '\0';
		dot = strchr(host, '.');
		if (dot != NULL)
			*dot = '\0';
	}
	snprintf(user_out, user_cap, "%s", user);
	snprintf(host_out, host_cap, "%s", host);
}

static void
format_value(char *out, size_t out_cap, const char *value, const char *value_color)
{
	size_t i = 0;
	size_t o = 0;

	if (out_cap == 0)
		return;
	if (value == NULL) {
		out[0] = '\0';
		return;
	}

	while (value[i] != '\0' && o + 1 < out_cap) {
		if (value[i] == '%' && value[i + 1] == '{') {
			size_t j = i + 2;
			int pct = 0;
			bool ok = false;
			int color = 32;
			char seq[64];
			int n;

			while (value[j] >= '0' && value[j] <= '9') {
				ok = true;
				pct = pct * 10 + (value[j] - '0');
				j++;
			}
			if (ok && value[j] == '}') {
				if (pct >= 80)
					color = 31;
				else if (pct >= 50)
					color = 33;
				n = snprintf(seq, sizeof(seq), "\033[%dm%d%%\033[%sm", color, pct, value_color);
				if (n > 0) {
					size_t copy = (size_t)n;
					if (copy > out_cap - o - 1)
						copy = out_cap - o - 1;
					memcpy(out + o, seq, copy);
					o += copy;
				}
				i = j + 1;
				continue;
			}
		}
		out[o++] = value[i++];
	}
	out[o] = '\0';
}

static void
apply_template(const char *fmt, const char *value, char *out, size_t out_cap)
{
	size_t i = 0;
	size_t o = 0;

	if (out_cap == 0)
		return;
	if (fmt == NULL || fmt[0] == '\0') {
		snprintf(out, out_cap, "%s", value ? value : "");
		return;
	}
	while (fmt[i] != '\0' && o + 1 < out_cap) {
		if (fmt[i] == '{' && fmt[i + 1] == '}') {
			size_t n = value ? strlen(value) : 0;

			if (n > out_cap - o - 1)
				n = out_cap - o - 1;
			if (n > 0)
				memcpy(out + o, value, n);
			o += n;
			i += 2;
			continue;
		}
		out[o++] = fmt[i++];
	}
	out[o] = '\0';
}

static void
append_sized_text(char *dst, size_t cap, size_t *o, const char *src, int width, bool lower)
{
	size_t i = 0;
	size_t n = 0;

	if (src == NULL)
		src = "";
	while (src[i] != '\0' && *o + 1 < cap && (width <= 0 || (int)n < width)) {
		char c = src[i++];

		if (lower)
			c = (char)tolower((unsigned char)c);
		dst[*o] = c;
		(*o)++;
		n++;
	}
	if (width > 0) {
		while ((int)n < width && *o + 1 < cap) {
			dst[*o] = ' ';
			(*o)++;
			n++;
		}
	}
}

static const char *
custom_token_value(const char *name, const struct shitfetch_data *data, const char *user, const char *host)
{
	if (strcmp(name, "user") == 0)
		return user;
	if (strcmp(name, "host") == 0)
		return host;
	if (strcmp(name, "os") == 0)
		return data->os_pretty;
	if (strcmp(name, "osid") == 0 || strcmp(name, "os-id") == 0)
		return data->os_id;
	if (strcmp(name, "kernel") == 0)
		return data->kernel;
	if (strcmp(name, "init") == 0 || strcmp(name, "ini") == 0)
		return data->init;
	if (strcmp(name, "wm") == 0 || strcmp(name, "dewm") == 0)
		return data->dewm;
	if (strcmp(name, "term") == 0 || strcmp(name, "terminal") == 0)
		return data->term;
	return "";
}

static void
build_custom_line(const char *src, const struct shitfetch_data *data, const char *user, const char *host,
	char *out, size_t out_cap)
{
	size_t i = 0;
	size_t o = 0;

	if (out_cap == 0)
		return;
	while (src[i] != '\0' && o + 1 < out_cap) {
		if (src[i] == '{') {
			char expr[128];
			char name[64];
			char *p;
			char *save;
			char *part;
			int width = 0;
			bool lower = false;
			size_t j = i + 1;
			size_t k = 0;
			const char *value;

			while (src[j] != '\0' && src[j] != '}' && k + 1 < sizeof(expr))
				expr[k++] = src[j++];
			if (src[j] == '}') {
				expr[k] = '\0';
				name[0] = '\0';
				p = expr;
				part = strtok_r(p, ":", &save);
				if (part != NULL)
					snprintf(name, sizeof(name), "%s", part);
				for (p = name; *p != '\0'; p++)
					*p = (char)tolower((unsigned char)*p);
				while ((part = strtok_r(NULL, ":", &save)) != NULL) {
					char low[32];
					size_t m;

					snprintf(low, sizeof(low), "%s", part);
					for (m = 0; low[m] != '\0'; m++)
						low[m] = (char)tolower((unsigned char)low[m]);
					if (strcmp(low, "lower") == 0)
						lower = true;
					else {
						char *endp = NULL;
						long v = strtol(low, &endp, 10);

						if (endp != NULL && *endp == '\0' && v > 0 && v < 1000)
							width = (int)v;
					}
				}
				value = custom_token_value(name, data, user, host);
				append_sized_text(out, out_cap, &o, value, width, lower);
				i = j + 1;
				continue;
			}
		}
		out[o++] = src[i++];
	}
	out[o] = '\0';
}

static void
add_line(struct shitfetch_info_line *lines, size_t *count, size_t cap, const char *key_color, const char *separator,
	const char *value_color, const char *key, const char *value, const char *fmt)
{
	char templ[SHITFETCH_MAX_LINE * 2];
	char value_fmt[SHITFETCH_MAX_LINE * 2];

	if (*count >= cap)
		return;
	apply_template(fmt, value, templ, sizeof(templ));
	format_value(value_fmt, sizeof(value_fmt), templ, value_color);
	int n = snprintf(lines[*count].text, sizeof(lines[*count].text), "\033[1;%sm%s\033[0m%s\033[%sm%s\033[0m",
		key_color, key, separator, value_color, value_fmt);
	if (n < 0 || (size_t)n >= sizeof(lines[*count].text)) {
		// Truncated, but at least we tried
	}
	(*count)++;
}

static void
add_palette_lines(struct shitfetch_info_line *lines, size_t *count, size_t cap)
{
	int j;
	size_t idx;
	char *p;
	size_t left;

	if (*count >= cap)
		return;
	idx = *count;
	p = lines[idx].text;
	left = sizeof(lines[idx].text);
	p[0] = '\0';
	for (j = 0; j < 8; j++) {
		int n = snprintf(p, left, "\033[%dm   \033[0m", 40 + j);
		if (n <= 0 || (size_t)n >= left)
			break;
		p += (size_t)n;
		left -= (size_t)n;
	}
	(*count)++;

	if (*count >= cap)
		return;
	idx = *count;
	p = lines[idx].text;
	left = sizeof(lines[idx].text);
	p[0] = '\0';
	for (j = 0; j < 8; j++) {
		int n = snprintf(p, left, "\033[%dm   \033[0m", 100 + j);
		if (n <= 0 || (size_t)n >= left)
			break;
		p += (size_t)n;
		left -= (size_t)n;
	}
	(*count)++;
}

static const char *
default_module_key(enum shitfetch_module module)
{
	switch (module) {
	case SHITFETCH_MODULE_OS:
		return "os";
	case SHITFETCH_MODULE_KERNEL:
		return "kernel";
	case SHITFETCH_MODULE_INIT:
		return "init";
	case SHITFETCH_MODULE_UPTIME:
		return "uptime";
	case SHITFETCH_MODULE_HOST:
		return "host";
	case SHITFETCH_MODULE_SHELL:
		return "shell";
	case SHITFETCH_MODULE_DEWM:
		return "wm/de";
	case SHITFETCH_MODULE_TERM:
		return "terminal";
	case SHITFETCH_MODULE_CPU:
		return "cpu";
	case SHITFETCH_MODULE_GPU:
		return "gpu";
	case SHITFETCH_MODULE_MEMORY:
		return "memory";
	case SHITFETCH_MODULE_SWAP:
		return "swap";
	case SHITFETCH_MODULE_DISK:
		return "disk";
	case SHITFETCH_MODULE_PACKAGES:
		return "pkgs";
	case SHITFETCH_MODULE_DISPLAY:
		return "display";
	default:
		return "unknown";
	}
}

static const char *
module_key(const struct shitfetch_settings *settings, enum shitfetch_module module)
{
	(void)settings;
	return default_module_key(module);
}

static const char *
module_value(enum shitfetch_module module, const struct shitfetch_data *data)
{
	switch (module) {
	case SHITFETCH_MODULE_OS:
		return data->os_pretty;
	case SHITFETCH_MODULE_KERNEL:
		return data->kernel;
	case SHITFETCH_MODULE_INIT:
		return data->init;
	case SHITFETCH_MODULE_UPTIME:
		return data->uptime;
	case SHITFETCH_MODULE_HOST:
		return data->host;
	case SHITFETCH_MODULE_SHELL:
		return data->shell;
	case SHITFETCH_MODULE_DEWM:
		return data->dewm;
	case SHITFETCH_MODULE_TERM:
		return data->term;
	case SHITFETCH_MODULE_CPU:
		return data->cpu;
	case SHITFETCH_MODULE_GPU:
		return data->gpu;
	case SHITFETCH_MODULE_MEMORY:
		return data->memory;
	case SHITFETCH_MODULE_SWAP:
		return data->swap;
	case SHITFETCH_MODULE_PACKAGES:
		return data->packages;
	case SHITFETCH_MODULE_DISPLAY:
		return data->display;
	case SHITFETCH_MODULE_DISK:
		return data->disk;
	default:
		return "unknown";
	}
}

size_t
shitfetch_build_info_lines(const struct shitfetch_settings *settings, const char *key_color,
	const struct shitfetch_data *data, struct shitfetch_info_line *lines, size_t cap)
{
	size_t i;
	size_t count = 0;
	enum shitfetch_module module;
	char fallback_key_color[16];
	char fallback_value_color[16];
	char key_color_resolved[32];
	char value_color_resolved[32];
	char custom_color_resolved[32];
	const char *effective_key_color;
	const char *effective_value_color;
	const char *effective_custom_color = NULL;
	char identity_user[128];
	char identity_host[128];

	if (key_color == NULL || key_color[0] == '\0') {
		snprintf(fallback_key_color, sizeof(fallback_key_color), "%d", settings->key_color);
		key_color = fallback_key_color;
	}
	effective_key_color = key_color;
	if (settings->key_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->key_color_spec, key_color, key_color_resolved, sizeof(key_color_resolved)))
		effective_key_color = key_color_resolved;
	if (settings->value_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->value_color_spec, key_color, value_color_resolved, sizeof(value_color_resolved))) {
		effective_value_color = value_color_resolved;
	} else {
		snprintf(fallback_value_color, sizeof(fallback_value_color), "%d", settings->value_color);
		effective_value_color = fallback_value_color;
	}
	if (settings->custom_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->custom_color_spec, key_color, custom_color_resolved, sizeof(custom_color_resolved)))
		effective_custom_color = custom_color_resolved;
	build_identity(identity_user, sizeof(identity_user), identity_host, sizeof(identity_host));

	for (i = 0; i < settings->entry_count; i++) {
		const char *entry_key_color = effective_key_color;
		const char *entry_separator = settings->separator[0] ? settings->separator : ": ";
		char entry_key_color_resolved[32];

		if (!settings->entries[i].enabled)
			continue;
		if (settings->entries[i].kind == SHITFETCH_ENTRY_BREAK) {
			if (count < cap) {
				lines[count].text[0] = '\0';
				count++;
			}
			continue;
		}
		if (settings->entries[i].kind == SHITFETCH_ENTRY_SEPARATOR) {
			if (count < cap) {
				snprintf(lines[count].text, sizeof(lines[count].text), "%s", settings->entries[i].text);
				count++;
			}
			continue;
		}
		if (settings->entries[i].kind == SHITFETCH_ENTRY_CUSTOM) {
			if (count < cap) {
				build_custom_line(settings->entries[i].text, data, identity_user, identity_host,
					lines[count].text, sizeof(lines[count].text));
				if (effective_custom_color != NULL) {
					char temp[SHITFETCH_MAX_LINE];

					snprintf(temp, sizeof(temp), "%s", lines[count].text);
					snprintf(lines[count].text, sizeof(lines[count].text), "\033[%sm%s\033[0m",
						effective_custom_color, temp);
				}
				count++;
			}
			continue;
		}
		if (settings->entries[i].kind == SHITFETCH_ENTRY_COLORS) {
			if (settings->show_ansi)
				add_palette_lines(lines, &count, cap);
			continue;
		}
		if (settings->entries[i].kind != SHITFETCH_ENTRY_MODULE)
			continue;
		module = settings->entries[i].module;
		if (module < 0 || module >= SHITFETCH_MODULE_COUNT)
			continue;
		if (!settings->module_enabled[module])
			continue;
		if (settings->entries[i].key_color_set && settings->entries[i].key_color[0] != '\0' &&
			sfcolor_resolve(settings->entries[i].key_color, key_color,
				entry_key_color_resolved, sizeof(entry_key_color_resolved)))
			entry_key_color = entry_key_color_resolved;

		if (module == SHITFETCH_MODULE_DISK) {
			size_t d;

			for (d = 0; d < data->disk_count && count < cap; d++) {
				char key[128];

				if (settings->entries[i].key_set)
					snprintf(key, sizeof(key), "%s (%s)", settings->entries[i].key, data->disk_mounts[d]);
				else
					snprintf(key, sizeof(key), "%s (%s)", module_key(settings, module), data->disk_mounts[d]);
				add_line(lines, &count, cap, entry_key_color, entry_separator, effective_value_color,
					key, data->disk_values[d], settings->entries[i].format_set ? settings->entries[i].format : NULL);
			}
			continue;
		}
		if (module == SHITFETCH_MODULE_GPU) {
			size_t g;
			const char *gpu_key = settings->entries[i].key_set ?
				settings->entries[i].key : module_key(settings, module);

			if (data->gpu_count == 0) {
				add_line(lines, &count, cap, entry_key_color, entry_separator, effective_value_color,
					gpu_key,
					module_value(module, data),
					settings->entries[i].format_set ? settings->entries[i].format : NULL);
				continue;
			}

			for (g = 0; g < data->gpu_count && count < cap; g++) {
				add_line(lines, &count, cap, entry_key_color, entry_separator, effective_value_color,
					gpu_key, data->gpu_values[g],
					settings->entries[i].format_set ? settings->entries[i].format : NULL);
			}
			continue;
		}
		if (module == SHITFETCH_MODULE_DISPLAY && !settings->entries[i].key_set && data->display_id[0] != '\0') {
			char key[96];

			snprintf(key, sizeof(key), "%s (%s)", module_key(settings, module), data->display_id);
			add_line(lines, &count, cap, entry_key_color, entry_separator, effective_value_color,
				key, module_value(module, data), settings->entries[i].format_set ? settings->entries[i].format : NULL);
			continue;
		}
		add_line(lines, &count, cap, entry_key_color, entry_separator, effective_value_color,
			settings->entries[i].key_set ? settings->entries[i].key : module_key(settings, module),
			module_value(module, data),
			settings->entries[i].format_set ? settings->entries[i].format : NULL);
	}

	return count;
}

void
shitfetch_render(const struct shitfetch_settings *settings,
	const struct shitfetch_logo_line *logo_lines, size_t logo_count,
	const struct shitfetch_info_line *info_lines, size_t info_count,
	const char *key_color)
{
	size_t i;
	size_t rows;
	size_t max_logo = 0;
	size_t ansi_rows = 0;
	size_t header_rows = settings->show_header ?
		(settings->template == SHITFETCH_TEMPLATE_MINI ? 1 : 2) : 0;
	char identity_user[128];
	char identity_host[128];
	size_t dash_len;
	char dash[256];
	bool has_colors_entry = false;
	char header_color[32];
	char border_color[32];
	const char *effective_header_color;
	const char *effective_border_color;

	for (i = 0; i < logo_count; i++) {
		if (logo_lines[i].visible_len > max_logo)
			max_logo = logo_lines[i].visible_len;
	}
	for (i = 0; i < settings->entry_count; i++) {
		if (settings->entries[i].kind == SHITFETCH_ENTRY_COLORS && settings->entries[i].enabled) {
			has_colors_entry = true;
			break;
		}
	}
	if (settings->show_ansi && !has_colors_entry)
		ansi_rows = 3;
	if (key_color == NULL || key_color[0] == '\0')
		key_color = "36";
	effective_header_color = key_color;
	if (settings->header_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->header_color_spec, key_color, header_color, sizeof(header_color)))
		effective_header_color = header_color;
	effective_border_color = "38;5;244";
	if (settings->border_color_spec[0] != '\0' &&
		sfcolor_resolve(settings->border_color_spec, key_color, border_color, sizeof(border_color)))
		effective_border_color = border_color;
	build_identity(identity_user, sizeof(identity_user), identity_host, sizeof(identity_host));
	dash_len = strlen(identity_user) + 1 + strlen(identity_host);
	if (dash_len >= sizeof(dash))
		dash_len = sizeof(dash) - 1;
	memset(dash, '-', dash_len);
	dash[dash_len] = '\0';

	rows = logo_count;
	if (header_rows + info_count + ansi_rows > rows)
		rows = header_rows + info_count + ansi_rows;
	for (i = 0; i < rows; i++) {
		bool has_info = i >= header_rows && (i - header_rows) < info_count;

		if (i < logo_count)
			fputs(logo_lines[i].text, stdout);
		if (settings->show_header && i < logo_count && i < header_rows &&
			(settings->template != SHITFETCH_TEMPLATE_MINI || i == 0)) {
			size_t pad;

			pad = max_logo > logo_lines[i].visible_len ? max_logo - logo_lines[i].visible_len : 0;
			printf("%*s", (int)pad + 2, "");
			if (i == 0) {
				printf("\033[1;%sm", effective_header_color);
				fputs(identity_user, stdout);
				fputs("\033[0m", stdout);
				printf("@%s", identity_host);
			} else {
				printf("\033[%sm%s\033[0m", effective_border_color, dash);
			}
		} else if (settings->show_header && i >= logo_count && i < header_rows &&
			(settings->template != SHITFETCH_TEMPLATE_MINI || i == 0)) {
			if (logo_count > 0)
				printf("%*s", (int)max_logo + 2, "");
			if (i == 0) {
				printf("\033[1;%sm", effective_header_color);
				fputs(identity_user, stdout);
				fputs("\033[0m", stdout);
				printf("@%s", identity_host);
			} else {
				printf("\033[%sm%s\033[0m", effective_border_color, dash);
			}
		} else if (i < logo_count && has_info) {
			size_t pad;

			pad = max_logo > logo_lines[i].visible_len ? max_logo - logo_lines[i].visible_len : 0;
			printf("%*s", (int)pad + 2, "");
			fputs(info_lines[i - header_rows].text, stdout);
		} else if (i >= logo_count && has_info) {
			if (logo_count > 0)
				printf("%*s", (int)max_logo + 2, "");
			fputs(info_lines[i - header_rows].text, stdout);
		} else if (settings->show_ansi && i >= header_rows + info_count + 1 && i < header_rows + info_count + 3) {
			int j;

			if (logo_count > 0 && i < logo_count) {
				size_t pad;

				pad = max_logo > logo_lines[i].visible_len ? max_logo - logo_lines[i].visible_len : 0;
				printf("%*s", (int)pad + 2, "");
			} else if (logo_count > 0) {
				printf("%*s", (int)max_logo + 2, "");
			}
			for (j = 0; j < 8; j++) {
				if (i == header_rows + info_count + 1)
					printf("\033[%dm   \033[0m", 40 + j);
				else
					printf("\033[%dm   \033[0m", 100 + j);
			}
		}
		putchar('\n');
	}
}
