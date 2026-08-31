/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "internal.h"

int acfg_match_name(const char *name, const char *desc, const char *want) {
    if (name && strcmp(name, want) == 0) {
        return 1;
    }
    if (desc && strcmp(desc, want) == 0) {
        return 1;
    }
    return 0;
}

int acfg_match_glob(const char *name, const char *pattern) {
    if (!name || !pattern) {
        return 0;
    }
    return fnmatch(pattern, name, 0) == 0;
}

static int parse_card_sel(const char *spec, int *kind, unsigned *card, const char **pattern,
                          int *sel) {
    if (spec[0] == ':') {
        char *end = NULL;
        if (!spec[1]) {
            return -1;
        }
        unsigned long c = strtoul(spec + 1, &end, 10);
        if (!end || *end != '\0' || end == spec + 1) {
            return -1;
        }
        *card = (unsigned)c;
        *sel = ACFG_SEL_CARD;
        return 0;
    }
    if (spec[0] == '/') {
        if (!spec[1]) {
            return -1;
        }
        *pattern = spec + 1;
        *sel = ACFG_SEL_DESC;
        return 0;
    }
    (void)kind;
    return -1;
}

int acfg_parse_device_spec(const char *spec, int *unified, int *kind, unsigned *card,
                           const char **pattern, int *sel) {
    if (!spec || !*spec) {
        return -1;
    }
    *unified = 0;
    *kind = -1;
    *card = 0;
    *pattern = NULL;
    *sel = ACFG_SEL_NAME;

    char *end = NULL;
    unsigned long n = strtoul(spec, &end, 10);
    if (end && *end == '\0' && n > 0) {
        *unified = (int)n;
        *sel = ACFG_SEL_UNIFIED;
        return 0;
    }

    if (spec[0] == ':' || spec[0] == '/') {
        return parse_card_sel(spec, kind, card, pattern, sel);
    }

    if (strncmp(spec, "playback", 8) == 0 && (spec[8] == ':' || spec[8] == '/')) {
        *kind = ACFG_PLAYBACK;
        return parse_card_sel(spec + 8, kind, card, pattern, sel);
    }
    if (strncmp(spec, "capture", 7) == 0 && (spec[7] == ':' || spec[7] == '/')) {
        *kind = ACFG_CAPTURE;
        return parse_card_sel(spec + 7, kind, card, pattern, sel);
    }

    *pattern = spec;
    *sel = ACFG_SEL_NAME;
    return 0;
}
