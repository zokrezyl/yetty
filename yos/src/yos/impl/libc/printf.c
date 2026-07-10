/* impl/printf.c — host-side printf family.
 *
 * clang's wasm32 ABI lowers a variadic call `printf(fmt, x, y, …)` into
 * a non-variadic call `printf(fmt_ptr, va_list_ptr)`. The caller packs
 * the varargs into the wasm shadow stack at `va_list_ptr`, with each
 * argument 8-byte-aligned (so an i32 occupies 8 bytes; an f64 occupies
 * 8 bytes; an i64 occupies 8 bytes).
 *
 * We can't build a host `va_list` portably from raw memory. Instead
 * we walk the format string ourselves: for each conversion spec, read
 * the matching slot(s) from `ctx->memory + va_list_ptr`, call host
 * `snprintf("%<spec>", val)` to format that single piece, and copy
 * the result into an output buffer. Strings (`%s`) we read from the
 * guest's linear memory.
 *
 * Limitations of this first cut:
 *   - `%n` is rejected (security; rarely used).
 *   - Wide-string formatters (`%ls`, `%C`) — passthrough as `%s` /
 *     `%c`. nvim doesn't use them.
 *   - Field width / precision passed via `*` arg are honoured via
 *     reading the extra slot.
 *   - `%a`/`%A` are passed through as host snprintf.
 *
 * Output:
 *   yos_vsnprintf(ctx, dst, n, fmt, ap) — formats into wasm-side buf
 *   yos_vfprintf (ctx, fp, fmt, ap)     — formats and fwrites to host FILE*
 *   yos_printf, yos_fprintf, yos_sprintf, yos_snprintf, yos_vprintf,
 *   yos_vsprintf — thin wrappers around the above, fed by hand-written
 *   m3w_<name> trampolines bound in main.c. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "yos/types.h"
#include <yos/ytrace/ytrace.h>
#include "impl/io/io-internal.h"   /* wstr_check */
#include <unistd.h>     /* write() for stream-handle fd_map routing */

/* Read one slot from the guest's va_list region.
 *
 * Clang's wasm32 variadic ABI packs each variadic arg at its NATURAL
 * alignment, then advances by the type's size — NOT a fixed 8-byte
 * slot. So:
 *   int / pointer (i32):   align 4, size 4
 *   long long (i64):       align 8, size 8
 *   double (f64):           align 8, size 8
 *
 * We were treating every slot as 8 bytes, which silently drifted the
 * read offset for any format like `"%s_%d"` (the `%d` ended up reading
 * past the int into adjacent memory). The clearenv FreeBSD test pinned
 * this — `snprintf("%s_%d", "TEST", i)` always returned "TEST_0". */
static inline void va_align(uint32_t *off, uint32_t a) {
    *off = (*off + a - 1) & ~(a - 1);
}
/* Range-check before reading 4/8 bytes from the va-pack region. The
 * va-pack itself lives in wasm linear memory, so a guest that hands
 * us an evil va_list could otherwise make these reads stomp past
 * memory_size. Return a safe zero on overflow — printf produces a
 * sensible-shaped output rather than reading garbage. */
static inline uint32_t va_i32(struct yos_exec_ctx *ctx, uint32_t *off) {
    va_align(off, 4);
    if ((uint64_t)*off + 4ULL > (uint64_t)ctx->memory_size) { *off += 4; return 0; }
    uint32_t v = *(uint32_t *)(ctx->memory + *off);
    *off += 4;
    return v;
}
static inline uint64_t va_i64(struct yos_exec_ctx *ctx, uint32_t *off) {
    va_align(off, 8);
    if ((uint64_t)*off + 8ULL > (uint64_t)ctx->memory_size) { *off += 8; return 0; }
    uint64_t v = *(uint64_t *)(ctx->memory + *off);
    *off += 8;
    return v;
}
static inline double va_f64(struct yos_exec_ctx *ctx, uint32_t *off) {
    va_align(off, 8);
    if ((uint64_t)*off + 8ULL > (uint64_t)ctx->memory_size) { *off += 8; return 0.0; }
    double v = *(double *)(ctx->memory + *off);
    *off += 8;
    return v;
}

/* Parse a single conversion spec starting at *fmt, append the formatted
 * result to `out` (capped at `out_cap` bytes total in `*outpos`). On
 * return *fmt points at the byte AFTER the spec. */
static void format_one(struct yos_exec_ctx *ctx,
                       const char **fmt, uint32_t *vap,
                       char *out, size_t out_cap, size_t *outpos)
{
    /* Copy the full spec verbatim into a scratch buffer so we can
     * pass it straight to host snprintf. Specs include flags, width,
     * precision, length modifier, conversion char. */
    char spec[64];
    size_t sp = 0;
    spec[sp++] = '%';
    const char *p = *fmt + 1;  /* past the '%' */
    /* flags */
    while (sp + 1 < sizeof(spec) && strchr("-+ #0'", *p)) spec[sp++] = *p++;
    /* width — may be '*' */
    int width_star = 0, prec_star = 0;
    if (*p == '*') {
        spec[sp++] = '*'; p++;
        width_star = 1;
    } else {
        while (sp + 1 < sizeof(spec) && *p >= '0' && *p <= '9')
            spec[sp++] = *p++;
    }
    /* precision */
    if (*p == '.') {
        spec[sp++] = *p++;
        if (*p == '*') {
            spec[sp++] = *p++;
            prec_star = 1;
        } else {
            while (sp + 1 < sizeof(spec) && *p >= '0' && *p <= '9')
                spec[sp++] = *p++;
        }
    }
    /* length modifier */
    int is_ll = 0, is_l = 0, is_h = 0, is_z = 0, is_j = 0;
    size_t mod_start = sp;  /* remember where length modifiers begin */
    while (sp + 1 < sizeof(spec) && strchr("hlLjztq", *p)) {
        if (*p == 'l' && p[1] == 'l') { is_ll = 1; spec[sp++] = *p++; }
        else if (*p == 'l') { is_l = 1; }
        else if (*p == 'h') { is_h = 1; }
        else if (*p == 'z') { is_z = 1; }
        else if (*p == 'j') { is_j = 1; }
        spec[sp++] = *p++;
    }
    /* conversion */
    char conv = *p;
    if (sp + 2 >= sizeof(spec) || conv == '\0') { *fmt = p; return; }
    spec[sp++] = conv;
    spec[sp] = '\0';
    *fmt = p + 1;

    /* GUEST-vs-HOST length-modifier mismatch.
     *
     * FreeBSD-i386 (the guest ABI) sizes:
     *   long      = 4 B    → %ld / %lu / %lx
     *   long long = 8 B    → %lld / %llu
     *   size_t    = 4 B    → %zd / %zu
     *   intmax_t  = 8 B    → %jd / %ju
     *
     * The host we'll forward to via host snprintf is typically x86_64
     * Linux / macOS / aarch64 — where `long` is 8 B and `size_t` is 8 B.
     * If we forward "%lu" literally and pass a 4-byte va arg, the host
     * snprintf reads 8 bytes from the stack and 4 of them are whatever
     * was adjacent. The visible symptom is what ls(1) was showing:
     *   snprintf(NULL, 0, "%lu", maxnlink) returns wrong digit count
     *   → every long-format column width collapses to ~0
     *   → "%*ju" rendering produces unpadded, jagged columns.
     *
     * Two fixes that work, depending on the conversion:
     *   1. For integer conversions where the guest expects 4 bytes
     *      (single 'l' or 'z') — strip the modifier so host snprintf
     *      reads a plain int / unsigned. We're already pulling a
     *      4-byte va arg below; this just keeps the format width
     *      promise in sync with what we hand the host varargs.
     *   2. 'll' / 'j' stay (both = 8 B on guest AND host — match).
     *      'h' / 'hh' stay (host snprintf handles short/char widths
     *      from a promoted int correctly).
     *
     * `%ls` (wide-char string) is left alone — we don't decode wide
     * strings from the guest today, and FreeBSD-i386 wchar_t happens
     * to be 4 B same as host x86_64. */
    if ((is_l && !is_ll) || is_z) {
        int is_int_conv = (conv == 'd' || conv == 'i' || conv == 'u' ||
                           conv == 'x' || conv == 'X' || conv == 'o');
        if (is_int_conv) {
            /* Drop the 'l' or 'z' that sits at spec[mod_start]. */
            memmove(spec + mod_start, spec + mod_start + 1,
                    sp - mod_start);
            sp--;
            spec[sp] = '\0';
        }
    }

    /* Pull star args first (host snprintf needs them in order). */
    int star_w = 0, star_p = 0;
    if (width_star) star_w = (int)va_i32(ctx, vap);
    if (prec_star)  star_p = (int)va_i32(ctx, vap);

    char buf[1024];
    int n = 0;

    switch (conv) {
    case 'd': case 'i': {
        if (is_ll || is_j) {
            int64_t v = (int64_t)va_i64(ctx, vap);
            n = width_star && prec_star ? snprintf(buf, sizeof(buf), spec, star_w, star_p, (long long)v)
              : width_star              ? snprintf(buf, sizeof(buf), spec, star_w, (long long)v)
              : prec_star               ? snprintf(buf, sizeof(buf), spec, star_p, (long long)v)
              :                            snprintf(buf, sizeof(buf), spec, (long long)v);
        } else {
            int32_t v = (int32_t)va_i32(ctx, vap);
            n = width_star && prec_star ? snprintf(buf, sizeof(buf), spec, star_w, star_p, (int)v)
              : width_star              ? snprintf(buf, sizeof(buf), spec, star_w, (int)v)
              : prec_star               ? snprintf(buf, sizeof(buf), spec, star_p, (int)v)
              :                            snprintf(buf, sizeof(buf), spec, (int)v);
        }
        break;
    }
    case 'u': case 'x': case 'X': case 'o': {
        if (is_ll || is_j) {
            uint64_t v = va_i64(ctx, vap);
            n = width_star && prec_star ? snprintf(buf, sizeof(buf), spec, star_w, star_p, (unsigned long long)v)
              : width_star              ? snprintf(buf, sizeof(buf), spec, star_w, (unsigned long long)v)
              : prec_star               ? snprintf(buf, sizeof(buf), spec, star_p, (unsigned long long)v)
              :                            snprintf(buf, sizeof(buf), spec, (unsigned long long)v);
        } else {
            uint32_t v = va_i32(ctx, vap);
            n = width_star && prec_star ? snprintf(buf, sizeof(buf), spec, star_w, star_p, (unsigned)v)
              : width_star              ? snprintf(buf, sizeof(buf), spec, star_w, (unsigned)v)
              : prec_star               ? snprintf(buf, sizeof(buf), spec, star_p, (unsigned)v)
              :                            snprintf(buf, sizeof(buf), spec, (unsigned)v);
        }
        break;
    }
    case 'p': {
        uint32_t v = va_i32(ctx, vap);
        /* Render guest pointers as their wasm offset in hex, like
         * host %p — keeps debug output readable. */
        n = snprintf(buf, sizeof(buf), "0x%x", v);
        break;
    }
    case 'c': {
        uint32_t v = va_i32(ctx, vap);
        n = snprintf(buf, sizeof(buf), spec, (int)v);
        break;
    }
    case 's': {
        uint32_t s_off = va_i32(ctx, vap);
        const char *s = wstr_check(ctx, s_off);
        /* wstr_check returns NULL if s_off is 0, OOB, or not NUL-
         * terminated within memory_size. Host snprintf would otherwise
         * walk past wasm memory. Render "(null)" so the format output
         * stays well-shaped. */
        if (!s) s = "(null)";
        n = width_star && prec_star ? snprintf(buf, sizeof(buf), spec, star_w, star_p, s)
          : width_star              ? snprintf(buf, sizeof(buf), spec, star_w, s)
          : prec_star               ? snprintf(buf, sizeof(buf), spec, star_p, s)
          :                            snprintf(buf, sizeof(buf), spec, s);
        break;
    }
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
    case 'a': case 'A': {
        double v = va_f64(ctx, vap);
        n = width_star && prec_star ? snprintf(buf, sizeof(buf), spec, star_w, star_p, v)
          : width_star              ? snprintf(buf, sizeof(buf), spec, star_w, v)
          : prec_star               ? snprintf(buf, sizeof(buf), spec, star_p, v)
          :                            snprintf(buf, sizeof(buf), spec, v);
        break;
    }
    case '%': {
        buf[0] = '%'; buf[1] = '\0'; n = 1; break;
    }
    case 'n':
        /* Refuse — would let guest write through a pointer arg. */
        n = 0; break;
    default:
        /* Unknown conversion — emit literally. */
        n = snprintf(buf, sizeof(buf), "%%%c", conv);
    }
    if (n < 0) return;
    /* snprintf semantics: the cursor (and the return value) MUST
     * advance by the number of characters the spec WOULD have written,
     * not by the number we were actually able to copy. The caller
     * uses the position to derive the digit count for things like
     *     snprintf(NULL, 0, "%lu", maxnlink)     // ls(1)
     * which is exactly the column-width probe ls -l uses to right-
     * align nlink / size / uid / gid. If we clamp the cursor at the
     * buffer cap, every probe returns 0 and every long-format column
     * collapses to zero padding — visible as the "ls -l columns are
     * jagged / chaotic" symptom the user kept reporting.
     *
     * Two separate values:
     *   cp = bytes safe to memcpy into `out` (clamped by out_cap)
     *   n  = bytes the spec would have produced (always advance by this)
     */
    size_t cp = (size_t)n;
    if (*outpos + cp > out_cap) cp = out_cap > *outpos ? out_cap - *outpos : 0;
    memcpy(out + *outpos, buf, cp);
    *outpos += (size_t)n;
}

/* Format `fmt` (with vararg slots starting at wasm offset `vap`) into
 * a host buffer. Returns the number of bytes that would have been
 * written if `out_cap` were unbounded (snprintf semantics). */
int yos_vsnprintf_core(struct yos_exec_ctx *ctx,
                       char *out, size_t out_cap,
                       uint32_t fmt_off, uint32_t va_off)
{
    /* Validate the guest format string is in-range AND NUL-terminated
     * before memory_size. Without this, a guest fmt_off near
     * memory_size with no NUL lets the `while (*fmt)` loop walk past
     * wasm memory and dereference whatever sits next in the host
     * address space. Bad fmt -> empty result, terminator written. */
    const char *fmt = wstr_check(ctx, fmt_off);
    if (!fmt) {
        if (out_cap > 0) out[0] = '\0';
        return 0;
    }
    size_t pos = 0;
    while (*fmt) {
        if (*fmt != '%') {
            if (pos < out_cap) out[pos] = *fmt;
            pos++;
            fmt++;
            continue;
        }
        format_one(ctx, &fmt, &va_off, out, out_cap, &pos);
    }
    if (out_cap > 0) out[pos < out_cap ? pos : out_cap - 1] = '\0';
    return (int)pos;
}

/* Resolve a guest FILE* handle to the host FILE* through impl/file.c's
 * handle table. Pre-bound: 1=stdin, 2=stdout, 3=stderr; 4..MAX are
 * fopen-allocated. Anything unknown falls back to stdout — partial
 * output is less bad than a NULL deref in fwrite. nvim's logger
 * writes to a real fopen()'d file (handle ≥ 4), and a previous shim
 * here was hard-coding 1→stdout / 2→stderr / else→stdout, sending
 * every log line to stdout instead. */
extern FILE *yos_handle_to_file(struct yos_exec_ctx *ctx, uint32_t h);
static FILE *guest_fp_to_host(struct yos_exec_ctx *ctx, uint32_t fp_off)
{
    (void)ctx;
    if (fp_off == 0) return stdout;
    FILE *f = yos_handle_to_file(ctx, fp_off);
    return f ? f : stdout;
}

extern int yos_fd_get(struct yos_exec_ctx *ctx, int wasm_fd);

int32_t yos_vfprintf(struct yos_exec_ctx *ctx,
                     uint32_t fp, uint32_t fmt_off, uint32_t va_off)
{
    if (fp == 3) ctx->stderr_written_since_exec = 1;
    char buf[8192];
    int n = yos_vsnprintf_core(ctx, buf, sizeof(buf), fmt_off, va_off);
    if (n < 0) return -1;
    size_t w = n < (int)sizeof(buf) ? (size_t)n : sizeof(buf) - 1;

    /* Stream handles 1/2/3 → route through fd_map. Going via host
     * glibc's stdout/stderr FILE* writes to host fd 1/2 (yos's own
     * stdio), which BYPASSES the wasm guest's fd_map[0/1/2] redirect
     * (e.g. zsh's dup2 of a pipe over stderr). Symptom: zsh's
     * `fprintf(stderr, "zsh:1: no such file or directory: %s\n", ...)`
     * landed in host glibc's stderr buffer and flushed to host fd 2 —
     * the user's terminal — but split across two writes that
     * interleaved with the bytes yos_fwrite/fputs routed through
     * fd_map, producing garbled output like "o such file or
     * directoryzsh:1: N: /bin/no-such". Routing here uses fd_map
     * exclusively, single write(), no host-glibc buffer involved. */
    if (fp == 1 || fp == 2 || fp == 3) {
        int wfd = (int)fp - 1;
        int hfd = yos_fd_get(ctx, wfd);
        if (hfd >= 0) {
            ydebug("vfprintf(fp=%u via fd_map[%d]=hfd %d, len=%zu): %.*s\n",
                   fp, wfd, hfd, w, (int)(w > 80 ? 80 : w), buf);
            ssize_t r = write(hfd, buf, w);
            return r < 0 ? -1 : (int32_t)r;
        }
        /* fd_map slot empty — fall through to host FILE* as last resort. */
    }

    FILE *f = guest_fp_to_host(ctx, fp);
    ydebug("vfprintf(fp=%u, host_fp=%p, len=%zu): %.*s\n",
           fp, (void *)f, w, (int)(w > 80 ? 80 : w), buf);
    fwrite(buf, 1, w, f);
    return n;
}

int32_t yos_fprintf(struct yos_exec_ctx *ctx,
                    uint32_t fp, uint32_t fmt_off, uint32_t va_off)
{
    return yos_vfprintf(ctx, fp, fmt_off, va_off);
}

/* impl/file.c reserves handle 2 = stdout (1 = stdin, 3 = stderr); using
 * 1 here used to silently send every printf to host stdin, which dropped
 * `nvim --version` etc. on the floor. */
int32_t yos_printf(struct yos_exec_ctx *ctx,
                   uint32_t fmt_off, uint32_t va_off)
{
    return yos_vfprintf(ctx, 2, fmt_off, va_off);
}

int32_t yos_vprintf(struct yos_exec_ctx *ctx,
                    uint32_t fmt_off, uint32_t va_off)
{
    return yos_vfprintf(ctx, 2, fmt_off, va_off);
}

int32_t yos_vsnprintf(struct yos_exec_ctx *ctx,
                      uint32_t dst_off, uint32_t n,
                      uint32_t fmt_off, uint32_t va_off)
{
    /* Validate the destination buffer's full [dst_off, dst_off+n)
     * range before vsnprintf_core writes into it. Without this a
     * guest can pass an out-of-range dst_off + large n and the
     * format loop scribbles past wasm memory. n==0 is the "size
     * probe" form (yos_snprintf(NULL, 0, ...)) — pass through with
     * dst==NULL so the core's count-only path still works. */
    if (n == 0) {
        return yos_vsnprintf_core(ctx, NULL, 0, fmt_off, va_off);
    }
    if (dst_off == 0 ||
        (uint64_t)dst_off + (uint64_t)n > (uint64_t)ctx->memory_size) {
        /* No safe place to write — drop the call. Standard libc
         * returns the would-have-written count; we approximate by
         * running the format-only count path. */
        return yos_vsnprintf_core(ctx, NULL, 0, fmt_off, va_off);
    }
    char *dst = (char *)(ctx->memory + dst_off);
    return yos_vsnprintf_core(ctx, dst, n, fmt_off, va_off);
}

int32_t yos_snprintf(struct yos_exec_ctx *ctx,
                     uint32_t dst_off, uint32_t n,
                     uint32_t fmt_off, uint32_t va_off)
{
    return yos_vsnprintf(ctx, dst_off, n, fmt_off, va_off);
}

int32_t yos_vsprintf(struct yos_exec_ctx *ctx,
                     uint32_t dst_off, uint32_t fmt_off, uint32_t va_off)
{
    /* sprintf has no bound; pretend the caller's buffer is huge. The
     * guest's libc would normally check and trap; we mirror that risk. */
    return yos_vsnprintf(ctx, dst_off, (uint32_t)-1, fmt_off, va_off);
}

int32_t yos_sprintf(struct yos_exec_ctx *ctx,
                    uint32_t dst_off, uint32_t fmt_off, uint32_t va_off)
{
    return yos_vsprintf(ctx, dst_off, fmt_off, va_off);
}
