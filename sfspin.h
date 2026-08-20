#ifndef SHITFETCH_SPIN_H
#define SHITFETCH_SPIN_H

#include "sf.h"

/* Runs the 2.5D spinning-logo animation until a keypress, a signal, or the configured
   frame budget runs out. Returns 0 if the animation ran, -1 if the environment cannot
   host it (not a tty, no logo, no room, allocation failure) in which case nothing was
   written. Either way the caller still prints the static frame afterwards, so `data`
   may have been refreshed in place. */
int shitfetch_spin_run(const struct shitfetch_settings *settings, struct shitfetch_data *data,
	const char *key_color);

bool shitfetch_spin_parse_axis(const char *value, enum shitfetch_spin_axis *out);
bool shitfetch_spin_parse_light(const char *value, enum shitfetch_spin_light *out);
bool shitfetch_spin_parse_shade(const char *value, enum shitfetch_spin_shade *out);

/* Forces every field into its documented range; called once after the config file and the
   command line have both had their say, so no individual setter has to clamp. */
void shitfetch_spin_clamp(struct shitfetch_spin *spin);

#endif
