#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "sfdetect.h"
#include "sfdetectpkgs.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

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
count_nonhidden_dir_entries(const char *path)
{
	DIR *dir = opendir(path);
	struct dirent *ent;
	int count = 0;
	if (dir == NULL) return -1;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		count++;
	}
	closedir(dir);
	return count;
}

static bool
dir_contains_file(const char *dir_path, const char *file_name)
{
	char path[SHITFETCH_MAX_PATH];
	struct stat st;

	if (dir_path == NULL || file_name == NULL)
		return false;
	snprintf(path, sizeof(path), "%s/%s", dir_path, file_name);
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

#define DEFINE_PATH_COUNTER(name, helper, path_literal) static int name##_path(const char *path) { return helper(path); } static int name(void) { return name##_path(path_literal); }

DEFINE_PATH_COUNTER(count_pacman_local, count_nonhidden_dir_entries, "/var/lib/pacman/local")


static int
count_dpkg_status_file(const char *path)
{
	FILE *fp;
	char io_buf[65536];
	char line[256];
	int count = 0;
	if (path == NULL || path[0] == '\0')
		return -1;
	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	(void)setvbuf(fp, io_buf, _IOFBF, sizeof(io_buf));
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

DEFINE_PATH_COUNTER(count_pkgtools, count_nonhidden_dir_entries, "/var/log/packages")
DEFINE_PATH_COUNTER(count_scratchpkg, count_nonhidden_dir_entries, "/var/db/scratchpkg/index")

static int
count_paludis_exndbam_path(const char *path)
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
			char pkg_path[SHITFETCH_MAX_PATH];
			struct stat st;
			if (pkg->d_name[0] == '.')
				continue;
			snprintf(pkg_path, sizeof(pkg_path), "%s/%s", cat_path, pkg->d_name);
			if (stat(pkg_path, &st) == 0 && S_ISDIR(st.st_mode))
				count++;
		}
		closedir(pkgs);
	}
	closedir(cats);
	return count;
}

static int
count_paludis_exndbam(void)
{
	return count_paludis_exndbam_path("/var/db/paludis/repositories/installed");
}

static int
count_portage_vardb_path(const char *path)
{
	DIR *cats;
	struct dirent *cat;
	int count = 0;

	cats = opendir(path);
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
			char pkg_path[SHITFETCH_MAX_PATH];

			if (pkg->d_name[0] == '.')
				continue;
			snprintf(pkg_path, sizeof(pkg_path), "%s/%s", cat_path, pkg->d_name);
			if (dir_contains_file(pkg_path, "CONTENTS"))
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
	return count_portage_vardb_path("/var/db/pkg");
}

DEFINE_PATH_COUNTER(count_sorcery, count_nonhidden_dir_entries, "/var/log/sorcery/install")
DEFINE_PATH_COUNTER(count_kiss_installed, count_nonhidden_dir_entries, "/var/db/kiss/installed")
DEFINE_PATH_COUNTER(count_tazpkg_installed, count_nonhidden_dir_entries, "/var/lib/tazpkg/installed")

static int
count_pisi_package_path(const char *path)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		char pkg_path[SHITFETCH_MAX_PATH];

		if (ent->d_name[0] == '.')
			continue;
		snprintf(pkg_path, sizeof(pkg_path), "%s/%s", path, ent->d_name);
		if (dir_contains_file(pkg_path, "metadata.xml") || dir_contains_file(pkg_path, "files.xml"))
			count++;
	}
	closedir(dir);
	return count;
}

static int
count_pisi(void)
{
	return count_pisi_package_path("/var/lib/pisi/package");
}

static int
count_first_positive_path(const char *const *paths, size_t count, int (*fn)(const char *));

static int
count_pkgsrc_path(const char *path)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		char pkg_path[SHITFETCH_MAX_PATH];

		if (ent->d_name[0] == '.')
			continue;
		snprintf(pkg_path, sizeof(pkg_path), "%s/%s", path, ent->d_name);
		if (dir_contains_file(pkg_path, "+CONTENTS"))
			count++;
	}
	closedir(dir);
	return count;
}

static int
count_pkgsrc(void)
{
	static const char *paths[] = {
		"/var/db/pkg",
		"/usr/pkg/pkgdb",
	};

	return count_first_positive_path(paths, sizeof(paths) / sizeof(paths[0]), count_pkgsrc_path);
}

static int
count_pkgutils_db_file(const char *path)
{
	FILE *fp;
	char line[512];
	int count = 0;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p = line;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0' || *p == '\n' || *p == '#')
			continue;
		if (strchr(p, '#') != NULL && strstr(p, ".pkg.tar") != NULL)
			count++;
	}
	fclose(fp);
	return count;
}

static int
count_pkgutils(void)
{
	return count_pkgutils_db_file("/var/lib/pkg/db");
}

static int
count_opkg_status_file(const char *path)
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
		if (strncmp(line, "Status:", 7) == 0 && strstr(line, " installed") != NULL)
			count++;
	}
	fclose(fp);
	return count;
}

static int
count_opkg(void)
{
	return count_opkg_status_file("/usr/lib/opkg/status");
}

DEFINE_PATH_COUNTER(count_eopkg, count_nonhidden_dir_entries, "/var/lib/eopkg/package")

static int
count_swupd_path(const char *path)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;

	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		size_t len;

		if (ent->d_name[0] == '.')
			continue;
		len = strlen(ent->d_name);
		if (len > 8 && strcmp(ent->d_name + len - 8, ".version") == 0)
			continue;
		count++;
	}
	closedir(dir);
	return count;
}

static int
count_swupd(void)
{
	return count_swupd_path("/usr/share/clear/bundles");
}

static int
count_manifest_nix_file(const char *path)
{
	FILE *fp;
	char line[512];
	int count = 0;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p = line;

		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "{ meta", 6) == 0 || strncmp(p, "{ name", 6) == 0)
			count++;
	}
	fclose(fp);
	return count;
}

static int
count_manifest_json_file(const char *path)
{
	FILE *fp;
	char line[1024];
	int count = 0;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strstr(line, "\"storePath\"") != NULL)
			count++;
	}
	fclose(fp);
	return count;
}

static int
count_nix_profile_path(const char *path)
{
	char manifest_path[SHITFETCH_MAX_PATH];
	int count;

	snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", path);
	count = count_manifest_json_file(manifest_path);
	if (count > 0)
		return count;
	snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.nix", path);
	return count_manifest_nix_file(manifest_path);
}

static bool
nix_manifest_seen(const struct stat *seen, size_t seen_count, const struct stat *st)
{
	size_t i;

	for (i = 0; i < seen_count; i++) {
		if (seen[i].st_dev == st->st_dev && seen[i].st_ino == st->st_ino)
			return true;
	}
	return false;
}

static void
add_nix_profile_count(const char *path, int *total, struct stat *seen, size_t *seen_count, size_t seen_cap)
{
	char manifest_path[SHITFETCH_MAX_PATH];
	struct stat st;
	int count;

	if (path == NULL || total == NULL || seen == NULL || seen_count == NULL)
		return;
	snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", path);
	if (stat(manifest_path, &st) == 0) {
		if (nix_manifest_seen(seen, *seen_count, &st))
			return;
		count = count_manifest_json_file(manifest_path);
		if (count > 0) {
			if (*seen_count < seen_cap)
				seen[(*seen_count)++] = st;
			*total += count;
		}
		return;
	}
	snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.nix", path);
	if (stat(manifest_path, &st) == 0) {
		if (nix_manifest_seen(seen, *seen_count, &st))
			return;
		count = count_manifest_nix_file(manifest_path);
		if (count > 0) {
			if (*seen_count < seen_cap)
				seen[(*seen_count)++] = st;
			*total += count;
		}
	}
}

static int
count_nix(void)
{
	const char *home = getenv("HOME");
	const char *user = getenv("USER");
	const char *xdg_state = getenv("XDG_STATE_HOME");
	char path[SHITFETCH_MAX_PATH];
	struct stat seen[8];
	size_t seen_count = 0;
	int total = 0;

	if (xdg_state != NULL && xdg_state[0] != '\0') {
		snprintf(path, sizeof(path), "%s/nix/profiles/profile", xdg_state);
		add_nix_profile_count(path, &total, seen, &seen_count, sizeof(seen) / sizeof(seen[0]));
	} else if (home != NULL && home[0] != '\0') {
		snprintf(path, sizeof(path), "%s/.local/state/nix/profiles/profile", home);
		add_nix_profile_count(path, &total, seen, &seen_count, sizeof(seen) / sizeof(seen[0]));
	}
	if (home != NULL && home[0] != '\0') {
		snprintf(path, sizeof(path), "%s/.nix-profile", home);
		add_nix_profile_count(path, &total, seen, &seen_count, sizeof(seen) / sizeof(seen[0]));
	}
	if (user != NULL && user[0] != '\0') {
		snprintf(path, sizeof(path), "/nix/var/nix/profiles/per-user/%s/profile", user);
		add_nix_profile_count(path, &total, seen, &seen_count, sizeof(seen) / sizeof(seen[0]));
	}
	add_nix_profile_count("/nix/var/nix/profiles/per-user/root/profile",
		&total, seen, &seen_count, sizeof(seen) / sizeof(seen[0]));
	add_nix_profile_count("/nix/var/nix/profiles/system",
		&total, seen, &seen_count, sizeof(seen) / sizeof(seen[0]));
	return total > 0 ? total : -1;
}

static int
count_guix_manifest_file(const char *path)
{
	FILE *fp;
	char line[512];
	int count = 0;

	fp = fopen(path, "r");
	if (fp == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p = line;

		while (*p == ' ' || *p == '\t')
			p++;
		if (strncmp(p, "(name ", 6) == 0 || strncmp(p, "(\"", 2) == 0)
			count++;
	}
	fclose(fp);
	return count;
}

static int
count_guix_store_path(const char *path)
{
	return count_nonhidden_dir_entries(path);
}

static int
count_guix(void)
{
	const char *home = getenv("HOME");
	const char *user = getenv("USER");
	const char *profile = getenv("GUIX_PROFILE");
	char path[SHITFETCH_MAX_PATH];
	int count;

	if (profile != NULL && profile[0] != '\0') {
		snprintf(path, sizeof(path), "%s/manifest", profile);
		count = count_guix_manifest_file(path);
		if (count > 0)
			return count;
	}
	if (home != NULL && home[0] != '\0') {
		snprintf(path, sizeof(path), "%s/.guix-profile/manifest", home);
		count = count_guix_manifest_file(path);
		if (count > 0)
			return count;
	}
	if (user != NULL && user[0] != '\0') {
		snprintf(path, sizeof(path), "/var/guix/profiles/per-user/%s/guix-profile/manifest", user);
		count = count_guix_manifest_file(path);
		if (count > 0)
			return count;
	}
	count = count_guix_manifest_file("/var/guix/profiles/per-user/root/guix-profile/manifest");
	if (count > 0)
		return count;
	return count_guix_store_path("/gnu/store");
}

static int
count_flatpak_dir(const char *path)
{
	return count_nonhidden_dir_entries(path);
}

static int
count_flatpak_system(void)
{
	int count = 0;
	int apps = count_flatpak_dir("/var/lib/flatpak/app");
	int runtimes = count_flatpak_dir("/var/lib/flatpak/runtime");
	if (apps > 0)
		count += apps;
	if (runtimes > 0)
		count += runtimes;
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
count_homebrew_cellar_path(const char *path)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;
	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((ent = readdir(dir)) != NULL) {
		DIR *vers;
		struct dirent *v;
		char version_path[SHITFETCH_MAX_PATH];
		if (ent->d_name[0] == '.')
			continue;
		snprintf(version_path, sizeof(version_path), "%s/%s", path, ent->d_name);
		vers = opendir(version_path);
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
	return count;
}

static int
count_macports_path(const char *path)
{
	return count_homebrew_cellar_path(path);
}

static int
count_macports(void)
{
	return count_macports_path("/opt/local/var/macports/software");
}

static int
count_first_positive_path(const char *const *paths, size_t count, int (*fn)(const char *))
{
	size_t i;
	int last = -1;
	for (i = 0; i < count; i++) {
		last = fn(paths[i]);
		if (last > 0)
			return last;
	}
	return last;
}

static int
count_homebrew_opt(void)
{
	const char *home = getenv("HOME");
	char user_linuxbrew[SHITFETCH_MAX_PATH];
	char user_homebrew[SHITFETCH_MAX_PATH];
	const char *paths[4];
	if (home == NULL)
		home = "/root";
	snprintf(user_linuxbrew, sizeof(user_linuxbrew), "%s/.linuxbrew/opt", home);
	snprintf(user_homebrew, sizeof(user_homebrew), "%s/homebrew/opt", home);
	paths[0] = user_linuxbrew;
	paths[1] = "/home/linuxbrew/.linuxbrew/opt";
	paths[2] = user_homebrew;
	paths[3] = "/usr/local/opt";
	return count_first_positive_path(paths, 4, count_nonhidden_dir_entries);
}

static int
count_homebrew_cellar(void)
{
	const char *home = getenv("HOME");
	char user_linuxbrew[SHITFETCH_MAX_PATH];
	char user_homebrew[SHITFETCH_MAX_PATH];
	const char *paths[4];
	if (home == NULL)
		home = "/root";
	snprintf(user_linuxbrew, sizeof(user_linuxbrew), "%s/.linuxbrew/Cellar", home);
	snprintf(user_homebrew, sizeof(user_homebrew), "%s/homebrew/Cellar", home);
	paths[0] = user_linuxbrew;
	paths[1] = "/home/linuxbrew/.linuxbrew/Cellar";
	paths[2] = user_homebrew;
	paths[3] = "/usr/local/Cellar";
	return count_first_positive_path(paths, 4, count_homebrew_cellar_path);
}

struct package_spec {
	const char *label;
	int (*count)(void);
	int (*count_path)(const char *path);
	const char *bedrock_relpath;
};

static const struct package_spec package_specs[] = {
	{"pacman", count_pacman_local, count_pacman_local_path, "var/lib/pacman/local"},
	{"dpkg", count_dpkg_status, count_dpkg_status_file, "var/lib/dpkg/status"},
	{"apk", count_apk_installed, count_apk_installed_path, "lib/apk/db/installed"},
	{"xbps", count_xbps_db, count_xbps_db_path, "var/db/xbps"},
	{"pkgtools", count_pkgtools, count_pkgtools_path, "var/log/packages"},
	{"scratchpkg", count_scratchpkg, count_scratchpkg_path, "var/db/scratchpkg/index"},
	{"paludis", count_paludis_exndbam, count_paludis_exndbam_path, "var/db/paludis/repositories/installed"},
	{"portage", count_portage_vardb, count_portage_vardb_path, "var/db/pkg"},
	{"sorcery", count_sorcery, count_sorcery_path, "var/log/sorcery/install"},
	{"kiss", count_kiss_installed, count_kiss_installed_path, "var/db/kiss/installed"},
	{"tazpkg", count_tazpkg_installed, count_tazpkg_installed_path, "var/lib/tazpkg/installed"},
	{"pisi", count_pisi, count_pisi_package_path, "var/lib/pisi/package"},
	{"pkgsrc", count_pkgsrc, count_pkgsrc_path, "var/db/pkg"},
	{"pkgutils", count_pkgutils, count_pkgutils_db_file, "var/lib/pkg/db"},
	{"opkg", count_opkg, count_opkg_status_file, "usr/lib/opkg/status"},
	{"eopkg", count_eopkg, count_eopkg_path, "var/lib/eopkg/package"},
	{"swupd", count_swupd, count_swupd_path, "usr/share/clear/bundles"},
	{"nix", count_nix, count_nix_profile_path, "nix/var/nix/profiles/system"},
	{"guix", count_guix, count_guix_store_path, "gnu/store"},
	{"macports", count_macports, count_macports_path, "opt/local/var/macports/software"},
};

static const struct package_spec bedrock_package_specs[] = {
	{"pacman", count_pacman_local, count_pacman_local_path, "var/lib/pacman/local"},
	{"dpkg", count_dpkg_status, count_dpkg_status_file, "var/lib/dpkg/status"},
	{"apk", count_apk_installed, count_apk_installed_path, "lib/apk/db/installed"},
	{"xbps", count_xbps_db, count_xbps_db_path, "var/db/xbps"},
	{"pkgtools", count_pkgtools, count_pkgtools_path, "var/log/packages"},
	{"paludis", count_paludis_exndbam, count_paludis_exndbam_path, "var/db/paludis/repositories/installed"},
	{"portage", count_portage_vardb, count_portage_vardb_path, "var/db/pkg"},
	{"kiss", count_kiss_installed, count_kiss_installed_path, "var/db/kiss/installed"},
	{"pkgutils", count_pkgutils, count_pkgutils_db_file, "var/lib/pkg/db"},
	{"opkg", count_opkg, count_opkg_status_file, "usr/lib/opkg/status"},
	{"eopkg", count_eopkg, count_eopkg_path, "var/lib/eopkg/package"},
	{"swupd", count_swupd, count_swupd_path, "usr/share/clear/bundles"},
};

static bool
append_pkg_count(char *packages, size_t packages_cap, const char *label, int count)
{
	char seg[64];
	if (count <= 0)
		return false;
	snprintf(seg, sizeof(seg), "%d (%s)", count, label);
	append_pkg_segment(packages, packages_cap, seg);
	return true;
}

static int
count_homebrew(void)
{
	int opt = count_homebrew_opt();
	if (opt > 0)
		return opt;
	return count_homebrew_cellar();
}

static bool
detect_homebrew(const char *home)
{
	char path[SHITFETCH_MAX_PATH];
	snprintf(path, sizeof(path), "%s/.linuxbrew/opt", home);
	if (shitfetch_file_exists(path) || shitfetch_file_exists("/home/linuxbrew/.linuxbrew/opt"))
		return true;
	snprintf(path, sizeof(path), "%s/homebrew/opt", home);
	return shitfetch_file_exists(path) || shitfetch_file_exists("/usr/local/opt");
}

static bool
collect_bedrock_packages(char *packages, size_t packages_cap)
{
	DIR *dir;
	struct dirent *ent;
	int totals[sizeof(bedrock_package_specs) / sizeof(bedrock_package_specs[0])] = {0};
	bool any = false;
	size_t i;
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
		for (i = 0; i < sizeof(bedrock_package_specs) / sizeof(bedrock_package_specs[0]); i++) {
			snprintf(path, sizeof(path), "%s/%s", root, bedrock_package_specs[i].bedrock_relpath);
			n = bedrock_package_specs[i].count_path(path);
			if (n > 0) {
				totals[i] += n;
				any = true;
			}
		}
	}
	closedir(dir);
	packages[0] = '\0';
	for (i = 0; i < sizeof(bedrock_package_specs) / sizeof(bedrock_package_specs[0]); i++)
		append_pkg_count(packages, packages_cap, bedrock_package_specs[i].label, totals[i]);
	return any;
}

void
detect_packages(const char *os_id, char *packages, size_t packages_cap)
{
	bool has_any = false;
	bool is_bedrock = strcmp(os_id, "bedrock") == 0;
	bool have_flatpak_system = shitfetch_file_exists("/var/lib/flatpak/app") ||
		shitfetch_file_exists("/var/lib/flatpak/runtime");
	bool have_flatpak_user = false;
	bool have_snap = shitfetch_file_exists("/var/lib/snapd/snaps") || shitfetch_file_exists("/snap");
	const char *home = getenv("HOME");
	char path[SHITFETCH_MAX_PATH];
	size_t i;
	int count;
	packages[0] = '\0';
	if (is_bedrock && collect_bedrock_packages(packages, packages_cap))
		return;
	if (home == NULL)
		home = "/root";
	snprintf(path, sizeof(path), "%s/.local/share/flatpak/app", home);
	have_flatpak_user = shitfetch_file_exists(path);
	for (i = 0; i < sizeof(package_specs) / sizeof(package_specs[0]); i++) {
		count = package_specs[i].count();
		if (append_pkg_count(packages, packages_cap, package_specs[i].label, count))
			has_any = true;
	}
	if (have_flatpak_system || have_flatpak_user) {
		count = count_flatpak();
		if (append_pkg_count(packages, packages_cap, "flatpak", count))
			has_any = true;
	}
	if (have_snap) {
		count = count_snap();
		if (append_pkg_count(packages, packages_cap, "snap", count))
			has_any = true;
	}
	if (detect_homebrew(home)) {
		count = count_homebrew();
		if (append_pkg_count(packages, packages_cap, "homebrew", count))
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
