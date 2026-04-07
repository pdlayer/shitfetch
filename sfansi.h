#ifndef SF_ANSI_H
#define SF_ANSI_H

#include <stddef.h>

void sfansi_parse_logo_line(const char *input, const char *palette[9],
	char *output, size_t output_cap, size_t *visible_len);

#endif
