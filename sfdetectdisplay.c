#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "sfdetect.h"
#include "sfdetectdisplay.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <winreg.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

#ifdef _WIN32
static void
get_monitor_name(const unsigned char *edid, char *out, size_t cap)
{
	if (cap == 0)
		return;
	out[0] = '\0';
	if (edid == NULL)
		return;
	for (int i = 54; i <= 108; i += 18) {
		if (edid[i] == 0 && edid[i + 1] == 0 && edid[i + 2] == 0 &&
		    edid[i + 3] == 0xfc) {
			int j;

			for (j = 0; j < 13 && (size_t)j + 1 < cap; j++) {
				char c = (char)edid[i + 5 + j];

				if (c == 0x0a || c == 0x0d || c == 0x00)
					break;
				out[j] = c;
			}
			out[j] = '\0';
			shitfetch_trim(out);
			return;
		}
	}
}

static bool
read_monitor_name_from_registry(const char *device_id, char *out, size_t cap)
{
	HKEY key;
	BYTE edid[256];
	DWORD type = 0;
	DWORD size = sizeof(edid);
	char path[512];

	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	if (device_id == NULL || strncmp(device_id, "MONITOR\\", 8) != 0)
		return false;

	snprintf(path, sizeof(path), "SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters", device_id);
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
		return false;
	if (RegQueryValueExA(key, "EDID", NULL, &type, edid, &size) == ERROR_SUCCESS &&
	    type == REG_BINARY && size >= 128) {
		get_monitor_name(edid, out, cap);
	}
	RegCloseKey(key);
	return out[0] != '\0';
}

static bool
find_monitor_name_from_registry(char *out, size_t cap)
{
	HKEY display_key;
	DWORD vendor_idx;
	char vendor[128];
	DWORD vendor_len;

	if (out == NULL || cap == 0)
		return false;
	out[0] = '\0';
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\DISPLAY",
	    0, KEY_READ, &display_key) != ERROR_SUCCESS)
		return false;

	for (vendor_idx = 0; ; vendor_idx++) {
		HKEY vendor_key;
		DWORD instance_idx;

		vendor_len = sizeof(vendor);
		if (RegEnumKeyExA(display_key, vendor_idx, vendor, &vendor_len,
		    NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
			break;
		if (RegOpenKeyExA(display_key, vendor, 0, KEY_READ, &vendor_key) != ERROR_SUCCESS)
			continue;

		for (instance_idx = 0; ; instance_idx++) {
			HKEY params_key;
			BYTE edid[256];
			DWORD type = 0;
			DWORD size = sizeof(edid);
			char instance[128];
			char params_path[256];
			DWORD instance_len = sizeof(instance);

			if (RegEnumKeyExA(vendor_key, instance_idx, instance, &instance_len,
			    NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
				break;
			snprintf(params_path, sizeof(params_path), "%s\\Device Parameters", instance);
			if (RegOpenKeyExA(vendor_key, params_path, 0, KEY_READ, &params_key) != ERROR_SUCCESS)
				continue;
			if (RegQueryValueExA(params_key, "EDID", NULL, &type, edid, &size) == ERROR_SUCCESS &&
			    type == REG_BINARY && size >= 128) {
				get_monitor_name(edid, out, cap);
			}
			RegCloseKey(params_key);
			if (out[0] != '\0') {
				RegCloseKey(vendor_key);
				RegCloseKey(display_key);
				return true;
			}
		}
		RegCloseKey(vendor_key);
	}

	RegCloseKey(display_key);
	return false;
}

void
detect_display(char *id, size_t id_cap, char *out, size_t cap)
{
	DEVMODEA mode;
	DISPLAY_DEVICEA adapter;
	DISPLAY_DEVICEA monitor;
	int width = GetSystemMetrics(SM_CXSCREEN);
	int height = GetSystemMetrics(SM_CYSCREEN);

	id[0] = '\0';
	out[0] = '\0';
	memset(&adapter, 0, sizeof(adapter));
	adapter.cb = sizeof(adapter);
	memset(&monitor, 0, sizeof(monitor));
	monitor.cb = sizeof(monitor);

	if (EnumDisplayDevicesA(NULL, 0, &adapter, 0) &&
	    EnumDisplayDevicesA(adapter.DeviceName, 0, &monitor, 0)) {
		if (!read_monitor_name_from_registry(monitor.DeviceID, id, id_cap) &&
		    monitor.DeviceString[0] != '\0')
			snprintf(id, id_cap, "%s", monitor.DeviceString);
	}
	if ((id[0] == '\0' || strcmp(id, "Generic PnP Monitor") == 0) &&
	    find_monitor_name_from_registry(id, id_cap)) {
		/* keep EDID model from registry */
	}
	if (id[0] == '\0')
		snprintf(id, id_cap, "Display");

	memset(&mode, 0, sizeof(mode));
	mode.dmSize = sizeof(mode);
	if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
		snprintf(out, cap, "%ldx%ld@%ldHz",
			(long)mode.dmPelsWidth, (long)mode.dmPelsHeight, (long)mode.dmDisplayFrequency);
		return;
	}
	if (width > 0 && height > 0)
		snprintf(out, cap, "%dx%d", width, height);
	else
		snprintf(out, cap, "unknown");
}
#else

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
format_display_mode(const drmModeModeInfo *mode, drmModeConnector *conn, char *out, size_t cap)
{
	double vrefresh;
	bool is_internal;
	double inches = 0;

	if (!mode || !conn || !out || cap == 0)
		return;

	vrefresh = mode->vrefresh;
	if (vrefresh == 0 && mode->clock > 0 && mode->htotal > 0 && mode->vtotal > 0)
		vrefresh = (double)mode->clock * 1000.0 / (mode->htotal * mode->vtotal);

	is_internal = conn->connector_type == DRM_MODE_CONNECTOR_eDP ||
		conn->connector_type == DRM_MODE_CONNECTOR_LVDS ||
		conn->connector_type == DRM_MODE_CONNECTOR_DSI;

	if (conn->mmWidth > 0 && conn->mmHeight > 0) {
		double w = (double)conn->mmWidth / 25.4;
		double h = (double)conn->mmHeight / 25.4;
		inches = sqrt(w * w + h * h);
	}

	if (inches > 0) {
		snprintf(out, cap, "%dx%d@%.0fHz, %.1f\" [%s]",
			mode->hdisplay, mode->vdisplay, round(vrefresh), inches,
			is_internal ? "Internal" : "External");
	} else {
		snprintf(out, cap, "%dx%d@%.0fHz, ?\" [%s]",
			mode->hdisplay, mode->vdisplay, round(vrefresh),
			is_internal ? "Internal" : "External");
	}
}

static const drmModeModeInfo *
pick_connector_mode(drmModeConnector *conn)
{
	const drmModeModeInfo *fallback = NULL;

	if (!conn || conn->count_modes <= 0)
		return NULL;

	for (int i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
			return &conn->modes[i];
		if (!fallback)
			fallback = &conn->modes[i];
	}

	return fallback;
}

void
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
					if (crtc != NULL && crtc->mode_valid)
						format_display_mode(&crtc->mode, conn, out, cap);
					if (crtc != NULL)
						drmModeFreeCrtc(crtc);
					drmModeFreeEncoder(enc);
				}
				if (out[0] == '\0') {
					const drmModeModeInfo *mode = pick_connector_mode(conn);

					if (mode)
						format_display_mode(mode, conn, out, cap);
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
#endif
