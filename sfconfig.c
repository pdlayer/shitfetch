#define _POSIX_C_SOURCE 200809L

#include "sf.h"
#include "sfutil.h"

#include <ctype.h>
#include <errno.h>
#include <kdl/parser.h>
#include <kdl/value.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const enum shitfetch_module default_order[] = {
	SHITFETCH_MODULE_OS,
	SHITFETCH_MODULE_KERNEL,
	SHITFETCH_MODULE_INIT,
	SHITFETCH_MODULE_UPTIME,
	SHITFETCH_MODULE_PACKAGES,
	SHITFETCH_MODULE_SHELL,
	SHITFETCH_MODULE_DISPLAY,
	SHITFETCH_MODULE_DEWM,
	SHITFETCH_MODULE_TERM,
	SHITFETCH_MODULE_CPU,
	SHITFETCH_MODULE_GPU,
	SHITFETCH_MODULE_MEMORY,
	SHITFETCH_MODULE_SWAP,
	SHITFETCH_MODULE_DISK,
};

static bool
kdl_to_cstr(kdl_str s, char *out, size_t cap)
{
	size_t len;

	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	if (s.data == NULL)
		return false;
	len = s.len;
	if (len >= cap)
		len = cap - 1;
	memcpy(out, s.data, len);
	out[len] = '\0';
	return true;
}

static bool
kdl_value_to_bool(const kdl_value *value, bool *out)
{
	if (value == NULL || out == NULL)
		return false;
	if (value->type == KDL_TYPE_BOOLEAN) {
		*out = value->boolean;
		return true;
	}
	if (value->type == KDL_TYPE_NUMBER && value->number.type == KDL_NUMBER_TYPE_INTEGER) {
		*out = value->number.integer != 0;
		return true;
	}
	if (value->type == KDL_TYPE_STRING) {
		char s[16];

		if (!kdl_to_cstr(value->string, s, sizeof(s)))
			return false;
		shitfetch_strlower(s);
		*out = strcmp(s, "true") == 0 || strcmp(s, "yes") == 0 || strcmp(s, "on") == 0 || strcmp(s, "1") == 0;
		return true;
	}
	return false;
}

static bool
kdl_value_to_int(const kdl_value *value, int *out)
{
	if (value == NULL || out == NULL)
		return false;
	if (value->type == KDL_TYPE_NUMBER && value->number.type == KDL_NUMBER_TYPE_INTEGER) {
		*out = (int)value->number.integer;
		return true;
	}
	if (value->type == KDL_TYPE_STRING) {
		char s[32];
		char *endp = NULL;
		long parsed;

		if (!kdl_to_cstr(value->string, s, sizeof(s)))
			return false;
		parsed = strtol(s, &endp, 10);
		if (endp == s || endp == NULL || *endp != '\0')
			return false;
		*out = (int)parsed;
		return true;
	}
	return false;
}

const char *
shitfetch_module_name(enum shitfetch_module module)
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
		return "dewm";
	case SHITFETCH_MODULE_TERM:
		return "term";
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
		return "packages";
	case SHITFETCH_MODULE_DISPLAY:
		return "display";
	default:
		return "unknown";
	}
}

bool
shitfetch_module_from_name(const char *name, enum shitfetch_module *out)
{
	char lower[64];
	enum shitfetch_module module;

	if (name == NULL || out == NULL)
		return false;
	snprintf(lower, sizeof(lower), "%s", name);
	shitfetch_strlower(lower);
	for (module = 0; module < SHITFETCH_MODULE_COUNT; module++) {
		if (strcmp(lower, shitfetch_module_name(module)) == 0) {
			*out = module;
			return true;
		}
	}
	if (strcmp(lower, "de") == 0 || strcmp(lower, "wm") == 0 || strcmp(lower, "de/wm") == 0) {
		*out = SHITFETCH_MODULE_DEWM;
		return true;
	}
	if (strcmp(lower, "ini") == 0) {
		*out = SHITFETCH_MODULE_INIT;
		return true;
	}
	if (strcmp(lower, "terminal") == 0) {
		*out = SHITFETCH_MODULE_TERM;
		return true;
	}
	if (strcmp(lower, "resolution") == 0) {
		*out = SHITFETCH_MODULE_DISPLAY;
		return true;
	}
	return false;
}

static void
reset_modules_once(struct shitfetch_settings *settings, bool *modules_reset)
{
	size_t i;

	if (*modules_reset)
		return;
	for (i = 0; i < SHITFETCH_MODULE_COUNT; i++)
		settings->module_enabled[i] = false;
	settings->module_count = 0;
	settings->entry_count = 0;
	*modules_reset = true;
}

static int
append_module(struct shitfetch_settings *settings, enum shitfetch_module module)
{
	size_t i;
	size_t idx = (size_t)-1;

	for (i = 0; i < settings->module_count; i++) {
		if (settings->module_order[i] == module)
			goto add_entry;
	}
	if (settings->module_count >= SHITFETCH_MAX_MODULES)
		return -1;
	settings->module_order[settings->module_count++] = module;
	settings->module_enabled[module] = true;

add_entry:
	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return -1;
	idx = settings->entry_count++;
	settings->entries[idx].kind = SHITFETCH_ENTRY_MODULE;
	settings->entries[idx].module = module;
	settings->entries[idx].enabled = true;
	settings->entries[idx].key_set = false;
	settings->entries[idx].key[0] = '\0';
	settings->entries[idx].key_color_set = false;
	settings->entries[idx].key_color[0] = '\0';
	settings->entries[idx].format_set = false;
	settings->entries[idx].format[0] = '\0';
	settings->entries[idx].text[0] = '\0';
	return (int)idx;
}

static int
append_break_entry(struct shitfetch_settings *settings)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return -1;
	idx = settings->entry_count++;
	settings->entries[idx].kind = SHITFETCH_ENTRY_BREAK;
	settings->entries[idx].module = SHITFETCH_MODULE_COUNT;
	settings->entries[idx].enabled = true;
	settings->entries[idx].key_set = false;
	settings->entries[idx].key_color_set = false;
	settings->entries[idx].format_set = false;
	settings->entries[idx].text[0] = '\0';
	return (int)idx;
}

static int
append_separator_entry(struct shitfetch_settings *settings)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return -1;
	idx = settings->entry_count++;
	settings->entries[idx].kind = SHITFETCH_ENTRY_SEPARATOR;
	settings->entries[idx].module = SHITFETCH_MODULE_COUNT;
	settings->entries[idx].enabled = true;
	settings->entries[idx].key_set = false;
	settings->entries[idx].key_color_set = false;
	settings->entries[idx].format_set = false;
	snprintf(settings->entries[idx].text, sizeof(settings->entries[idx].text), "%s", settings->separator);
	return (int)idx;
}

static int
append_custom_entry(struct shitfetch_settings *settings)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return -1;
	idx = settings->entry_count++;
	settings->entries[idx].kind = SHITFETCH_ENTRY_CUSTOM;
	settings->entries[idx].module = SHITFETCH_MODULE_COUNT;
	settings->entries[idx].enabled = true;
	settings->entries[idx].key_set = false;
	settings->entries[idx].key_color_set = false;
	settings->entries[idx].format_set = false;
	settings->entries[idx].text[0] = '\0';
	return (int)idx;
}

static int
append_colors_entry(struct shitfetch_settings *settings)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return -1;
	idx = settings->entry_count++;
	settings->entries[idx].kind = SHITFETCH_ENTRY_COLORS;
	settings->entries[idx].module = SHITFETCH_MODULE_COUNT;
	settings->entries[idx].enabled = true;
	settings->entries[idx].key_set = false;
	settings->entries[idx].key_color_set = false;
	settings->entries[idx].format_set = false;
	settings->entries[idx].text[0] = '\0';
	return (int)idx;
}

void
shitfetch_settings_init(struct shitfetch_settings *settings)
{
	size_t i;

	memset(settings, 0, sizeof(*settings));
	settings->show_logo = true;
	settings->show_header = true;
	snprintf(settings->logo, sizeof(settings->logo), "auto");
	settings->show_ansi = true;
	settings->key_color = 36;
	settings->value_color = 39;
	snprintf(settings->key_color_spec, sizeof(settings->key_color_spec), "logo");
	snprintf(settings->separator, sizeof(settings->separator), ": ");
	settings->disk_all = true;
	settings->disk_show_fs = true;
	settings->disk_mount_filter_count = 0;
	snprintf(settings->ascii_dir, sizeof(settings->ascii_dir), "%s", SHITFETCH_ASCII_DIR);

	for (i = 0; i < SHITFETCH_MODULE_COUNT; i++)
		settings->module_enabled[i] = true;
	settings->module_count = sizeof(default_order) / sizeof(default_order[0]);
	for (i = 0; i < settings->module_count; i++)
		settings->module_order[i] = default_order[i];
	settings->entry_count = 0;
	for (i = 0; i < settings->module_count; i++)
		(void)append_module(settings, settings->module_order[i]);
}

static bool
read_whole_file(const char *path, char **out)
{
	FILE *fp;
	long size;
	char *buf;
	size_t read_size;

	*out = NULL;
	fp = fopen(path, "rb");
	if (fp == NULL)
		return false;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return false;
	}
	size = ftell(fp);
	if (size < 0) {
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return false;
	}
	buf = calloc((size_t)size + 1, 1);
	if (buf == NULL) {
		fclose(fp);
		return false;
	}
	read_size = fread(buf, 1, (size_t)size, fp);
	fclose(fp);
	if (read_size != (size_t)size) {
		free(buf);
		return false;
	}
	buf[size] = '\0';
	*out = buf;
	return true;
}

bool
shitfetch_load_config(struct shitfetch_settings *settings, const char *path)
{
	char *doc;
	kdl_parser *parser;
	kdl_event_data *ev;
	int depth = 0;
	char node[64] = {0};
	char child[64] = {0};
	bool modules_reset = false;
	int current_entry = -1;

	if (path == NULL || path[0] == '\0')
		return false;
	if (!shitfetch_file_exists(path))
		return false;
	if (!read_whole_file(path, &doc))
		return false;

	parser = kdl_create_string_parser(kdl_str_from_cstr(doc), KDL_DEFAULTS);
	if (parser == NULL) {
		free(doc);
		return false;
	}

	for (;;) {
		ev = kdl_parser_next_event(parser);
		if (ev == NULL)
			break;
		if (ev->event == KDL_EVENT_EOF || ev->event == KDL_EVENT_PARSE_ERROR)
			break;

		if ((ev->event & 0xffff) == KDL_EVENT_START_NODE) {
			depth++;
			if (depth == 1) {
				kdl_to_cstr(ev->name, node, sizeof(node));
				shitfetch_strlower(node);
				child[0] = '\0';
				current_entry = -1;
				if (strcmp(node, "modules") == 0)
					reset_modules_once(settings, &modules_reset);
			} else if (depth == 2) {
				kdl_to_cstr(ev->name, child, sizeof(child));
				shitfetch_strlower(child);
				if (strcmp(node, "modules") == 0) {
					enum shitfetch_module module;

					if (strcmp(child, "break") == 0)
						current_entry = append_break_entry(settings);
					else if (strcmp(child, "separator") == 0)
						current_entry = append_separator_entry(settings);
					else if (strcmp(child, "custom") == 0)
						current_entry = append_custom_entry(settings);
					else if (strcmp(child, "colors") == 0)
						current_entry = append_colors_entry(settings);
					else if (shitfetch_module_from_name(child, &module))
						current_entry = append_module(settings, module);
					else
						current_entry = -1;
				}
			}
			continue;
		}

		if ((ev->event & 0xffff) == KDL_EVENT_END_NODE) {
			if (depth == 2) {
				child[0] = '\0';
				current_entry = -1;
			} else if (depth == 1) {
				node[0] = '\0';
				child[0] = '\0';
				current_entry = -1;
			}
			if (depth > 0)
				depth--;
			continue;
		}

		if ((ev->event & 0xffff) == KDL_EVENT_ARGUMENT) {
			if (depth == 1) {
				if (strcmp(node, "logo") == 0 && ev->value.type == KDL_TYPE_STRING) {
					kdl_to_cstr(ev->value.string, settings->logo, sizeof(settings->logo));
					shitfetch_strlower(settings->logo);
					settings->show_logo = strcmp(settings->logo, "none") != 0;
				} else if (strcmp(node, "ansi") == 0) {
					bool b;

					if (kdl_value_to_bool(&ev->value, &b))
						settings->show_ansi = b;
				} else if (strcmp(node, "header") == 0) {
					bool b;

					if (kdl_value_to_bool(&ev->value, &b))
						settings->show_header = b;
				} else if (strcmp(node, "ascii-dir") == 0 && ev->value.type == KDL_TYPE_STRING) {
					kdl_to_cstr(ev->value.string, settings->ascii_dir, sizeof(settings->ascii_dir));
				} else if (strcmp(node, "separator") == 0 && ev->value.type == KDL_TYPE_STRING) {
					kdl_to_cstr(ev->value.string, settings->separator, sizeof(settings->separator));
				}
			} else if (depth == 2) {
				if (strcmp(node, "colors") == 0) {
					int v;

					if (strcmp(child, "key") == 0) {
						if (kdl_value_to_int(&ev->value, &v)) {
							settings->key_color = v;
							snprintf(settings->key_color_spec, sizeof(settings->key_color_spec), "%d", v);
						} else if (ev->value.type == KDL_TYPE_STRING) {
							kdl_to_cstr(ev->value.string, settings->key_color_spec, sizeof(settings->key_color_spec));
						}
					} else if (strcmp(child, "value") == 0) {
						if (kdl_value_to_int(&ev->value, &v)) {
							settings->value_color = v;
							snprintf(settings->value_color_spec, sizeof(settings->value_color_spec), "%d", v);
						} else if (ev->value.type == KDL_TYPE_STRING) {
							kdl_to_cstr(ev->value.string, settings->value_color_spec, sizeof(settings->value_color_spec));
						}
					} else if (strcmp(child, "header") == 0) {
						if (kdl_value_to_int(&ev->value, &v))
							snprintf(settings->header_color_spec, sizeof(settings->header_color_spec), "%d", v);
						else if (ev->value.type == KDL_TYPE_STRING)
							kdl_to_cstr(ev->value.string, settings->header_color_spec, sizeof(settings->header_color_spec));
					} else if (strcmp(child, "border") == 0) {
						if (kdl_value_to_int(&ev->value, &v))
							snprintf(settings->border_color_spec, sizeof(settings->border_color_spec), "%d", v);
						else if (ev->value.type == KDL_TYPE_STRING)
							kdl_to_cstr(ev->value.string, settings->border_color_spec, sizeof(settings->border_color_spec));
					} else if (strcmp(child, "custom") == 0) {
						if (kdl_value_to_int(&ev->value, &v))
							snprintf(settings->custom_color_spec, sizeof(settings->custom_color_spec), "%d", v);
						else if (ev->value.type == KDL_TYPE_STRING)
							kdl_to_cstr(ev->value.string, settings->custom_color_spec, sizeof(settings->custom_color_spec));
					} else if (strcmp(child, "logo") == 0) {
						if (kdl_value_to_int(&ev->value, &v))
							snprintf(settings->logo_color_spec, sizeof(settings->logo_color_spec), "%d", v);
						else if (ev->value.type == KDL_TYPE_STRING)
							kdl_to_cstr(ev->value.string, settings->logo_color_spec, sizeof(settings->logo_color_spec));
					}
				} else if (strcmp(node, "modules") == 0 && strcmp(child, "disk") == 0 &&
					ev->value.type == KDL_TYPE_STRING) {
					if (settings->disk_mount_filter_count < SHITFETCH_MAX_DISK_FILTERS) {
						kdl_to_cstr(ev->value.string,
							settings->disk_mount_filter[settings->disk_mount_filter_count],
							sizeof(settings->disk_mount_filter[settings->disk_mount_filter_count]));
						if (settings->disk_mount_filter[settings->disk_mount_filter_count][0] != '\0')
							settings->disk_mount_filter_count++;
					}
				} else if (strcmp(node, "modules") == 0 && strcmp(child, "separator") == 0 &&
					ev->value.type == KDL_TYPE_STRING && current_entry >= 0) {
					kdl_to_cstr(ev->value.string, settings->entries[current_entry].text,
						sizeof(settings->entries[current_entry].text));
				} else if (strcmp(node, "modules") == 0 && strcmp(child, "custom") == 0 &&
					ev->value.type == KDL_TYPE_STRING && current_entry >= 0 &&
					settings->entries[current_entry].kind == SHITFETCH_ENTRY_CUSTOM) {
					kdl_to_cstr(ev->value.string, settings->entries[current_entry].text,
						sizeof(settings->entries[current_entry].text));
				}
			}
			continue;
		}

		if ((ev->event & 0xffff) == KDL_EVENT_PROPERTY) {
			if (depth == 2 && strcmp(node, "modules") == 0) {
				enum shitfetch_module module;
				char key[64];

				if (!shitfetch_module_from_name(child, &module))
					module = SHITFETCH_MODULE_COUNT;
				kdl_to_cstr(ev->name, key, sizeof(key));
				shitfetch_strlower(key);
				if (strcmp(key, "enabled") == 0 && module != SHITFETCH_MODULE_COUNT) {
					bool b;

					if (kdl_value_to_bool(&ev->value, &b))
						settings->module_enabled[module] = b;
					if (current_entry >= 0 && settings->entries[current_entry].kind == SHITFETCH_ENTRY_MODULE)
						settings->entries[current_entry].enabled = settings->module_enabled[module];
				} else if (strcmp(key, "key") == 0 && ev->value.type == KDL_TYPE_STRING && current_entry >= 0 &&
					settings->entries[current_entry].kind == SHITFETCH_ENTRY_MODULE) {
					kdl_to_cstr(ev->value.string, settings->entries[current_entry].key,
						sizeof(settings->entries[current_entry].key));
					settings->entries[current_entry].key_set = settings->entries[current_entry].key[0] != '\0';
				} else if ((strcmp(key, "key-color") == 0 || strcmp(key, "keycolor") == 0) && current_entry >= 0 &&
					settings->entries[current_entry].kind == SHITFETCH_ENTRY_MODULE) {
					if (ev->value.type == KDL_TYPE_STRING) {
						kdl_to_cstr(ev->value.string, settings->entries[current_entry].key_color,
							sizeof(settings->entries[current_entry].key_color));
						settings->entries[current_entry].key_color_set =
							settings->entries[current_entry].key_color[0] != '\0';
					} else if (ev->value.type == KDL_TYPE_NUMBER && ev->value.number.type == KDL_NUMBER_TYPE_INTEGER) {
						snprintf(settings->entries[current_entry].key_color,
							sizeof(settings->entries[current_entry].key_color), "%lld",
							(long long)ev->value.number.integer);
						settings->entries[current_entry].key_color_set = true;
					}
				} else if (strcmp(key, "format") == 0 && ev->value.type == KDL_TYPE_STRING && current_entry >= 0 &&
					settings->entries[current_entry].kind == SHITFETCH_ENTRY_MODULE) {
					kdl_to_cstr(ev->value.string, settings->entries[current_entry].format,
						sizeof(settings->entries[current_entry].format));
					settings->entries[current_entry].format_set = settings->entries[current_entry].format[0] != '\0';
				} else if (strcmp(key, "format") == 0 && ev->value.type == KDL_TYPE_STRING && current_entry >= 0 &&
					settings->entries[current_entry].kind == SHITFETCH_ENTRY_CUSTOM) {
					kdl_to_cstr(ev->value.string, settings->entries[current_entry].text,
						sizeof(settings->entries[current_entry].text));
				} else if (module == SHITFETCH_MODULE_DISK && strcmp(key, "all") == 0) {
					bool b;

					if (kdl_value_to_bool(&ev->value, &b))
						settings->disk_all = b;
				} else if (module == SHITFETCH_MODULE_DISK && strcmp(key, "show-fs") == 0) {
					bool b;

					if (kdl_value_to_bool(&ev->value, &b))
						settings->disk_show_fs = b;
				}
			}
			continue;
		}
	}

	kdl_destroy_parser(parser);
	free(doc);
	return true;
}

static void
build_user_config_path(char *path, size_t cap)
{
	const char *xdg;
	const char *home;

	xdg = getenv("XDG_CONFIG_HOME");
	if (xdg != NULL && xdg[0] != '\0') {
		snprintf(path, cap, "%s/shitfetch/config.kdl", xdg);
		return;
	}
	home = getenv("HOME");
	if (home != NULL && home[0] != '\0')
		snprintf(path, cap, "%s/.config/shitfetch/config.kdl", home);
	else
		path[0] = '\0';
}

bool
shitfetch_load_default_configs(struct shitfetch_settings *settings)
{
	char user_path[SHITFETCH_MAX_PATH];

	(void)shitfetch_load_config(settings, SHITFETCH_SYSTEM_CONFIG);
	build_user_config_path(user_path, sizeof(user_path));
	if (user_path[0] != '\0')
		(void)shitfetch_load_config(settings, user_path);
	return true;
}

static void
mkdir_p(const char *path)
{
	char tmp[SHITFETCH_MAX_PATH];
	char *p;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (p = tmp + 1; *p != '\0'; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		(void)mkdir(tmp, 0755);
		*p = '/';
	}
	(void)mkdir(tmp, 0755);
}

bool
shitfetch_generate_config(const struct shitfetch_settings *settings)
{
	char path[SHITFETCH_MAX_PATH];
	char dir[SHITFETCH_MAX_PATH];
	char *slash;
	FILE *fp;
	size_t i;

	build_user_config_path(path, sizeof(path));
	if (path[0] == '\0')
		return false;
	if (access(path, F_OK) == 0)
		return true;

	snprintf(dir, sizeof(dir), "%s", path);
	slash = strrchr(dir, '/');
	if (slash != NULL) {
		*slash = '\0';
		mkdir_p(dir);
	}

	fp = fopen(path, "w");
	if (fp == NULL)
		return false;

	fprintf(fp, "logo \"%s\"\n", settings->show_logo ? settings->logo : "none");
	fprintf(fp, "header %s\n", settings->show_header ? "true" : "false");
	fprintf(fp, "ansi %s\n", settings->show_ansi ? "true" : "false");
	fprintf(fp, "separator \"%s\"\n", settings->separator);
	fprintf(fp, "colors {\n");
	if (settings->key_color_spec[0] != '\0')
		fprintf(fp, "\tkey \"%s\"\n", settings->key_color_spec);
	else
		fprintf(fp, "\tkey %d\n", settings->key_color);
	if (settings->value_color_spec[0] != '\0')
		fprintf(fp, "\tvalue \"%s\"\n", settings->value_color_spec);
	else
		fprintf(fp, "\tvalue %d\n", settings->value_color);
	if (settings->header_color_spec[0] != '\0')
		fprintf(fp, "\theader \"%s\"\n", settings->header_color_spec);
	if (settings->border_color_spec[0] != '\0')
		fprintf(fp, "\tborder \"%s\"\n", settings->border_color_spec);
	if (settings->custom_color_spec[0] != '\0')
		fprintf(fp, "\tcustom \"%s\"\n", settings->custom_color_spec);
	if (settings->logo_color_spec[0] != '\0')
		fprintf(fp, "\tlogo \"%s\"\n", settings->logo_color_spec);
	fprintf(fp, "}\n");
	fprintf(fp, "modules {\n");
	for (i = 0; i < settings->entry_count; i++) {
		if (settings->entries[i].kind == SHITFETCH_ENTRY_BREAK) {
			fprintf(fp, "\tbreak\n");
			continue;
		}
		if (settings->entries[i].kind == SHITFETCH_ENTRY_SEPARATOR) {
			fprintf(fp, "\tseparator \"%s\"\n", settings->entries[i].text);
			continue;
		}
		if (settings->entries[i].kind == SHITFETCH_ENTRY_CUSTOM) {
			fprintf(fp, "\tcustom \"%s\"\n", settings->entries[i].text);
			continue;
		}
		if (settings->entries[i].kind == SHITFETCH_ENTRY_COLORS) {
			fprintf(fp, "\tcolors\n");
			continue;
		}
		if (settings->entries[i].kind == SHITFETCH_ENTRY_MODULE) {
			enum shitfetch_module module = settings->entries[i].module;

			fprintf(fp, "\t%s", shitfetch_module_name(module));
			if (!settings->entries[i].enabled)
				fprintf(fp, " enabled=false");
			if (settings->entries[i].key_set)
				fprintf(fp, " key=\"%s\"", settings->entries[i].key);
			if (settings->entries[i].key_color_set)
				fprintf(fp, " key-color=\"%s\"", settings->entries[i].key_color);
			if (settings->entries[i].format_set)
				fprintf(fp, " format=\"%s\"", settings->entries[i].format);
			if (module == SHITFETCH_MODULE_DISK) {
				size_t j;

				if (!settings->disk_all)
					fprintf(fp, " all=false");
				if (!settings->disk_show_fs)
					fprintf(fp, " show-fs=false");
				for (j = 0; j < settings->disk_mount_filter_count; j++)
					fprintf(fp, " \"%s\"", settings->disk_mount_filter[j]);
			}
			fprintf(fp, "\n");
		}
	}
	fprintf(fp, "}\n");
	fclose(fp);
	return true;
}
