#ifndef SHITFETCH_H
#define SHITFETCH_H

#include <stdbool.h>
#include <stddef.h>

#define SHITFETCH_VERSION "0.1.0"
#define SHITFETCH_MAX_LINE 512
#define SHITFETCH_MAX_PATH 4096
#define SHITFETCH_MAX_MODULES 32
#define SHITFETCH_MAX_INFO_LINES 64
#define SHITFETCH_MAX_LOGO_LINES 256
#define SHITFETCH_MAX_DISKS 32
#define SHITFETCH_MAX_GPUS 8
#define SHITFETCH_MAX_DISK_FILTERS 16
#define SHITFETCH_MAX_MODULE_ENTRIES 96

enum shitfetch_entry_kind {
	SHITFETCH_ENTRY_MODULE,
	SHITFETCH_ENTRY_BREAK,
	SHITFETCH_ENTRY_SEPARATOR,
	SHITFETCH_ENTRY_CUSTOM,
	SHITFETCH_ENTRY_COLORS,
};

#ifndef SHITFETCH_ASCII_DIR
#define SHITFETCH_ASCII_DIR "/usr/local/share/shitfetch/ascii"
#endif

enum shitfetch_module {
	SHITFETCH_MODULE_OS,
	SHITFETCH_MODULE_KERNEL,
	SHITFETCH_MODULE_INIT,
	SHITFETCH_MODULE_UPTIME,
	SHITFETCH_MODULE_HOST,
	SHITFETCH_MODULE_SHELL,
	SHITFETCH_MODULE_DEWM,
	SHITFETCH_MODULE_TERM,
	SHITFETCH_MODULE_CPU,
	SHITFETCH_MODULE_GPU,
	SHITFETCH_MODULE_MEMORY,
	SHITFETCH_MODULE_SWAP,
	SHITFETCH_MODULE_DISK,
	SHITFETCH_MODULE_PACKAGES,
	SHITFETCH_MODULE_DISPLAY,
	SHITFETCH_MODULE_COUNT,
};

enum shitfetch_template {
	SHITFETCH_TEMPLATE_DEFAULT,
	SHITFETCH_TEMPLATE_MINI,
};

struct shitfetch_settings {
	enum shitfetch_template template;
	bool show_logo;
	bool show_header;
	char logo[64];
	bool show_ansi;
	int key_color;
	int value_color;
	char key_color_spec[32];
	char value_color_spec[32];
	char header_color_spec[32];
	char border_color_spec[32];
	char custom_color_spec[32];
	char logo_color_spec[32];
	char separator[16];
	enum shitfetch_module module_order[SHITFETCH_MAX_MODULES];
	size_t module_count;
	bool module_enabled[SHITFETCH_MODULE_COUNT];
	struct {
		enum shitfetch_entry_kind kind;
		enum shitfetch_module module;
		bool enabled;
		bool key_set;
		char key[64];
		bool key_color_set;
		char key_color[32];
		bool format_set;
		char format[128];
		char text[256];
	} entries[SHITFETCH_MAX_MODULE_ENTRIES];
	size_t entry_count;
	bool disk_all;
	bool disk_show_fs;
	char disk_mount_filter[SHITFETCH_MAX_DISK_FILTERS][SHITFETCH_MAX_PATH];
	size_t disk_mount_filter_count;
	char ascii_dir[SHITFETCH_MAX_PATH];
};

struct shitfetch_info_line {
	char text[SHITFETCH_MAX_LINE];
};

struct shitfetch_logo_line {
	char text[SHITFETCH_MAX_LINE * 2];
	size_t visible_len;
};

struct shitfetch_data {
	char os_pretty[128];
	char os_id[64];
	char kernel[128];
	char init[128];
	char uptime[128];
	char host[128];
	char shell[128];
	char dewm[128];
	char term[128];
	char cpu[256];
	char gpu[256];
	size_t gpu_count;
	char gpu_ids[SHITFETCH_MAX_GPUS][32];
	char gpu_values[SHITFETCH_MAX_GPUS][256];
	char memory[128];
	char swap[128];
	char disk[128];
	size_t disk_count;
	char disk_mounts[SHITFETCH_MAX_DISKS][64];
	char disk_values[SHITFETCH_MAX_DISKS][128];
	char packages[256];
	char display_id[64];
	char display[128];
};

void shitfetch_settings_init(struct shitfetch_settings *settings);
void shitfetch_settings_apply_template(struct shitfetch_settings *settings,
	enum shitfetch_template template);

void shitfetch_collect_data(const struct shitfetch_settings *settings, struct shitfetch_data *data);

size_t shitfetch_load_logo(const struct shitfetch_settings *settings, const char *os_id,
	struct shitfetch_logo_line *lines, size_t cap);
void shitfetch_logo_main_color(const struct shitfetch_settings *settings, const char *os_id,
	char *out, size_t out_cap);

size_t shitfetch_build_info_lines(const struct shitfetch_settings *settings, const char *key_color,
	const struct shitfetch_data *data, struct shitfetch_info_line *lines, size_t cap);

void shitfetch_render(const struct shitfetch_settings *settings,
	const struct shitfetch_logo_line *logo_lines, size_t logo_count,
	const struct shitfetch_info_line *info_lines, size_t info_count,
	const char *key_color);

#endif
