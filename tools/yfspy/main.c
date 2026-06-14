/*
 * yfspy — render a Python-subset yfsvm shader as a yplot composite.
 *
 * This is tools/yplot's sibling, but the source language is the restricted
 * Python subset (the compiler ships under tools/yfspy/assets/) instead of yexpr. The Python compiler
 * lowers the `.py` shader to a serialized yfsvm program client-side; yfspy
 * wraps that bytecode in a yplot prim and emits the OSC envelope
 * (YETTY_DCS_YDRAW_BIN) the yetty ydraw scrolling layer renders. The Python
 * frontend never runs inside yetty — only the compiled bytecode crosses the
 * wire, which is what makes this safe on webasm / android render targets.
 *
 * The shader's first `def` becomes function 0 (the rendered curve / field);
 * parameters bind positionally to [x, y, time, s0..s7] (see the frontend's
 * PYTHON-FRONTEND.md). A program that reads `y` renders as a heatmap field.
 *
 * Typical use inside a yetty session:
 *   yetty -e 'yfspy mandel.py --xrange=-2.5..1 --yrange=-1.2..1.2'
 *
 * Without uv available, feed precompiled bytecode instead:
 *   uv run .../pyc/cli.py compile mandel.py -o - | yfspy --bytecode -
 */

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/yplot/yplot.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#define YFSPY_POPEN_MODE "rb"
#else
#define YFSPY_POPEN_MODE "r"
#endif

#ifndef YFSPY_COMPILER_DEFAULT
#define YFSPY_COMPILER_DEFAULT ""
#endif

struct yfspy_opts {
    float width;
    float height;
    float x_min, x_max;
    float y_min, y_max;
    bool x_range_set;
    bool y_range_set;
    bool no_grid;
    bool no_axes;
    bool no_labels;
    bool no_newline;
    const char *compiler;  /* path to pyc/cli.py */
    const char *bytecode;  /* precompiled blob path, or "-" for stdin */
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [options] <shader.py>\n"
            "       %s [options] --bytecode <file|->\n"
            "\n"
            "Compile a Python-subset yfsvm shader and emit a yplot OSC envelope\n"
            "(YETTY_DCS_YDRAW_BIN, 600001) consumed by the yetty ydraw layer.\n"
            "\n"
            "Options:\n"
            "  -w, --width=N        plot width  in pixels (default 400)\n"
            "  -H, --height=N       plot height in pixels (default 200)\n"
            "      --xrange=lo..hi  X axis range (default -3.14..3.14)\n"
            "      --yrange=lo..hi  Y axis range (default -1.5..1.5)\n"
            "      --no-grid        disable the grid overlay\n"
            "      --no-axes        disable the axes overlay\n"
            "      --no-labels      disable axis labels\n"
            "      --compiler=PATH  path to the pyc/cli.py compiler\n"
            "                       (default: $YFSPY_COMPILER or the build path)\n"
            "      --bytecode=FILE  use a precompiled yfsvm blob (or '-' for stdin)\n"
            "                       instead of compiling a .py shader\n"
            "  -n                   no trailing newline after the OSC\n"
            "  -h, --help           show this help\n",
            prog, prog);
}

/* Parse "lo..hi" into two floats. Returns 0 on success, -1 on error. */
static int parse_range(const char *text, float *low, float *high)
{
    if (!text) {
        return -1;
    }
    const char *dots = strstr(text, "..");
    if (!dots) {
        return -1;
    }
    char buf[64];
    size_t prefix_len = (size_t)(dots - text);
    if (prefix_len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, text, prefix_len);
    buf[prefix_len] = '\0';
    char *endp = NULL;
    float lo = strtof(buf, &endp);
    if (!endp || *endp != '\0') {
        return -1;
    }
    float hi = strtof(dots + 2, &endp);
    if (!endp || *endp != '\0') {
        return -1;
    }
    *low = lo;
    *high = hi;
    return 0;
}

/* Read an entire stream into a malloc'd byte buffer. Returns bytes read; the
 * caller frees *out_data. Returns SIZE_MAX on allocation failure. */
static size_t read_all(FILE *stream, uint8_t **out_data)
{
    size_t capacity = 65536;
    size_t size = 0;
    uint8_t *data = malloc(capacity);
    if (!data) {
        *out_data = NULL;
        return SIZE_MAX;
    }
    for (;;) {
        if (size == capacity) {
            size_t next = capacity * 2;
            uint8_t *grown = realloc(data, next);
            if (!grown) {
                free(data);
                *out_data = NULL;
                return SIZE_MAX;
            }
            data = grown;
            capacity = next;
        }
        size_t got = fread(data + size, 1, capacity - size, stream);
        size += got;
        if (got == 0) {
            break;
        }
    }
    *out_data = data;
    return size;
}

/* Convert a little-endian byte blob into a u32 word array (the serialized
 * yfsvm program). Returns the word array (caller frees) and the count, or
 * NULL on error. */
static uint32_t *bytes_to_words(const uint8_t *bytes, size_t byte_count, uint32_t *out_words)
{
    if (byte_count == 0 || (byte_count % 4u) != 0u) {
        return NULL;
    }
    uint32_t count = (uint32_t)(byte_count / 4u);
    uint32_t *words = malloc((size_t)count * sizeof(uint32_t));
    if (!words) {
        return NULL;
    }
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = bytes + (size_t)i * 4u;
        words[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                   ((uint32_t)p[3] << 24);
    }
    *out_words = count;
    return words;
}

/* Resolve the compiler path: explicit flag > env > build-time default. */
static const char *resolve_compiler(const struct yfspy_opts *opts)
{
    if (opts->compiler && opts->compiler[0]) {
        return opts->compiler;
    }
    const char *env = getenv("YFSPY_COMPILER");
    if (env && env[0]) {
        return env;
    }
    return YFSPY_COMPILER_DEFAULT;
}

/* Compile `shader_path` via the Python frontend, returning the serialized
 * program words (caller frees) and count. Returns NULL on failure. */
static uint32_t *compile_shader(const struct yfspy_opts *opts, const char *shader_path,
                                uint32_t *out_words)
{
    const char *compiler = resolve_compiler(opts);
    if (!compiler[0]) {
        fprintf(stderr,
                "yfspy: no compiler path; pass --compiler=PATH or set $YFSPY_COMPILER\n");
        return NULL;
    }
    /* Reject quotes in paths — we shell out, and quoting them safely is not
     * worth the surface area for a dev tool. */
    if (strchr(compiler, '"') || strchr(shader_path, '"')) {
        fprintf(stderr, "yfspy: paths must not contain double quotes\n");
        return NULL;
    }

    char command[4096];
    int written = snprintf(command, sizeof(command),
                           "uv run \"%s\" compile \"%s\" -o -", compiler, shader_path);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        fprintf(stderr, "yfspy: compiler command too long\n");
        return NULL;
    }

    FILE *pipe = popen(command, YFSPY_POPEN_MODE);
    if (!pipe) {
        fprintf(stderr, "yfspy: failed to launch compiler (%s)\n", command);
        return NULL;
    }
    uint8_t *bytes = NULL;
    size_t byte_count = read_all(pipe, &bytes);
    int status = pclose(pipe);
    if (byte_count == SIZE_MAX) {
        fprintf(stderr, "yfspy: out of memory reading compiler output\n");
        return NULL;
    }
    if (status != 0) {
        /* The compiler prints its own diagnostic (e.g. "compile error: ...")
         * to stderr, which the user already saw. */
        fprintf(stderr, "yfspy: compiler exited with status %d\n", status);
        free(bytes);
        return NULL;
    }
    uint32_t *words = bytes_to_words(bytes, byte_count, out_words);
    free(bytes);
    if (!words) {
        fprintf(stderr, "yfspy: compiler produced no usable bytecode\n");
        return NULL;
    }
    return words;
}

/* Read precompiled bytecode from a file path ("-" = stdin). */
static uint32_t *load_bytecode(const char *path, uint32_t *out_words)
{
    FILE *stream = (strcmp(path, "-") == 0) ? stdin : fopen(path, "rb");
    if (!stream) {
        fprintf(stderr, "yfspy: cannot open bytecode '%s'\n", path);
        return NULL;
    }
    uint8_t *bytes = NULL;
    size_t byte_count = read_all(stream, &bytes);
    if (stream != stdin) {
        fclose(stream);
    }
    if (byte_count == SIZE_MAX) {
        fprintf(stderr, "yfspy: out of memory reading bytecode\n");
        return NULL;
    }
    uint32_t *words = bytes_to_words(bytes, byte_count, out_words);
    free(bytes);
    if (!words) {
        fprintf(stderr, "yfspy: bytecode is empty or not a whole number of words\n");
        return NULL;
    }
    return words;
}

enum {
    OPT_XRANGE = 1000,
    OPT_YRANGE,
    OPT_NO_GRID,
    OPT_NO_AXES,
    OPT_NO_LABELS,
    OPT_COMPILER,
    OPT_BYTECODE,
};

int main(int argc, char **argv)
{
    struct yfspy_opts opts = {
        .width = 400.0f,
        .height = 200.0f,
        .x_min = -3.14159f,
        .x_max = 3.14159f,
        .y_min = -1.5f,
        .y_max = 1.5f,
    };

    static const struct yetty_yplatform_option long_opts[] = {
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'H'},
        {"xrange", required_argument, NULL, OPT_XRANGE},
        {"yrange", required_argument, NULL, OPT_YRANGE},
        {"no-grid", no_argument, NULL, OPT_NO_GRID},
        {"no-axes", no_argument, NULL, OPT_NO_AXES},
        {"no-labels", no_argument, NULL, OPT_NO_LABELS},
        {"compiler", required_argument, NULL, OPT_COMPILER},
        {"bytecode", required_argument, NULL, OPT_BYTECODE},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int option;
    while ((option = yetty_yplatform_getopt_long(argc, argv, "w:H:nh", long_opts, NULL)) != -1) {
        switch (option) {
        case 'w':
            opts.width = (float)atof(yetty_yplatform_optarg);
            break;
        case 'H':
            opts.height = (float)atof(yetty_yplatform_optarg);
            break;
        case OPT_XRANGE:
            if (parse_range(yetty_yplatform_optarg, &opts.x_min, &opts.x_max) < 0) {
                fprintf(stderr, "yfspy: invalid xrange %s\n", yetty_yplatform_optarg);
                return 2;
            }
            opts.x_range_set = true;
            break;
        case OPT_YRANGE:
            if (parse_range(yetty_yplatform_optarg, &opts.y_min, &opts.y_max) < 0) {
                fprintf(stderr, "yfspy: invalid yrange %s\n", yetty_yplatform_optarg);
                return 2;
            }
            opts.y_range_set = true;
            break;
        case OPT_NO_GRID:
            opts.no_grid = true;
            break;
        case OPT_NO_AXES:
            opts.no_axes = true;
            break;
        case OPT_NO_LABELS:
            opts.no_labels = true;
            break;
        case OPT_COMPILER:
            opts.compiler = yetty_yplatform_optarg;
            break;
        case OPT_BYTECODE:
            opts.bytecode = yetty_yplatform_optarg;
            break;
        case 'n':
            opts.no_newline = true;
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
    }

    uint32_t words_count = 0;
    uint32_t *words = NULL;
    if (opts.bytecode) {
        words = load_bytecode(opts.bytecode, &words_count);
    } else if (yetty_yplatform_optind < argc) {
        words = compile_shader(&opts, argv[yetty_yplatform_optind], &words_count);
    } else {
        fprintf(stderr, "yfspy: missing <shader.py>\n");
        usage(stderr, argv[0]);
        return 2;
    }
    if (!words) {
        return 1;
    }

    uint32_t flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS;
    if (opts.no_grid) {
        flags &= ~YETTY_YPLOT_FLAG_GRID;
    }
    if (opts.no_axes) {
        flags &= ~YETTY_YPLOT_FLAG_AXES;
    }
    if (opts.no_labels) {
        flags &= ~YETTY_YPLOT_FLAG_LABELS;
    }

    struct yetty_yplot_render_config cfg = {
        .bounds_w = opts.width,
        .bounds_h = opts.height,
        .x_min = opts.x_range_set ? opts.x_min : -3.14159f,
        .x_max = opts.x_range_set ? opts.x_max : 3.14159f,
        .y_min = opts.y_range_set ? opts.y_min : -1.5f,
        .y_max = opts.y_range_set ? opts.y_max : 1.5f,
        .flags = flags,
    };

    struct yetty_ydraw_drawable_list_result rr =
        yetty_yplot_render_program(words, words_count, &cfg);
    free(words);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "yfspy: render failed: %s\n", rr.error.msg);
        for (const struct yetty_ycore_error *cause = rr.error.cause; cause; cause = cause->cause) {
            fprintf(stderr, "  caused by: %s\n", cause->msg);
        }
        yetty_ycore_error_destroy(rr.error);
        return 1;
    }

    int rc = 0;
    struct yetty_ycore_size_result wr = yetty_yplot_osc_bin_emit(rr.value, stdout);
    yetty_ydraw_drawable_list_destroy(rr.value);
    if (YETTY_IS_ERR(wr)) {
        fprintf(stderr, "yfspy: OSC emit failed: %s\n", wr.error.msg);
        for (const struct yetty_ycore_error *cause = wr.error.cause; cause; cause = cause->cause) {
            fprintf(stderr, "  caused by: %s\n", cause->msg);
        }
        yetty_ycore_error_destroy(wr.error);
        rc = 1;
    }

    if (!opts.no_newline) {
        fputc('\n', stdout);
    }
    fflush(stdout);
    return rc;
}
