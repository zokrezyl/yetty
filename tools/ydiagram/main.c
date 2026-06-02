/*
 * ydiagram — render a diagram file (Mermaid) into a ydraw buffer and emit
 * an OSC YDRAW_BIN envelope on stdout so a running yetty ydraw pane
 * redraws it. Pure one-shot: parse → layout → render → emit → exit.
 *
 * Text width comes from a real MSDF font (yetty_yfont_msdf_font) so node
 * boxes are sized to fit their labels exactly the way the yetty pane will
 * draw them. Without that, layout falls back to a crude `0.6 * font_size *
 * char_count` heuristic and boxes are too tight (clipped labels) or too
 * loose (whitespace).
 *
 * Wire: a single OSC 600001 envelope. The buffer's first prim is CMD_ZERO
 * (added by the renderer), which clears the canvas + resets the cursor on
 * decode — replacing the obsolete separate OSC 600000 CLEAR envelope.
 *
 *   ydiagram <file.mmd>      # OSC envelope to stdout (default)
 *   ydiagram -o <file>       # raw serialized buffer, no OSC framing
 *   ydiagram -               # read Mermaid from stdin
 *   ydiagram --font-cdb <p>  # override the MSDF CDB used for measurement
 *   ydiagram --font-shader <p>
 */

#include <yetty/ydiagram/ydiagram.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*=============================================================================
 * Font measurement
 *
 * The msdf-font's measure_text honours real glyph advances so the layout
 * sizes boxes to actually fit their labels. font_create requires both a
 * CDB and a shader file even in measure-only use; shader_path content is
 * never consulted on this path but a readable file must exist.
 *===========================================================================*/

/* Resolve yetty's data dir without dragging in src/yetty/yplatform/paths/
 * (which only links into the main yetty binary). We use the same XDG
 * fallback chain as src/yetty/yplatform/paths/linux.c. */
static const char *resolve_data_dir(char *buf, size_t buf_size)
{
    const char *xdg  = getenv("XDG_DATA_HOME");
    if (xdg && *xdg) {
        snprintf(buf, buf_size, "%s/yetty", xdg);
        return buf;
    }
    const char *home = getenv("HOME");
    if (home && *home) {
        snprintf(buf, buf_size, "%s/.local/share/yetty", home);
        return buf;
    }
    return NULL;
}

static float measure_with_font(const char *text, size_t text_len, float font_size, void *userdata)
{
    struct yetty_yfont_font *font = (struct yetty_yfont_font *)userdata;
    if (!font || !font->ops || !font->ops->measure_text) {
        return font_size * 0.6f * (float)text_len;
    }
    struct float_result r = font->ops->measure_text(font, text, text_len, font_size);
    if (YETTY_IS_ERR(r)) {
        return font_size * 0.6f * (float)text_len;
    }
    return r.value;
}

static bool path_readable(const char *path)
{
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

/* Build the standard yetty asset paths from the platform's data_dir. The
 * canvas does the same construction in src/yetty/ydraw/canvas.c:335. */
static struct yetty_yfont_font *open_default_font(const char *cdb_override,
                                                   const char *shader_override)
{
    char cdb_buf[512];
    char sh_buf[512];
    const char *cdb_path = cdb_override;
    const char *sh_path  = shader_override;

    char        data_buf[512];
    const char *data_dir = NULL;
    if (!cdb_path || !sh_path) {
        const char *fonts_env = getenv("YETTY_FONTS_DIR");
        const char *sh_env    = getenv("YETTY_SHADERS_DIR");
        if (!fonts_env || !sh_env) {
            data_dir = resolve_data_dir(data_buf, sizeof(data_buf));
        }
        if (!cdb_path) {
            if (fonts_env) {
                snprintf(cdb_buf, sizeof(cdb_buf),
                         "%s/../msdf-fonts/DejaVuSansMNerdFontMono-Regular.cdb", fonts_env);
            } else if (data_dir) {
                snprintf(cdb_buf, sizeof(cdb_buf),
                         "%s/msdf-fonts/DejaVuSansMNerdFontMono-Regular.cdb", data_dir);
            } else {
                return NULL;
            }
            cdb_path = cdb_buf;
        }
        if (!sh_path) {
            if (sh_env) {
                snprintf(sh_buf, sizeof(sh_buf), "%s/msdf-font.wgsl", sh_env);
            } else if (data_dir) {
                snprintf(sh_buf, sizeof(sh_buf), "%s/shaders/msdf-font.wgsl", data_dir);
            } else {
                return NULL;
            }
            sh_path = sh_buf;
        }
    }

    if (!path_readable(cdb_path)) {
        fprintf(stderr, "ydiagram: font CDB not found: %s\n", cdb_path);
        return NULL;
    }
    if (!path_readable(sh_path)) {
        fprintf(stderr, "ydiagram: msdf shader not found: %s\n", sh_path);
        return NULL;
    }

    struct yetty_font_font_result fr =
        yetty_yfont_msdf_font_create(cdb_path, sh_path, "ydiagram_default");
    if (YETTY_IS_ERR(fr)) {
        fprintf(stderr, "ydiagram: msdf font create failed: %s\n",
                fr.error.msg ? fr.error.msg : "?");
        return NULL;
    }
    return fr.value;
}

/*=============================================================================
 * OSC / raw emission (unchanged)
 *===========================================================================*/

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

static int emit_osc_bin(FILE *out, struct yetty_ydraw_draw_list *buf)
{
    const uint8_t *raw  = NULL;
    size_t         size = yetty_ydraw_draw_list_serialize(buf, &raw);
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
    return emit_envelope(out, YETTY_OSC_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta),
                         raw, size);
}

static int write_raw(FILE *out, struct yetty_ydraw_draw_list *buf)
{
    const uint8_t *raw  = NULL;
    size_t         size = yetty_ydraw_draw_list_serialize(buf, &raw);
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
            "Emits an OSC YDRAW_BIN envelope on stdout for a running yetty\n"
            "ydraw pane. Box sizing uses real MSDF glyph widths.\n"
            "\n"
            "Options:\n"
            "  -o <file>             Write raw serialized ydraw buffer (no OSC).\n"
            "  --font-cdb <path>     Override the MSDF CDB used for measurement.\n"
            "  --font-shader <path>  Override the msdf-font.wgsl shader path.\n"
            "  --no-font             Skip MSDF font; use the heuristic fallback.\n"
            "  -h                    Show this help and exit.\n",
            prog);
}

int main(int argc, char **argv)
{
    ytrace_set_all_enabled(false);

    const char *input_path    = NULL;
    const char *out_path      = NULL;
    const char *cdb_override  = NULL;
    const char *sh_override   = NULL;
    bool        no_font       = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { usage(argv[0]); return 0; }
        if (strcmp(a, "--no-font") == 0) { no_font = true; continue; }
        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "ydiagram: -o requires a path\n"); return 2; }
            out_path = argv[++i];
            continue;
        }
        if (strcmp(a, "--font-cdb") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "ydiagram: --font-cdb requires a path\n"); return 2; }
            cdb_override = argv[++i];
            continue;
        }
        if (strcmp(a, "--font-shader") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "ydiagram: --font-shader requires a path\n"); return 2; }
            sh_override = argv[++i];
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

    /* Open the MSDF font for text measurement. On failure (no asset
     * install yet, --no-font, etc.) fall back to the heuristic so the
     * tool still works — boxes will just be a bit off. */
    struct yetty_yfont_font *font =
        no_font ? NULL : open_default_font(cdb_override, sh_override);
    yetty_ydiagram_measure_text_fn measure_fn = font ? measure_with_font : NULL;

    struct yetty_ydiagram_buffer_result br = yetty_ydiagram_render_mermaid_full(
        in_buf, in_len, NULL, NULL, measure_fn, font);
    free(in_buf);
    if (YETTY_IS_ERR(br)) {
        fprintf(stderr, "ydiagram: render failed: %s\n",
                br.error.msg ? br.error.msg : "?");
        if (font && font->ops && font->ops->destroy) font->ops->destroy(font);
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
        rc = emit_osc_bin(stdout, br.value);
        fflush(stdout);
    }

    yetty_ydraw_draw_list_destroy(br.value);
    if (font && font->ops && font->ops->destroy) font->ops->destroy(font);
    return rc;
}
