#define _POSIX_C_SOURCE 200809L

#include "sfdetect.h"
#include "sfdetectdisk.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

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

void
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
