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
#include <locale.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <lmcons.h>
#include <tlhelp32.h>
#include <winreg.h>
#else
#include <dirent.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#endif

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

#ifdef _WIN32
typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);

static void
detect_windows_version(DWORD *major, DWORD *minor, DWORD *build)
{
	OSVERSIONINFOW info;
	HMODULE ntdll;
	RtlGetVersionFn rtl_get_version;
	FARPROC proc;

	*major = 0;
	*minor = 0;
	*build = 0;
	ntdll = GetModuleHandleA("ntdll.dll");
	proc = ntdll != NULL ? GetProcAddress(ntdll, "RtlGetVersion") : NULL;
	memcpy(&rtl_get_version, &proc, sizeof(rtl_get_version));
	if (rtl_get_version == NULL)
		return;
	memset(&info, 0, sizeof(info));
	info.dwOSVersionInfoSize = sizeof(info);
	if (rtl_get_version(&info) == 0) {
		*major = info.dwMajorVersion;
		*minor = info.dwMinorVersion;
		*build = info.dwBuildNumber;
	}
}

static void
read_windows_os(char *id, size_t id_cap, char *pretty, size_t pretty_cap)
{
	DWORD major;
	DWORD minor;
	DWORD build;

	detect_windows_version(&major, &minor, &build);
	if (major == 10 && build >= 22000) {
		snprintf(id, id_cap, "windows_11");
		snprintf(pretty, pretty_cap, "Windows 11 (build %lu)", (unsigned long)build);
	} else if (major == 10) {
		snprintf(id, id_cap, "windows_10");
		snprintf(pretty, pretty_cap, "Windows 10 (build %lu)", (unsigned long)build);
	} else if (major == 6 && minor == 3) {
		snprintf(id, id_cap, "windows_8.1");
		snprintf(pretty, pretty_cap, "Windows 8.1");
	} else if (major == 6 && minor == 2) {
		snprintf(id, id_cap, "windows_8");
		snprintf(pretty, pretty_cap, "Windows 8");
	} else if (major == 6 && minor == 1) {
		snprintf(id, id_cap, "windows_7");
		snprintf(pretty, pretty_cap, "Windows 7");
	} else if (major != 0) {
		snprintf(id, id_cap, "windows");
		snprintf(pretty, pretty_cap, "Windows %lu.%lu (build %lu)",
			(unsigned long)major, (unsigned long)minor, (unsigned long)build);
	} else {
		snprintf(id, id_cap, "windows");
		snprintf(pretty, pretty_cap, "Windows");
	}
}

static void
format_duration(unsigned long long sec, char *out, size_t cap)
{
	unsigned long long days = sec / 86400ULL;
	unsigned long long hours = (sec % 86400ULL) / 3600ULL;
	unsigned long long mins = (sec % 3600ULL) / 60ULL;

	if (days > 0)
		snprintf(out, cap, "%llud %lluh %llum", days, hours, mins);
	else if (hours > 0)
		snprintf(out, cap, "%lluh %llum", hours, mins);
	else
		snprintf(out, cap, "%llum", mins);
}

static void
detect_uptime(char *out, size_t cap)
{
	format_duration((unsigned long long)(GetTickCount64() / 1000ULL), out, cap);
}

static void
detect_shell(char *out, size_t cap)
{
	HANDLE snap;
	PROCESSENTRY32 pe;
	DWORD pid = GetCurrentProcessId();
	DWORD parent = 0;
	char name[128] = "";
	size_t len;

	snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap != INVALID_HANDLE_VALUE) {
		memset(&pe, 0, sizeof(pe));
		pe.dwSize = sizeof(pe);
		if (Process32First(snap, &pe)) {
			do {
				if (pe.th32ProcessID == pid) {
					parent = pe.th32ParentProcessID;
					break;
				}
			} while (Process32Next(snap, &pe));
		}
		if (parent != 0) {
			memset(&pe, 0, sizeof(pe));
			pe.dwSize = sizeof(pe);
			if (Process32First(snap, &pe)) {
				do {
					if (pe.th32ProcessID == parent) {
						snprintf(name, sizeof(name), "%s", pe.szExeFile);
						break;
					}
				} while (Process32Next(snap, &pe));
			}
		}
		CloseHandle(snap);
	}
	if (name[0] == '\0') {
		const char *shell = getenv("SHELL");

		if (shell == NULL || shell[0] == '\0')
			shell = getenv("ComSpec");
		if (shell == NULL || shell[0] == '\0')
			shell = getenv("COMSPEC");
		shitfetch_basename(shell != NULL ? shell : "unknown", name, sizeof(name));
	}
	len = strlen(name);
	if (len > 4 && _stricmp(name + len - 4, ".exe") == 0)
		name[len - 4] = '\0';
	snprintf(out, cap, "%s", name[0] != '\0' ? name : "unknown");
}

static void
detect_dewm(char *out, size_t cap)
{
	snprintf(out, cap, "Explorer");
}

static void
detect_init(char *out, size_t cap)
{
	snprintf(out, cap, "Service Control Manager");
}

static void
detect_term(char *out, size_t cap)
{
	const char *value = getenv("TERM_PROGRAM");
	if (value != NULL && value[0] != '\0') {
		snprintf(out, cap, "%s", value);
		return;
	}
	if (getenv("WT_SESSION") != NULL) {
		snprintf(out, cap, "Windows Terminal");
		return;
	}
	if (getenv("ConEmuANSI") != NULL) {
		snprintf(out, cap, "ConEmu");
		return;
	}
	if (getenv("ANSICON") != NULL) {
		snprintf(out, cap, "ANSICON");
		return;
	}
	snprintf(out, cap, "console");
}

static void
detect_cpu(char *out, size_t cap)
{
	HKEY key;
	char value[256];
	DWORD value_size = sizeof(value);

	out[0] = '\0';
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
	    "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS) {
		if (RegQueryValueExA(key, "ProcessorNameString", NULL, NULL,
		    (LPBYTE)value, &value_size) == ERROR_SUCCESS) {
			value[sizeof(value) - 1] = '\0';
			shitfetch_trim(value);
			snprintf(out, cap, "%s", value);
		}
		RegCloseKey(key);
	}
	if (out[0] == '\0')
		snprintf(out, cap, "unknown");
}

static void
detect_memory_swap(char *mem_out, size_t mem_cap, char *swap_out, size_t swap_cap)
{
	MEMORYSTATUSEX st;
	unsigned long long total_mb;
	unsigned long long used_mb;
	unsigned long long swap_total_mb;
	unsigned long long swap_used_mb;

	memset(&st, 0, sizeof(st));
	st.dwLength = sizeof(st);
	if (!GlobalMemoryStatusEx(&st)) {
		snprintf(mem_out, mem_cap, "unknown");
		snprintf(swap_out, swap_cap, "unknown");
		return;
	}
	total_mb = st.ullTotalPhys / (1024ULL * 1024ULL);
	used_mb = (st.ullTotalPhys - st.ullAvailPhys) / (1024ULL * 1024ULL);
	snprintf(mem_out, mem_cap, "%lluMB / %lluMB (%%{%lu})",
		used_mb, total_mb, (unsigned long)st.dwMemoryLoad);

	swap_total_mb = st.ullTotalPageFile / (1024ULL * 1024ULL);
	swap_used_mb = (st.ullTotalPageFile - st.ullAvailPageFile) / (1024ULL * 1024ULL);
	snprintf(swap_out, swap_cap, "%lluMB / %lluMB (%%{%u})",
		swap_used_mb, swap_total_mb,
		swap_total_mb == 0 ? 0 : (unsigned int)((swap_used_mb * 100ULL) / swap_total_mb));
}

static void
detect_host(char *out, size_t cap)
{
	DWORD size = (DWORD)cap;
	if (GetComputerNameA(out, &size) == 0)
		snprintf(out, cap, "unknown");
}

static void
detect_kernel(char *out, size_t cap)
{
	DWORD major;
	DWORD minor;
	DWORD build;

	detect_windows_version(&major, &minor, &build);
	if (major != 0)
		snprintf(out, cap, "%lu.%lu.%lu", (unsigned long)major, (unsigned long)minor, (unsigned long)build);
	else
		snprintf(out, cap, "unknown");
}

static void
detect_locale(char *out, size_t cap)
{
	char name[LOCALE_NAME_MAX_LENGTH];
	if (GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SNAME, name, sizeof(name)) > 0) {
		snprintf(out, cap, "%s", name);
		return;
	}
	if (GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SABBREVLANGNAME, name, sizeof(name)) > 0) {
		snprintf(out, cap, "%s", name);
		return;
	}
	if (GetUserDefaultLCID() != 0) {
		snprintf(out, cap, "%lu", (unsigned long)GetUserDefaultLCID());
		return;
	}
	snprintf(out, cap, "%s", setlocale(LC_CTYPE, NULL) != NULL ? setlocale(LC_CTYPE, NULL) : "C");
}

static void
detect_local_ip(char *out, size_t cap)
{
	IP_ADAPTER_ADDRESSES *addrs;
	IP_ADAPTER_ADDRESSES *adapter;
	ULONG len = 15000;
	DWORD ret;

	out[0] = '\0';
	addrs = malloc(len);
	if (addrs == NULL) {
		snprintf(out, cap, "unknown");
		return;
	}
	ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
		GAA_FLAG_SKIP_DNS_SERVER, NULL, addrs, &len);
	if (ret == ERROR_BUFFER_OVERFLOW) {
		free(addrs);
		addrs = malloc(len);
		if (addrs != NULL)
			ret = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
				GAA_FLAG_SKIP_DNS_SERVER, NULL, addrs, &len);
	}
	if (addrs == NULL || ret != NO_ERROR) {
		free(addrs);
		snprintf(out, cap, "unknown");
		return;
	}
	for (adapter = addrs; adapter != NULL; adapter = adapter->Next) {
		IP_ADAPTER_UNICAST_ADDRESS *ua;
		if (adapter->OperStatus != IfOperStatusUp)
			continue;
		for (ua = adapter->FirstUnicastAddress; ua != NULL; ua = ua->Next) {
			struct sockaddr_in *sa;
			if (ua->Address.lpSockaddr == NULL || ua->Address.lpSockaddr->sa_family != AF_INET)
				continue;
			sa = (struct sockaddr_in *)ua->Address.lpSockaddr;
			if ((ntohl(sa->sin_addr.s_addr) >> 24) == 127)
				continue;
			if (InetNtopA(AF_INET, &sa->sin_addr, out, (DWORD)cap) != NULL) {
				free(addrs);
				return;
			}
		}
	}
	free(addrs);
	snprintf(out, cap, "unknown");
}

#else

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
		if (len >= 2 && (value[0] == '"' || value[0] == '\'') &&
		    value[len - 1] == value[0]) {
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
openrc_is_running(void)
{
	return shitfetch_file_exists("/run/openrc/softlevel") ||
		shitfetch_file_exists("/sbin/openrc-run");
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

	if (generic[0] != '\0') {
		if (openrc_is_running()) {
			snprintf(out, cap, "OpenRC");
			return;
		}
		snprintf(out, cap, "%s", generic);
		return;
	}
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

static void
detect_locale(char *out, size_t cap)
{
	const char *value;

	if (out == NULL || cap == 0)
		return;
	value = getenv("LC_ALL");
	if (value == NULL || value[0] == '\0')
		value = getenv("LC_CTYPE");
	if (value == NULL || value[0] == '\0')
		value = getenv("LANG");
	if (value == NULL || value[0] == '\0')
		value = setlocale(LC_CTYPE, NULL);
	if (value == NULL || value[0] == '\0' || strcmp(value, "C") == 0)
		value = "C";
	snprintf(out, cap, "%s", value);
}

static void
detect_local_ip(char *out, size_t cap)
{
	struct ifaddrs *ifaddr;
	struct ifaddrs *ifa;

	if (out == NULL || cap == 0)
		return;
	out[0] = '\0';
	if (getifaddrs(&ifaddr) != 0) {
		snprintf(out, cap, "unknown");
		return;
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		const struct sockaddr_in *addr;
		char ip[INET_ADDRSTRLEN];

		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if ((ifa->ifa_flags & IFF_LOOPBACK) != 0 || (ifa->ifa_flags & IFF_UP) == 0)
			continue;
		addr = (const struct sockaddr_in *)ifa->ifa_addr;
		if (inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)) == NULL)
			continue;
		snprintf(out, cap, "%s", ip);
		break;
	}

	freeifaddrs(ifaddr);
	if (out[0] == '\0')
		snprintf(out, cap, "unknown");
}
#endif

void
shitfetch_collect_data(const struct shitfetch_settings *settings, struct shitfetch_data *data)
{
	memset(data, 0, sizeof(*data));

#ifdef _WIN32
	read_windows_os(data->os_id, sizeof(data->os_id), data->os_pretty, sizeof(data->os_pretty));
#else
	read_os_release(data->os_id, sizeof(data->os_id), data->os_pretty, sizeof(data->os_pretty));
#endif
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

	if (settings->module_enabled[SHITFETCH_MODULE_DISK])
		detect_disks(settings, data);
	else {
		data->disk_count = 1;
		snprintf(data->disk_mounts[0], sizeof(data->disk_mounts[0]), "/");
		snprintf(data->disk_values[0], sizeof(data->disk_values[0]), "unknown");
		snprintf(data->disk, sizeof(data->disk), "unknown");
	}

	if (settings->module_enabled[SHITFETCH_MODULE_PACKAGES])
		detect_packages(data->os_id, data->packages, sizeof(data->packages));
	else
		snprintf(data->packages, sizeof(data->packages), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_DISPLAY])
		detect_display(data->display_id, sizeof(data->display_id), data->display, sizeof(data->display));
	else {
		snprintf(data->display_id, sizeof(data->display_id), "unknown");
		snprintf(data->display, sizeof(data->display), "unknown");
	}

	if (settings->module_enabled[SHITFETCH_MODULE_LOCALE])
		detect_locale(data->locale, sizeof(data->locale));
	else
		snprintf(data->locale, sizeof(data->locale), "unknown");

	if (settings->module_enabled[SHITFETCH_MODULE_LOCAL_IP])
		detect_local_ip(data->local_ip, sizeof(data->local_ip));
	else
		snprintf(data->local_ip, sizeof(data->local_ip), "unknown");

	if (data->os_pretty[0] == '\0')
		snprintf(data->os_pretty, sizeof(data->os_pretty), "unknown");
	if (data->os_id[0] == '\0')
		snprintf(data->os_id, sizeof(data->os_id), "linux");
	if (data->gpu[0] == '\0')
		snprintf(data->gpu, sizeof(data->gpu), "unknown");
	if (data->display[0] == '\0')
		snprintf(data->display, sizeof(data->display), "unknown");
	if (data->locale[0] == '\0')
		snprintf(data->locale, sizeof(data->locale), "unknown");
	if (data->local_ip[0] == '\0')
		snprintf(data->local_ip, sizeof(data->local_ip), "unknown");
}

/* Re-reads only the values that move while the spinning logo is on screen. A full
   shitfetch_collect_data() would re-scan package managers and probe DRM every tick. */
void
shitfetch_refresh_live(const struct shitfetch_settings *settings, struct shitfetch_data *data)
{
	if (settings->module_enabled[SHITFETCH_MODULE_UPTIME])
		detect_uptime(data->uptime, sizeof(data->uptime));
	if (settings->module_enabled[SHITFETCH_MODULE_MEMORY] || settings->module_enabled[SHITFETCH_MODULE_SWAP])
		detect_memory_swap(data->memory, sizeof(data->memory), data->swap, sizeof(data->swap));
}
