#define _POSIX_C_SOURCE 200809L

#include "sf.h"

#include <stdio.h>
#include <string.h>

static void
print_help(FILE *out)
{
	fputs("shitfetch - minimal but configurable linux fetch\n", out);
	fputs("\n", out);
	fputs("usage:\n", out);
	fputs("  shitfetch [options]\n", out);
	fputs("\n", out);
	fputs("options:\n", out);
	fputs("  -h, --help        show help\n", out);
	fputs("  -v, --version     show version\n", out);
	fputs("  -g, --conf-gen    generate ~/.config/shitfetch/config.kdl\n", out);
	fputs("  -c, --config PATH load config file\n", out);
	fputs("  -l, --logo NAME   set logo name (or none)\n", out);
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
	int i;
	bool cli_logo = false;
	const char *cli_config = NULL;

	shitfetch_settings_init(&settings);
	(void)shitfetch_load_default_configs(&settings);

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			print_help(stdout);
			return 0;
		}
		if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
			printf("shitfetch %s\n", SHITFETCH_VERSION);
			return 0;
		}
		if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--conf-gen") == 0) {
			if (!shitfetch_generate_config(&settings)) {
				fprintf(stderr, "shitfetch: failed to generate config\n");
				return 1;
			}
			return 0;
		}
		if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "shitfetch: missing value for %s\n", argv[i]);
				return 1;
			}
			cli_config = argv[++i];
			continue;
		}
		if (strncmp(argv[i], "--config=", 9) == 0) {
			cli_config = argv[i] + 9;
			continue;
		}
		if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--logo") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "shitfetch: missing value for %s\n", argv[i]);
				return 1;
			}
			snprintf(settings.logo, sizeof(settings.logo), "%s", argv[++i]);
			cli_logo = true;
			continue;
		}
		if (strncmp(argv[i], "--logo=", 7) == 0) {
			snprintf(settings.logo, sizeof(settings.logo), "%s", argv[i] + 7);
			cli_logo = true;
			continue;
		}

		fprintf(stderr, "shitfetch: unknown option: %s\n", argv[i]);
		print_help(stderr);
		return 1;
	}

	if (cli_config != NULL) {
		if (!shitfetch_load_config(&settings, cli_config)) {
			fprintf(stderr, "shitfetch: failed to load config: %s\n", cli_config);
			return 1;
		}
	}
	if (cli_logo) {
		if (strcmp(settings.logo, "none") == 0)
			settings.show_logo = false;
		else
			settings.show_logo = true;
	}

	shitfetch_collect_data(&settings, &data);
	logo_count = shitfetch_load_logo(&settings, data.os_id, logo_lines, SHITFETCH_MAX_LOGO_LINES);
	shitfetch_logo_main_color(&settings, data.os_id, key_color, sizeof(key_color));
	info_count = shitfetch_build_info_lines(&settings, key_color, &data, info_lines, SHITFETCH_MAX_INFO_LINES);
	shitfetch_render(&settings, logo_lines, logo_count, info_lines, info_count, key_color);

	return 0;
}
