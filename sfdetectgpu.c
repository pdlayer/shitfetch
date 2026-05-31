#define _POSIX_C_SOURCE 200809L

#include "sfdetect.h"
#include "sfdetectgpu.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>

static bool
find_pci_ids_path(char *out, size_t cap)
{
	static const char *paths[] = {
		"/usr/share/hwdata/pci.ids",
		"/usr/share/misc/pci.ids",
		"/usr/share/pci.ids",
	};
	size_t i;
	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		if (shitfetch_file_exists(paths[i])) {
			snprintf(out, cap, "%s", paths[i]);
			return true;
		}
	}
	return false;
}

static void
format_pci_gpu_name(const char *vendor, const char *device_name, char *out, size_t cap)
{
	const char *bracket_open;
	const char *bracket_close;
	if (out == NULL || cap == 0)
		return;
	out[0] = '\0';
	if (device_name == NULL || device_name[0] == '\0') {
		snprintf(out, cap, "%s Graphics", vendor ? vendor : "Unknown");
		return;
	}
	bracket_open = strchr(device_name, '[');
	bracket_close = bracket_open ? strchr(bracket_open, ']') : NULL;
	if (bracket_open != NULL && bracket_close != NULL && bracket_close > bracket_open + 1) {
		char codename[128];
		char pretty[192];
		size_t code_len = (size_t)(bracket_open - device_name);
		size_t pretty_len = (size_t)(bracket_close - bracket_open - 1);
		while (code_len > 0 && isspace((unsigned char)device_name[code_len - 1]))
			code_len--;
		if (code_len >= sizeof(codename))
			code_len = sizeof(codename) - 1;
		if (pretty_len >= sizeof(pretty))
			pretty_len = sizeof(pretty) - 1;
		memcpy(codename, device_name, code_len);
		codename[code_len] = '\0';
		memcpy(pretty, bracket_open + 1, pretty_len);
		pretty[pretty_len] = '\0';
		shitfetch_trim(codename);
		shitfetch_trim(pretty);
		if (pretty[0] != '\0' && codename[0] != '\0') {
			if (strcmp(vendor ? vendor : "", "AMD") == 0 &&
				strstr(pretty, "Radeon Vega") != NULL) {
				snprintf(out, cap, "AMD Radeon Vega %s", codename);
				return;
			}
			snprintf(out, cap, "%s %s (%s)", vendor ? vendor : "Unknown", pretty, codename);
			return;
		}
		if (pretty[0] != '\0') {
			snprintf(out, cap, "%s %s", vendor ? vendor : "Unknown", pretty);
			return;
		}
	}
	snprintf(out, cap, "%s %s", vendor ? vendor : "Unknown", device_name);
}


static bool
lookup_pci_device_name(const char *vendor_id, const char *device_id,
	const char *subsystem_vendor_id, const char *subsystem_device_id,
	char *out, size_t cap)
{
	char pci_ids_path[SHITFETCH_MAX_PATH];
	FILE *fp;
	char line[512];
	char vendor_hex[16];
	char device_hex[16];
	char subvendor_hex[16];
	char subdevice_hex[16];
	char io_buf[65536];
	bool in_vendor = false;
	bool in_device = false;
	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	if (vendor_id == NULL || device_id == NULL)
		return false;
	if (!find_pci_ids_path(pci_ids_path, sizeof(pci_ids_path)))
		return false;
	snprintf(vendor_hex, sizeof(vendor_hex), "%s", vendor_id);
	snprintf(device_hex, sizeof(device_hex), "%s", device_id);
	shitfetch_strlower(vendor_hex);
	shitfetch_strlower(device_hex);
	if (strncmp(vendor_hex, "0x", 2) == 0)
		memmove(vendor_hex, vendor_hex + 2, strlen(vendor_hex + 2) + 1);
	if (strncmp(device_hex, "0x", 2) == 0)
		memmove(device_hex, device_hex + 2, strlen(device_hex + 2) + 1);
	subvendor_hex[0] = '\0';
	subdevice_hex[0] = '\0';
	if (subsystem_vendor_id != NULL && subsystem_vendor_id[0] != '\0') {
		snprintf(subvendor_hex, sizeof(subvendor_hex), "%s", subsystem_vendor_id);
		shitfetch_strlower(subvendor_hex);
		if (strncmp(subvendor_hex, "0x", 2) == 0)
			memmove(subvendor_hex, subvendor_hex + 2, strlen(subvendor_hex + 2) + 1);
	}
	if (subsystem_device_id != NULL && subsystem_device_id[0] != '\0') {
		snprintf(subdevice_hex, sizeof(subdevice_hex), "%s", subsystem_device_id);
		shitfetch_strlower(subdevice_hex);
		if (strncmp(subdevice_hex, "0x", 2) == 0)
			memmove(subdevice_hex, subdevice_hex + 2, strlen(subdevice_hex + 2) + 1);
	}
	fp = fopen(pci_ids_path, "r");
	if (fp == NULL)
		return false;
	(void)setvbuf(fp, io_buf, _IOFBF, sizeof(io_buf));
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *name;
		if (line[0] == '#')
			continue;
		if (line[0] != '\t') {
			in_vendor = false;
			in_device = false;
			if (strncmp(line, vendor_hex, 4) == 0 &&
				isspace((unsigned char)line[4])) {
				in_vendor = true;
			}
			continue;
		}
		if (!in_vendor)
			continue;
		if (line[1] != '\t') {
			in_device = false;
			if (strncmp(line + 1, device_hex, 4) == 0 &&
				isspace((unsigned char)line[5])) {
				in_device = true;
				name = line + 6;
				shitfetch_trim(name);
				snprintf(out, cap, "%s", name);
			}
			continue;
		}
		if (!in_device || subvendor_hex[0] == '\0' || subdevice_hex[0] == '\0')
			continue;
		if (strncmp(line + 2, subvendor_hex, 4) == 0 &&
			isspace((unsigned char)line[6]) &&
			strncmp(line + 7, subdevice_hex, 4) == 0 &&
			isspace((unsigned char)line[11])) {
			name = line + 12;
			shitfetch_trim(name);
			snprintf(out, cap, "%s", name);
			fclose(fp);
			return true;
		}
	}
	fclose(fp);
	return out[0] != '\0';
}

void
detect_gpu(struct shitfetch_data *data)
{
	DIR *dir;
	struct dirent *ent;
	char path[256];
	char info_path[256];
	char vendor_id[64];
	char subsystem_vendor_id[64];
	char subsystem_device_id[64];
	char slot_name[64];
	char model[256];
	const char *vendor;
	size_t gpu_count = 0;
	data->gpu[0] = '\0';
	data->gpu_count = 0;
	dir = opendir("/sys/class/drm");
	if (dir != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			char value[256];
			bool have_model = false;
			if (strncmp(ent->d_name, "card", 4) != 0 || strchr(ent->d_name, '-') != NULL)
				continue;
			if (gpu_count >= SHITFETCH_MAX_GPUS)
				break;
			snprintf(path, sizeof(path), "/sys/class/drm/%.120s/device/vendor", ent->d_name);
			read_first_line(path, vendor_id, sizeof(vendor_id));
			if (vendor_id[0] == '\0')
				continue;
			snprintf(path, sizeof(path), "/sys/class/drm/%.120s/device/subsystem_vendor", ent->d_name);
			read_first_line(path, subsystem_vendor_id, sizeof(subsystem_vendor_id));
			snprintf(path, sizeof(path), "/sys/class/drm/%.120s/device/subsystem_device", ent->d_name);
			read_first_line(path, subsystem_device_id, sizeof(subsystem_device_id));
			if (strcmp(vendor_id, "0x10de") == 0)
				vendor = "NVIDIA";
			else if (strcmp(vendor_id, "0x1002") == 0 || strcmp(vendor_id, "0x1022") == 0)
				vendor = "AMD";
			else if (strcmp(vendor_id, "0x8086") == 0)
				vendor = "Intel";
			else
				vendor = "Unknown";
			snprintf(data->gpu_ids[gpu_count], sizeof(data->gpu_ids[gpu_count]), "%s", ent->d_name);
			snprintf(path, sizeof(path), "/sys/class/drm/%.120s/device/uevent", ent->d_name);
			slot_name[0] = '\0';
			(void)read_keyed_line(path, "PCI_SLOT_NAME=", slot_name, sizeof(slot_name));
			if (strcmp(vendor_id, "0x10de") == 0 && slot_name[0] != '\0') {
				snprintf(info_path, sizeof(info_path),
					"/proc/driver/nvidia/gpus/%s/information", slot_name);
				if (read_keyed_line(info_path, "Model:", value, sizeof(value)) && value[0] != '\0') {
					snprintf(data->gpu_values[gpu_count], sizeof(data->gpu_values[gpu_count]), "%s", value);
					have_model = true;
				}
			}
			if (!have_model) {
				snprintf(path, sizeof(path), "/sys/class/drm/%.120s/device/device", ent->d_name);
				read_first_line(path, model, sizeof(model));
				if (model[0] != '\0' &&
					lookup_pci_device_name(vendor_id, model, subsystem_vendor_id, subsystem_device_id,
						value, sizeof(value))) {
					format_pci_gpu_name(vendor, value,
						data->gpu_values[gpu_count], sizeof(data->gpu_values[gpu_count]));
				} else if (model[0] != '\0') {
					snprintf(data->gpu_values[gpu_count], sizeof(data->gpu_values[gpu_count]),
						"%s Graphics (%s)", vendor, model);
				} else {
					snprintf(data->gpu_values[gpu_count], sizeof(data->gpu_values[gpu_count]),
						"%s Graphics", vendor);
				}
			}
			gpu_count++;
		}
		closedir(dir);
	}
	data->gpu_count = gpu_count;
	if (gpu_count > 0) {
		snprintf(data->gpu, sizeof(data->gpu), "%s", data->gpu_values[0]);
		return;
	}
	snprintf(data->gpu, sizeof(data->gpu), "unknown");
}
