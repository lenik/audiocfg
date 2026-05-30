#ifndef LIB_H
#define LIB_H

#include <stdio.h>

struct acfg_session;

struct acfg_session *acfg_open(const char *prog);
void acfg_close(struct acfg_session *s);

int acfg_list_devices(struct acfg_session *s, FILE *out);
int acfg_list_profiles(struct acfg_session *s, const char *device, FILE *out);
int acfg_set_profile(struct acfg_session *s, const char *device, const char *profile);
int acfg_toggle_profiles(struct acfg_session *s, const char *device, const char *profiles);

int acfg_match_name(const char *name, const char *desc, const char *want);

/* Parse device spec: N, :CARD, [playback|capture]:CARD, or name. Returns 0 on success. */
int acfg_parse_device_spec(const char *spec, int *unified, int *kind, unsigned *card,
                           const char **name);

#endif /* LIB_H */
