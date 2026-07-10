/* impl/getopt.c — per-ctx getopt(3) implementation.
 *
 * Cross-guest leak fix for the four global getopt state vars
 * (`optind`, `optarg`, `optopt`, `opterr`) listed in
 * build-tools/libbridge/policies/libc.yaml under `leaks:`. We can't
 * just bridge to host getopt — that mutates the host globals, and
 * once we have two wasm guests in one yos process (zsh forks python
 * which calls getopt), guest A's optind walk corrupts guest B's.
 *
 * Implementation: a stripped-down POSIX getopt parser writing to
 * `ctx->getopt_state` instead of host libc's globals. POSIX-only
 * (no GNU long-option handling, no GNU permutation); long-options
 * fall through to env.getopt_long which is policy-refused for now.
 *
 * The auto-generated yos_getopt() in build-linux/.../yos_bridge.c is a
 * -ENOSYS stub today (the bridge generator can't render `char *const
 * argv[]`). We bind env.getopt directly in main.c BEFORE the
 * auto-link runs so this implementation wins the registration race.
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

/* The wasm guest's char *const argv[] is a flat list of wasm-memory
 * offsets: each argv[i] is a uint32 offset to the C string in linear
 * memory. argc tells us how many; we stop at the first NULL slot or
 * at argc, whichever comes first. */
static const char *_argv_str(struct yos_exec_ctx *ctx, uint32_t argv_off, int i)
{
    if (argv_off == 0) return NULL;
    if (argv_off + (uint64_t)(i + 1) * 4 > ctx->memory_size) return NULL;
    uint32_t s_off = *(uint32_t *)(ctx->memory + argv_off + i * 4u);
    if (s_off == 0 || s_off >= ctx->memory_size) return NULL;
    return (const char *)(ctx->memory + s_off);
}

static uint32_t _argv_str_off(struct yos_exec_ctx *ctx, uint32_t argv_off, int i)
{
    if (argv_off == 0) return 0;
    if (argv_off + (uint64_t)(i + 1) * 4 > ctx->memory_size) return 0;
    return *(uint32_t *)(ctx->memory + argv_off + i * 4u);
}

/* Helper: in-string position within the current argv element, used
 * for clustered short opts like `tar -xvf`. Kept as a function-static
 * because POSIX getopt's call-to-call resumption is keyed only by
 * optind; the per-character position inside that argv[optind] is
 * implementation-internal. POSIX-permitted; not visible to the guest.
 *
 * Subtle: this IS shared across guests on a single host thread. yos's
 * fork = pthread-per-guest means each guest has its own host thread,
 * so making it _Thread_local recovers per-guest isolation cheaply
 * for free (no cross-thread coordination cost since each thread
 * sees its own copy). */
static _Thread_local int _nextchar = 0;

int32_t yos_getopt(struct yos_exec_ctx *ctx,
                   int32_t argc, uint32_t argv_off, uint32_t optstring_off)
{
    if (!ctx) return -1;

    /* First-call init: POSIX says optind starts at 1. */
    if (ctx->getopt_state.optind <= 0) ctx->getopt_state.optind = 1;
    if (ctx->getopt_state.optind == 1) _nextchar = 0;

    const char *optstring = (optstring_off
                             ? (const char *)(ctx->memory + optstring_off)
                             : "");

    /* Done? */
    if (ctx->getopt_state.optind >= argc) {
        ydebug("getopt: optind=%d >= argc=%d, done\n",
               ctx->getopt_state.optind, argc);
        return -1;
    }

    /* If we're not mid-element, advance to the next argv slot. */
    if (_nextchar == 0) {
        const char *arg = _argv_str(ctx, argv_off, ctx->getopt_state.optind);
        if (!arg) return -1;
        /* Non-option element → POSIX says stop (no permutation). */
        if (arg[0] != '-' || arg[1] == '\0') {
            ydebug("getopt: argv[%d]='%s' not an option, done\n",
                   ctx->getopt_state.optind, arg);
            return -1;
        }
        /* "--" terminator → consume and stop. */
        if (arg[0] == '-' && arg[1] == '-' && arg[2] == '\0') {
            ctx->getopt_state.optind++;
            ydebug("getopt: '--' terminator at argv[%d]\n",
                   ctx->getopt_state.optind);
            return -1;
        }
        _nextchar = 1;
    }

    const char *arg = _argv_str(ctx, argv_off, ctx->getopt_state.optind);
    if (!arg) return -1;

    char c = arg[_nextchar];
    if (c == '\0') {
        /* Reached end of this argv element. */
        _nextchar = 0;
        ctx->getopt_state.optind++;
        return yos_getopt(ctx, argc, argv_off, optstring_off);
    }
    _nextchar++;

    /* Look up in optstring. ":" prefix changes error-style; skip. */
    const char *os = optstring;
    if (*os == ':' || *os == '+' || *os == '-') os++;
    const char *p = strchr(os, c);
    if (!p) {
        ctx->getopt_state.optopt = (unsigned char)c;
        if (ctx->getopt_state.opterr && optstring[0] != ':') {
            fprintf(stderr, "yos: unknown option -- '%c'\n", c);
        }
        return '?';
    }

    if (p[1] == ':') {
        /* Option takes an argument. */
        if (arg[_nextchar] != '\0') {
            /* -Xvalue (no space) */
            ctx->getopt_state.optarg_off =
                _argv_str_off(ctx, argv_off, ctx->getopt_state.optind) + _nextchar;
            _nextchar = 0;
            ctx->getopt_state.optind++;
        } else {
            /* -X value (next argv) */
            _nextchar = 0;
            ctx->getopt_state.optind++;
            if (ctx->getopt_state.optind >= argc) {
                ctx->getopt_state.optopt = (unsigned char)c;
                ctx->getopt_state.optarg_off = 0;
                if (ctx->getopt_state.opterr && optstring[0] != ':') {
                    fprintf(stderr, "yos: option requires argument -- '%c'\n", c);
                }
                return (optstring[0] == ':') ? ':' : '?';
            }
            ctx->getopt_state.optarg_off =
                _argv_str_off(ctx, argv_off, ctx->getopt_state.optind);
            ctx->getopt_state.optind++;
        }
    }

    ydebug("getopt: returning '%c' (optind=%d, optarg_off=0x%x)\n",
           c, ctx->getopt_state.optind, ctx->getopt_state.optarg_off);
    return (unsigned char)c;
}

/* env.__getopt_optind / __getopt_optarg / __getopt_optopt / __getopt_opterr
 * — accessors so the guest's libc's getopt(3) macros (which reference
 * the globals directly) can read/write through the per-ctx slot
 * instead. yos's wasm-side libc resolves the four extern decls
 * (`extern int optind` etc.) as IMPORTS of these accessor names; the
 * guest libc's getopt expansion turns into env.* calls. */
int32_t yos_getopt_optind_get(struct yos_exec_ctx *ctx)
{ return ctx ? ctx->getopt_state.optind : 1; }

void    yos_getopt_optind_set(struct yos_exec_ctx *ctx, int32_t v)
{ if (ctx) { ctx->getopt_state.optind = v; if (v <= 1) _nextchar = 0; } }

uint32_t yos_getopt_optarg_get(struct yos_exec_ctx *ctx)
{ return ctx ? ctx->getopt_state.optarg_off : 0; }

int32_t yos_getopt_optopt_get(struct yos_exec_ctx *ctx)
{ return ctx ? ctx->getopt_state.optopt : 0; }

int32_t yos_getopt_opterr_get(struct yos_exec_ctx *ctx)
{ return ctx ? ctx->getopt_state.opterr : 1; }

void    yos_getopt_opterr_set(struct yos_exec_ctx *ctx, int32_t v)
{ if (ctx) ctx->getopt_state.opterr = v; }
