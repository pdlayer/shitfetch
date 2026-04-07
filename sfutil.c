#define _POSIX_C_SOURCE 200809L

#include "sfutil.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool
shitfetch_file_exists(const char *path)
{
	struct stat st;

	if (path == NULL || path[0] == '\0')
		return false;
	return stat(path, &st) == 0;
}

bool
shitfetch_executable_exists(const char *name)
{
	const char *path_env;
	char *paths;
	char *token;
	char *saveptr;
	char full[4096];

	if (name == NULL || name[0] == '\0')
		return false;
	if (strchr(name, '/') != NULL)
		return access(name, X_OK) == 0;

	path_env = getenv("PATH");
	if (path_env == NULL || path_env[0] == '\0')
		return false;

	paths = strdup(path_env);
	if (paths == NULL)
		return false;

	token = strtok_r(paths, ":", &saveptr);
	while (token != NULL) {
		snprintf(full, sizeof(full), "%s/%s", token, name);
		if (access(full, X_OK) == 0) {
			free(paths);
			return true;
		}
		token = strtok_r(NULL, ":", &saveptr);
	}

	free(paths);
	return false;
}

void
shitfetch_strlower(char *s)
{
	for (; s != NULL && *s != '\0'; s++)
		*s = (char)tolower((unsigned char)*s);
}

void
shitfetch_trim(char *s)
{
	size_t len;
	size_t start;
	size_t i;

	if (s == NULL)
		return;

	len = strlen(s);
	while (len > 0 && isspace((unsigned char)s[len - 1]))
		s[--len] = '\0';

	start = 0;
	while (s[start] != '\0' && isspace((unsigned char)s[start]))
		start++;
	if (start == 0)
		return;

	for (i = 0; s[start + i] != '\0'; i++)
		s[i] = s[start + i];
	s[i] = '\0';
}

void
shitfetch_basename(const char *path, char *out, size_t out_cap)
{
	const char *base;

	if (out == NULL || out_cap == 0)
		return;
	out[0] = '\0';
	if (path == NULL)
		return;

	base = strrchr(path, '/');
	if (base == NULL)
		base = path;
	else
		base++;

	snprintf(out, out_cap, "%s", base);
}
