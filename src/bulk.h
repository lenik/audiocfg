#include <bas/locale/i18n.h>

#include <libintl.h>
#ifndef _
#define _(msgid) gettext(msgid)
#endif

#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
