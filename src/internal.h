/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef ACFG_INTERNAL_H
#define ACFG_INTERNAL_H

#include "acfg.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>

#include <pulse/pulseaudio.h>

#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum acfg_kind { ACFG_PLAYBACK = 0, ACFG_CAPTURE = 1 };

typedef struct {
    enum acfg_kind kind;
    uint32_t pa_index;
    uint32_t card;
    char *name;
    char *description;
    int disabled;
} acfg_dev_t;

typedef struct {
    char *name;
    char *desc;
    uint32_t priority;
    uint32_t n_sinks;
    uint32_t n_sources;
    int available;
} acfg_profile_t;

typedef struct {
    char *name;
    char *desc;
    int direction;
    char **profile_names;
    size_t n_profiles;
} acfg_port_t;

typedef struct {
    uint32_t pa_index;
    char *name;
    char *active_profile;
    acfg_profile_t *profiles;
    size_t n_profiles;
    acfg_port_t *ports;
    size_t n_ports;
} acfg_card_t;

typedef struct {
    uint32_t card;
    enum acfg_kind kind;
    char *port_name;
} acfg_cover_t;

struct acfg_session {
    const char *prog;
    pa_mainloop *ml;
    pa_context *ctx;
    int err;
    acfg_dev_t *devs;
    size_t n_devs;
    acfg_card_t *cards;
    size_t n_cards;
    acfg_cover_t *covers;
    size_t n_covers;
};

typedef struct {
    uint32_t card;
    int kind;
    char **ports;
    size_t n_ports;
} acfg_card_sel_t;


char *xstrdup(const char *s);
void free_catalog(struct acfg_session *s);
void wait_ready(struct acfg_session *s);
int run_op(struct acfg_session *s, pa_operation *op);
void profile_done(pa_context *c, int success, void *userdata);

int acfg_load_devices(struct acfg_session *s);
int acfg_load_cards(struct acfg_session *s);
const char *kind_str(enum acfg_kind k);
const acfg_card_t *card_by_pa_index(struct acfg_session *s, uint32_t card_index);

void free_card_sels(acfg_card_sel_t *sels, size_t n);
int select_cards(struct acfg_session *s, const char **specs, size_t n_specs,
                 acfg_card_sel_t **sels_out, size_t *n_out);

#endif /* ACFG_INTERNAL_H */
