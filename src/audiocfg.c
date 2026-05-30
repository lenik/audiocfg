/*
 * Copyright (C) 2026 Lenik <audiocfg@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _POSIX_C_SOURCE 200809L

#include "audiocfg.h"

#include "config.h"
#include "lib.h"

#include <bas/locale/i18n.h>
#include <bas/log/deflog.h>
#include <bas/proc/env.h>

#include <getopt.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

define_logger();

enum { OPT_VERSION = 256 };

void usage(FILE *out) {
    fputs(_("Usage: audiocfg [OPTION]...\n"
            "Configure PulseAudio devices and card profiles.\n"),
          out);
    fputs("\n", out);
    fputs("  -l, --list           ", out);
    fputs(_("list playback and capture devices\n"), out);
    fputs("  -d, --device=DEVICE  ", out);
    fputs(_("index, :CARD, playback:CARD, capture:CARD, or name (TYPE optional)\n"), out);
    fputs("  -L, --list-profiles  ", out);
    fputs(_("list profiles (all cards, or one with --device)\n"), out);
    fputs("  -p, --profile=PROFILE  ", out);
    fputs(_("profile index (from --list-profiles) or profile name\n"), out);
    fputs("  -t, --toggle=PROFILES  ", out);
    fputs(_("cycle profile: comma-separated list, wrap after last (needs --device)\n"), out);
    fputs("\n", out);
    fputs("  -v, --verbose        ", out);
    fputs(_("repeat for more verbose loggings\n"), out);
    fputs("  -q, --quiet          ", out);
    fputs(_("show less logging messages\n"), out);
    fputs("  -h, --help           ", out);
    fputs(_("display this help and exit\n"), out);
    fputs("      --version        ", out);
    fputs(_("output version information and exit\n"), out);
    fputs("\n", out);
    fprintf(out, _("Report bugs to: <%s>\n"), PROJECT_EMAIL);
}

int main(int argc, char **argv) {
    const char *exe = self_exe();
    init_i18n(LOCALEDIR);

    int do_list = 0;
    int do_list_profiles = 0;
    const char *device = NULL;
    const char *profile = NULL;
    const char *toggle = NULL;

    static const struct option long_opts[] = {
        {"list", no_argument, NULL, 'l'},
        {"device", required_argument, NULL, 'd'},
        {"list-profiles", no_argument, NULL, 'L'},
        {"profile", required_argument, NULL, 'p'},
        {"toggle", required_argument, NULL, 't'},
        {"verbose", no_argument, NULL, 'v'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, OPT_VERSION},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "ld:Lp:t:vqh", long_opts, NULL);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'l':
            do_list = 1;
            break;
        case 'd':
            device = optarg;
            break;
        case 'L':
            do_list_profiles = 1;
            break;
        case 'p':
            profile = optarg;
            break;
        case 't':
            toggle = optarg;
            break;
        case 'v':
            log_more();
            break;
        case 'q':
            log_less();
            break;
        case 'h':
            usage(stdout);
            return 0;
        case OPT_VERSION:
            printf("audiocfg %s\n", PROJECT_VERSION);
            printf(_("Copyright (C) %d %s\n"), PROJECT_YEAR, PROJECT_AUTHOR);
            fputs(_("License AGPL-3.0-or-later: <https://www.gnu.org/licenses/agpl-3.0.html>\n"),
                  stdout);
            fputs(_("This is free software: you are free to change and redistribute it.\n"),
                  stdout);
            fputs(_("This project opposes AI exploitation and AI hegemony.\n"), stdout);
            fputs(_("This project rejects mindless MIT-style licensing and politically naive "
                    "BSD-style licensing.\n"),
                  stdout);
            fputs(_("There is NO WARRANTY, to the extent permitted by law.\n"), stdout);
            return 0;
        default:
            usage(stderr);
            return 1;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: %s\n", exe, "unexpected extra arguments");
        usage(stderr);
        return 1;
    }

    if (!do_list && !do_list_profiles && !profile && !toggle) {
        usage(stderr);
        return 1;
    }
    if (toggle && profile) {
        fprintf(stderr, "%s: %s\n", exe, _("--profile and --toggle are mutually exclusive"));
        return 1;
    }

    struct acfg_session *sess = acfg_open(exe);
    if (!sess) {
        fprintf(stderr, "%s: %s\n", exe, _("failed to connect to PulseAudio"));
        return 1;
    }

    int rc = 0;

    if (do_list && acfg_list_devices(sess, stdout) != 0) {
        rc = 1;
    }
    if (rc == 0 && do_list_profiles && acfg_list_profiles(sess, device, stdout) != 0) {
        rc = 1;
    }
    if (rc == 0 && profile && acfg_set_profile(sess, device, profile) != 0) {
        rc = 1;
    }
    if (rc == 0 && toggle && acfg_toggle_profiles(sess, device, toggle) != 0) {
        rc = 1;
    }

    acfg_close(sess);
    return rc;
}
