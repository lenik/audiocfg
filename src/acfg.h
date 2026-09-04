#ifndef LIB_H
#define LIB_H

#include <stdio.h>

enum acfg_sel {
    ACFG_SEL_UNIFIED = 0, /* 1-based index from --list */
    ACFG_SEL_CARD = 1,    /* :NUM */
    ACFG_SEL_DESC = 2,    /* /PATTERN (substring of description or name) */
    ACFG_SEL_NAME = 3,    /* PulseAudio name, glob supported */
};

struct acfg_session;

struct acfg_session *acfg_open(const char *prog);
void acfg_close(struct acfg_session *s);

int acfg_list_devices(struct acfg_session *s, FILE *out);
int acfg_list_profiles(struct acfg_session *s, const char **devices, size_t n_devices, FILE *out);
int acfg_set_profile(struct acfg_session *s, const char **devices, size_t n_devices,
                     const char *profile);
int acfg_toggle_profiles(struct acfg_session *s, const char **devices, size_t n_devices,
                         const char *profiles);
int acfg_set_enabled(struct acfg_session *s, const char **devices, size_t n_devices, int enable);

/* Exact name or description match (legacy helper for tests). */
int acfg_match_name(const char *name, const char *desc, const char *want);

/* Glob-match PulseAudio name (fnmatch). */
int acfg_match_glob(const char *name, const char *pattern);

/* Parse device spec. kind: -1 any, 0 playback, 1 capture. Returns 0 on success. */
int acfg_parse_device_spec(const char *spec, int *unified, int *kind, unsigned *card,
                           const char **pattern, int *sel);

#endif /* LIB_H */
