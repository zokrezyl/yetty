/*
 * ydiagram — render a diagram file (Mermaid) into a ypaint buffer and emit
 * an OSC envelope on stdout so a running yetty ypaint pane redraws it.
 *
 * Pure one-shot: parse → layout → render → emit → exit. Same wire as
 * tools/ymaze and tools/yzoo (OSC 600000 clear + OSC 600001 bin).
 *
 *   ydiagram <file.mmd>           # OSC envelope to stdout (default)
 *   ydiagram -o <file>            # raw serialized buffer, no OSC framing
 *   ydiagram -                    # read Mermaid from stdin
 */

#include <yetty/ydiagram/ydiagram.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yterm/osc-codes.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int emit_envelope(FILE *out, int osc_code, int compressed, const void *args,
                         size_t args_len, const void *body, size_t body_len)
{
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result r =
        yetty_yface_emit(osc_code, compressed, args, args_len, body, body_len, &env);
    int rc = 0;
    if (YETTY_IS_OK(r) && env.size > 0) {
        if (fwrite(env.data, 1, env.size, out) != env.size) rc = 1;
    } else if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ydiagram: yface_emit failed: %s\n",
                r.error.msg ? r.error.msg : "?");
        rc = 1;
    }
    yetty_ycore_buffer_destroy(&env);
    return rc;
}

static int emit_osc(FILE *out, struct yetty_ypaint_core_buffer *buf, bool with_clear)
{
    if (with_clear) {
        int rc = emit_envelope(out, YETTY_OSC_YPAINT_CLEAR, 0, NULL, 0, NULL, 0);
        if (rc) return rc;
    }
    const uint8_t *raw  = NULL;
    size_t         size = yetty_ypaint_core_buffer_serialize(buf, &raw);
    if (size == 0 || !raw) {
        fprintf(stderr, "ydiagram: serialize produced empty buffer\n");
        return 1;
    }
    struct yetty_yface_bin_meta meta = {
        .magic            = YETTY_YFACE_BIN_MAGIC,
        .version          = YETTY_YFACE_BIN_VERSION,
        .compressed       = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size         = size,
        .reserved         = {0, 0},
    };
    return emit_envelope(out, YETTY_OSC_YPAINT_BIN, /*compressed=*/1, &meta, sizeof(meta),
                         raw, size);
}

static int write_raw(FILE *out, struct yetty_ypaint_core_buffer *buf)
{
    const uint8_t *raw  = NULL;
    size_t         size = yetty_ypaint_core_buffer_serialize(buf, &raw);
    if (size == 0 || !raw) {
        fprintf(stderr, "ydiagram: serialize produced empty buffer\n");
        return 1;
    }
    if (fwrite(raw, 1, size, out) != size) {
        perror("ydiagram: write");
        return 1;
    }
    return 0;
}

static char *slurp_file(const char *path, size_t *out_len)
{
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ydiagram: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    size_t cap  = 8192;
    size_t size = 0;
    char  *buf  = malloc(cap);
    if (!buf) {
        if (f != stdin) fclose(f);
        return NULL;
    }
    for (;;) {
        if (size + 4096 + 1 > cap) {
            size_t nc = cap * 2;
            char  *nb = realloc(buf, nc);
            if (!nb) {
                free(buf);
                if (f != stdin) fclose(f);
                return NULL;
            }
            buf = nb;
            cap = nc;
        }
        size_t n = fread(buf + size, 1, cap - size - 1, f);
        size += n;
        if (n == 0) break;
    }
    buf[size] = 0;
    if (f != stdin) fclose(f);
    *out_len = size;
    return buf;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] <file.mmd | ->\n"
            "\n"
            "Emits OSC envelopes on stdout for a running yetty ypaint pane.\n"
            "\n"
            "Options:\n"
            "  -o <file>     Write raw serialized ypaint buffer (no OSC) to <file>.\n"
            "  -q            Skip the OSC clear envelope before the bin payload.\n"
            "  -h            Show this help and exit.\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *out_path   = NULL;
    bool        quiet      = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(argv[0]); return 0; }
        if (strcmp(a, "-q") == 0) { quiet = true; continue; }
        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "ydiagram: -o requires a path\n"); return 2; }
            out_path = argv[++i];
            continue;
        }
        if (a[0] == '-' && strcmp(a, "-") != 0) {
            fprintf(stderr, "ydiagram: unknown option '%s'\n", a);
            usage(argv[0]);
            return 2;
        }
        if (input_path) { fprintf(stderr, "ydiagram: multiple inputs\n"); return 2; }
        input_path = a;
    }
    if (!input_path) { usage(argv[0]); return 2; }

    size_t in_len = 0;
    char  *in_buf = slurp_file(input_path, &in_len);
    if (!in_buf) return 1;

    struct yetty_ydiagram_buffer_result br = yetty_ydiagram_render_mermaid(in_buf, in_len);
    free(in_buf);
    if (YETTY_IS_ERR(br)) {
        fprintf(stderr, "ydiagram: render failed: %s\n",
                br.error.msg ? br.error.msg : "?");
        return 1;
    }

    int rc;
    if (out_path) {
        FILE *out = strcmp(out_path, "-") == 0 ? stdout : fopen(out_path, "wb");
        if (!out) {
            fprintf(stderr, "ydiagram: cannot open '%s': %s\n", out_path, strerror(errno));
            rc = 1;
        } else {
            rc = write_raw(out, br.value);
            if (out != stdout) fclose(out);
        }
    } else {
        rc = emit_osc(stdout, br.value, !quiet);
        fflush(stdout);
    }

    yetty_ypaint_core_buffer_destroy(br.value);
    return rc;
}
