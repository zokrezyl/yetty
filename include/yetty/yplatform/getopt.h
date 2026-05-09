/*
 * yetty/getopt.h - Portable getopt_long (vendored from NetBSD).
 *
 * Replaces <getopt.h> on all platforms so that behavior is identical
 * across glibc / musl / Apple libc / Windows (which lacks it entirely).
 */

#ifndef YETTY_GETOPT_H
#define YETTY_GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Values for has_arg field in struct option. */
#define no_argument 0
#define required_argument 1
#define optional_argument 2

struct yetty_yplatform_option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

extern char *yetty_yplatform_optarg;
extern int yetty_yplatform_optind;
extern int yetty_yplatform_opterr;
extern int yetty_yplatform_optopt;
extern int yetty_yplatform_optreset;

int yetty_yplatform_getopt(int nargc, char *const *nargv, const char *options);
int yetty_yplatform_getopt_long(int nargc, char *const *nargv, const char *options,
                                const struct yetty_yplatform_option *long_options, int *idx);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_GETOPT_H */
