#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "sf.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

static void
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

static void
detect_init(char *out, size_t cap)
{
	char target[SHITFETCH_MAX_PATH];
	char basename_buf[256];
	ssize_t len;
	const char *pretty;

	len = readlink("/sbin/init", target, sizeof(target) - 1);
	if (len > 0) {
		target[len] = '\0';
		shitfetch_basename(target, basename_buf, sizeof(basename_buf));
		pretty = init_name_prettify(basename_buf);
		if (pretty != NULL) {
			snprintf(out, cap, "%s", pretty);
			return;
		}
		snprintf(out, cap, "%s", basename_buf);
		return;
	}

	read_first_line("/proc/1/comm", out, cap);
	if (out[0] != '\0') {
		pretty = init_name_prettify(out);
		if (pretty != NULL) {
			snprintf(out, cap, "%s", pretty);
			return;
		}
	} else {
		snprintf(out, cap, "unknown");
	}
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
	const char *de;
	uid_t uid;
	DIR *procdir;
	struct dirent *ent;
	char proc_path[SHITFETCH_MAX_PATH];
	char cmdline[256];
	FILE *fp;
	char basename_buf[128];
	char wm_found[128] = {0};

	session_type = getenv("XDG_SESSION_TYPE");
	de = getenv("XDG_CURRENT_DESKTOP");

	procdir = opendir("/proc");
	if (procdir == NULL) {
		if (de != NULL && de[0] != '\0') {
			if (session_type != NULL && session_type[0] != '\0')
				snprintf(out, cap, "%s (%s)", de, session_type);
			else
				snprintf(out, cap, "%s", de);
		} else {
			snprintf(out, cap, "unknown");
		}
		return;
	}

	uid = getuid();

	while ((ent = readdir(procdir)) != NULL) {
		int n;

		if (ent->d_type != DT_DIR || ent->d_name[0] < '0' || ent->d_name[0] > '9')
			continue;

		snprintf(proc_path, sizeof(proc_path), "/proc/%s/loginuid", ent->d_name);
		fp = fopen(proc_path, "r");
		if (fp == NULL)
			continue;
		n = fscanf(fp, "%d", &n);
		fclose(fp);
		if (n != 1 || (uid_t)n != uid)
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
		if (strlen(basename_buf) > strlen(wm_found))
			snprintf(wm_found, sizeof(wm_found), "%s", basename_buf);
	}
	closedir(procdir);

	if (wm_found[0] != '\0') {
		if (session_type != NULL && session_type[0] != '\0')
			snprintf(out, cap, "%s (%s)", wm_found, session_type);
		else
			snprintf(out, cap, "%s", wm_found);
		return;
	}

	if (de != NULL && de[0] != '\0') {
		if (session_type != NULL && session_type[0] != '\0')
			snprintf(out, cap, "%s (%s)", de, session_type);
		else
			snprintf(out, cap, "%s", de);
		return;
	}

	if (session_type != NULL && session_type[0] != '\0') {
		snprintf(out, cap, "%s", session_type);
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
detect_gpu(char *out, size_t cap)
{
	DIR *dir;
	struct dirent *ent;
	char path[256];
	char vendor_id[64];
	char model[256];
	const char *vendor;
	FILE *fp;

	out[0] = '\0';

	dir = opendir("/proc/driver/nvidia/gpus");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "/proc/driver/nvidia/gpus/%.120s/information", ent->d_name);
			fp = fopen(path, "r");
			if (fp == NULL)
				continue;
			while (fgets(model, sizeof(model), fp) != NULL) {
				char *value;
				if (strncmp(model, "Model:", 6) != 0)
					continue;
				value = model + 6;
				shitfetch_trim(value);
				if (value[0] != '\0') {
					snprintf(out, cap, "%s", value);
					fclose(fp);
					closedir(dir);
					return;
				}
			}
			fclose(fp);
		}
		closedir(dir);
	}

	dir = opendir("/sys/class/drm");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (strncmp(ent->d_name, "card", 4) != 0 || strchr(ent->d_name, '-') != NULL)
				continue;
			snprintf(path, sizeof(path), "/sys/class/drm/%.120s/device/vendor", ent->d_name);
			read_first_line(path, vendor_id, sizeof(vendor_id));
			if (vendor_id[0] == '\0')
				continue;
			if (strcmp(vendor_id, "0x10de") == 0)
				vendor = "NVIDIA";
			else if (strcmp(vendor_id, "0x1002") == 0 || strcmp(vendor_id, "0x1022") == 0)
				vendor = "AMD";
			else if (strcmp(vendor_id, "0x8086") == 0)
				vendor = "Intel";
			else
				vendor = "Unknown";

			snprintf(path, sizeof(path), "/sys/class/drm/%.120s/device/device", ent->d_name);
			read_first_line(path, model, sizeof(model));
			if (model[0] != '\0')
				snprintf(out, cap, "%s Graphics (%s)", vendor, model);
			else
				snprintf(out, cap, "%s Graphics", vendor);
			closedir(dir);
			return;
		}
		closedir(dir);
	}

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
	double used_gib;
	double total_gib;
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
		used_gib = (double)used / (1024.0 * 1024.0);
		total_gib = (double)mem_total / (1024.0 * 1024.0);
		percent = (unsigned int)((used * 100ULL) / mem_total);
		snprintf(mem_out, mem_cap, "%.1f / %.1f GiB (%%{%u})", used_gib, total_gib, percent);
	}

	if (swap_total == 0) {
		snprintf(swap_out, swap_cap, "0.0 / 0.0 GiB (%%{0})");
		return;
	}
	if (free_kib > swap_total)
		free_kib = 0;
	used = swap_total - free_kib;
	used_gib = (double)used / (1024.0 * 1024.0);
	total_gib = (double)swap_total / (1024.0 * 1024.0);
	percent = (unsigned int)((used * 100ULL) / swap_total);
	snprintf(swap_out, swap_cap, "%.1f / %.1f GiB (%%{%u})", used_gib, total_gib, percent);
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

static void
decode_mount_token(const char *src, char *dst, size_t cap)
{
	size_t i = 0;
	size_t o = 0;

	if (cap == 0)
		return;
	while (src[i] != '\0' && o + 1 < cap) {
		if (src[i] == '\\' && src[i + 1] >= '0' && src[i + 1] <= '7' &&
			src[i + 2] >= '0' && src[i + 2] <= '7' && src[i + 3] >= '0' && src[i + 3] <= '7') {
			int v = (src[i + 1] - '0') * 64 + (src[i + 2] - '0') * 8 + (src[i + 3] - '0');
			dst[o++] = (char)v;
			i += 4;
			continue;
		}
		dst[o++] = src[i++];
	}
	dst[o] = '\0';
}

static bool
is_pseudo_fs(const char *fs)
{
	static const char *skip[] = {
		"proc", "sysfs", "devtmpfs", "devpts", "tmpfs", "cgroup", "cgroup2", "pstore",
		"securityfs", "tracefs", "debugfs", "configfs", "fusectl", "mqueue", "hugetlbfs",
		"rpc_pipefs", "autofs", "overlay", "squashfs"
	};
	size_t i;

	for (i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
		if (strcmp(fs, skip[i]) == 0)
			return true;
	}
	return false;
}

static bool
mount_selected(const struct shitfetch_settings *settings, const char *mountpoint)
{
	size_t i;

	if (settings->disk_mount_filter_count == 0)
		return settings->disk_all ? true : strcmp(mountpoint, "/") == 0;
	for (i = 0; i < settings->disk_mount_filter_count; i++) {
		if (strcmp(settings->disk_mount_filter[i], mountpoint) == 0)
			return true;
	}
	return false;
}

static bool
is_ignored_mount(const char *mountpoint)
{
	static const char *ignored[] = {
		"/boot",
		"/boot/efi",
		"/efi",
		"/snap",
	};
	size_t i;

	for (i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
		size_t len = strlen(ignored[i]);

		if (strcmp(mountpoint, ignored[i]) == 0)
			return true;
		if (strncmp(mountpoint, ignored[i], len) == 0 && mountpoint[len] == '/')
			return true;
	}
	if (strlen(mountpoint) >= 9 && strcmp(mountpoint + strlen(mountpoint) - 9, "/boot/efi") == 0)
		return true;
	return false;
}

static void
detect_disks(const struct shitfetch_settings *settings, struct shitfetch_data *data)
{
	FILE *fp;
	char line[512];
	char seen_dev[SHITFETCH_MAX_DISKS][128];
	char seen_fs[SHITFETCH_MAX_DISKS][64];

	data->disk_count = 0;
	data->disk[0] = '\0';
	fp = fopen("/proc/self/mounts", "r");
	if (fp == NULL)
		return;

	while (fgets(line, sizeof(line), fp) != NULL && data->disk_count < SHITFETCH_MAX_DISKS) {
		struct statvfs st;
		char dev[128];
		char mnt_enc[128];
		char mnt[128];
		char fstype[64];
		unsigned long long total;
		unsigned long long avail;
		unsigned long long used;
		unsigned int percent;
		size_t idx;
		size_t i;
		bool merged = false;

		if (sscanf(line, "%127s %127s %63s", dev, mnt_enc, fstype) != 3)
			continue;
		decode_mount_token(mnt_enc, mnt, sizeof(mnt));
		if (is_pseudo_fs(fstype))
			continue;
		if (dev[0] != '/')
			continue;
		if (is_ignored_mount(mnt))
			continue;
		if (!mount_selected(settings, mnt))
			continue;
		if (statvfs(mnt, &st) != 0)
			continue;

		total = (unsigned long long)st.f_blocks * st.f_frsize;
		avail = (unsigned long long)st.f_bavail * st.f_frsize;
		if (total == 0)
			continue;
		used = total > avail ? total - avail : 0;
		percent = total == 0 ? 0 : (unsigned int)((used * 100ULL) / total);

		for (i = 0; i < data->disk_count; i++) {
			if (strcmp(seen_dev[i], dev) == 0 && strcmp(seen_fs[i], fstype) == 0) {
				if (strlen(mnt) < strlen(data->disk_mounts[i]))
					snprintf(data->disk_mounts[i], sizeof(data->disk_mounts[i]), "%.63s", mnt);
				merged = true;
				break;
			}
		}
		if (merged)
			continue;

		idx = data->disk_count++;
		snprintf(seen_dev[idx], sizeof(seen_dev[idx]), "%.127s", dev);
		snprintf(seen_fs[idx], sizeof(seen_fs[idx]), "%.63s", fstype);
		snprintf(data->disk_mounts[idx], sizeof(data->disk_mounts[idx]), "%.63s", mnt);
		if (settings->disk_show_fs) {
			snprintf(data->disk_values[idx], sizeof(data->disk_values[idx]),
				"%llu / %llu GiB (%%{%u}) - %s",
				(unsigned long long)(used / (1024ULL * 1024ULL * 1024ULL)),
				(unsigned long long)(total / (1024ULL * 1024ULL * 1024ULL)),
				percent, fstype);
		} else {
			snprintf(data->disk_values[idx], sizeof(data->disk_values[idx]),
				"%llu / %llu GiB (%%{%u})",
				(unsigned long long)(used / (1024ULL * 1024ULL * 1024ULL)),
				(unsigned long long)(total / (1024ULL * 1024ULL * 1024ULL)),
				percent);
		}
	}
	fclose(fp);
	if (data->disk_count == 0) {
		snprintf(data->disk_mounts[0], sizeof(data->disk_mounts[0]), "/");
		snprintf(data->disk_values[0], sizeof(data->disk_values[0]), "unknown");
		data->disk_count = 1;
	}
	snprintf(data->disk, sizeof(data->disk), "%s", data->disk_values[0]);
}

static bool
append_pkg_segment(char *dst, size_t cap, const char *segment)
{
	size_t have = strlen(dst);

	if (have == 0)
		return snprintf(dst, cap, "%s", segment) > 0;
	if (have + 2 >= cap)
		return false;
	dst[have++] = ',';
	dst[have++] = ' ';
	dst[have] = '\0';
	strncat(dst, segment, cap - have - 1);
	return true;
}

static int
count_pacman_local_path(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *ent;
	int count = 0;

	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		count++;
	}
	closedir(dir);
	return count;
}

static int
count_pacman_local(void)
{
	return count_pacman_local_path("/var/lib/pacman/local");
}

static int
count_dpkg_status_file(const char *path)
{
	FILE *fp;
	char line[256];
	int count = 0;

	if (path == NULL || path[0] == '\0')
		return -1;
	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "Status: install ok installed", 28) == 0)
			count++;
	}
	fclose(fp);
	return count;
}

static int
count_dpkg_status(void)
{
	int primary = count_dpkg_status_file("/var/lib/dpkg/status");

	if (primary > 0 || !shitfetch_file_exists("/bedrock/etc/bedrock-release"))
		return primary;

	DIR *dir = opendir("/bedrock/strata");
	struct dirent *ent;
	int total = 0;
	bool any = false;

	if (dir == NULL)
		return primary;
	while ((ent = readdir(dir)) != NULL) {
		char path[SHITFETCH_MAX_PATH];
		int n;

		if (ent->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/bedrock/strata/%s/var/lib/dpkg/status", ent->d_name);
		n = count_dpkg_status_file(path);
		if (n > 0) {
			total += n;
			any = true;
		}
	}
	closedir(dir);
	return any ? total : primary;
}

static int
count_apk_installed_path(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[256];
	int count = 0;

	if (fp == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, "P:", 2) == 0)
			count++;
	}
	fclose(fp);
	return count;
}

static int
count_apk_installed(void)
{
	return count_apk_installed_path("/lib/apk/db/installed");
}

static int
count_xbps_db_path(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *ent;
	int count = 0;
	size_t len;

	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		len = strlen(ent->d_name);
		if (len > 6 && strcmp(ent->d_name + len - 6, ".plist") == 0)
			count++;
	}
	closedir(dir);
	return count;
}

static int
count_xbps_db(void)
{
	return count_xbps_db_path("/var/db/xbps");
}

static int
count_portage_db_path(const char *path)
{
	DIR *cats = opendir(path);
	struct dirent *cat;
	int count = 0;

	if (cats == NULL)
		return -1;
	while ((cat = readdir(cats)) != NULL) {
		DIR *pkgs;
		struct dirent *pkg;
		char cat_path[SHITFETCH_MAX_PATH];

		if (cat->d_name[0] == '.')
			continue;
		snprintf(cat_path, sizeof(cat_path), "%s/%s", path, cat->d_name);
		pkgs = opendir(cat_path);
		if (pkgs == NULL)
			continue;
		while ((pkg = readdir(pkgs)) != NULL) {
			if (pkg->d_name[0] == '.')
				continue;
			count++;
		}
		closedir(pkgs);
	}
	closedir(cats);
	return count;
}

static int
count_portage_vardb(void)
{
	return count_portage_db_path("/var/db/pkg");
}

static int
count_nix_store_path(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *ent;
	int count = 0;

	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if (strchr(ent->d_name, '-') != NULL)
			count++;
	}
	closedir(dir);
	return count;
}

static int
count_nix_store(void)
{
	return count_nix_store_path("/nix/store");
}

static int
count_flatpak_system(void)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	dir = opendir("/var/lib/flatpak/app");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.')
				continue;
			count++;
		}
		closedir(dir);
	}

	dir = opendir("/var/lib/flatpak/runtime");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.')
				continue;
			count++;
		}
		closedir(dir);
	}
	return count;
}

static int
count_flatpak_user(void)
{
	const char *home;
	char path[SHITFETCH_MAX_PATH];
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return -1;
	snprintf(path, sizeof(path), "%s/.local/share/flatpak/app", home);
	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		count++;
	}
	closedir(dir);
	return count;
}

static int
count_flatpak(void)
{
	int system = count_flatpak_system();
	int user = count_flatpak_user();
	int system_ok = system > 0 ? system : 0;
	int user_ok = user > 0 ? user : 0;

	return system_ok + user_ok;
}

static int
count_snap_core(void)
{
	DIR *dir = opendir("/snap");
	struct dirent *ent;
	int count = 0;

	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if (ent->d_type == DT_DIR)
			count++;
	}
	closedir(dir);
	return count;
}

static int
count_snap_varlib(void)
{
	DIR *dir = opendir("/var/lib/snapd/snaps");
	struct dirent *ent;
	int count = 0;
	size_t len;

	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		len = strlen(ent->d_name);
		if (len > 5 && strcmp(ent->d_name + len - 5, ".snap") == 0)
			count++;
	}
	closedir(dir);
	return count;
}

static int
count_snap(void)
{
	int cores = count_snap_core();
	int varlib = count_snap_varlib();

	if (varlib > 0)
		return varlib;
	if (cores > 0)
		return cores;
	return -1;
}

static int
count_homebrew_cellar(void)
{
	const char *home;
	char path[SHITFETCH_MAX_PATH];
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	home = getenv("HOME");
	if (home == NULL)
		home = "/root";

	snprintf(path, sizeof(path), "%s/.linuxbrew/Cellar", home);
	dir = opendir(path);
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			DIR *vers;
			struct dirent *v;

			if (ent->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "%s/.linuxbrew/Cellar/%s", home, ent->d_name);
			vers = opendir(path);
			if (vers == NULL)
				continue;
			while ((v = readdir(vers)) != NULL) {
				if (v->d_name[0] == '.')
					continue;
				count++;
			}
			closedir(vers);
		}
		closedir(dir);
		if (count > 0)
			return count;
	}

	dir = opendir("/home/linuxbrew/.linuxbrew/Cellar");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			DIR *vers;
			struct dirent *v;

			if (ent->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "/home/linuxbrew/.linuxbrew/Cellar/%s", ent->d_name);
			vers = opendir(path);
			if (vers == NULL)
				continue;
			while ((v = readdir(vers)) != NULL) {
				if (v->d_name[0] == '.')
					continue;
				count++;
			}
			closedir(vers);
		}
		closedir(dir);
		if (count > 0)
			return count;
	}

	snprintf(path, sizeof(path), "%s/homebrew/Cellar", home);
	dir = opendir(path);
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			DIR *vers;
			struct dirent *v;

			if (ent->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "%s/homebrew/Cellar/%s", home, ent->d_name);
			vers = opendir(path);
			if (vers == NULL)
				continue;
			while ((v = readdir(vers)) != NULL) {
				if (v->d_name[0] == '.')
					continue;
				count++;
			}
			closedir(vers);
		}
		closedir(dir);
		if (count > 0)
			return count;
	}

	dir = opendir("/usr/local/Cellar");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			DIR *vers;
			struct dirent *v;

			if (ent->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "/usr/local/Cellar/%s", ent->d_name);
			vers = opendir(path);
			if (vers == NULL)
				continue;
			while ((v = readdir(vers)) != NULL) {
				if (v->d_name[0] == '.')
					continue;
				count++;
			}
			closedir(vers);
		}
		closedir(dir);
	}
	return count;
}

static int
count_homebrew_opt(void)
{
	const char *home;
	char path[SHITFETCH_MAX_PATH];
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	home = getenv("HOME");
	if (home == NULL)
		home = "/root";

	snprintf(path, sizeof(path), "%s/.linuxbrew/opt", home);
	dir = opendir(path);
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.')
				continue;
			count++;
		}
		closedir(dir);
		if (count > 0)
			return count;
	}

	dir = opendir("/home/linuxbrew/.linuxbrew/opt");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.')
				continue;
			count++;
		}
		closedir(dir);
		if (count > 0)
			return count;
	}

	snprintf(path, sizeof(path), "%s/homebrew/opt", home);
	dir = opendir(path);
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.')
				continue;
			count++;
		}
		closedir(dir);
		if (count > 0)
			return count;
	}

	dir = opendir("/usr/local/opt");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_name[0] == '.')
				continue;
			count++;
		}
		closedir(dir);
	}
	return count;
}

static int
count_homebrew(void)
{
	int cellar = count_homebrew_cellar();

	if (cellar > 0)
		return cellar;
	return count_homebrew_opt();
}

static bool
collect_bedrock_packages(char *packages, size_t packages_cap)
{
	DIR *dir;
	struct dirent *ent;
	int pacman_total = 0;
	int dpkg_total = 0;
	int apk_total = 0;
	int xbps_total = 0;
	int portage_total = 0;
	int nix_total = 0;
	int flatpak_total = 0;
	int snap_total = 0;
	int homebrew_total = 0;
	bool any = false;
	char seg[64];

	dir = opendir("/bedrock/strata");
	if (dir == NULL)
		return false;
	while ((ent = readdir(dir)) != NULL) {
		const char *stratum = ent->d_name;
		char root[SHITFETCH_MAX_PATH];
		char path[SHITFETCH_MAX_PATH];
		int n;

		if (stratum[0] == '.')
			continue;
		if (strcmp(stratum, "bedrock") == 0 || strcmp(stratum, "init") == 0 ||
			strcmp(stratum, "local") == 0 || strcmp(stratum, "hijacked") == 0)
			continue;

		snprintf(root, sizeof(root), "/bedrock/strata/%s", stratum);

		snprintf(path, sizeof(path), "%s/var/lib/pacman/local", root);
		n = count_pacman_local_path(path);
		if (n > 0) {
			pacman_total += n;
			any = true;
		}

		snprintf(path, sizeof(path), "%s/var/lib/dpkg/status", root);
		n = count_dpkg_status_file(path);
		if (n > 0) {
			dpkg_total += n;
			any = true;
		}

		snprintf(path, sizeof(path), "%s/lib/apk/db/installed", root);
		n = count_apk_installed_path(path);
		if (n > 0) {
			apk_total += n;
			any = true;
		}

		snprintf(path, sizeof(path), "%s/var/db/xbps", root);
		n = count_xbps_db_path(path);
		if (n > 0) {
			xbps_total += n;
			any = true;
		}

		snprintf(path, sizeof(path), "%s/var/db/pkg", root);
		n = count_portage_db_path(path);
		if (n > 0) {
			portage_total += n;
			any = true;
		}

		snprintf(path, sizeof(path), "%s/nix/store", root);
		n = count_nix_store_path(path);
		if (n > 0) {
			nix_total += n;
			any = true;
		}

		snprintf(path, sizeof(path), "%s/var/lib/flatpak/app", root);
		n = count_flatpak_system();
		if (n > 0) {
			flatpak_total += n;
			any = true;
		}
	}
	closedir(dir);

	packages[0] = '\0';
	if (pacman_total > 0) {
		snprintf(seg, sizeof(seg), "%d (pacman)", pacman_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (dpkg_total > 0) {
		snprintf(seg, sizeof(seg), "%d (dpkg)", dpkg_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (apk_total > 0) {
		snprintf(seg, sizeof(seg), "%d (apk)", apk_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (xbps_total > 0) {
		snprintf(seg, sizeof(seg), "%d (xbps)", xbps_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (portage_total > 0) {
		snprintf(seg, sizeof(seg), "%d (portage)", portage_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (nix_total > 0) {
		snprintf(seg, sizeof(seg), "%d (nix)", nix_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (flatpak_total > 0) {
		snprintf(seg, sizeof(seg), "%d (flatpak)", flatpak_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (snap_total > 0) {
		snprintf(seg, sizeof(seg), "%d (snap)", snap_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	if (homebrew_total > 0) {
		snprintf(seg, sizeof(seg), "%d (homebrew)", homebrew_total);
		append_pkg_segment(packages, packages_cap, seg);
	}
	return any;
}

static void
detect_packages(const char *os_id, char *packages, size_t packages_cap)
{
	bool has_any = false;
	bool is_bedrock = strcmp(os_id, "bedrock") == 0;
	char seg[64];
	int count;

	packages[0] = '\0';
	if (is_bedrock && collect_bedrock_packages(packages, packages_cap))
		return;

	count = count_pacman_local();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (pacman)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_dpkg_status();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (dpkg)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_apk_installed();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (apk)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_xbps_db();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (xbps)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_portage_vardb();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (portage)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_nix_store();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (nix)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_flatpak();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (flatpak)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_snap();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (snap)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}
	count = count_homebrew();
	if (count > 0) {
		snprintf(seg, sizeof(seg), "%d (homebrew)", count);
		append_pkg_segment(packages, packages_cap, seg);
		has_any = true;
	}

	if (!has_any) {
		if (is_bedrock && shitfetch_executable_exists("brl")) {
			snprintf(packages, packages_cap, "strata-managed");
			return;
		}
		snprintf(packages, packages_cap, "unknown");
	}
}

static void
get_monitor_name(const unsigned char *edid, char *out, size_t cap)
{
	out[0] = '\0';
	if (!edid) return;
	for (int i = 54; i <= 108; i += 18) {
		if (edid[i] == 0 && edid[i+1] == 0 && edid[i+2] == 0 && edid[i+3] == 0xfc) {
			int j;
			for (j = 0; j < 13; j++) {
				char c = (char)edid[i + 5 + j];
				if (c == 0x0a || c == 0x0d || c == 0x00) break;
				if ((size_t)j + 1 < cap) {
					out[j] = c;
					out[j+1] = '\0';
				}
			}
			shitfetch_trim(out);
			return;
		}
	}
}

static void
detect_display(char *id, size_t id_cap, char *out, size_t cap)
{
	int fd;
	drmModeRes *res;
	drmModeConnector *conn;
	drmModeEncoder *enc;
	drmModeCrtc *crtc;
	int i;
	bool found = false;
	char card_path[64];

	id[0] = '\0';
	out[0] = '\0';

	for (int card_idx = 0; card_idx < 8; card_idx++) {
		snprintf(card_path, sizeof(card_path), "/dev/dri/card%d", card_idx);
		fd = open(card_path, O_RDONLY | O_CLOEXEC);
		if (fd < 0) continue;

		res = drmModeGetResources(fd);
		if (!res) {
			close(fd);
			continue;
		}

		for (i = 0; i < res->count_connectors; i++) {
			conn = drmModeGetConnector(fd, res->connectors[i]);
			if (!conn) continue;

			if (conn->connection == DRM_MODE_CONNECTED) {
				const char *connector_name = "Unknown";
				static const char *connector_names[] = {
					"None", "VGA", "DVI-I", "DVI-D", "DVI-A",
					"Composite", "SVIDEO", "LVDS", "Component",
					"9PinDIN", "DisplayPort", "HDMI-A", "HDMI-B",
					"TV", "eDP", "VIRTUAL", "DSI", "DPI", "Writeback",
					"SPI", "USB"
				};
				if (conn->connector_type < sizeof(connector_names)/sizeof(connector_names[0]))
					connector_name = connector_names[conn->connector_type];

				snprintf(id, id_cap, "%s-%d", connector_name, conn->connector_type_id);

				drmModePropertyBlobPtr edid_blob = NULL;
				for (int j = 0; j < conn->count_props; j++) {
					drmModePropertyPtr prop = drmModeGetProperty(fd, conn->props[j]);
					if (prop) {
						if (strcmp(prop->name, "EDID") == 0) {
							edid_blob = drmModeGetPropertyBlob(fd, conn->prop_values[j]);
							drmModeFreeProperty(prop);
							break;
						}
						drmModeFreeProperty(prop);
					}
				}
				if (edid_blob) {
					char monitor_name[64];
					get_monitor_name((const unsigned char *)edid_blob->data, monitor_name, sizeof(monitor_name));
					if (monitor_name[0] != '\0') {
						snprintf(id, id_cap, "%s", monitor_name);
					}
					drmModeFreePropertyBlob(edid_blob);
				}

				enc = drmModeGetEncoder(fd, conn->encoder_id);
				if (enc) {
					crtc = drmModeGetCrtc(fd, enc->crtc_id);
					if (crtc && crtc->mode_valid) {
						double vrefresh = crtc->mode.vrefresh;
						if (vrefresh == 0 && crtc->mode.clock > 0 && crtc->mode.htotal > 0 && crtc->mode.vtotal > 0) {
							vrefresh = (double)crtc->mode.clock * 1000.0 / (crtc->mode.htotal * crtc->mode.vtotal);
						}
						
						bool is_internal = conn->connector_type == DRM_MODE_CONNECTOR_eDP ||
						                  conn->connector_type == DRM_MODE_CONNECTOR_LVDS ||
						                  conn->connector_type == DRM_MODE_CONNECTOR_DSI;

						double inches = 0;
						if (conn->mmWidth > 0 && conn->mmHeight > 0) {
							double w = (double)conn->mmWidth / 25.4;
							double h = (double)conn->mmHeight / 25.4;
							inches = sqrt(w * w + h * h);
						}

	if (inches > 0) {
		snprintf(out, cap, "%dx%d@%.0fHz, %.1f\" [%s]",
			crtc->mode.hdisplay, crtc->mode.vdisplay,
			round(vrefresh), inches, is_internal ? "Internal" : "External");
	} else {
		snprintf(out, cap, "%dx%d@%.0fHz, ?\" [%s]",
			crtc->mode.hdisplay, crtc->mode.vdisplay,
			round(vrefresh), is_internal ? "Internal" : "External");
	}
						drmModeFreeCrtc(crtc);
					}
					drmModeFreeEncoder(enc);
				}
				drmModeFreeConnector(conn);
				found = true;
				break;
			}
			drmModeFreeConnector(conn);
		}
		drmModeFreeResources(res);
		close(fd);
		if (found) break;
	}

	if (!found)
		snprintf(out, cap, "unknown");
}

void
shitfetch_collect_data(const struct shitfetch_settings *settings, struct shitfetch_data *data)
{
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

	if (settings->module_enabled[SHITFETCH_MODULE_DEWM])
		detect_dewm(data->dewm, sizeof(data->dewm));
	else
		snprintf(data->dewm, sizeof(data->dewm), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_HOST])
		detect_host(data->host, sizeof(data->host));
	else
		snprintf(data->host, sizeof(data->host), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_TERM])
		detect_term(data->term, sizeof(data->term));
	else
		snprintf(data->term, sizeof(data->term), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_CPU])
		detect_cpu(data->cpu, sizeof(data->cpu));
	else
		snprintf(data->cpu, sizeof(data->cpu), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_GPU])
		detect_gpu(data->gpu, sizeof(data->gpu));
	else
		snprintf(data->gpu, sizeof(data->gpu), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_MEMORY] || settings->module_enabled[SHITFETCH_MODULE_SWAP])
		detect_memory_swap(data->memory, sizeof(data->memory), data->swap, sizeof(data->swap));
	else {
		snprintf(data->memory, sizeof(data->memory), "unknown");
		snprintf(data->swap, sizeof(data->swap), "unknown");
	}

	if (settings->module_enabled[SHITFETCH_MODULE_DISK]) {
		detect_disks(settings, data);
	} else {
		data->disk_count = 1;
		snprintf(data->disk_mounts[0], sizeof(data->disk_mounts[0]), "/");
		snprintf(data->disk_values[0], sizeof(data->disk_values[0]), "unknown");
		snprintf(data->disk, sizeof(data->disk), "unknown");
	}

	if (settings->module_enabled[SHITFETCH_MODULE_PACKAGES])
		detect_packages(data->os_id, data->packages, sizeof(data->packages));
	else
		snprintf(data->packages, sizeof(data->packages), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_DISPLAY]) {
		detect_display(data->display_id, sizeof(data->display_id), data->display, sizeof(data->display));
	} else {
		snprintf(data->display_id, sizeof(data->display_id), "unknown");
		snprintf(data->display, sizeof(data->display), "unknown");
	}

	if (data->os_pretty[0] == '\0')
		snprintf(data->os_pretty, sizeof(data->os_pretty), "unknown");
	if (data->os_id[0] == '\0')
		snprintf(data->os_id, sizeof(data->os_id), "linux");
	if (data->gpu[0] == '\0')
		snprintf(data->gpu, sizeof(data->gpu), "unknown");
	if (data->display[0] == '\0')
		snprintf(data->display, sizeof(data->display), "unknown");
}
