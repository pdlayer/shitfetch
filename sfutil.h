#ifndef SHITFETCH_UTIL_H
#define SHITFETCH_UTIL_H

#include <stdbool.h>
#include <stddef.h>

bool shitfetch_file_exists(const char *path);
bool shitfetch_executable_exists(const char *name);
void shitfetch_identity(char *user_out, size_t user_cap, char *host_out, size_t host_cap);
void shitfetch_strlower(char *s);
void shitfetch_trim(char *s);
void shitfetch_basename(const char *path, char *out, size_t out_cap);

#endif
