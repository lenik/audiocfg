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
} acfg_dev_t;

typedef struct {
    char *name;
    char *desc;
} acfg_profile_t;

typedef struct {
    uint32_t pa_index;
    char *name;
    char *active_profile;
    acfg_profile_t *profiles;
    size_t n_profiles;
} acfg_card_t;

struct acfg_session {
    const char *prog;
    pa_mainloop *ml;
    pa_context *ctx;
    int err;
    acfg_dev_t *devs;
    size_t n_devs;
    acfg_card_t *cards;
    size_t n_cards;
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

    for (size_t c = 0; c < s->n_cards; c++) {
        free(s->cards[c].name);
        free(s->cards[c].active_profile);
        for (size_t p = 0; p < s->cards[c].n_profiles; p++) {
            free(s->cards[c].profiles[p].name);
            free(s->cards[c].profiles[p].desc);
        }
        free(s->cards[c].profiles);
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
    if (strcmp(name, want) == 0) {
        return 1;
    }
    if (desc && strcmp(desc, want) == 0) {
        return 1;
    }
    return 0;
}

int acfg_parse_device_spec(const char *spec, int *unified, int *kind, unsigned *card,
                           const char **name) {
    if (!spec || !*spec) {
        return -1;
    }
    *unified = 0;
    *kind = -1;
    *card = 0;
    *name = NULL;

    const char *colon = strchr(spec, ':');
    if (colon) {
        size_t tlen = (size_t)(colon - spec);
        char *end = NULL;
        unsigned long c = strtoul(colon + 1, &end, 10);
        if (!end || *end != '\0' || c == 0) {
            return -1;
        }
        *card = (unsigned)c;
        if (tlen == 0) {
            return 0;
        }
        if (tlen == 8 && strncmp(spec, "playback", 8) == 0) {
            *kind = ACFG_PLAYBACK;
            return 0;
        }
        if (tlen == 7 && strncmp(spec, "capture", 7) == 0) {
            *kind = ACFG_CAPTURE;
            return 0;
        }
        return -1;
    }

    char *end = NULL;
    unsigned long n = strtoul(spec, &end, 10);
    if (end && *end == '\0' && n > 0) {
        *unified = (int)n;
        return 0;
    }

    *name = spec;
    return 0;
}

static int append_dev(struct acfg_session *s, enum acfg_kind kind, const pa_sink_info *sink,
                      const pa_source_info *source) {
    acfg_dev_t *d = realloc(s->devs, (s->n_devs + 1) * sizeof *d);
    if (!d) {
        return -1;
    }
    s->devs = d;
    acfg_dev_t *e = &s->devs[s->n_devs++];
    e->kind = kind;
    if (kind == ACFG_PLAYBACK && sink) {
        e->pa_index = sink->index;
        e->card = sink->card;
        e->name = xstrdup(sink->name);
        e->description = xstrdup(sink->description);
    } else if (kind == ACFG_CAPTURE && source) {
        e->pa_index = source->index;
        e->card = source->card;
        e->name = xstrdup(source->name);
        e->description = xstrdup(source->description);
    } else {
        return -1;
    }
    return 0;
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
    if (append_dev(s, ACFG_PLAYBACK, i, NULL) != 0) {
        s->err = 1;
    }
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
    if (append_dev(s, ACFG_CAPTURE, NULL, i) != 0) {
        s->err = 1;
    }
}

static int acfg_load_devices(struct acfg_session *s) {
    free_catalog(s);
    s->err = 0;

    pa_operation *op = pa_context_get_sink_info_list(s->ctx, load_sink_cb, s);
    if (run_op(s, op) != 0) {
        return -1;
    }

    op = pa_context_get_source_info_list(s->ctx, load_source_cb, s);
    return run_op(s, op);
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
    e->active_profile = xstrdup(i->active_profile->name);
    e->n_profiles = i->n_profiles;
    e->profiles = calloc(e->n_profiles, sizeof *e->profiles);
    if (!e->profiles) {
        return -1;
    }
    for (uint32_t n = 0; n < i->n_profiles; n++) {
        e->profiles[n].name = xstrdup(i->profiles[n].name);
        e->profiles[n].desc = xstrdup(i->profiles[n].description);
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

static int acfg_load_cards(struct acfg_session *s) {
    for (size_t c = 0; c < s->n_cards; c++) {
        free(s->cards[c].name);
        free(s->cards[c].active_profile);
        for (size_t p = 0; p < s->cards[c].n_profiles; p++) {
            free(s->cards[c].profiles[p].name);
            free(s->cards[c].profiles[p].desc);
        }
        free(s->cards[c].profiles);
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
        fprintf(out, "%zu\t%s\t%s\t%s\t(card %u)\n", i + 1, kind_str(e->kind), e->name,
                e->description, e->card);
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

static int resolve_card(struct acfg_session *s, const char *device, uint32_t *card_index) {
    int unified = 0;
    int kind = -1;
    unsigned card = 0;
    const char *name = NULL;

    if (acfg_parse_device_spec(device, &unified, &kind, &card, &name) != 0) {
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("invalid device: %s\n"), device);
        return -1;
    }

    if (acfg_load_devices(s) != 0) {
        return -1;
    }

    if (unified > 0) {
        if ((size_t)unified > s->n_devs) {
            fprintf(stderr, "%s: ", s->prog);
            fprintf(stderr, _("device index out of range: %d\n"), unified);
            return -1;
        }
        *card_index = s->devs[unified - 1].card;
        return 0;
    }

    if (card > 0 && kind < 0) {
        if (acfg_load_cards(s) != 0) {
            return -1;
        }
        if (card_by_pa_index(s, card)) {
            *card_index = card;
            return 0;
        }
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("card not found: %u\n"), card);
        return -1;
    }

    if (kind >= 0 && card > 0) {
        for (size_t i = 0; i < s->n_devs; i++) {
            const acfg_dev_t *e = &s->devs[i];
            if (e->kind == kind && e->card == card) {
                *card_index = card;
                return 0;
            }
        }
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("%s device not found for card %u\n"), kind_str(kind), card);
        return -1;
    }

    if (name) {
        for (size_t i = 0; i < s->n_devs; i++) {
            const acfg_dev_t *e = &s->devs[i];
            if (acfg_match_name(e->name, e->description, name)) {
                *card_index = e->card;
                return 0;
            }
        }
        if (acfg_load_cards(s) != 0) {
            return -1;
        }
        char idxbuf[32];
        for (size_t c = 0; c < s->n_cards; c++) {
            snprintf(idxbuf, sizeof idxbuf, "%zu", c + 1);
            if (strcmp(name, idxbuf) == 0 || acfg_match_name(s->cards[c].name, NULL, name)) {
                *card_index = s->cards[c].pa_index;
                return 0;
            }
        }
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("device not found: %s\n"), device);
        return -1;
    }

    return -1;
}

int acfg_list_profiles(struct acfg_session *s, const char *device, FILE *out) {
    uint32_t filter_card = PA_INVALID_INDEX;

    if (acfg_load_cards(s) != 0) {
        return -1;
    }

    if (device) {
        if (resolve_card(s, device, &filter_card) != 0) {
            return -1;
        }
        if (acfg_load_cards(s) != 0) {
            return -1;
        }
    }

    size_t card_no = 0;
    for (size_t c = 0; c < s->n_cards; c++) {
        const acfg_card_t *card = &s->cards[c];
        if (filter_card != PA_INVALID_INDEX && card->pa_index != filter_card) {
            continue;
        }
        card_no++;
        fprintf(out, "%zu\t%s\t%s\n", card_no, card->name, card->active_profile);
        for (size_t p = 0; p < card->n_profiles; p++) {
            const acfg_profile_t *pr = &card->profiles[p];
            const char *mark =
                (strcmp(pr->name, card->active_profile) == 0) ? "*" : " ";
            fprintf(out, "  %s %zu\t%s\t%s\n", mark, p + 1, pr->name, pr->desc);
        }
    }
    return 0;
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

int acfg_toggle_profiles(struct acfg_session *s, const char *device, const char *profiles) {
    uint32_t card_index;
    size_t n_toggle = 0;
    char **toggle = NULL;
    size_t next = 0;

    s->err = 0;
    if (!device || !profiles) {
        fprintf(stderr, "%s: %s\n", s->prog, _("-d/--device and -t/--toggle are required"));
        return -1;
    }

    toggle = parse_profile_list(profiles, &n_toggle);
    if (!toggle) {
        fprintf(stderr, "%s: %s\n", s->prog, _("empty or invalid profile list"));
        return -1;
    }

    if (resolve_card(s, device, &card_index) != 0) {
        free_profile_list(toggle, n_toggle);
        return -1;
    }
    if (acfg_load_cards(s) != 0) {
        free_profile_list(toggle, n_toggle);
        return -1;
    }

    const acfg_card_t *card = card_by_pa_index(s, card_index);
    if (!card) {
        fprintf(stderr, "%s: %s\n", s->prog, _("card not found"));
        free_profile_list(toggle, n_toggle);
        return -1;
    }

    for (size_t i = 0; i < n_toggle; i++) {
        const char *resolved = resolve_profile_name(card, toggle[i]);
        if (!profile_on_card(card, resolved)) {
            fprintf(stderr, "%s: ", s->prog);
            fprintf(stderr, _("profile not on card: %s\n"), toggle[i]);
            free_profile_list(toggle, n_toggle);
            return -1;
        }
        char *copy = xstrdup(resolved);
        free(toggle[i]);
        toggle[i] = copy;
        if (!toggle[i]) {
            free_profile_list(toggle, n_toggle);
            return -1;
        }
    }

    for (size_t i = 0; i < n_toggle; i++) {
        if (strcmp(card->active_profile, toggle[i]) == 0) {
            next = (i + 1) % n_toggle;
            break;
        }
    }

    const char *next_name = toggle[next];
    int rc = apply_profile(s, card_index, next_name);
    if (rc == 0) {
        const acfg_profile_t *pr = find_profile(card, next_name);
        fprintf(stdout, "* %zu\t%s\t%s\n", next + 1, next_name, pr ? pr->desc : "");
    }
    free_profile_list(toggle, n_toggle);
    return rc;
}

int acfg_set_profile(struct acfg_session *s, const char *device, const char *profile) {
    uint32_t card_index;
    s->err = 0;

    if (!device || !profile) {
        fprintf(stderr, "%s: %s\n", s->prog, "-d/--device and -p/--profile are required");
        return -1;
    }
    if (resolve_card(s, device, &card_index) != 0) {
        return -1;
    }
    if (acfg_load_cards(s) != 0) {
        return -1;
    }

    const acfg_card_t *card = card_by_pa_index(s, card_index);
    if (!card) {
        fprintf(stderr, "%s: %s\n", s->prog, _("card not found"));
        return -1;
    }

    const char *profile_name = resolve_profile_name(card, profile);
    if (!profile_on_card(card, profile_name)) {
        fprintf(stderr, "%s: ", s->prog);
        fprintf(stderr, _("profile not found: %s\n"), profile);
        return -1;
    }
    return apply_profile(s, card_index, profile_name);
}
