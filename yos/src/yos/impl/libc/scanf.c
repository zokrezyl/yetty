/* impl/libc/scanf.c — host-side scanf family.
 *
 * Symmetric to impl/libc/printf.c. clang's wasm32 ABI lowers
 *   sscanf(src, fmt, &x, str, …)
 * into
 *   sscanf(src_off, fmt_off, va_list_off)
 * where va_list_off points at the wasm shadow stack region holding
 * the pointer arguments packed at their natural alignment (each
 * wasm pointer is a uint32_t).
 *
 * Approach: walk the format string, for each conversion specifier
 * call host vsscanf-equivalent on the input substring, then write
 * the result back into the wasm pointer that the va_list points at.
 * For numerics we go through host sscanf with `%n` to consume the
 * exact width.
 *
 * Supports: %d %i %u %o %x %X %c %s %[set] %n %% with optional
 *           assignment-suppression `*`, decimal width, length
 *           modifiers `hh`/`h`/`l`/`ll`/`j`/`z`/`t`.
 *
 * Not supported (assignment ignored, conversion ignored): %f %e
 * %g %a wide-char specs. ssh / zsh don't need them at the moment.
 *
 * Returns number of successful conversions (excluding %n), or -1
 * (EOF) on early input exhaustion before any match.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/io/io-internal.h"   /* wstr_check */

static inline void va_align(uint32_t *off, uint32_t a) {
    *off = (*off + a - 1) & ~(a - 1);
}
static inline uint32_t va_p(struct yos_exec_ctx *ctx, uint32_t *off) {
    va_align(off, 4);
    /* Range-check the va-pack slot — the va-list lives in wasm memory
     * and a bogus va_off / format-arg-count mismatch can otherwise
     * walk us past memory_size. Return 0 on overflow so per-conv
     * handlers below take their "bad pointer → skip" branches. */
    if ((uint64_t)*off + 4ULL > (uint64_t)ctx->memory_size) {
        *off += 4;
        return 0;
    }
    uint32_t v = *(uint32_t *)(ctx->memory + *off);
    *off += 4;
    return v;
}

/* Skip whitespace in the input. */
static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Core: scan `src` against `fmt`, storing results through pointers
 * read from `vap`. Returns matches count, or -1 (EOF) on input
 * exhaustion before any conversion. */
static int do_scan(struct yos_exec_ctx *ctx, const char *src,
                   const char *fmt, uint32_t va_off)
{
    int matches = 0;
    const char *in = src;
    const char *f  = fmt;
    int hit_eof = 0;

    while (*f) {
        /* Literal: copy whitespace as "skip any amount of ws",
         * other chars must match. */
        if (isspace((unsigned char)*f)) {
            f++;
            in = skip_ws(in);
            continue;
        }
        if (*f != '%') {
            if (*in == 0) { hit_eof = 1; break; }
            if (*in != *f) break;
            in++; f++;
            continue;
        }

        /* Parse spec. */
        f++;
        int suppress = 0;
        int width = -1;
        int length = 0;  /* 0=int, 1='h', 2='hh', 3='l', 4='ll', 5='j/z/t' */
        if (*f == '*') { suppress = 1; f++; }
        if (*f >= '0' && *f <= '9') {
            width = 0;
            while (*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
        }
        if      (*f == 'h' && f[1] == 'h') { length = 2; f += 2; }
        else if (*f == 'h')                { length = 1; f++; }
        else if (*f == 'l' && f[1] == 'l') { length = 4; f += 2; }
        else if (*f == 'l')                { length = 3; f++; }
        else if (*f == 'j' || *f == 'z' || *f == 't') { length = 5; f++; }
        else if (*f == 'L')                { length = 5; f++; }

        char conv = *f;
        if (!conv) break;
        f++;

        if (conv == '%') {
            if (*in != '%') break;
            in++;
            continue;
        }

        /* %n — store bytes consumed so far. No input consumed. */
        if (conv == 'n') {
            if (!suppress) {
                uint32_t p = va_p(ctx, &va_off);
                if (p && (uint64_t)p + 4ULL <= (uint64_t)ctx->memory_size)
                    *(int32_t *)(ctx->memory + p) = (int32_t)(in - src);
            }
            continue;
        }

        /* Numeric conversions */
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' ||
            conv == 'x' || conv == 'X' || conv == 'p') {
            in = skip_ws(in);
            if (*in == 0) { hit_eof = (matches == 0); break; }

            int base = 10;
            int is_signed = (conv == 'd' || conv == 'i');
            if (conv == 'i') base = 0;       /* C-style auto */
            else if (conv == 'o') base = 8;
            else if (conv == 'x' || conv == 'X' || conv == 'p') base = 16;
            else if (conv == 'u' || conv == 'd') base = 10;

            /* Bound the conversion to `width` chars by copying into a
             * scratch buffer. */
            char scratch[64];
            size_t cap = sizeof(scratch) - 1;
            if (width > 0 && (size_t)width < cap) cap = (size_t)width;
            size_t got = 0;
            /* Optional sign */
            if (got < cap && (*in == '+' || *in == '-')) scratch[got++] = *in++;
            /* 0x prefix */
            if (base == 16 && got + 1 < cap && in[0] == '0' &&
                (in[1] == 'x' || in[1] == 'X')) {
                scratch[got++] = *in++;
                scratch[got++] = *in++;
            }
            while (got < cap && *in) {
                int c = (unsigned char)*in;
                int ok = 0;
                if (base == 10 || base == 0) ok = (c >= '0' && c <= '9');
                if (!ok && (base == 16 || base == 0)) ok = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
                if (!ok && (base == 8 || base == 0)) ok = (c >= '0' && c <= '7');
                if (!ok) break;
                scratch[got++] = *in++;
            }
            if (got == 0) break;
            scratch[got] = 0;

            char *end = NULL;
            uint64_t uv = 0;
            int64_t  sv = 0;
            if (is_signed) sv = strtoll(scratch, &end, base);
            else           uv = strtoull(scratch, &end, base);
            if (end == scratch) break;

            if (!suppress) {
                uint32_t p = va_p(ctx, &va_off);
                if (p && p < ctx->memory_size) {
                    switch (length) {
                    case 2:  /* hh */
                        if ((uint64_t)p + 1ULL <= (uint64_t)ctx->memory_size)
                            *(int8_t *)(ctx->memory + p) =
                                is_signed ? (int8_t)sv : (int8_t)uv;
                        break;
                    case 1:  /* h */
                        if ((uint64_t)p + 2ULL <= (uint64_t)ctx->memory_size)
                            *(int16_t *)(ctx->memory + p) =
                                is_signed ? (int16_t)sv : (int16_t)uv;
                        break;
                    case 4:  /* ll */
                    case 5:  /* j/z/t/L — guest is wasm32, 64-bit */
                        if ((uint64_t)p + 8ULL <= (uint64_t)ctx->memory_size)
                            *(int64_t *)(ctx->memory + p) =
                                is_signed ? sv : (int64_t)uv;
                        break;
                    default:
                        if ((uint64_t)p + 4ULL <= (uint64_t)ctx->memory_size)
                            *(int32_t *)(ctx->memory + p) =
                                is_signed ? (int32_t)sv : (int32_t)uv;
                        break;
                    }
                }
                matches++;
            }
            continue;
        }

        /* %s — non-whitespace word, max `width` chars (default huge) */
        if (conv == 's') {
            in = skip_ws(in);
            if (*in == 0) { hit_eof = (matches == 0); break; }
            uint32_t p = 0;
            if (!suppress) p = va_p(ctx, &va_off);
            size_t cap = (width > 0) ? (size_t)width : SIZE_MAX;
            size_t i = 0;
            while (*in && !isspace((unsigned char)*in) && i < cap) {
                if (!suppress && p && (uint64_t)p + (uint64_t)i + 1ULL <= (uint64_t)ctx->memory_size)
                    ctx->memory[p + i] = (uint8_t)*in;
                in++; i++;
            }
            if (!suppress && p && p + i + 1 <= ctx->memory_size)
                ctx->memory[p + i] = 0;
            if (!suppress) matches++;
            continue;
        }

        /* %c — exactly `width` chars (default 1), no whitespace skip */
        if (conv == 'c') {
            if (*in == 0) { hit_eof = (matches == 0); break; }
            size_t n = (width > 0) ? (size_t)width : 1;
            uint32_t p = 0;
            if (!suppress) p = va_p(ctx, &va_off);
            for (size_t i = 0; i < n; i++) {
                if (*in == 0) { n = i; break; }
                if (!suppress && p && (uint64_t)p + (uint64_t)i + 1ULL <= (uint64_t)ctx->memory_size)
                    ctx->memory[p + i] = (uint8_t)*in;
                in++;
            }
            if (!suppress) matches++;
            continue;
        }

        /* %[set] — scanset */
        if (conv == '[') {
            int negate = 0;
            if (*f == '^') { negate = 1; f++; }
            char accept[256] = {0};
            /* First char (incl ']') is literal. */
            if (*f == ']') { accept[(unsigned char)']'] = 1; f++; }
            while (*f && *f != ']') {
                if (f[1] == '-' && f[2] && f[2] != ']') {
                    unsigned char a = f[0], b = f[2];
                    if (a > b) { unsigned char t = a; a = b; b = t; }
                    for (int c = a; c <= b; c++) accept[c] = 1;
                    f += 3;
                } else {
                    accept[(unsigned char)*f] = 1;
                    f++;
                }
            }
            if (*f == ']') f++;

            if (*in == 0) { hit_eof = (matches == 0); break; }
            size_t cap = (width > 0) ? (size_t)width : SIZE_MAX;
            uint32_t p = 0;
            if (!suppress) p = va_p(ctx, &va_off);
            size_t i = 0;
            while (*in && i < cap) {
                int in_set = accept[(unsigned char)*in] != 0;
                if (negate) in_set = !in_set;
                if (!in_set) break;
                if (!suppress && p && (uint64_t)p + (uint64_t)i + 1ULL <= (uint64_t)ctx->memory_size)
                    ctx->memory[p + i] = (uint8_t)*in;
                in++; i++;
            }
            if (i == 0) break;  /* nothing matched — abort */
            if (!suppress && p && p + i + 1 <= ctx->memory_size)
                ctx->memory[p + i] = 0;
            if (!suppress) matches++;
            continue;
        }

        /* %f %e %g %a — floats. Use strtod through a scratch buffer. */
        if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'a' ||
            conv == 'E' || conv == 'G' || conv == 'A') {
            in = skip_ws(in);
            if (*in == 0) { hit_eof = (matches == 0); break; }
            char *end = NULL;
            double d = strtod(in, &end);
            if (end == in) break;
            if (!suppress) {
                uint32_t p = va_p(ctx, &va_off);
                if (p && p < ctx->memory_size) {
                    if (length >= 3) {  /* l/L — double */
                        if ((uint64_t)p + 8ULL <= (uint64_t)ctx->memory_size)
                            *(double *)(ctx->memory + p) = d;
                    } else {
                        if ((uint64_t)p + 4ULL <= (uint64_t)ctx->memory_size)
                            *(float *)(ctx->memory + p) = (float)d;
                    }
                }
                matches++;
            }
            in = end;
            continue;
        }

        /* Unknown conversion — bail. */
        break;
    }

    if (matches == 0 && hit_eof) return -1;
    return matches;
}

int32_t yos_sscanf(struct yos_exec_ctx *ctx, uint32_t src_off,
                   uint32_t fmt_off, uint32_t va_off)
{
    /* wstr_check ensures the strings are in-range AND NUL-terminated
     * inside wasm memory. do_scan walks both with no upper bound; an
     * unterminated guest string would let it read past memory_size. */
    const char *src = wstr_check(ctx, src_off);
    const char *fmt = wstr_check(ctx, fmt_off);
    if (!src || !fmt) return -1;
    int r = do_scan(ctx, src, fmt, va_off);
    ydebug("sscanf(\"%s\", \"%s\") = %d\n", src, fmt, r);
    return r;
}

/* fscanf — read full line from FILE handle into a scratch, scan that.
 * Same handle table as printf.c uses. */
extern FILE *yos_handle_to_file(struct yos_exec_ctx *ctx, uint32_t h);
int32_t yos_fscanf(struct yos_exec_ctx *ctx, uint32_t fp,
                   uint32_t fmt_off, uint32_t va_off)
{
    const char *fmt = wstr_check(ctx, fmt_off);
    if (!fmt) return -1;
    FILE *f = yos_handle_to_file(ctx, fp);
    if (!f) return -1;
    char buf[4096];
    if (!fgets(buf, sizeof(buf), f)) return -1;
    return do_scan(ctx, buf, fmt, va_off);
}

int32_t yos_scanf(struct yos_exec_ctx *ctx, uint32_t fmt_off, uint32_t va_off)
{
    const char *fmt = wstr_check(ctx, fmt_off);
    if (!fmt) return -1;
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    return do_scan(ctx, buf, fmt, va_off);
}
