/*
 * getopt.c - Portable getopt / getopt_long.
 *
 * Vendored from NetBSD libc (lib/libc/stdlib/getopt_long.c, rev 1.28),
 * stripped of NetBSD-internal helpers (__weak_alias, __UNCONST, _DIAGASSERT,
 * <namespace.h>, <sys/cdefs.h>) and with warnx() replaced by fprintf(stderr,...).
 *
 * Original copyright follows.
 *
 * --------------------------------------------------------------------------
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Dieter Baron and Thomas Klausner.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * --------------------------------------------------------------------------
 */

#include <yetty/yplatform/getopt.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNCONST(p) ((void *)(uintptr_t)(const void *)(p))

int yetty_yplatform_opterr = 1;   /* if error message should be printed */
int yetty_yplatform_optind = 1;   /* index into parent argv vector */
int yetty_yplatform_optopt = '?'; /* character checked for validity */
int yetty_yplatform_optreset = 0; /* reset getopt */
char *yetty_yplatform_optarg = NULL;

#define IGNORE_FIRST (*options == '-' || *options == '+')
#define PRINT_ERROR                                                                                \
    ((yetty_yplatform_opterr) && ((*options != ':') || (IGNORE_FIRST && options[1] != ':')))
#define IS_POSIXLY_CORRECT (getenv("POSIXLY_CORRECT") != NULL)
#define PERMUTE (!IS_POSIXLY_CORRECT && !IGNORE_FIRST)
#define IN_ORDER (!IS_POSIXLY_CORRECT && *options == '-')

#define BADCH (int)'?'
#define BADARG ((IGNORE_FIRST && options[1] == ':') || (*options == ':') ? (int)':' : (int)'?')
#define INORDER (int)1

#define EMSG ""

static const char *place = EMSG; /* option letter processing */
static int nonopt_start = -1;    /* first non option argument (for permute) */
static int nonopt_end = -1;      /* first option after non options (for permute) */

static const char recargchar[] = "%s: option requires an argument -- %c\n";
static const char recargstring[] = "%s: option requires an argument -- %s\n";
static const char ambig[] = "%s: ambiguous option -- %.*s\n";
static const char noarg[] = "%s: option doesn't take an argument -- %.*s\n";
static const char illoptchar[] = "%s: unknown option -- %c\n";
static const char illoptstring[] = "%s: unknown option -- %s\n";

static const char *progname(void)
{
    /* Best-effort; libc's warnx uses getprogname(). */
    return "getopt";
}

static int gcd(int a, int b)
{
    int c = a % b;
    while (c != 0) {
        a = b;
        b = c;
        c = a % b;
    }
    return b;
}

/*
 * Exchange the block from nonopt_start to nonopt_end with the block
 * from nonopt_end to opt_end (keeping the same order of arguments
 * in each block).
 */
static void permute_args(int panonopt_start, int panonopt_end, int opt_end, char **nargv)
{
    int cstart, cyclelen, i, j, ncycle, nnonopts, nopts, pos;
    char *swap;

    nnonopts = panonopt_end - panonopt_start;
    nopts = opt_end - panonopt_end;
    ncycle = gcd(nnonopts, nopts);
    cyclelen = (opt_end - panonopt_start) / ncycle;

    for (i = 0; i < ncycle; i++) {
        cstart = panonopt_end + i;
        pos = cstart;
        for (j = 0; j < cyclelen; j++) {
            if (pos >= panonopt_end) {
                pos -= nnonopts;
            } else {
                pos += nopts;
            }
            swap = nargv[pos];
            nargv[pos] = nargv[cstart];
            nargv[cstart] = swap;
        }
    }
}

/*
 * getopt_internal --
 *  Parse argc/argv argument vector.  Called by user level routines.
 *  Returns -2 if -- is found (can be long option or end of options marker).
 */
static int getopt_internal(int nargc, char **nargv, const char *options)
{
    const char *oli;
    int optchar;

    yetty_yplatform_optarg = NULL;

    if (yetty_yplatform_optind == 0) {
        yetty_yplatform_optind = 1;
    }

    if (yetty_yplatform_optreset) {
        nonopt_start = nonopt_end = -1;
    }
start:
    if (yetty_yplatform_optreset || !*place) {
        yetty_yplatform_optreset = 0;
        if (yetty_yplatform_optind >= nargc) {
            place = EMSG;
            if (nonopt_end != -1) {
                permute_args(nonopt_start, nonopt_end, yetty_yplatform_optind, nargv);
                yetty_yplatform_optind -= nonopt_end - nonopt_start;
            } else if (nonopt_start != -1) {
                yetty_yplatform_optind = nonopt_start;
            }
            nonopt_start = nonopt_end = -1;
            return -1;
        }
        if ((*(place = nargv[yetty_yplatform_optind]) != '-') || (place[1] == '\0')) {
            place = EMSG;
            if (IN_ORDER) {
                yetty_yplatform_optarg = nargv[yetty_yplatform_optind++];
                return INORDER;
            }
            if (!PERMUTE) {
                return -1;
            }
            if (nonopt_start == -1) {
                nonopt_start = yetty_yplatform_optind;
            } else if (nonopt_end != -1) {
                permute_args(nonopt_start, nonopt_end, yetty_yplatform_optind, nargv);
                nonopt_start = yetty_yplatform_optind - (nonopt_end - nonopt_start);
                nonopt_end = -1;
            }
            yetty_yplatform_optind++;
            goto start;
        }
        if (nonopt_start != -1 && nonopt_end == -1) {
            nonopt_end = yetty_yplatform_optind;
        }
        if (place[1] && *++place == '-') {
            place++;
            return -2;
        }
    }
    if ((optchar = (int)*place++) == (int)':' ||
        (oli = strchr(options + (IGNORE_FIRST ? 1 : 0), optchar)) == NULL) {
        if (!*place) {
            ++yetty_yplatform_optind;
        }
        if (PRINT_ERROR) {
            fprintf(stderr, illoptchar, progname(), optchar);
        }
        yetty_yplatform_optopt = optchar;
        return BADCH;
    }
    if (optchar == 'W' && oli[1] == ';') {
        if (*place) {
            return -2;
        }
        if (++yetty_yplatform_optind >= nargc) {
            place = EMSG;
            if (PRINT_ERROR) {
                fprintf(stderr, recargchar, progname(), optchar);
            }
            yetty_yplatform_optopt = optchar;
            return BADARG;
        } else {
            place = nargv[yetty_yplatform_optind];
        }
        return -2;
    }
    if (*++oli != ':') {
        if (!*place) {
            ++yetty_yplatform_optind;
        }
    } else {
        yetty_yplatform_optarg = NULL;
        if (*place) {
            yetty_yplatform_optarg = UNCONST(place);
        } else if (oli[1] != ':') {
            if (++yetty_yplatform_optind >= nargc) {
                place = EMSG;
                if (PRINT_ERROR) {
                    fprintf(stderr, recargchar, progname(), optchar);
                }
                yetty_yplatform_optopt = optchar;
                return BADARG;
            } else {
                yetty_yplatform_optarg = nargv[yetty_yplatform_optind];
            }
        }
        place = EMSG;
        ++yetty_yplatform_optind;
    }
    return optchar;
}

int yetty_yplatform_getopt(int nargc, char *const *nargv, const char *options)
{
    int retval = getopt_internal(nargc, UNCONST(nargv), options);
    if (retval == -2) {
        ++yetty_yplatform_optind;
        if (nonopt_end != -1) {
            permute_args(nonopt_start, nonopt_end, yetty_yplatform_optind, UNCONST(nargv));
            yetty_yplatform_optind -= nonopt_end - nonopt_start;
        }
        nonopt_start = nonopt_end = -1;
        retval = -1;
    }
    return retval;
}

int yetty_yplatform_getopt_long(int nargc, char *const *nargv, const char *options,
                                const struct yetty_yplatform_option *long_options, int *idx)
{
    int retval;

#define IDENTICAL_INTERPRETATION(_x, _y)                                                           \
    (long_options[(_x)].has_arg == long_options[(_y)].has_arg &&                                   \
     long_options[(_x)].flag == long_options[(_y)].flag &&                                         \
     long_options[(_x)].val == long_options[(_y)].val)

    retval = getopt_internal(nargc, UNCONST(nargv), options);
    if (retval == -2) {
        char *current_argv, *has_equal;
        size_t current_argv_len;
        int i, ambiguous, match;

        current_argv = UNCONST(place);
        match = -1;
        ambiguous = 0;

        yetty_yplatform_optind++;
        place = EMSG;

        if (*current_argv == '\0') { /* found "--" */
            if (nonopt_end != -1) {
                permute_args(nonopt_start, nonopt_end, yetty_yplatform_optind, UNCONST(nargv));
                yetty_yplatform_optind -= nonopt_end - nonopt_start;
            }
            nonopt_start = nonopt_end = -1;
            return -1;
        }
        if ((has_equal = strchr(current_argv, '=')) != NULL) {
            current_argv_len = (size_t)(has_equal - current_argv);
            has_equal++;
        } else {
            current_argv_len = strlen(current_argv);
        }

        for (i = 0; long_options[i].name; i++) {
            if (strncmp(current_argv, long_options[i].name, current_argv_len)) {
                continue;
            }
            if (strlen(long_options[i].name) == current_argv_len) {
                match = i;
                ambiguous = 0;
                break;
            }
            if (match == -1) {
                match = i;
            } else if (!IDENTICAL_INTERPRETATION(i, match)) {
                ambiguous = 1;
            }
        }
        if (ambiguous) {
            if (PRINT_ERROR) {
                fprintf(stderr, ambig, progname(), (int)current_argv_len, current_argv);
            }
            yetty_yplatform_optopt = 0;
            return BADCH;
        }
        if (match != -1) {
            if (long_options[match].has_arg == no_argument && has_equal) {
                if (PRINT_ERROR) {
                    fprintf(stderr, noarg, progname(), (int)current_argv_len, current_argv);
                }
                if (long_options[match].flag == NULL) {
                    yetty_yplatform_optopt = long_options[match].val;
                } else {
                    yetty_yplatform_optopt = 0;
                }
                return BADARG;
            }
            if (long_options[match].has_arg == required_argument ||
                long_options[match].has_arg == optional_argument) {
                if (has_equal) {
                    yetty_yplatform_optarg = has_equal;
                } else if (long_options[match].has_arg == required_argument) {
                    yetty_yplatform_optarg = nargv[yetty_yplatform_optind++];
                }
            }
            if ((long_options[match].has_arg == required_argument) &&
                (yetty_yplatform_optarg == NULL)) {
                if (PRINT_ERROR) {
                    fprintf(stderr, recargstring, progname(), current_argv);
                }
                if (long_options[match].flag == NULL) {
                    yetty_yplatform_optopt = long_options[match].val;
                } else {
                    yetty_yplatform_optopt = 0;
                }
                --yetty_yplatform_optind;
                return BADARG;
            }
        } else {
            if (PRINT_ERROR) {
                fprintf(stderr, illoptstring, progname(), current_argv);
            }
            yetty_yplatform_optopt = 0;
            return BADCH;
        }
        if (long_options[match].flag) {
            *long_options[match].flag = long_options[match].val;
            retval = 0;
        } else {
            retval = long_options[match].val;
        }
        if (idx) {
            *idx = match;
        }
    }
    return retval;
#undef IDENTICAL_INTERPRETATION
}
