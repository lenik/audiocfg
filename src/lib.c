/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "lib.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>

#include <pulse/pulseaudio.h>

#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__attribute__((weak))
define_logger();

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

static char *xstrdup(const char *s) {
    const char *src = s ? s : "";
    size_t n = strlen(src) + 1;
    char *d = malloc(n);
    if (d) {
        memcpy(d, src, n);
    }
    return d;
}

static void free_catalog(struct acfg_session *s) {
    for (size_t i = 0; i < s->n_devs; i++) {
        free(s->devs[i].name);
        free(s->devs[i].description);
    }
    free(s->devs);
    s->devs = NULL;
    s->n_devs = 0;

    for (size_t i = 0; i < s->n_covers; i++) {
        free(s->covers[i].port_name);
    }
    free(s->covers);
    s->covers = NULL;
    s->n_covers = 0;

    for (size_t c = 0; c < s->n_cards; c++) {
        free(s->cards[c].name);
        free(s->cards[c].active_profile);
        for (size_t p = 0; p < s->cards[c].n_profiles; p++) {
            free(s->cards[c].profiles[p].name);
            free(s->cards[c].profiles[p].desc);
        }
        free(s->cards[c].profiles);
        for (size_t p = 0; p < s->cards[c].n_ports; p++) {
            free(s->cards[c].ports[p].name);
            free(s->cards[c].ports[p].desc);
            for (size_t q = 0; q < s->cards[c].ports[p].n_profiles; q++) {
                free(s->cards[c].ports[p].profile_names[q]);
            }
            free(s->cards[c].ports[p].profile_names);
        }
        free(s->cards[c].ports);
    }
    free(s->cards);
    s->cards = NULL;
    s->n_cards = 0;
}

static void wait_ready(struct acfg_session *s) {
    for (;;) {
        if (pa_mainloop_iterate(s->ml, 1, NULL) < 0) {
            s->err = 1;
            return;
        }
        pa_context_state_t st = pa_context_get_state(s->ctx);
        if (st == PA_CONTEXT_READY) {
            return;
        }
        if (!PA_CONTEXT_IS_GOOD(st)) {
            fprintf(stderr, "%s: %s\n", s->prog, _("PulseAudio connection failed"));
            s->err = 1;
            return;
        }
    }
}

static int run_op(struct acfg_session *s, pa_operation *op) {
    if (!op) {
        return -1;
    }
    while (pa_mainloop_iterate(s->ml, 1, NULL) >= 0 &&
           pa_operation_get_state(op) == PA_OPERATION_RUNNING && !s->err) {
    }
    pa_operation_unref(op);
    return s->err ? -1 : 0;
}

static void profile_done(pa_context *c, int success, void *userdata) {
    (void)c;
    struct acfg_session *s = userdata;
    s->err = success ? 0 : 1;
}

struct acfg_session *acfg_open(const char *prog) {
    struct acfg_session *s = calloc(1, sizeof *s);
    if (!s) {
        return NULL;
    }
    s->prog = prog;
    s->ml = pa_mainloop_new();
    if (!s->ml) {
        free(s);
        return NULL;
    }
    s->ctx = pa_context_new(pa_mainloop_get_api(s->ml), "audiocfg");
    if (!s->ctx) {
        pa_mainloop_free(s->ml);
        free(s);
        return NULL;
    }
    pa_context_connect(s->ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
    wait_ready(s);
    if (s->err) {
        acfg_close(s);
        return NULL;
    }
    return s;
}

void acfg_close(struct acfg_session *s) {
    if (!s) {
        return;
    }
    free_catalog(s);
    if (s->ctx) {
        pa_context_disconnect(s->ctx);
        pa_context_unref(s->ctx);
    }
    if (s->ml) {
        pa_mainloop_free(s->ml);
    }
    free(s);
}

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

static int append_dev(struct acfg_session *s, enum acfg_kind kind, uint32_t card, uint32_t pa_index,
                      const char *name, const char *desc, int disabled) {
    acfg_dev_t *d = realloc(s->devs, (s->n_devs + 1) * sizeof *d);
    if (!d) {
        return -1;
    }
    s->devs = d;
    acfg_dev_t *e = &s->devs[s->n_devs++];
    e->kind = kind;
    e->pa_index = pa_index;
    e->card = card;
    e->name = xstrdup(name);
    e->description = xstrdup(desc);
    e->disabled = disabled;
    if (!e->name || !e->description) {
        return -1;
    }
    return 0;
}

static int is_monitor_source(const char *name) {
    if (!name) {
        return 0;
    }
    const char *dot = strrchr(name, '.');
    return dot && strcmp(dot, ".monitor") == 0;
}

static int sink_is_disabled(const pa_sink_info *sink) {
    if (sink->state == PA_SINK_SUSPENDED || sink->state == PA_SINK_UNLINKED) {
        return 1;
    }
    if (sink->active_port && sink->active_port->available == PA_PORT_AVAILABLE_NO) {
        return 1;
    }
    return 0;
}

static int source_is_disabled(const pa_source_info *source) {
    if (source->state == PA_SOURCE_SUSPENDED || source->state == PA_SOURCE_UNLINKED) {
        return 1;
    }
    if (source->active_port && source->active_port->available == PA_PORT_AVAILABLE_NO) {
        return 1;
    }
    return 0;
}

static int add_cover(struct acfg_session *s, uint32_t card, enum acfg_kind kind, const char *port) {
    acfg_cover_t *c = realloc(s->covers, (s->n_covers + 1) * sizeof *c);
    if (!c) {
        return -1;
    }
    s->covers = c;
    acfg_cover_t *e = &s->covers[s->n_covers++];
    e->card = card;
    e->kind = kind;
    e->port_name = port ? xstrdup(port) : xstrdup("*");
    return e->port_name ? 0 : -1;
}

static int port_is_covered(struct acfg_session *s, uint32_t card, enum acfg_kind kind,
                           const char *port) {
    for (size_t i = 0; i < s->n_covers; i++) {
        const acfg_cover_t *c = &s->covers[i];
        if (c->card != card || c->kind != kind) {
            continue;
        }
        if (strcmp(c->port_name, "*") == 0 || strcmp(c->port_name, port) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_endpoint_covers(struct acfg_session *s, uint32_t card, enum acfg_kind kind,
                                uint32_t n_ports, void **ports) {
    if (card == PA_INVALID_INDEX) {
        return;
    }
    if (n_ports == 0 || !ports) {
        add_cover(s, card, kind, "*");
        return;
    }
    for (uint32_t p = 0; p < n_ports; p++) {
        const char *port_name = NULL;
        if (kind == ACFG_PLAYBACK) {
            port_name = ((pa_sink_port_info **)ports)[p]->name;
        } else {
            port_name = ((pa_source_port_info **)ports)[p]->name;
        }
        if (port_name) {
            add_cover(s, card, kind, port_name);
        }
    }
}

static void load_sink_cb(pa_context *c, const pa_sink_info *i, int eol, void *ud) {
    struct acfg_session *s = ud;
    (void)c;
    if (eol < 0) {
        s->err = 1;
        return;
    }
    if (eol > 0) {
        return;
    }
    if (append_dev(s, ACFG_PLAYBACK, i->card, i->index, i->name, i->description,
                   sink_is_disabled(i)) != 0) {
        s->err = 1;
        return;
    }
    add_endpoint_covers(s, i->card, ACFG_PLAYBACK, i->n_ports, (void **)i->ports);
}

static void load_source_cb(pa_context *c, const pa_source_info *i, int eol, void *ud) {
    struct acfg_session *s = ud;
    (void)c;
    if (eol < 0) {
        s->err = 1;
        return;
    }
    if (eol > 0) {
        return;
    }
    if (append_dev(s, ACFG_CAPTURE, i->card, i->index, i->name, i->description,
                   source_is_disabled(i)) != 0) {
        s->err = 1;
        return;
    }
    if (!is_monitor_source(i->name)) {
        add_endpoint_covers(s, i->card, ACFG_CAPTURE, i->n_ports, (void **)i->ports);
    }
}

static int append_disabled_ports(struct acfg_session *s) {
    for (size_t c = 0; c < s->n_cards; c++) {
        const acfg_card_t *card = &s->cards[c];
        for (size_t p = 0; p < card->n_ports; p++) {
            const acfg_port_t *port = &card->ports[p];
            char name[512];

            if ((port->direction & PA_DIRECTION_OUTPUT) &&
                !port_is_covered(s, card->pa_index, ACFG_PLAYBACK, port->name)) {
                snprintf(name, sizeof name, "%s:%s", card->name, port->name);
                if (append_dev(s, ACFG_PLAYBACK, card->pa_index, PA_INVALID_INDEX, name,
                               port->desc, 1) != 0) {
                    return -1;
                }
            }
            if ((port->direction & PA_DIRECTION_INPUT) &&
                !port_is_covered(s, card->pa_index, ACFG_CAPTURE, port->name)) {
                snprintf(name, sizeof name, "%s:%s", card->name, port->name);
                if (append_dev(s, ACFG_CAPTURE, card->pa_index, PA_INVALID_INDEX, name,
                               port->desc, 1) != 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int append_card(struct acfg_session *s, const pa_card_info *i) {
    acfg_card_t *c = realloc(s->cards, (s->n_cards + 1) * sizeof *c);
    if (!c) {
        return -1;
    }
    s->cards = c;
    acfg_card_t *e = &s->cards[s->n_cards++];
    e->pa_index = i->index;
    e->name = xstrdup(i->name);
    e->active_profile = xstrdup(i->active_profile ? i->active_profile->name : "");
    e->n_profiles = i->n_profiles;
    e->n_ports = i->n_ports;
    e->ports = NULL;
    e->profiles = calloc(e->n_profiles, sizeof *e->profiles);
    if (!e->profiles) {
        return -1;
    }
    for (uint32_t n = 0; n < i->n_profiles; n++) {
        if (i->profiles2 && i->profiles2[n]) {
            e->profiles[n].name = xstrdup(i->profiles2[n]->name);
            e->profiles[n].desc = xstrdup(i->profiles2[n]->description);
            e->profiles[n].priority = i->profiles2[n]->priority;
            e->profiles[n].available = i->profiles2[n]->available;
        } else {
            e->profiles[n].name = xstrdup(i->profiles[n].name);
            e->profiles[n].desc = xstrdup(i->profiles[n].description);
            e->profiles[n].priority = i->profiles[n].priority;
            e->profiles[n].available = 1;
        }
        if (!e->profiles[n].name || !e->profiles[n].desc) {
            return -1;
        }
    }
    if (i->n_ports > 0) {
        e->ports = calloc(i->n_ports, sizeof *e->ports);
        if (!e->ports) {
            return -1;
        }
        for (uint32_t n = 0; n < i->n_ports; n++) {
            const pa_card_port_info *port = i->ports[n];
            e->ports[n].name = xstrdup(port->name);
            e->ports[n].desc = xstrdup(port->description);
            e->ports[n].direction = port->direction;
            e->ports[n].n_profiles = 0;
            e->ports[n].profile_names = NULL;
            if (!e->ports[n].name || !e->ports[n].desc) {
                return -1;
            }
            if (port->profiles2) {
                size_t np = 0;
                for (pa_card_profile_info2 **pp = port->profiles2; *pp; pp++) {
                    np++;
                }
                e->ports[n].profile_names = calloc(np, sizeof *e->ports[n].profile_names);
                if (!e->ports[n].profile_names) {
                    return -1;
                }
                for (size_t q = 0; q < np; q++) {
                    e->ports[n].profile_names[q] = xstrdup(port->profiles2[q]->name);
                    if (!e->ports[n].profile_names[q]) {
                        return -1;
                    }
                }
                e->ports[n].n_profiles = np;
            } else if (port->n_profiles > 0 && port->profiles) {
                e->ports[n].profile_names =
                    calloc(port->n_profiles, sizeof *e->ports[n].profile_names);
                if (!e->ports[n].profile_names) {
                    return -1;
                }
                for (uint32_t q = 0; q < port->n_profiles; q++) {
                    e->ports[n].profile_names[q] = xstrdup(port->profiles[q]->name);
                    if (!e->ports[n].profile_names[q]) {
                        return -1;
                    }
                }
                e->ports[n].n_profiles = port->n_profiles;
            }
        }
    }
    return 0;
}

static void load_card_cb(pa_context *c, const pa_card_info *i, int eol, void *ud) {
    struct acfg_session *s = ud;
    (void)c;
    if (eol < 0) {
        s->err = 1;
        return;
    }
    if (eol > 0) {
        return;
    }
    if (append_card(s, i) != 0) {
        s->err = 1;
    }
}

static int acfg_load_devices(struct acfg_session *s) {
    free_catalog(s);
    s->err = 0;

    pa_operation *op = pa_context_get_card_info_list(s->ctx, load_card_cb, s);
    if (run_op(s, op) != 0) {
        return -1;
    }

    op = pa_context_get_sink_info_list(s->ctx, load_sink_cb, s);
    if (run_op(s, op) != 0) {
        return -1;
    }

    op = pa_context_get_source_info_list(s->ctx, load_source_cb, s);
    if (run_op(s, op) != 0) {
        return -1;
    }

    return append_disabled_ports(s);
}

static int acfg_load_cards(struct acfg_session *s) {
    for (size_t c = 0; c < s->n_cards; c++) {
        free(s->cards[c].name);
        free(s->cards[c].active_profile);
        for (size_t p = 0; p < s->cards[c].n_profiles; p++) {
            free(s->cards[c].profiles[p].name);
            free(s->cards[c].profiles[p].desc);
        }
        free(s->cards[c].profiles);
        for (size_t p = 0; p < s->cards[c].n_ports; p++) {
            free(s->cards[c].ports[p].name);
            free(s->cards[c].ports[p].desc);
            for (size_t q = 0; q < s->cards[c].ports[p].n_profiles; q++) {
                free(s->cards[c].ports[p].profile_names[q]);
            }
            free(s->cards[c].ports[p].profile_names);
        }
        free(s->cards[c].ports);
    }
    free(s->cards);
    s->cards = NULL;
    s->n_cards = 0;
    s->err = 0;

    pa_operation *op = pa_context_get_card_info_list(s->ctx, load_card_cb, s);
    return run_op(s, op);
}

static const char *kind_str(enum acfg_kind k) {
    return k == ACFG_PLAYBACK ? "playback" : "capture";
}

int acfg_list_devices(struct acfg_session *s, FILE *out) {
    if (acfg_load_devices(s) != 0) {
        return -1;
    }
    for (size_t i = 0; i < s->n_devs; i++) {
        const acfg_dev_t *e = &s->devs[i];
        fprintf(out, "%zu\t%s\t%s\t%s\t(card %u)%s\n", i + 1, kind_str(e->kind), e->name,
                e->description, e->card, e->disabled ? "\t(disabled)" : "");
    }
    return 0;
}

static const acfg_card_t *card_by_pa_index(struct acfg_session *s, uint32_t card_index) {
    for (size_t c = 0; c < s->n_cards; c++) {
        if (s->cards[c].pa_index == card_index) {
            return &s->cards[c];
        }
    }
    return NULL;
}

static const char *synthetic_port_name(const acfg_dev_t *e, const acfg_card_t *card) {
    if (e->pa_index != PA_INVALID_INDEX || !card || !e->name) {
        return NULL;
    }
    size_t prefix = strlen(card->name);
    if (strncmp(e->name, card->name, prefix) != 0 || e->name[prefix] != ':') {
        return NULL;
    }
    return e->name + prefix + 1;
}

typedef struct {
    uint32_t card;
    int kind;
    char **ports;
    size_t n_ports;
} acfg_card_sel_t;

static void free_card_sels(acfg_card_sel_t *sels, size_t n) {
    for (size_t i = 0; i < n; i++) {
        for (size_t p = 0; p < sels[i].n_ports; p++) {
            free(sels[i].ports[p]);
        }
        free(sels[i].ports);
    }
    free(sels);
}

static int card_sel_seen(const acfg_card_sel_t *sels, size_t n, uint32_t card) {
    for (size_t i = 0; i < n; i++) {
        if (sels[i].card == card) {
            return 1;
        }
    }
    return 0;
}

static int add_port_pref(acfg_card_sel_t *sel, const char *port) {
    if (!port || !*port) {
        return 0;
    }
    for (size_t i = 0; i < sel->n_ports; i++) {
        if (strcmp(sel->ports[i], port) == 0) {
            return 0;
        }
    }
    char **grown = realloc(sel->ports, (sel->n_ports + 1) * sizeof *grown);
    if (!grown) {
        return -1;
    }
    sel->ports = grown;
    sel->ports[sel->n_ports] = xstrdup(port);
    if (!sel->ports[sel->n_ports]) {
        return -1;
    }
    sel->n_ports++;
    return 0;
}

static acfg_card_sel_t *find_or_add_sel(acfg_card_sel_t **sels, size_t *n, size_t *cap,
                                        uint32_t card) {
    for (size_t i = 0; i < *n; i++) {
        if ((*sels)[i].card == card) {
            return &(*sels)[i];
        }
    }
    if (*n >= *cap) {
        size_t ncap = *cap ? *cap * 2 : 4;
        acfg_card_sel_t *grown = realloc(*sels, ncap * sizeof *grown);
        if (!grown) {
            return NULL;
        }
        *sels = grown;
        *cap = ncap;
    }
    acfg_card_sel_t *e = &(*sels)[(*n)++];
    e->card = card;
    e->kind = -1;
    e->ports = NULL;
    e->n_ports = 0;
    return e;
}

static int note_dev_selection(struct acfg_session *s, acfg_card_sel_t **sels, size_t *n,
                              size_t *cap, const acfg_dev_t *dev, int kind_filter) {
    acfg_card_sel_t *sel = find_or_add_sel(sels, n, cap, dev->card);
    if (!sel) {
        return -1;
    }
    if (kind_filter >= 0) {
        if (sel->kind < 0) {
            sel->kind = kind_filter;
        } else if (sel->kind != kind_filter) {
            sel->kind = -1;
        }
    } else if (sel->kind < 0) {
        sel->kind = (int)dev->kind;
    } else if (sel->kind != (int)dev->kind) {
        sel->kind = -1;
    }
    const acfg_card_t *card = card_by_pa_index(s, dev->card);
    const char *port = synthetic_port_name(dev, card);
    if (port && add_port_pref(sel, port) != 0) {
        return -1;
    }
    return 0;
}

static int match_spec_to_sels(struct acfg_session *s, const char *spec, acfg_card_sel_t **sels,
                              size_t *n_sels, size_t *cap) {
    int unified = 0;
    int kind = -1;
    unsigned card = 0;
    const char *pattern = NULL;
    int sel_mode = ACFG_SEL_NAME;

    if (acfg_parse_device_spec(spec, &unified, &kind, &card, &pattern, &sel_mode) != 0) {
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("invalid device: %s\n"), spec);
        return -1;
    }

    size_t matched = 0;

    if (sel_mode == ACFG_SEL_UNIFIED) {
        if ((size_t)unified > s->n_devs) {
            fprintf(stderr, "%s: ", s->prog);
            fprintf(stderr, _("device index out of range: %d\n"), unified);
            return -1;
        }
        return note_dev_selection(s, sels, n_sels, cap, &s->devs[unified - 1], kind);
    }

    if (sel_mode == ACFG_SEL_CARD) {
        if (!card_by_pa_index(s, card)) {
            fprintf(stderr, "%s: ", s->prog);
            fprintf(stderr, _("card not found: %u\n"), card);
            return -1;
        }
        for (size_t i = 0; i < s->n_devs; i++) {
            const acfg_dev_t *e = &s->devs[i];
            if (e->card != card) {
                continue;
            }
            if (kind >= 0 && (int)e->kind != kind) {
                continue;
            }
            if (note_dev_selection(s, sels, n_sels, cap, e, kind) != 0) {
                return -1;
            }
            matched++;
        }
        if (matched == 0) {
            acfg_card_sel_t *sel = find_or_add_sel(sels, n_sels, cap, card);
            if (!sel) {
                return -1;
            }
            if (kind >= 0) {
                sel->kind = kind;
            }
        }
        return 0;
    }

    if (sel_mode == ACFG_SEL_DESC) {
        for (size_t i = 0; i < s->n_devs; i++) {
            const acfg_dev_t *e = &s->devs[i];
            if (kind >= 0 && (int)e->kind != kind) {
                continue;
            }
            if (!e->description || !strstr(e->description, pattern)) {
                continue;
            }
            if (note_dev_selection(s, sels, n_sels, cap, e, kind) != 0) {
                return -1;
            }
            matched++;
        }
        if (matched == 0) {
            fprintf(stderr, "%s: ", s->prog);
            fprintf(stderr, _("device not found: %s\n"), spec);
            return -1;
        }
        return 0;
    }

    for (size_t i = 0; i < s->n_devs; i++) {
        const acfg_dev_t *e = &s->devs[i];
        if (kind >= 0 && (int)e->kind != kind) {
            continue;
        }
        if (!acfg_match_glob(e->name, pattern)) {
            continue;
        }
        if (note_dev_selection(s, sels, n_sels, cap, e, kind) != 0) {
            return -1;
        }
        matched++;
    }
    for (size_t c = 0; c < s->n_cards; c++) {
        if (!acfg_match_glob(s->cards[c].name, pattern)) {
            continue;
        }
        if (card_sel_seen(*sels, *n_sels, s->cards[c].pa_index)) {
            continue;
        }
        acfg_card_sel_t *sel = find_or_add_sel(sels, n_sels, cap, s->cards[c].pa_index);
        if (!sel) {
            return -1;
        }
        if (kind >= 0) {
            sel->kind = kind;
        }
        matched++;
    }
    if (matched == 0) {
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("device not found: %s\n"), spec);
        return -1;
    }
    return 0;
}

static int select_cards(struct acfg_session *s, const char **specs, size_t n_specs,
                        acfg_card_sel_t **sels_out, size_t *n_out) {
    if (!specs || n_specs == 0) {
        fprintf(stderr, "%s: %s\n", s->prog, _("-d/--device is required"));
        return -1;
    }
    if (acfg_load_devices(s) != 0) {
        return -1;
    }
    if (acfg_load_cards(s) != 0) {
        return -1;
    }

    acfg_card_sel_t *sels = NULL;
    size_t n = 0;
    size_t cap = 0;
    for (size_t i = 0; i < n_specs; i++) {
        if (match_spec_to_sels(s, specs[i], &sels, &n, &cap) != 0) {
            free_card_sels(sels, n);
            return -1;
        }
    }
    if (n == 0) {
        fprintf(stderr, "%s: %s\n", s->prog, _("no matching devices"));
        free_card_sels(sels, n);
        return -1;
    }
    *sels_out = sels;
    *n_out = n;
    return 0;
}

static int port_has_profile(const acfg_port_t *port, const char *profile) {
    for (size_t i = 0; i < port->n_profiles; i++) {
        if (strcmp(port->profile_names[i], profile) == 0) {
            return 1;
        }
    }
    return 0;
}

static const acfg_port_t *find_card_port(const acfg_card_t *card, const char *port_name) {
    for (size_t i = 0; i < card->n_ports; i++) {
        if (strcmp(card->ports[i].name, port_name) == 0) {
            return &card->ports[i];
        }
    }
    return NULL;
}

static int profile_covers_ports(const acfg_card_t *card, const char *profile, char **ports,
                                size_t n_ports) {
    if (n_ports == 0) {
        return 1;
    }
    for (size_t i = 0; i < n_ports; i++) {
        const acfg_port_t *port = find_card_port(card, ports[i]);
        if (!port || !port_has_profile(port, profile)) {
            return 0;
        }
    }
    return 1;
}

static int profile_has_direction(const acfg_profile_t *pr, int kind) {
    if (kind == ACFG_PLAYBACK) {
        return pr->n_sinks > 0;
    }
    if (kind == ACFG_CAPTURE) {
        return pr->n_sources > 0;
    }
    return 1;
}

static int is_off_profile(const char *name) {
    return name && strcmp(name, "off") == 0;
}

static const char *pick_enable_profile(const acfg_card_t *card, const acfg_card_sel_t *sel) {
    const acfg_profile_t *best = NULL;
    for (size_t p = 0; p < card->n_profiles; p++) {
        const acfg_profile_t *pr = &card->profiles[p];
        if (is_off_profile(pr->name) || !pr->available) {
            continue;
        }
        if (!profile_has_direction(pr, sel->kind)) {
            continue;
        }
        if (!profile_covers_ports(card, pr->name, sel->ports, sel->n_ports)) {
            continue;
        }
        if (!best || pr->priority > best->priority) {
            best = pr;
        }
    }
    if (best) {
        return best->name;
    }
    /* Fall back: any available non-off profile, ignoring direction/port prefs. */
    for (size_t p = 0; p < card->n_profiles; p++) {
        const acfg_profile_t *pr = &card->profiles[p];
        if (is_off_profile(pr->name) || !pr->available) {
            continue;
        }
        if (!best || pr->priority > best->priority) {
            best = pr;
        }
    }
    return best ? best->name : NULL;
}

static const char *resolve_profile_name(const acfg_card_t *card, const char *profile) {
    char *end = NULL;
    unsigned long n = strtoul(profile, &end, 10);
    if (end && *end == '\0' && n > 0 && n <= card->n_profiles) {
        return card->profiles[n - 1].name;
    }
    return profile;
}

static int profile_on_card(const acfg_card_t *card, const char *profile_name) {
    for (size_t p = 0; p < card->n_profiles; p++) {
        if (strcmp(card->profiles[p].name, profile_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const acfg_profile_t *find_profile(const acfg_card_t *card, const char *profile_name) {
    for (size_t p = 0; p < card->n_profiles; p++) {
        if (strcmp(card->profiles[p].name, profile_name) == 0) {
            return &card->profiles[p];
        }
    }
    return NULL;
}

static int apply_profile(struct acfg_session *s, uint32_t card_index, const char *profile_name) {
    s->err = 0;
    pa_operation *op =
        pa_context_set_card_profile_by_index(s->ctx, card_index, profile_name, profile_done, s);
    if (!op) {
        return -1;
    }
    if (run_op(s, op) != 0 || s->err) {
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("failed to set profile %s\n"), profile_name);
        return -1;
    }
    loginfo_fmt("%s: card %u profile %s", s->prog, card_index, profile_name);
    return 0;
}

static char *trim_inplace(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '\0') {
        return s;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t')) {
        *end-- = '\0';
    }
    return s;
}

static char **parse_profile_list(const char *profiles, size_t *n_out) {
    char *buf = xstrdup(profiles);
    if (!buf) {
        return NULL;
    }
    size_t n = 0;
    size_t cap = 4;
    char **list = calloc(cap, sizeof *list);
    if (!list) {
        free(buf);
        return NULL;
    }

    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        tok = trim_inplace(tok);
        if (*tok == '\0') {
            continue;
        }
        if (n >= cap) {
            cap *= 2;
            char **grown = realloc(list, cap * sizeof *list);
            if (!grown) {
                goto fail;
            }
            list = grown;
        }
        list[n] = xstrdup(tok);
        if (!list[n]) {
            goto fail;
        }
        n++;
    }
    free(buf);
    if (n == 0) {
        free(list);
        return NULL;
    }
    *n_out = n;
    return list;

fail:
    for (size_t i = 0; i < n; i++) {
        free(list[i]);
    }
    free(list);
    free(buf);
    return NULL;
}

static void free_profile_list(char **list, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(list[i]);
    }
    free(list);
}

static int card_in_filter(const uint32_t *filter, size_t n_filter, uint32_t card) {
    if (!filter || n_filter == 0) {
        return 1;
    }
    for (size_t i = 0; i < n_filter; i++) {
        if (filter[i] == card) {
            return 1;
        }
    }
    return 0;
}

int acfg_list_profiles(struct acfg_session *s, const char **devices, size_t n_devices, FILE *out) {
    uint32_t *filter = NULL;
    size_t n_filter = 0;

    if (n_devices > 0) {
        acfg_card_sel_t *sels = NULL;
        size_t n_sels = 0;
        if (select_cards(s, devices, n_devices, &sels, &n_sels) != 0) {
            return -1;
        }
        filter = calloc(n_sels, sizeof *filter);
        if (!filter) {
            free_card_sels(sels, n_sels);
            return -1;
        }
        for (size_t i = 0; i < n_sels; i++) {
            filter[i] = sels[i].card;
        }
        n_filter = n_sels;
        free_card_sels(sels, n_sels);
        if (acfg_load_cards(s) != 0) {
            free(filter);
            return -1;
        }
    } else if (acfg_load_cards(s) != 0) {
        return -1;
    }

    size_t card_no = 0;
    for (size_t c = 0; c < s->n_cards; c++) {
        const acfg_card_t *card = &s->cards[c];
        if (!card_in_filter(filter, n_filter, card->pa_index)) {
            continue;
        }
        card_no++;
        fprintf(out, "%zu\t%s\t%s\n", card_no, card->name, card->active_profile);
        for (size_t p = 0; p < card->n_profiles; p++) {
            const acfg_profile_t *pr = &card->profiles[p];
            const char *mark = (strcmp(pr->name, card->active_profile) == 0) ? "*" : " ";
            fprintf(out, "  %s %zu\t%s\t%s\n", mark, p + 1, pr->name, pr->desc);
        }
    }
    free(filter);
    return 0;
}

int acfg_set_enabled(struct acfg_session *s, const char **devices, size_t n_devices, int enable) {
    acfg_card_sel_t *sels = NULL;
    size_t n_sels = 0;
    int rc = 0;

    s->err = 0;
    if (select_cards(s, devices, n_devices, &sels, &n_sels) != 0) {
        return -1;
    }

    for (size_t i = 0; i < n_sels; i++) {
        const acfg_card_t *card = card_by_pa_index(s, sels[i].card);
        if (!card) {
            fprintf(stderr, "%s: %s\n", s->prog, _("card not found"));
            rc = -1;
            break;
        }
        const char *profile_name;
        if (enable) {
            profile_name = pick_enable_profile(card, &sels[i]);
            if (!profile_name) {
                fprintf(stderr, "%s: ", s->prog);
                fprintf(stderr, _("no usable profile for card %s\n"), card->name);
                rc = -1;
                break;
            }
        } else {
            if (!profile_on_card(card, "off")) {
                fprintf(stderr, "%s: ", s->prog);
                fprintf(stderr, _("profile not found: %s\n"), "off");
                rc = -1;
                break;
            }
            profile_name = "off";
        }
        if (strcmp(card->active_profile, profile_name) == 0) {
            loginfo_fmt("%s: card %s already %s (%s)", s->prog, card->name,
                        enable ? "enabled" : "disabled", profile_name);
            continue;
        }
        if (apply_profile(s, card->pa_index, profile_name) != 0) {
            rc = -1;
            break;
        }
        fprintf(stdout, "%s\t%s\t%s\n", enable ? "enabled" : "disabled", card->name, profile_name);
    }

    free_card_sels(sels, n_sels);
    return rc;
}

int acfg_toggle_profiles(struct acfg_session *s, const char **devices, size_t n_devices,
                         const char *profiles) {
    acfg_card_sel_t *sels = NULL;
    size_t n_sels = 0;
    size_t n_toggle = 0;
    char **toggle = NULL;
    int rc = 0;

    s->err = 0;
    if (!profiles) {
        fprintf(stderr, "%s: %s\n", s->prog, _("-d/--device and -t/--toggle are required"));
        return -1;
    }

    toggle = parse_profile_list(profiles, &n_toggle);
    if (!toggle) {
        fprintf(stderr, "%s: %s\n", s->prog, _("empty or invalid profile list"));
        return -1;
    }

    if (select_cards(s, devices, n_devices, &sels, &n_sels) != 0) {
        free_profile_list(toggle, n_toggle);
        return -1;
    }

    for (size_t ci = 0; ci < n_sels; ci++) {
        const acfg_card_t *card = card_by_pa_index(s, sels[ci].card);
        if (!card) {
            fprintf(stderr, "%s: %s\n", s->prog, _("card not found"));
            rc = -1;
            break;
        }

        char **resolved = calloc(n_toggle, sizeof *resolved);
        if (!resolved) {
            rc = -1;
            break;
        }
        size_t next = 0;
        int ok = 1;
        for (size_t i = 0; i < n_toggle; i++) {
            const char *name = resolve_profile_name(card, toggle[i]);
            if (!profile_on_card(card, name)) {
                fprintf(stderr, "%s: ", s->prog);
                fprintf(stderr, _("profile not on card: %s\n"), toggle[i]);
                ok = 0;
                break;
            }
            resolved[i] = xstrdup(name);
            if (!resolved[i]) {
                ok = 0;
                break;
            }
        }
        if (!ok) {
            free_profile_list(resolved, n_toggle);
            rc = -1;
            break;
        }

        for (size_t i = 0; i < n_toggle; i++) {
            if (strcmp(card->active_profile, resolved[i]) == 0) {
                next = (i + 1) % n_toggle;
                break;
            }
        }

        const char *next_name = resolved[next];
        if (apply_profile(s, card->pa_index, next_name) != 0) {
            free_profile_list(resolved, n_toggle);
            rc = -1;
            break;
        }
        const acfg_profile_t *pr = find_profile(card, next_name);
        fprintf(stdout, "* %zu\t%s\t%s\t%s\n", next + 1, card->name, next_name,
                pr ? pr->desc : "");
        free_profile_list(resolved, n_toggle);
    }

    free_card_sels(sels, n_sels);
    free_profile_list(toggle, n_toggle);
    return rc;
}

int acfg_set_profile(struct acfg_session *s, const char **devices, size_t n_devices,
                     const char *profile) {
    acfg_card_sel_t *sels = NULL;
    size_t n_sels = 0;
    int rc = 0;

    s->err = 0;
    if (!profile) {
        fprintf(stderr, "%s: %s\n", s->prog, "-d/--device and -p/--profile are required");
        return -1;
    }
    if (select_cards(s, devices, n_devices, &sels, &n_sels) != 0) {
        return -1;
    }

    for (size_t i = 0; i < n_sels; i++) {
        const acfg_card_t *card = card_by_pa_index(s, sels[i].card);
        if (!card) {
            fprintf(stderr, "%s: %s\n", s->prog, _("card not found"));
            rc = -1;
            break;
        }
        const char *profile_name = resolve_profile_name(card, profile);
        if (!profile_on_card(card, profile_name)) {
            fprintf(stderr, "%s: ", s->prog);
            fprintf(stderr, _("profile not found: %s\n"), profile);
            rc = -1;
            break;
        }
        if (apply_profile(s, card->pa_index, profile_name) != 0) {
            rc = -1;
            break;
        }
    }

    free_card_sels(sels, n_sels);
    return rc;
}
