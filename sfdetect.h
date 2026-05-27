#ifndef SHITFETCH_DETECT_H
#define SHITFETCH_DETECT_H

#include <stdio.h>
#include "sf.h"

/*
 * Shared helpers used across detect modules.
 */

void read_first_line(const char *path, char *out, size_t cap);
bool read_keyed_line(const char *path, const char *key, char *out, size_t cap);

/*
 * Module-level detect functions called from sfdetect.c
 */

void detect_gpu(struct shitfetch_data *data);
void detect_disks(const struct shitfetch_settings *settings, struct shitfetch_data *data);
void detect_packages(const char *os_id, char *packages, size_t packages_cap);
void detect_display(char *id, size_t id_cap, char *out, size_t cap);

#endif
