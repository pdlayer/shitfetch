#define _POSIX_C_SOURCE 200809L

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
read_ppid(pid_t pid)
{
	char path[64];
	FILE *fp;
	int parsed_pid;
	char comm[256];
	char state;
	int ppid;

	snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	if (fscanf(fp, "%d (%255[^)]) %c %d", &parsed_pid, comm, &state, &ppid) != 4) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return ppid;
}

static bool
read_comm(pid_t pid, char *out, size_t cap)
{
	char path[64];

	snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
	read_first_line(path, out, cap);
	return out[0] != '\0';
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
read_os_release(char *id, size_t id_cap, char *pretty, size_t pretty_cap)
{
	FILE *fp;
	char line[512];
	char bedrock[128];

	id[0] = '\0';
	pretty[0] = '\0';
	fp = fopen("/etc/os-release", "r");
	if (fp != NULL) {
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
			if (strcmp(key, "PRETTY_NAME") == 0)
				snprintf(pretty, pretty_cap, "%s", value);
		}
		fclose(fp);
	}

	if (shitfetch_file_exists("/bedrock/etc/bedrock-release")) {
		read_first_line("/bedrock/etc/bedrock-release", bedrock, sizeof(bedrock));
		snprintf(id, id_cap, "bedrock");
		if (bedrock[0] != '\0')
			snprintf(pretty, pretty_cap, "Bedrock Linux (%s)", bedrock);
		else
			snprintf(pretty, pretty_cap, "Bedrock Linux");
	}

	if (pretty[0] == '\0' && id[0] != '\0')
		snprintf(pretty, pretty_cap, "%s", id);
	shitfetch_strlower(id);
}

static void
detect_uptime(char *out, size_t cap)
{
	FILE *fp;
	double sec;
	long total;
	long days;
	long hours;
	long mins;

	out[0] = '\0';
	fp = fopen("/proc/uptime", "r");
	if (fp == NULL)
		return;
	if (fscanf(fp, "%lf", &sec) != 1) {
		fclose(fp);
		return;
	}
	fclose(fp);

	total = (long)sec;
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

static void
detect_init(char *out, size_t cap)
{
	read_first_line("/proc/1/comm", out, cap);
	if (out[0] == '\0')
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
	const char *v;

	v = getenv("XDG_CURRENT_DESKTOP");
	if (v != NULL && v[0] != '\0') {
		snprintf(out, cap, "%s", v);
		return;
	}
	v = getenv("DESKTOP_SESSION");
	if (v != NULL && v[0] != '\0') {
		snprintf(out, cap, "%s", v);
		return;
	}
	v = getenv("WAYLAND_DISPLAY");
	if (v != NULL && v[0] != '\0') {
		snprintf(out, cap, "Wayland");
		return;
	}
	v = getenv("DISPLAY");
	if (v != NULL && v[0] != '\0') {
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
	pid_t pid;
	int ppid;
	char comm[256];
	char first_term[128] = {0};
	int i;

	pid = getpid();
	ppid = read_ppid(pid);
	for (i = 0; i < 24 && ppid > 1; i++) {
		if (!read_comm((pid_t)ppid, comm, sizeof(comm)))
			break;
		if (strcmp(comm, "est") == 0) {
			snprintf(out, cap, "est");
			return;
		}
		if (first_term[0] == '\0' && !is_shell_name(comm) && !is_wrapper_name(comm))
			snprintf(first_term, sizeof(first_term), "%s", comm);
		ppid = read_ppid((pid_t)ppid);
	}
	if (first_term[0] != '\0') {
		snprintf(out, cap, "%s", first_term);
		return;
	}

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
detect_memory(char *out, size_t cap)
{
	FILE *fp;
	char key[64];
	unsigned long long value;
	char unit[32];
	unsigned long long total = 0;
	unsigned long long avail = 0;
	unsigned long long used;
	double used_gib;
	double total_gib;
	unsigned int percent;

	out[0] = '\0';
	fp = fopen("/proc/meminfo", "r");
	if (fp == NULL)
		return;

	while (fscanf(fp, "%63s %llu %31s", key, &value, unit) == 3) {
		if (strcmp(key, "MemTotal:") == 0)
			total = value;
		else if (strcmp(key, "MemAvailable:") == 0)
			avail = value;
	}
	fclose(fp);

	if (total == 0) {
		snprintf(out, cap, "unknown");
		return;
	}
	if (avail > total)
		avail = 0;
	used = total - avail;
	used_gib = (double)used / (1024.0 * 1024.0);
	total_gib = (double)total / (1024.0 * 1024.0);
	percent = total == 0 ? 0 : (unsigned int)((used * 100ULL) / total);
	snprintf(out, cap, "%.1f / %.1f GiB (%%{%u})", used_gib, total_gib, percent);
}

static void
detect_swap(char *out, size_t cap)
{
	FILE *fp;
	char key[64];
	unsigned long long value;
	char unit[32];
	unsigned long long total = 0;
	unsigned long long free_kib = 0;
	unsigned long long used;
	double used_gib;
	double total_gib;
	unsigned int percent;

	out[0] = '\0';
	fp = fopen("/proc/meminfo", "r");
	if (fp == NULL)
		return;

	while (fscanf(fp, "%63s %llu %31s", key, &value, unit) == 3) {
		if (strcmp(key, "SwapTotal:") == 0)
			total = value;
		else if (strcmp(key, "SwapFree:") == 0)
			free_kib = value;
	}
	fclose(fp);

	if (total == 0) {
		snprintf(out, cap, "0.0 / 0.0 GiB (%%{0})");
		return;
	}
	if (free_kib > total)
		free_kib = 0;
	used = total - free_kib;
	used_gib = (double)used / (1024.0 * 1024.0);
	total_gib = (double)total / (1024.0 * 1024.0);
	percent = (unsigned int)((used * 100ULL) / total);
	snprintf(out, cap, "%.1f / %.1f GiB (%%{%u})", used_gib, total_gib, percent);
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
count_pacman_local(void)
{
	DIR *dir = opendir("/var/lib/pacman/local");
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
count_dpkg_status(void)
{
	FILE *fp = fopen("/var/lib/dpkg/status", "r");
	char line[256];
	int count = 0;

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
count_apk_installed(void)
{
	FILE *fp = fopen("/lib/apk/db/installed", "r");
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
count_xbps_db(void)
{
	DIR *dir = opendir("/var/db/xbps");
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

static void
detect_packages(const char *os_id, char *packages, size_t packages_cap)
{
	bool has_any = false;
	bool is_bedrock = strcmp(os_id, "bedrock") == 0;
	char seg[64];
	int count;

	packages[0] = '\0';

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
	detect_kernel(data->kernel, sizeof(data->kernel));
	detect_init(data->init, sizeof(data->init));
	detect_uptime(data->uptime, sizeof(data->uptime));
	detect_shell(data->shell, sizeof(data->shell));
	detect_dewm(data->dewm, sizeof(data->dewm));
	detect_host(data->host, sizeof(data->host));
	detect_term(data->term, sizeof(data->term));
	detect_cpu(data->cpu, sizeof(data->cpu));
	detect_gpu(data->gpu, sizeof(data->gpu));
	detect_memory(data->memory, sizeof(data->memory));
	detect_swap(data->swap, sizeof(data->swap));
	detect_disks(settings, data);
	detect_packages(data->os_id, data->packages, sizeof(data->packages));
	detect_display(data->display_id, sizeof(data->display_id), data->display, sizeof(data->display));

	if (data->os_pretty[0] == '\0')
		snprintf(data->os_pretty, sizeof(data->os_pretty), "unknown");
	if (data->os_id[0] == '\0')
		snprintf(data->os_id, sizeof(data->os_id), "linux");
	if (data->gpu[0] == '\0')
		snprintf(data->gpu, sizeof(data->gpu), "unknown");
	if (data->display[0] == '\0')
		snprintf(data->display, sizeof(data->display), "unknown");
}
