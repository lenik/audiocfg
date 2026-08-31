/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "internal.h"

const acfg_card_t *card_by_pa_index(struct acfg_session *s, uint32_t card_index) {
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

void free_card_sels(acfg_card_sel_t *sels, size_t n) {
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

int select_cards(struct acfg_session *s, const char **specs, size_t n_specs,
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
