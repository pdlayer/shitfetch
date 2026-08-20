#define _POSIX_C_SOURCE 200809L

#include "sf.h"
#include "sfconfig.h"
#include "sfspin.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
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
	SHITFETCH_MODULE_LOCALE,
	SHITFETCH_MODULE_LOCAL_IP,
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
	settings->spin.enabled = false;
	settings->spin.axis = SHITFETCH_SPIN_AXIS_XY;
	settings->spin.light = SHITFETCH_SPIN_LIGHT_TOP_LEFT;
	settings->spin.shade = SHITFETCH_SPIN_SHADE_AUTO;
	settings->spin.speed = 1.0f;
	settings->spin.size = 1.0f;
	settings->spin.depth = 0.0f;
	settings->spin.height = 0;
	settings->spin.frames = 0;
	settings->spin.fps = 30;
	snprintf(settings->spin.ramp, sizeof(settings->spin.ramp), "%s", SHITFETCH_SPIN_RAMP_DEFAULT);

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
	fputs("      --modules CSV set module order, e.g. os,kernel,shell,local-ip\n", out);
	fputs("  -c, --config PATH load config file\n", out);
	fputs("      --no-config   skip automatic config loading\n", out);
	fputs("\n", out);
	fputs("spin options:\n", out);
	fputs("      --spin[=AXIS] animate the logo as spinning 2.5D relief (x, y or xy)\n", out);
	fputs("      --spin-speed N    radians per second (0.05-20, default 1)\n", out);
	fputs("      --spin-size N     projection scale (0.5-5, default 1)\n", out);
	fputs("      --spin-depth N    relief thickness (0.1-10, 0 = auto)\n", out);
	fputs("      --spin-height N   relief rows (6-200, 0 = auto)\n", out);
	fputs("      --spin-frames N   stop after N frames (0 = until a key is pressed)\n", out);
	fputs("      --spin-fps N      frames per second (1-120, default 30)\n", out);
	fputs("      --spin-light DIR  light direction (top-left, top, front, right, ...)\n", out);
	fputs("      --spin-shade MODE relief glyphs (auto, ascii, braille, blocks)\n", out);
	fputs("      --spin-chars STR  shading ramp from darkest to brightest\n", out);
}

struct spin_flags {
	bool enabled;
	bool axis;
	bool light;
	bool shade;
	bool speed;
	bool size;
	bool depth;
	bool height;
	bool frames;
	bool fps;
	bool ramp;
};

/* 0 = argv[*i] is not this flag, 1 = matched and *value is set, -1 = matched but the
   value is missing. The `--name VALUE` form advances *i past the value. */
static int
flag_value(char **argv, int argc, int *i, const char *name, const char **value)
{
	size_t n = strlen(name);

	if (strcmp(argv[*i], name) == 0) {
		if (*i + 1 >= argc)
			return -1;
		*value = argv[++(*i)];
		return 1;
	}
	if (strncmp(argv[*i], name, n) == 0 && argv[*i][n] == '=') {
		*value = argv[*i] + n + 1;
		return 1;
	}
	return 0;
}

static bool
parse_float(const char *s, float *out)
{
	char *end = NULL;
	double v;

	if (s == NULL || s[0] == '\0')
		return false;
	v = strtod(s, &end);
	if (end == NULL || *end != '\0')
		return false;
	*out = (float)v;
	return true;
}

static bool
parse_long(const char *s, long *out)
{
	char *end = NULL;
	long v;

	if (s == NULL || s[0] == '\0')
		return false;
	v = strtol(s, &end, 10);
	if (end == NULL || *end != '\0')
		return false;
	*out = v;
	return true;
}

/* 0 = not a spin flag, 1 = consumed, -1 = bad or missing value (already reported). */
static int
parse_spin_arg(char **argv, int argc, int *i, struct shitfetch_spin *spin, struct spin_flags *set)
{
	const char *flag = argv[*i];
	const char *value = NULL;
	float f;
	long n;
	int m;

	if (strcmp(flag, "--spin") == 0) {
		spin->enabled = true;
		set->enabled = true;
		return 1;
	}
	if (strncmp(flag, "--spin=", 7) == 0) {
		if (!shitfetch_spin_parse_axis(flag + 7, &spin->axis)) {
			fprintf(stderr, "shitfetch: unknown spin axis: %s\n", flag + 7);
			return -1;
		}
		spin->enabled = true;
		set->enabled = true;
		set->axis = true;
		return 1;
	}
	if (strncmp(flag, "--spin-", 7) != 0)
		return 0;

	m = flag_value(argv, argc, i, "--spin-light", &value);
	if (m != 0) {
		if (m < 0 || !shitfetch_spin_parse_light(value, &spin->light)) {
			fprintf(stderr, "shitfetch: unknown spin light: %s\n", m < 0 ? "(missing)" : value);
			return -1;
		}
		set->light = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-shade", &value);
	if (m != 0) {
		if (m < 0 || !shitfetch_spin_parse_shade(value, &spin->shade)) {
			fprintf(stderr, "shitfetch: unknown spin shade: %s\n", m < 0 ? "(missing)" : value);
			return -1;
		}
		set->shade = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-chars", &value);
	if (m != 0) {
		if (m < 0 || value[0] == '\0') {
			fprintf(stderr, "shitfetch: --spin-chars needs a non-empty ramp\n");
			return -1;
		}
		snprintf(spin->ramp, sizeof(spin->ramp), "%s", value);
		set->ramp = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-speed", &value);
	if (m != 0) {
		if (m < 0 || !parse_float(value, &f))
			goto bad;
		spin->speed = f;
		set->speed = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-size", &value);
	if (m != 0) {
		if (m < 0 || !parse_float(value, &f))
			goto bad;
		spin->size = f;
		set->size = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-depth", &value);
	if (m != 0) {
		if (m < 0 || !parse_float(value, &f))
			goto bad;
		spin->depth = f;
		set->depth = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-height", &value);
	if (m != 0) {
		if (m < 0 || !parse_long(value, &n))
			goto bad;
		spin->height = (int)n;
		set->height = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-frames", &value);
	if (m != 0) {
		if (m < 0 || !parse_long(value, &n))
			goto bad;
		spin->frames = n;
		set->frames = true;
		return 1;
	}
	m = flag_value(argv, argc, i, "--spin-fps", &value);
	if (m != 0) {
		if (m < 0 || !parse_long(value, &n))
			goto bad;
		spin->fps = (int)n;
		set->fps = true;
		return 1;
	}
	return 0;
bad:
	fprintf(stderr, "shitfetch: invalid value for %s\n", flag);
	return -1;
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
	const char *requested_modules = NULL;
	const char *config_path = NULL;
	bool explicit_config = false;
	bool load_config = true;
	char default_config[SHITFETCH_MAX_PATH];
	char config_err[512];
	struct shitfetch_spin spin_cli;
	struct spin_flags spin_set;
	int i;

	shitfetch_settings_init(&settings);
	spin_cli = settings.spin;
	memset(&spin_set, 0, sizeof(spin_set));

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
		if (strcmp(argv[i], "--modules") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "shitfetch: missing value for %s\n", argv[i]);
				return 1;
			}
			requested_modules = argv[++i];
			continue;
		}
		if (strncmp(argv[i], "--modules=", 10) == 0) {
			requested_modules = argv[i] + 10;
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
		if (strncmp(argv[i], "--spin", 6) == 0) {
			int r = parse_spin_arg(argv, argc, &i, &spin_cli, &spin_set);

			if (r < 0)
				return 1;
			if (r > 0)
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
	if (requested_modules != NULL &&
		shitfetch_apply_modules_csv(&settings, requested_modules, config_err, sizeof(config_err)) < 0) {
		fprintf(stderr, "shitfetch: modules: %s\n", config_err);
		return 1;
	}
	if (logo_set) {
		snprintf(settings.logo, sizeof(settings.logo), "%s", requested_logo);
		settings.show_logo = strcmp(settings.logo, "none") != 0;
	}
	if (spin_set.enabled)
		settings.spin.enabled = spin_cli.enabled;
	if (spin_set.axis)
		settings.spin.axis = spin_cli.axis;
	if (spin_set.light)
		settings.spin.light = spin_cli.light;
	if (spin_set.shade)
		settings.spin.shade = spin_cli.shade;
	if (spin_set.speed)
		settings.spin.speed = spin_cli.speed;
	if (spin_set.size)
		settings.spin.size = spin_cli.size;
	if (spin_set.depth)
		settings.spin.depth = spin_cli.depth;
	if (spin_set.height)
		settings.spin.height = spin_cli.height;
	if (spin_set.frames)
		settings.spin.frames = spin_cli.frames;
	if (spin_set.fps)
		settings.spin.fps = spin_cli.fps;
	if (spin_set.ramp)
		snprintf(settings.spin.ramp, sizeof(settings.spin.ramp), "%s", spin_cli.ramp);
	shitfetch_spin_clamp(&settings.spin);

	shitfetch_collect_data(&settings, &data);
	shitfetch_logo_main_color(&settings, data.os_id, key_color, sizeof(key_color));
	if (settings.spin.enabled && settings.show_logo)
		shitfetch_spin_run(&settings, &data, key_color);
	logo_count = shitfetch_load_logo(&settings, data.os_id, logo_lines, SHITFETCH_MAX_LOGO_LINES);
	info_count = shitfetch_build_info_lines(&settings, key_color, &data, info_lines, SHITFETCH_MAX_INFO_LINES);
	shitfetch_render(&settings, logo_lines, logo_count, info_lines, info_count, key_color);

	return 0;
}
