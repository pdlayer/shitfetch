#define _POSIX_C_SOURCE 200809L

#include "sf.h"

#include <stdio.h>
#include <string.h>

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

static int
append_module(struct shitfetch_settings *settings, enum shitfetch_module module);

static void
apply_template_mini(struct shitfetch_settings *settings)
{
	size_t i;
	static const enum shitfetch_module mini_order[] = {
		SHITFETCH_MODULE_OS,
		SHITFETCH_MODULE_KERNEL,
		SHITFETCH_MODULE_INIT,
		SHITFETCH_MODULE_DEWM,
		SHITFETCH_MODULE_PACKAGES,
		SHITFETCH_MODULE_SHELL,
		SHITFETCH_MODULE_MEMORY,
	};

	settings->template = SHITFETCH_TEMPLATE_MINI;
	settings->show_header = true;
	settings->show_ansi = false;
	settings->entry_count = 0;
	settings->module_count = sizeof(mini_order) / sizeof(mini_order[0]);

	for (i = 0; i < SHITFETCH_MODULE_COUNT; i++)
		settings->module_enabled[i] = false;
	for (i = 0; i < settings->module_count; i++) {
		settings->module_order[i] = mini_order[i];
		settings->module_enabled[mini_order[i]] = true;
		(void)append_module(settings, mini_order[i]);
	}
}

static int
append_module(struct shitfetch_settings *settings, enum shitfetch_module module)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return -1;
	idx = settings->entry_count++;
	settings->entries[idx].kind = SHITFETCH_ENTRY_MODULE;
	settings->entries[idx].module = module;
	settings->entries[idx].enabled = true;
	return 0;
}

void
shitfetch_settings_init(struct shitfetch_settings *settings)
{
	size_t i;

	memset(settings, 0, sizeof(*settings));
	settings->template = SHITFETCH_TEMPLATE_DEFAULT;
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

static void
print_help(FILE *out)
{
	fputs("shitfetch - minimal linux fetch\n", out);
	fputs("\n", out);
	fputs("usage:\n", out);
	fputs("  shitfetch [options]\n", out);
	fputs("\n", out);
	fputs("options:\n", out);
	fputs("  -h, --help        show help\n", out);
	fputs("  -v, --version     show version\n", out);
	fputs("  -l, --logo NAME   set logo name (or none)\n", out);
	fputs("  -t, --template    set template (default or mini)\n", out);
}

int
main(int argc, char **argv)
{
	struct shitfetch_settings settings;
	struct shitfetch_data data;
	struct shitfetch_logo_line logo_lines[SHITFETCH_MAX_LOGO_LINES];
	struct shitfetch_info_line info_lines[SHITFETCH_MAX_INFO_LINES];
	char key_color[32];
	size_t logo_count;
	size_t info_count;
	enum shitfetch_template requested_template = SHITFETCH_TEMPLATE_DEFAULT;
	int i;

	shitfetch_settings_init(&settings);

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help(stdout);
			return 0;
		}
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
			printf("shitfetch %s\n", SHITFETCH_VERSION);
			return 0;
		}
		if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--logo") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "shitfetch: missing value for %s\n", argv[i]);
				return 1;
			}
			snprintf(settings.logo, sizeof(settings.logo), "%s", argv[++i]);
			settings.show_logo = strcmp(settings.logo, "none") != 0;
			continue;
		}
		if (strncmp(argv[i], "--logo=", 7) == 0) {
			snprintf(settings.logo, sizeof(settings.logo), "%s", argv[i] + 7);
			settings.show_logo = strcmp(settings.logo, "none") != 0;
			continue;
		}
		if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--template") == 0) {
			const char *value;

			if (i + 1 >= argc) {
				fprintf(stderr, "shitfetch: missing value for %s\n", argv[i]);
				return 1;
			}
			value = argv[++i];
			if (strcmp(value, "default") == 0)
				requested_template = SHITFETCH_TEMPLATE_DEFAULT;
			else if (strcmp(value, "mini") == 0)
				requested_template = SHITFETCH_TEMPLATE_MINI;
			else {
				fprintf(stderr, "shitfetch: unknown template: %s\n", value);
				return 1;
			}
			continue;
		}
		if (strncmp(argv[i], "--template=", 11) == 0) {
			const char *value = argv[i] + 11;

			if (strcmp(value, "default") == 0)
				requested_template = SHITFETCH_TEMPLATE_DEFAULT;
			else if (strcmp(value, "mini") == 0)
				requested_template = SHITFETCH_TEMPLATE_MINI;
			else {
				fprintf(stderr, "shitfetch: unknown template: %s\n", value);
				return 1;
			}
			continue;
		}

		fprintf(stderr, "shitfetch: unknown option: %s\n", argv[i]);
		print_help(stderr);
		return 1;
	}

	if (requested_template == SHITFETCH_TEMPLATE_MINI)
		apply_template_mini(&settings);

	shitfetch_collect_data(&settings, &data);
	logo_count = shitfetch_load_logo(&settings, data.os_id, logo_lines, SHITFETCH_MAX_LOGO_LINES);
	shitfetch_logo_main_color(&settings, data.os_id, key_color, sizeof(key_color));
	info_count = shitfetch_build_info_lines(&settings, key_color, &data, info_lines, SHITFETCH_MAX_INFO_LINES);
	shitfetch_render(&settings, logo_lines, logo_count, info_lines, info_count, key_color);

	return 0;
}
