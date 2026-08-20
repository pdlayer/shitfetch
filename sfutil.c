#define _POSIX_C_SOURCE 200809L

#include "sfutil.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define access _access
#ifndef X_OK
#define X_OK 0
#endif
#else
#include <unistd.h>
#endif

void
shitfetch_identity(char *user_out, size_t user_cap, char *host_out, size_t host_cap)
{
#ifdef _WIN32
	const char *user = getenv("USERNAME");
	DWORD host_size = (DWORD)host_cap;

	if (user_cap == 0 || host_cap == 0)
		return;
	if (user == NULL || user[0] == '\0')
		user = getenv("USER");
	if (user == NULL || user[0] == '\0')
		user = "user";
	if (GetComputerNameA(host_out, &host_size) == 0)
		snprintf(host_out, host_cap, "host");
	snprintf(user_out, user_cap, "%s", user);
#else
	const char *user = getenv("USER");
	char host[128];
	char *dot;

	if (user_cap == 0 || host_cap == 0)
		return;
	if (user == NULL || user[0] == '\0')
		user = "user";
	if (gethostname(host, sizeof(host)) != 0) {
		snprintf(host, sizeof(host), "host");
	} else {
		host[sizeof(host) - 1] = '\0';
		dot = strchr(host, '.');
		if (dot != NULL)
			*dot = '\0';
	}
	snprintf(user_out, user_cap, "%s", user);
	snprintf(host_out, host_cap, "%s", host);
#endif
}

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
#ifndef _WIN32
	char *saveptr;
#endif
	char full[4096];
#ifdef _WIN32
	const char *exts[] = { "", ".exe", ".cmd", ".bat", ".com" };
	size_t i;
#endif

	if (name == NULL || name[0] == '\0')
		return false;
#ifdef _WIN32
	if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) {
		for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
			snprintf(full, sizeof(full), "%s%s", name, exts[i]);
			if (access(full, X_OK) == 0)
				return true;
		}
		return false;
	}
#else
	if (strchr(name, '/') != NULL)
		return access(name, X_OK) == 0;
#endif

	path_env = getenv("PATH");
	if (path_env == NULL || path_env[0] == '\0')
		return false;

#ifdef _WIN32
	paths = _strdup(path_env);
#else
	paths = strdup(path_env);
#endif
	if (paths == NULL)
		return false;

#ifdef _WIN32
	token = strtok(paths, ";");
	while (token != NULL) {
		for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
			snprintf(full, sizeof(full), "%s\\%s%s", token, name, exts[i]);
			if (access(full, X_OK) == 0) {
				free(paths);
				return true;
			}
		}
		token = strtok(NULL, ";");
	}
#else
	token = strtok_r(paths, ":", &saveptr);
	while (token != NULL) {
		snprintf(full, sizeof(full), "%s/%s", token, name);
		if (access(full, X_OK) == 0) {
			free(paths);
			return true;
		}
		token = strtok_r(NULL, ":", &saveptr);
	}
#endif

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
#ifdef _WIN32
	{
		const char *winbase = strrchr(path, '\\');
		if (base == NULL || (winbase != NULL && winbase > base))
			base = winbase;
	}
#endif
	if (base == NULL)
		base = path;
	else
		base++;

	snprintf(out, out_cap, "%s", base);
}
