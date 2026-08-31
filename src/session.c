/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "internal.h"

__attribute__((weak))
define_logger();

char *xstrdup(const char *s) {
    const char *src = s ? s : "";
    size_t n = strlen(src) + 1;
    char *d = malloc(n);
    if (d) {
        memcpy(d, src, n);
    }
    return d;
}

void free_catalog(struct acfg_session *s) {
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

void wait_ready(struct acfg_session *s) {
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

int run_op(struct acfg_session *s, pa_operation *op) {
    if (!op) {
        return -1;
    }
    while (pa_mainloop_iterate(s->ml, 1, NULL) >= 0 &&
           pa_operation_get_state(op) == PA_OPERATION_RUNNING && !s->err) {
    }
    pa_operation_unref(op);
    return s->err ? -1 : 0;
}

void profile_done(pa_context *c, int success, void *userdata) {
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
