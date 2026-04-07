#ifndef SF_COLOR_H
#define SF_COLOR_H

#include <stdbool.h>
#include <stddef.h>

void sfcolor_palette_for_os(const char *os_id, const char *out[9]);
bool sfcolor_resolve(const char *spec, const char *logo_color, char *out, size_t out_cap);

#endif
