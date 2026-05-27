#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "sfdetect.h"
#include "sfdetectdisplay.h"
#include "sfutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

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
					if (crtc && crtc->mode_valid) {
						format_display_mode(&crtc->mode, conn, out, cap);
						drmModeFreeCrtc(crtc);
					}
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

