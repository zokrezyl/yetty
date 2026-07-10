/* impl/cv_trace.c — runtime hooks for the auto-generated struct
 * converters. Provides the YOS_CONVERT_TRACE=1 toggle that the
 * generated cv_<name>_h2w / w2h call into.
 *
 * Output format (one line per conversion call):
 *   yos_cv h2w stat host=0x7f...  wasm=0xfe40 size=192 [ first 32 bytes hex ]
 *
 * Off by default — the gating function is `static inline` in the
 * .h via the indirection here, evaluated once per process from the
 * env var.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cv_trace_enabled_cached = -1;

int yos_cv_trace_enabled(void)
{
    if (cv_trace_enabled_cached < 0) {
        const char *e = getenv("YOS_CONVERT_TRACE");
        cv_trace_enabled_cached = (e && (e[0] == '1' || strcmp(e, "yes") == 0))
                                  ? 1 : 0;
    }
    return cv_trace_enabled_cached;
}

static void dump_bytes(const unsigned char *p, unsigned n, unsigned cap)
{
    if (n > cap) n = cap;
    for (unsigned i = 0; i < n; i++) fprintf(stderr, "%02x ", p[i]);
}

void yos_cv_trace_h2w(const char *name, const void *w_off,
                      const void *h_ptr, unsigned wasm_size)
{
    fprintf(stderr, "yos_cv h2w %-12s wasm=%p host=%p size=%u | ",
            name, w_off, h_ptr, wasm_size);
    /* Host bytes BEFORE the conversion — the source. */
    dump_bytes((const unsigned char *)h_ptr, 32, 32);
    fputc('\n', stderr);
}

void yos_cv_trace_w2h(const char *name, const void *h_ptr,
                      const void *w_off, unsigned wasm_size)
{
    fprintf(stderr, "yos_cv w2h %-12s host=%p wasm=%p size=%u | ",
            name, h_ptr, w_off, wasm_size);
    /* Wasm bytes BEFORE the conversion — the source. */
    dump_bytes((const unsigned char *)w_off, 32, 32);
    fputc('\n', stderr);
}
