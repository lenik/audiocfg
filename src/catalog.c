/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "internal.h"

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

int acfg_load_devices(struct acfg_session *s) {
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

int acfg_load_cards(struct acfg_session *s) {
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

const char *kind_str(enum acfg_kind k) {
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
