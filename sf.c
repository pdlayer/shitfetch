#define _POSIX_C_SOURCE 200809L

#include "sf.h"
#include "sfconfig.h"
#include "sfutil.h"

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
apply_template_order(struct shitfetch_settings *settings, enum shitfetch_template template,
	const enum shitfetch_module *order, size_t count, bool show_ansi)
{
	size_t i;

	settings->template = template;
	settings->show_header = true;
	settings->show_ansi = show_ansi;
	settings->entry_count = 0;
	settings->module_count = count;

	for (i = 0; i < SHITFETCH_MODULE_COUNT; i++)
		settings->module_enabled[i] = false;
	for (i = 0; i < settings->module_count; i++) {
		settings->module_order[i] = order[i];
		settings->module_enabled[order[i]] = true;
		(void)append_module(settings, order[i]);
	}
}

void
shitfetch_settings_apply_template(struct shitfetch_settings *settings,
	enum shitfetch_template template)
{
	static const enum shitfetch_module mini_order[] = {
		SHITFETCH_MODULE_OS,
		SHITFETCH_MODULE_KERNEL,
		SHITFETCH_MODULE_INIT,
		SHITFETCH_MODULE_DEWM,
		SHITFETCH_MODULE_PACKAGES,
		SHITFETCH_MODULE_SHELL,
		SHITFETCH_MODULE_MEMORY,
	};

	if (template == SHITFETCH_TEMPLATE_MINI) {
		apply_template_order(settings, SHITFETCH_TEMPLATE_MINI, mini_order,
			sizeof(mini_order) / sizeof(mini_order[0]), false);
		return;
	}

	apply_template_order(settings, SHITFETCH_TEMPLATE_DEFAULT, default_order,
		sizeof(default_order) / sizeof(default_order[0]), true);
}

static int
append_module(struct shitfetch_settings *settings, enum shitfetch_module module)
{
	size_t idx;

	if (settings->entry_count >= SHITFETCH_MAX_MODULE_ENTRIES)
		return -1;
	idx = settings->entry_count++;
	memset(&settings->entries[idx], 0, sizeof(settings->entries[idx]));
	settings->entries[idx].kind = SHITFETCH_ENTRY_MODULE;
	settings->entries[idx].module = module;
	settings->entries[idx].enabled = true;
	return 0;
}

void
shitfetch_settings_init(struct shitfetch_settings *settings)
{
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

	shitfetch_settings_apply_template(settings, SHITFETCH_TEMPLATE_DEFAULT);
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
	fputs("  -c, --config PATH load config file\n", out);
	fputs("      --no-config   skip automatic config loading\n", out);
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
	bool template_set = false;
	char requested_logo[64];
	bool logo_set = false;
	const char *config_path = NULL;
	bool explicit_config = false;
	bool load_config = true;
	char default_config[SHITFETCH_MAX_PATH];
	char config_err[512];
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
			snprintf(requested_logo, sizeof(requested_logo), "%s", argv[++i]);
			logo_set = true;
			continue;
		}
		if (strncmp(argv[i], "--logo=", 7) == 0) {
			snprintf(requested_logo, sizeof(requested_logo), "%s", argv[i] + 7);
			logo_set = true;
			continue;
		}
		if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--template") == 0) {
			const char *value;

			if (i + 1 >= argc) {
				fprintf(stderr, "shitfetch: missing value for %s\n", argv[i]);
				return 1;
			}
			value = argv[++i];
			if (strcmp(value, "default") == 0) {
				requested_template = SHITFETCH_TEMPLATE_DEFAULT;
				template_set = true;
			} else if (strcmp(value, "mini") == 0) {
				requested_template = SHITFETCH_TEMPLATE_MINI;
				template_set = true;
			} else {
				fprintf(stderr, "shitfetch: unknown template: %s\n", value);
				return 1;
			}
			continue;
		}
		if (strncmp(argv[i], "--template=", 11) == 0) {
			const char *value = argv[i] + 11;

			if (strcmp(value, "default") == 0) {
				requested_template = SHITFETCH_TEMPLATE_DEFAULT;
				template_set = true;
			} else if (strcmp(value, "mini") == 0) {
				requested_template = SHITFETCH_TEMPLATE_MINI;
				template_set = true;
			} else {
				fprintf(stderr, "shitfetch: unknown template: %s\n", value);
				return 1;
			}
			continue;
		}
		if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "shitfetch: missing value for %s\n", argv[i]);
				return 1;
			}
			config_path = argv[++i];
			explicit_config = true;
			load_config = true;
			continue;
		}
		if (strncmp(argv[i], "--config=", 9) == 0) {
			config_path = argv[i] + 9;
			explicit_config = true;
			load_config = true;
			continue;
		}
		if (strcmp(argv[i], "--no-config") == 0) {
			config_path = NULL;
			explicit_config = false;
			load_config = false;
			continue;
		}

		fprintf(stderr, "shitfetch: unknown option: %s\n", argv[i]);
		print_help(stderr);
		return 1;
	}

	if (load_config) {
		if (!explicit_config && shitfetch_default_config_path(default_config, sizeof(default_config)) &&
			shitfetch_file_exists(default_config)) {
			config_path = default_config;
		}
		if (config_path != NULL &&
			shitfetch_load_config(&settings, config_path, config_err, sizeof(config_err)) < 0) {
			fprintf(stderr, "shitfetch: config: %s\n", config_err);
			return 1;
		}
	}

	if (template_set)
		shitfetch_settings_apply_template(&settings, requested_template);
	if (logo_set) {
		snprintf(settings.logo, sizeof(settings.logo), "%s", requested_logo);
		settings.show_logo = strcmp(settings.logo, "none") != 0;
	}

	shitfetch_collect_data(&settings, &data);
	logo_count = shitfetch_load_logo(&settings, data.os_id, logo_lines, SHITFETCH_MAX_LOGO_LINES);
	shitfetch_logo_main_color(&settings, data.os_id, key_color, sizeof(key_color));
	info_count = shitfetch_build_info_lines(&settings, key_color, &data, info_lines, SHITFETCH_MAX_INFO_LINES);
	shitfetch_render(&settings, logo_lines, logo_count, info_lines, info_count, key_color);

	return 0;
}
