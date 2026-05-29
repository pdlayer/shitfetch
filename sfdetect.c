#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "sfdetect.h"
#include "sfdetectgpu.h"
#include "sfdetectpkgs.h"
#include "sfdetectdisk.h"
#include "sfdetectdisplay.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <threads.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

void
read_first_line(const char *path, char *out, size_t cap)
{
	FILE *fp;

	out[0] = '\0';
	fp = fopen(path, "r");
	if (fp == NULL)
		return;
	if (fgets(out, (int)cap, fp) == NULL)
		out[0] = '\0';
	fclose(fp);
	shitfetch_trim(out);
}

bool
read_keyed_line(const char *path, const char *key, char *out, size_t cap)
{
	FILE *fp;
	char line[256];
	size_t key_len;

	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	if (path == NULL || key == NULL)
		return false;

	fp = fopen(path, "r");
	if (fp == NULL)
		return false;
	key_len = strlen(key);
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *value;

		if (strncmp(line, key, key_len) != 0)
			continue;
		value = line + key_len;
		shitfetch_trim(value);
		snprintf(out, cap, "%s", value);
		fclose(fp);
		return true;
	}
	fclose(fp);
	return false;
}


static int
read_proc_stat(pid_t pid, char *comm, size_t comm_cap, pid_t *ppid_out)
{
	char path[64];
	FILE *fp;
	int parsed_pid;
	char comm_buf[256];
	char state;
	int ppid;

	if (comm != NULL && comm_cap > 0)
		comm[0] = '\0';
	if (ppid_out != NULL)
		*ppid_out = -1;

	snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	if (fscanf(fp, "%d (%255[^)]) %c %d", &parsed_pid, comm_buf, &state, &ppid) != 4) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	if (comm != NULL && comm_cap > 0)
		snprintf(comm, comm_cap, "%s", comm_buf);
	if (ppid_out != NULL)
		*ppid_out = (pid_t)ppid;
	return 0;
}

static bool
is_shell_name(const char *s)
{
	return strcmp(s, "sh") == 0 || strcmp(s, "bash") == 0 || strcmp(s, "zsh") == 0 ||
		strcmp(s, "fish") == 0 || strcmp(s, "dash") == 0 || strcmp(s, "ash") == 0 ||
		strcmp(s, "nu") == 0 || strcmp(s, "xonsh") == 0 || strcmp(s, "elvish") == 0;
}

static bool
is_wrapper_name(const char *s)
{
	return strcmp(s, "sudo") == 0 || strcmp(s, "doas") == 0 || strcmp(s, "login") == 0 ||
		strcmp(s, "systemd") == 0 || strcmp(s, "tmux") == 0 || strcmp(s, "screen") == 0;
}

static bool
is_known_dewm_name(const char *s)
{
	static const char *known[] = {
		"hyprland", "niri", "sway", "river", "wayfire", "labwc", "waybox", "hikari",
		"kwin_wayland", "kwin", "mutter", "gnome-shell", "cinnamon", "xfwm4", "openbox",
		"bspwm", "i3", "i3wm", "dwm", "xmonad", "awesome", "qtile", "spectrwm", "icewm",
		"jwm", "blackbox", "fluxbox", "enlightenment", "weston", "mate-session", "lxqt-session",
		"startplasma-wayland", "startplasma-x11"
	};
	size_t i;

	for (i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
		if (strcmp(s, known[i]) == 0)
			return true;
	}
	return false;
}

static const char *
detect_dewm_from_env(void)
{
	const char *desktop_env;

	if (getenv("HYPRLAND_INSTANCE_SIGNATURE") != NULL)
		return "Hyprland";
	if (getenv("SWAYSOCK") != NULL)
		return "Sway";
	if (getenv("NIRI_SOCKET") != NULL)
		return "Niri";
	if (getenv("RIVER_SOCKET") != NULL)
		return "River";
	if (getenv("WAYFIRE_SOCKET") != NULL)
		return "Wayfire";
	if (getenv("LABWC_PID") != NULL)
		return "labwc";

	desktop_env = getenv("XDG_CURRENT_DESKTOP");
	if (desktop_env != NULL) {
		if (strstr(desktop_env, "KDE") != NULL)
			return "KDE";
		if (strstr(desktop_env, "GNOME") != NULL)
			return "GNOME";
		if (strstr(desktop_env, "X-Cinnamon") != NULL)
			return "Cinnamon";
		if (strstr(desktop_env, "MATE") != NULL)
			return "MATE";
		if (strstr(desktop_env, "LXQt") != NULL)
			return "LXQt";
		if (strstr(desktop_env, "XFCE") != NULL)
			return "XFCE";
	}

	return NULL;
}

static const char *
first_nonempty_env(const char *a, const char *b, const char *c)
{
	const char *value;

	value = getenv(a);
	if (value != NULL && value[0] != '\0')
		return value;
	value = getenv(b);
	if (value != NULL && value[0] != '\0')
		return value;
	value = getenv(c);
	if (value != NULL && value[0] != '\0')
		return value;
	return NULL;
}

static void
copy_desktop_name(const char *src, char *out, size_t cap)
{
	const char *end;
	size_t len;

	if (cap == 0)
		return;
	out[0] = '\0';
	if (src == NULL || src[0] == '\0')
		return;

	end = strchr(src, ':');
	len = end != NULL ? (size_t)(end - src) : strlen(src);
	if (len >= cap)
		len = cap - 1;
	memcpy(out, src, len);
	out[len] = '\0';
}

static void
read_os_release_fields(const char *path, char *id, size_t id_cap, char *name, size_t name_cap,
	char *version_id, size_t version_id_cap)
{
	FILE *fp;
	char line[512];

	if (id_cap > 0)
		id[0] = '\0';
	if (name_cap > 0)
		name[0] = '\0';
	if (version_id_cap > 0)
		version_id[0] = '\0';
	fp = fopen(path, "r");
	if (fp == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *eq;
		char *key;
		char *value;
		size_t len;

		shitfetch_trim(line);
		if (line[0] == '\0' || line[0] == '#')
			continue;

		eq = strchr(line, '=');
		if (eq == NULL)
			continue;
		*eq = '\0';
		key = line;
		value = eq + 1;
		len = strlen(value);
		if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
			value[len - 1] = '\0';
			value++;
		}

		if (strcmp(key, "ID") == 0)
			snprintf(id, id_cap, "%s", value);
		else if (strcmp(key, "NAME") == 0)
			snprintf(name, name_cap, "%s", value);
		else if (strcmp(key, "VERSION_ID") == 0)
			snprintf(version_id, version_id_cap, "%s", value);
	}
	fclose(fp);
}

static void
read_os_release(char *id, size_t id_cap, char *pretty, size_t pretty_cap)
{
	char name[128];
	char version_id[64];
	char bedrock_id[64];
	char bedrock_name[128];
	char bedrock_version_id[64];

	id[0] = '\0';
	pretty[0] = '\0';
	name[0] = '\0';
	version_id[0] = '\0';
	read_os_release_fields("/etc/os-release", id, id_cap, name, sizeof(name), version_id, sizeof(version_id));

	if (shitfetch_file_exists("/bedrock/etc/bedrock-release")) {
		read_os_release_fields("/bedrock/etc/os-release",
			bedrock_id, sizeof(bedrock_id),
			bedrock_name, sizeof(bedrock_name),
			bedrock_version_id, sizeof(bedrock_version_id));
		snprintf(id, id_cap, "bedrock");
		if (bedrock_name[0] != '\0')
			snprintf(name, sizeof(name), "%s", bedrock_name);
		else
			snprintf(name, sizeof(name), "Bedrock Linux");
		if (bedrock_version_id[0] != '\0')
			snprintf(version_id, sizeof(version_id), "%s", bedrock_version_id);
	}

	if (name[0] != '\0' && version_id[0] != '\0')
		snprintf(pretty, pretty_cap, "%s %s", name, version_id);
	else if (name[0] != '\0')
		snprintf(pretty, pretty_cap, "%s", name);
	else if (id[0] != '\0')
		snprintf(pretty, pretty_cap, "%s", id);
	shitfetch_strlower(id);
}

static void
detect_uptime(char *out, size_t cap)
{
	struct timespec ts;
	long sec = 0;
	long total;
	long days;
	long hours;
	long mins;

	out[0] = '\0';
#ifdef CLOCK_BOOTTIME
	if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0)
		sec = ts.tv_sec;
#endif
	if (sec <= 0 && clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
		sec = ts.tv_sec;
	if (sec <= 0) {
		FILE *fp;
		double proc_sec = 0.0;

		fp = fopen("/proc/uptime", "r");
		if (fp == NULL)
			return;
		if (fscanf(fp, "%lf", &proc_sec) == 1)
			sec = (long)proc_sec;
		fclose(fp);
	}
	if (sec <= 0)
		return;

	total = sec;
	days = total / 86400;
	hours = (total % 86400) / 3600;
	mins = (total % 3600) / 60;

	if (days > 0)
		snprintf(out, cap, "%ldd %ldh %ldm", days, hours, mins);
	else if (hours > 0)
		snprintf(out, cap, "%ldh %ldm", hours, mins);
	else
		snprintf(out, cap, "%ldm", mins);
}

static const char *
init_name_prettify(const char *name)
{
	if (strcmp(name, "systemd") == 0) return "systemd";
	if (strcmp(name, "openrc-init") == 0) return "OpenRC";
	if (strcmp(name, "uxinit") == 0) return "uxinit";
	if (strcmp(name, "runit") == 0) return "runit";
	if (strcmp(name, "s6-svscan") == 0) return "s6";
	if (strcmp(name, "s6-init") == 0) return "s6";
	if (strcmp(name, "dinit") == 0) return "dinit";
	if (strcmp(name, "runit-init") == 0) return "runit";
	if (strcmp(name, "sv") == 0) return "runit";
	if (strcmp(name, "init") == 0) return "sysvinit";
	if (strcmp(name, "tini") == 0) return "tini";
	if (strcmp(name, "dumb-init") == 0) return "dumb-init";
	return NULL;
}

static bool
is_generic_init_name(const char *name)
{
	return strcmp(name, "init") == 0;
}

static bool
normalize_init_name(const char *name, char *out, size_t cap)
{
	size_t i;

	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	if (name == NULL || name[0] == '\0')
		return false;

	shitfetch_basename(name, out, cap);
	for (i = 0; out[i] != '\0'; i++) {
		if (isspace((unsigned char)out[i])) {
			out[i] = '\0';
			break;
		}
	}
	return out[0] != '\0';
}

static bool
format_init_name(const char *name, char *out, size_t cap)
{
	char normalized[256];
	const char *pretty;

	if (!normalize_init_name(name, normalized, sizeof(normalized)))
		return false;
	pretty = init_name_prettify(normalized);
	snprintf(out, cap, "%s", pretty != NULL ? pretty : normalized);
	return true;
}

static bool
read_proc_cmdline_name(const char *path, char *out, size_t cap)
{
	FILE *fp;
	size_t len;

	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	fp = fopen(path, "rb");
	if (fp == NULL)
		return false;
	len = fread(out, 1, cap - 1, fp);
	fclose(fp);
	if (len == 0)
		return false;
	out[len] = '\0';
	return true;
}

static void
detect_init(char *out, size_t cap)
{
	char target[SHITFETCH_MAX_PATH];
	char raw[256];
	char name[256];
	char generic[256];
	ssize_t len;

	generic[0] = '\0';

	if (read_proc_cmdline_name("/proc/1/cmdline", raw, sizeof(raw)) &&
	    normalize_init_name(raw, name, sizeof(name))) {
		if (!is_generic_init_name(name)) {
			format_init_name(name, out, cap);
			return;
		}
		format_init_name(name, generic, sizeof(generic));
	}

	read_first_line("/proc/1/comm", raw, sizeof(raw));
	if (normalize_init_name(raw, name, sizeof(name))) {
		if (!is_generic_init_name(name)) {
			format_init_name(name, out, cap);
			return;
		}
		if (generic[0] == '\0')
			format_init_name(name, generic, sizeof(generic));
	}

	len = readlink("/proc/1/exe", target, sizeof(target) - 1);
	if (len > 0) {
		target[len] = '\0';
		if (normalize_init_name(target, name, sizeof(name)) &&
		    !is_generic_init_name(name)) {
			format_init_name(name, out, cap);
			return;
		}
	}

	len = readlink("/sbin/init", target, sizeof(target) - 1);
	if (len > 0) {
		target[len] = '\0';
		format_init_name(target, out, cap);
		return;
	}

	if (generic[0] != '\0')
		snprintf(out, cap, "%s", generic);
	else
		snprintf(out, cap, "unknown");
}

static void
detect_shell(char *out, size_t cap)
{
	const char *shell;

	shell = getenv("SHELL");
	if (shell == NULL || shell[0] == '\0')
		shell = "unknown";
	shitfetch_basename(shell, out, cap);
}

static void
detect_dewm(char *out, size_t cap)
{
	const char *session_type;
	const char *desktop_env;
	const char *env_dewm;
	uid_t uid;
	DIR *procdir;
	struct dirent *ent;
	char proc_path[SHITFETCH_MAX_PATH];
	char cmdline[256];
	char desktop_name[128];
	FILE *fp;
	char basename_buf[128];
	char wm_found[128] = {0};
	uid_t loginuid;
	int loginuid_read;

	session_type = getenv("XDG_SESSION_TYPE");
	env_dewm = detect_dewm_from_env();
	if (env_dewm != NULL) {
		if (session_type != NULL && session_type[0] != '\0')
			snprintf(out, cap, "%s (%s)", env_dewm, session_type);
		else
			snprintf(out, cap, "%s", env_dewm);
		return;
	}
	desktop_env = first_nonempty_env("XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "DESKTOP_SESSION");
	copy_desktop_name(desktop_env, desktop_name, sizeof(desktop_name));
	if (desktop_name[0] != '\0') {
		if (session_type != NULL && session_type[0] != '\0')
			snprintf(out, cap, "%s (%s)", desktop_name, session_type);
		else
			snprintf(out, cap, "%s", desktop_name);
		return;
	}
	if (session_type != NULL && session_type[0] != '\0') {
		snprintf(out, cap, "%s", session_type);
		return;
	}

	procdir = opendir("/proc");
	if (procdir == NULL) {
		snprintf(out, cap, "unknown");
		return;
	}

	uid = getuid();

	while ((ent = readdir(procdir)) != NULL) {
		if (ent->d_type != DT_DIR || ent->d_name[0] < '0' || ent->d_name[0] > '9')
			continue;

		snprintf(proc_path, sizeof(proc_path), "/proc/%s/loginuid", ent->d_name);
		fp = fopen(proc_path, "r");
		if (fp == NULL)
			continue;
		loginuid_read = fscanf(fp, "%u", &loginuid);
		fclose(fp);
		if (loginuid_read != 1 || loginuid != uid)
			continue;

		snprintf(proc_path, sizeof(proc_path), "/proc/%s/cmdline", ent->d_name);
		fp = fopen(proc_path, "r");
		if (fp == NULL)
			continue;
		cmdline[0] = '\0';
		fgets(cmdline, sizeof(cmdline), fp);
		fclose(fp);
		if (cmdline[0] == '\0')
			continue;
		shitfetch_basename(cmdline, basename_buf, sizeof(basename_buf));
		if (is_known_dewm_name(basename_buf)) {
			snprintf(wm_found, sizeof(wm_found), "%s", basename_buf);
			break;
		}
		if (strlen(basename_buf) > strlen(wm_found))
			snprintf(wm_found, sizeof(wm_found), "%s", basename_buf);
	}
	closedir(procdir);

	if (wm_found[0] != '\0') {
		snprintf(out, cap, "%s", wm_found);
		return;
	}

	if (getenv("WAYLAND_DISPLAY") != NULL) {
		snprintf(out, cap, "Wayland");
		return;
	}
	if (getenv("DISPLAY") != NULL) {
		snprintf(out, cap, "X11");
		return;
	}
	snprintf(out, cap, "unknown");
}

static void
detect_term(char *out, size_t cap)
{
	const char *tp;
	const char *e;
	const char *term;
	const char *est_tty;
	pid_t walk_pid;
	pid_t next_ppid;
	char comm[256];
	char first_term[128] = {0};
	int i;

	est_tty = getenv("EST_TTY");
	if (est_tty != NULL && est_tty[0] != '\0') {
		snprintf(out, cap, "est");
		return;
	}

	tp = getenv("TERM_PROGRAM");
	if (tp != NULL && tp[0] != '\0') {
		snprintf(out, cap, "%s", tp);
		return;
	}
	if (getenv("KITTY_PID") != NULL) {
		snprintf(out, cap, "kitty");
		return;
	}
	if (getenv("WEZTERM_PANE") != NULL) {
		snprintf(out, cap, "wezterm");
		return;
	}
	if (getenv("KONSOLE_VERSION") != NULL) {
		snprintf(out, cap, "konsole");
		return;
	}
	if (getenv("ALACRITTY_SOCKET") != NULL || getenv("ALACRITTY_LOG") != NULL) {
		snprintf(out, cap, "alacritty");
		return;
	}
	if (getenv("GNOME_TERMINAL_SCREEN") != NULL) {
		snprintf(out, cap, "gnome-terminal");
		return;
	}
	e = getenv("TERMINAL");
	if (e != NULL && e[0] != '\0') {
		snprintf(out, cap, "%s", e);
		return;
	}
	term = getenv("TERM");
	if (term != NULL && strstr(term, "est") != NULL) {
		snprintf(out, cap, "est");
		return;
	}

	walk_pid = getppid();
	for (i = 0; i < 24 && walk_pid > 1; i++) {
		if (read_proc_stat(walk_pid, comm, sizeof(comm), &next_ppid) != 0 || comm[0] == '\0')
			break;
		if (strcmp(comm, "est") == 0) {
			snprintf(out, cap, "est");
			return;
		}
		if (first_term[0] == '\0' && !is_shell_name(comm) && !is_wrapper_name(comm))
			snprintf(first_term, sizeof(first_term), "%s", comm);
		walk_pid = next_ppid;
	}
	if (first_term[0] != '\0') {
		snprintf(out, cap, "%s", first_term);
		return;
	}

	snprintf(out, cap, "unknown");
}

static void
detect_cpu(char *out, size_t cap)
{
	FILE *fp;
	char line[512];

	out[0] = '\0';
	fp = fopen("/proc/cpuinfo", "r");
	if (fp == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *value;

		if (strncmp(line, "model name", 10) != 0)
			continue;
		value = strchr(line, ':');
		if (value == NULL)
			continue;
		value++;
		shitfetch_trim(value);
		snprintf(out, cap, "%s", value);
		break;
	}

	fclose(fp);
	if (out[0] == '\0')
		snprintf(out, cap, "unknown");
}


static void
detect_memory_swap(char *mem_out, size_t mem_cap, char *swap_out, size_t swap_cap)
{
	FILE *fp;
	char line[256];
	unsigned long long mem_total = 0;
	unsigned long long mem_avail = 0;
	unsigned long long swap_total = 0;
	unsigned long long free_kib = 0;
	unsigned long long used;
	unsigned long long used_mb;
	unsigned long long total_mb;
	unsigned int percent;

	mem_out[0] = '\0';
	swap_out[0] = '\0';
	fp = fopen("/proc/meminfo", "r");
	if (fp == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *key;
		char *value;
		unsigned long long val;

		key = line;
		value = strchr(line, ':');
		if (value == NULL)
			continue;
		*value = '\0';
		value++;
		val = strtoull(value, NULL, 10);

		if (strcmp(key, "MemTotal") == 0)
			mem_total = val;
		else if (strcmp(key, "MemAvailable") == 0)
			mem_avail = val;
		else if (strcmp(key, "SwapTotal") == 0)
			swap_total = val;
		else if (strcmp(key, "SwapFree") == 0)
			free_kib = val;
	}
	fclose(fp);

	if (mem_total == 0) {
		snprintf(mem_out, mem_cap, "unknown");
	} else {
		if (mem_avail > mem_total)
			mem_avail = 0;
		used = mem_total - mem_avail;
		used_mb = used / 1024ULL;
		total_mb = mem_total / 1024ULL;
		percent = (unsigned int)((used * 100ULL) / mem_total);
		snprintf(mem_out, mem_cap, "%lluMB / %lluMB (%%{%u})", used_mb, total_mb, percent);
	}

	if (swap_total == 0) {
		snprintf(swap_out, swap_cap, "0MB / 0MB (%%{0})");
		return;
	}
	if (free_kib > swap_total)
		free_kib = 0;
	used = swap_total - free_kib;
	used_mb = used / 1024ULL;
	total_mb = swap_total / 1024ULL;
	percent = (unsigned int)((used * 100ULL) / swap_total);
	snprintf(swap_out, swap_cap, "%lluMB / %lluMB (%%{%u})", used_mb, total_mb, percent);
}

static void
detect_host(char *out, size_t cap)
{
	char board_vendor[128];
	char board_name[128];
	char hostname[128];
	bool board_vendor_ok;
	bool board_name_ok;

	read_first_line("/sys/devices/virtual/dmi/id/board_vendor", board_vendor, sizeof(board_vendor));
	read_first_line("/sys/devices/virtual/dmi/id/board_name", board_name, sizeof(board_name));

	board_vendor_ok = board_vendor[0] != '\0' && strcmp(board_vendor, "To Be Filled By O.E.M.") != 0;
	board_name_ok = board_name[0] != '\0' && strcmp(board_name, "System Product Name") != 0 &&
		strcmp(board_name, "To Be Filled By O.E.M.") != 0;

	if (board_vendor_ok && board_name_ok)
		snprintf(out, cap, "%s %s", board_vendor, board_name);
	else if (board_name_ok)
		snprintf(out, cap, "%s", board_name);
	else if (board_vendor_ok)
		snprintf(out, cap, "%s", board_vendor);
	else if (gethostname(hostname, sizeof(hostname)) == 0)
		snprintf(out, cap, "%s", hostname);
	else
		snprintf(out, cap, "unknown");
}

static void
detect_kernel(char *out, size_t cap)
{
	struct utsname u;

	if (uname(&u) != 0) {
		snprintf(out, cap, "unknown");
		return;
	}
	snprintf(out, cap, "%s", u.release);
}


struct string_detect_task {
	char *out;
	size_t cap;
	void (*fn)(char *, size_t);
};

struct packages_detect_task {
	const char *os_id;
	char *packages;
	size_t packages_cap;
};

struct disks_detect_task {
	const struct shitfetch_settings *settings;
	struct shitfetch_data *data;
};

struct display_detect_task {
	char *id;
	size_t id_cap;
	char *out;
	size_t out_cap;
};

static int
run_string_detect(void *arg)
{
	struct string_detect_task *task = arg;

	task->fn(task->out, task->cap);
	return 0;
}

static int
run_packages_detect(void *arg)
{
	struct packages_detect_task *task = arg;

	detect_packages(task->os_id, task->packages, task->packages_cap);
	return 0;
}

static int
run_disks_detect(void *arg)
{
	struct disks_detect_task *task = arg;

	detect_disks(task->settings, task->data);
	return 0;
}

static int
run_display_detect(void *arg)
{
	struct display_detect_task *task = arg;

	detect_display(task->id, task->id_cap, task->out, task->out_cap);
	return 0;
}

static bool
start_thread(thrd_t *thread, int (*fn)(void *), void *arg)
{
	return thrd_create(thread, fn, arg) == thrd_success;
}

void
shitfetch_collect_data(const struct shitfetch_settings *settings, struct shitfetch_data *data)
{
	thrd_t dewm_thread;
	thrd_t term_thread;
	thrd_t disks_thread;
	thrd_t packages_thread;
	thrd_t display_thread;
	bool dewm_started = false;
	bool term_started = false;
	bool disks_started = false;
	bool packages_started = false;
	bool display_started = false;
	struct string_detect_task dewm_task;
	struct string_detect_task term_task;
	struct disks_detect_task disks_task;
	struct packages_detect_task packages_task;
	struct display_detect_task display_task;

	memset(data, 0, sizeof(*data));

	read_os_release(data->os_id, sizeof(data->os_id), data->os_pretty, sizeof(data->os_pretty));
	if (settings->module_enabled[SHITFETCH_MODULE_KERNEL])
		detect_kernel(data->kernel, sizeof(data->kernel));
	else
		snprintf(data->kernel, sizeof(data->kernel), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_INIT])
		detect_init(data->init, sizeof(data->init));
	else
		snprintf(data->init, sizeof(data->init), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_UPTIME])
		detect_uptime(data->uptime, sizeof(data->uptime));
	else
		snprintf(data->uptime, sizeof(data->uptime), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_SHELL])
		detect_shell(data->shell, sizeof(data->shell));
	else
		snprintf(data->shell, sizeof(data->shell), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_DEWM]) {
		dewm_task.out = data->dewm;
		dewm_task.cap = sizeof(data->dewm);
		dewm_task.fn = detect_dewm;
		dewm_started = start_thread(&dewm_thread, run_string_detect, &dewm_task);
		if (!dewm_started)
			detect_dewm(data->dewm, sizeof(data->dewm));
	} else
		snprintf(data->dewm, sizeof(data->dewm), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_HOST])
		detect_host(data->host, sizeof(data->host));
	else
		snprintf(data->host, sizeof(data->host), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_TERM]) {
		term_task.out = data->term;
		term_task.cap = sizeof(data->term);
		term_task.fn = detect_term;
		term_started = start_thread(&term_thread, run_string_detect, &term_task);
		if (!term_started)
			detect_term(data->term, sizeof(data->term));
	} else
		snprintf(data->term, sizeof(data->term), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_CPU])
		detect_cpu(data->cpu, sizeof(data->cpu));
	else
		snprintf(data->cpu, sizeof(data->cpu), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_GPU])
		detect_gpu(data);
	else {
		snprintf(data->gpu, sizeof(data->gpu), "unknown");
		data->gpu_count = 0;
	}

	if (settings->module_enabled[SHITFETCH_MODULE_MEMORY] || settings->module_enabled[SHITFETCH_MODULE_SWAP])
		detect_memory_swap(data->memory, sizeof(data->memory), data->swap, sizeof(data->swap));
	else {
		snprintf(data->memory, sizeof(data->memory), "unknown");
		snprintf(data->swap, sizeof(data->swap), "unknown");
	}

	if (settings->module_enabled[SHITFETCH_MODULE_DISK]) {
		disks_task.settings = settings;
		disks_task.data = data;
		disks_started = start_thread(&disks_thread, run_disks_detect, &disks_task);
		if (!disks_started)
			detect_disks(settings, data);
	} else {
		data->disk_count = 1;
		snprintf(data->disk_mounts[0], sizeof(data->disk_mounts[0]), "/");
		snprintf(data->disk_values[0], sizeof(data->disk_values[0]), "unknown");
		snprintf(data->disk, sizeof(data->disk), "unknown");
	}

	if (settings->module_enabled[SHITFETCH_MODULE_PACKAGES]) {
		packages_task.os_id = data->os_id;
		packages_task.packages = data->packages;
		packages_task.packages_cap = sizeof(data->packages);
		packages_started = start_thread(&packages_thread, run_packages_detect, &packages_task);
		if (!packages_started)
			detect_packages(data->os_id, data->packages, sizeof(data->packages));
	} else
		snprintf(data->packages, sizeof(data->packages), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_DISPLAY]) {
		display_task.id = data->display_id;
		display_task.id_cap = sizeof(data->display_id);
		display_task.out = data->display;
		display_task.out_cap = sizeof(data->display);
		display_started = start_thread(&display_thread, run_display_detect, &display_task);
		if (!display_started)
			detect_display(data->display_id, sizeof(data->display_id), data->display, sizeof(data->display));
	} else {
		snprintf(data->display_id, sizeof(data->display_id), "unknown");
		snprintf(data->display, sizeof(data->display), "unknown");
	}

	if (dewm_started)
		thrd_join(dewm_thread, NULL);
	if (term_started)
		thrd_join(term_thread, NULL);
	if (disks_started)
		thrd_join(disks_thread, NULL);
	if (packages_started)
		thrd_join(packages_thread, NULL);
	if (display_started)
		thrd_join(display_thread, NULL);

	if (data->os_pretty[0] == '\0')
		snprintf(data->os_pretty, sizeof(data->os_pretty), "unknown");
	if (data->os_id[0] == '\0')
		snprintf(data->os_id, sizeof(data->os_id), "linux");
	if (data->gpu[0] == '\0')
		snprintf(data->gpu, sizeof(data->gpu), "unknown");
	if (data->display[0] == '\0')
		snprintf(data->display, sizeof(data->display), "unknown");
}
