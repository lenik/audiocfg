/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "internal.h"

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
