#ifndef SHITFETCH_CONFIG_H
#define SHITFETCH_CONFIG_H

#include "sf.h"

#include <stdbool.h>
#include <stddef.h>

bool shitfetch_default_config_path(char *buf, size_t cap);
int shitfetch_load_config(struct shitfetch_settings *settings, const char *path,
	char *err, size_t err_cap);

#endif
