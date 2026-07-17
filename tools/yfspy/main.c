/*
 * yfspy — render a Python-subset yfsvm shader as a yplot composite.
 *
 * tools/yplot's sibling, but the source language is the restricted Python
 * subset. The shader is compiled to yfsvm bytecode IN-PROCESS via the embedded
 * libpython-free CPython parser (yetty_cpython) + the yfsvm codegen
 * (yetty_yfspy_compile) — no subprocess, no CPython runtime. yfspy wraps the
 * bytecode in a yplot prim and emits the DCS envelope (YETTY_DCS_YDRAW_BIN)
 * the yetty ydraw scrolling layer renders.
 *
 * The shader's first `def` becomes function 0 (the rendered curve / field);
 * parameters bind positionally to [x, y, time, s0..s7]. A program that reads
 * `y` renders as a heatmap field.
 *
 * Typical use inside a yetty session:
 *   yetty -e 'yfspy mandel.py --xrange=-2.5..1 --yrange=-1.2..1.2'
 *
 * A precompiled blob can be rendered without recompiling:
 *   yfspy --bytecode mandel.bin
 */

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfspy/compile.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/yplot/yplot.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    bool emit_bytecode;   /* write raw serialized words to stdout, don't render */
    const char *bytecode; /* precompiled blob path, or "-" for stdin */
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [options] <shader.py>\n"
            "       %s [options] --bytecode <file|->\n"
            "\n"
            "Compile a Python-subset yfsvm shader and emit a yplot DCS envelope\n"
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
            "      --bytecode=FILE  render a precompiled yfsvm blob (or '-' for stdin)\n"
            "                       instead of compiling a .py shader\n"
            "      --emit-bytecode  write the compiled yfsvm bytecode (little-endian\n"
            "                       u32) to stdout and exit, instead of rendering\n"
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

/* Read an entire stream into a malloc'd buffer. Returns bytes read; the caller
 * frees *out_data. Returns SIZE_MAX on allocation failure. */
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

/* Read a whole text file into a malloc'd, NUL-terminated buffer. */
static char *read_file(const char *path)
{
    FILE *stream = fopen(path, "rb");
    if (!stream) {
        return NULL;
    }
    uint8_t *bytes = NULL;
    size_t size = read_all(stream, &bytes);
    fclose(stream);
    if (size == SIZE_MAX) {
        return NULL;
    }
    char *text = realloc(bytes, size + 1);
    if (!text) {
        free(bytes);
        return NULL;
    }
    text[size] = '\0';
    return text;
}

/* Little-endian byte blob -> u32 word array (caller frees), or NULL on error. */
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
    OPT_BYTECODE,
    OPT_EMIT_BYTECODE,
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
        {"bytecode", required_argument, NULL, OPT_BYTECODE},
        {"emit-bytecode", no_argument, NULL, OPT_EMIT_BYTECODE},
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
        case OPT_BYTECODE:
            opts.bytecode = yetty_yplatform_optarg;
            break;
        case OPT_EMIT_BYTECODE:
            opts.emit_bytecode = true;
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

    /* Obtain the serialized program words, either by compiling a .py shader in
     * process or by loading a precompiled blob. */
    uint32_t stack_words[1024];
    uint32_t *words = NULL;
    uint32_t word_count = 0;
    int words_owned = 0;

    if (opts.bytecode) {
        words = load_bytecode(opts.bytecode, &word_count);
        words_owned = 1;
        if (!words) {
            return 1;
        }
    } else if (yetty_yplatform_optind < argc) {
        const char *path = argv[yetty_yplatform_optind];
        char *source = read_file(path);
        if (!source) {
            fprintf(stderr, "yfspy: cannot read shader '%s'\n", path);
            return 1;
        }
        struct yetty_yfspy_program program;
        char err[256];
        int rc = yetty_yfspy_compile(source, path, &program, err, sizeof(err));
        free(source);
        if (rc != YETTY_YFSPY_OK) {
            fprintf(stderr, "yfspy: %s\n", err);
            return 1;
        }
        word_count =
            yetty_yfsvm_program_serialize(&program.program, stack_words,
                                          (uint32_t)(sizeof(stack_words) / sizeof(stack_words[0])));
        if (word_count == 0) {
            fprintf(stderr, "yfspy: bytecode serialize failed\n");
            return 1;
        }
        words = stack_words;
    } else {
        fprintf(stderr, "yfspy: missing <shader.py>\n");
        usage(stderr, argv[0]);
        return 2;
    }

    if (opts.emit_bytecode) {
        for (uint32_t i = 0; i < word_count; i++) {
            uint32_t w = words[i];
            putchar((int)(w & 0xFFu));
            putchar((int)((w >> 8) & 0xFFu));
            putchar((int)((w >> 16) & 0xFFu));
            putchar((int)((w >> 24) & 0xFFu));
        }
        fflush(stdout);
        if (words_owned) {
            free(words);
        }
        return 0;
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
        yetty_yplot_render_program(words, word_count, &cfg);
    if (words_owned) {
        free(words);
    }
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "yfspy: render failed: %s\n", rr.error.msg);
        for (const struct yetty_ycore_error *cause = rr.error.cause; cause; cause = cause->cause) {
            fprintf(stderr, "  caused by: %s\n", cause->msg);
        }
        yetty_ycore_error_destroy(rr.error);
        return 1;
    }

    int rc = 0;
    struct yetty_ycore_size_result wr = yetty_yplot_dcs_bin_emit(rr.value, stdout);
    yetty_ydraw_drawable_list_destroy(rr.value);
    if (YETTY_IS_ERR(wr)) {
        fprintf(stderr, "yfspy: DCS emit failed: %s\n", wr.error.msg);
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
