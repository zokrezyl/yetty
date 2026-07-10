/* <regex.h> compat — POSIX regex. Windows has no native POSIX regex
 * implementation in msvcrt; yos's impl/libc/regex.c declares the host-
 * side bridge that processes wasm regex. The struct shapes follow
 * glibc / FreeBSD libc. Behavioural wiring lives in yos's own regex
 * subsystem. */
#ifndef YOS_WIN_COMPAT_REGEX_H
#define YOS_WIN_COMPAT_REGEX_H

#include <stddef.h>
#include <sys/types.h>

typedef struct {
    void   *re_g;
    int     re_magic;
    size_t  re_nsub;
    const char *re_endp;
    void   *re_guts;
} regex_t;

typedef long regoff_t;
typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

/* regcomp flags */
#define REG_EXTENDED 0x0001
#define REG_ICASE    0x0002
#define REG_NOSUB    0x0004
#define REG_NEWLINE  0x0008
#define REG_NOSPEC   0x0010
#define REG_PEND     0x0020
#define REG_DUMP     0x0080
#define REG_BASIC    0x0000

/* regexec flags */
#define REG_NOTBOL   0x0001
#define REG_NOTEOL   0x0002
#define REG_STARTEND 0x0004
#define REG_TRACE    0x0100
#define REG_LARGE    0x0200
#define REG_BACKR    0x0400

/* error codes */
#define REG_NOMATCH    1
#define REG_BADPAT     2
#define REG_ECOLLATE   3
#define REG_ECTYPE     4
#define REG_EESCAPE    5
#define REG_ESUBREG    6
#define REG_EBRACK     7
#define REG_EPAREN     8
#define REG_EBRACE     9
#define REG_BADBR     10
#define REG_ERANGE    11
#define REG_ESPACE    12
#define REG_BADRPT    13
#define REG_EMPTY     14
#define REG_ASSERT    15
#define REG_INVARG    16
#define REG_ATOI      255
#define REG_ITOA      256

#ifdef __cplusplus
extern "C" {
#endif

extern int     regcomp (regex_t *r, const char *pattern, int flags);
extern int     regexec (const regex_t *r, const char *str, size_t nm,
                        regmatch_t pm[], int flags);
extern size_t  regerror(int errcode, const regex_t *r, char *buf, size_t bsz);
extern void    regfree (regex_t *r);

#ifdef __cplusplus
}
#endif

#endif /* YOS_WIN_COMPAT_REGEX_H */
