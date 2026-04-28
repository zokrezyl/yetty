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

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;
extern int optreset;

int getopt(int nargc, char *const *nargv, const char *options);
int getopt_long(int nargc, char *const *nargv, const char *options,
                const struct option *long_options, int *idx);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_GETOPT_H */
