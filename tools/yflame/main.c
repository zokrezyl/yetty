/*
 * yflame — emit a flame-graph OSC envelope from folded-stack input.
 *
 * Reads the "folded" format (Brendan Gregg / stackcollapse-* lingua franca:
 * one `frame1;frame2;frame3 <count>` line per collapsed stack) from a file or
 * stdin, and writes a YDRAW_BIN OSC envelope (the ydraw scrolling layer
 * renders it as boxes + labels). yflame parses no profiler binary format and
 * bundles no profiler code — the standard pipeline is:
 *
 *   perf record -g …; perf script | stackcollapse-perf.pl | yflame
 *   async-profiler -o collapsed … | yflame
 *   py-spy record --format raw … ; yflame profile.folded
 *
 * Inside a yetty terminal the OSC renders; outside, the bytes print raw.
 */

#include <yetty/yflame/flame.h>
#include <yetty/yclass/class.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef _WIN32
#include <io.h> /* _fileno */
#define YFLAME_STDOUT_FD (_fileno(stdout))
#else
#include <unistd.h>
#define YFLAME_STDOUT_FD (STDOUT_FILENO)
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OPT_MIN_WIDTH = 256,
    OPT_NO_LABELS,
    OPT_ICICLE,
};

struct yflame_opts {
    float width;
    float frame_height;
    float min_width;
    bool icicle;
    bool no_labels;
    bool no_newline;
    bool interactive;
};

/* Implemented in interactive.c: ship the graph as a live yview figure and drive
 * hover-highlight + click-to-zoom from pane mouse events. */
int yflame_interactive_run(const char *input, size_t input_len, float min_width,
                           uint32_t base_flags);

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [options] [file]\n"
            "\n"
            "Render folded stacks (perf/FlameGraph format) as a flame-graph OSC\n"
            "envelope (YETTY_DCS_YDRAW_BIN, 600001) for the yetty ydraw layer.\n"
            "Reads from <file>, or stdin when no file is given.\n"
            "\n"
            "Input format (one collapsed stack per line):\n"
            "  main;parse;lex 42\n"
            "\n"
            "Options:\n"
            "  -w, --width=N         graph width in pixels (default 1200)\n"
            "  -f, --frame-height=N  height per stack level in pixels (default 18)\n"
            "      --min-width=N     skip boxes narrower than N pixels (default 0.5)\n"
            "      --icicle          root at the top, growing downward\n"
            "      --no-labels       omit frame-name labels\n"
            "  -I, --interactive     live figure: hover highlights, left-click zooms\n"
            "                        in, right-click/Esc zooms out, q quits (in yetty)\n"
            "  -n                    no trailing newline\n"
            "  -h, --help            show this help\n"
            "\n"
            "Example:\n"
            "  perf script | stackcollapse-perf.pl | %s\n",
            prog, prog);
}

/* Read the whole stream into a heap buffer. Returns NULL on error. */
static char *read_all(FILE *in, size_t *out_len)
{
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    const size_t chunk = 65536;
    for (;;) {
        if (len + chunk > cap) {
            size_t new_cap = cap ? cap * 2 : chunk * 2;
            char *grown = realloc(buf, new_cap);
            if (!grown) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap = new_cap;
        }
        size_t got = fread(buf + len, 1, chunk, in);
        len += got;
        if (got < chunk) {
            if (ferror(in)) {
                free(buf);
                return NULL;
            }
            break; /* EOF */
        }
    }
    *out_len = len;
    return buf ? buf : calloc(1, 1);
}

int main(int argc, char **argv)
{
    struct yflame_opts opts = {
        .width = 1200.0f,
        .frame_height = 18.0f,
        .min_width = 0.5f,
    };

    static const struct yetty_yplatform_option long_opts[] = {
        {"width", required_argument, NULL, 'w'},
        {"frame-height", required_argument, NULL, 'f'},
        {"min-width", required_argument, NULL, OPT_MIN_WIDTH},
        {"icicle", no_argument, NULL, OPT_ICICLE},
        {"no-labels", no_argument, NULL, OPT_NO_LABELS},
        {"interactive", no_argument, NULL, 'I'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int c;
    while ((c = yetty_yplatform_getopt_long(argc, argv, "w:f:nhI", long_opts, NULL)) != -1) {
        switch (c) {
        case 'w':
            opts.width = (float)atof(yetty_yplatform_optarg);
            break;
        case 'f':
            opts.frame_height = (float)atof(yetty_yplatform_optarg);
            break;
        case OPT_MIN_WIDTH:
            opts.min_width = (float)atof(yetty_yplatform_optarg);
            break;
        case OPT_ICICLE:
            opts.icicle = true;
            break;
        case OPT_NO_LABELS:
            opts.no_labels = true;
            break;
        case 'I':
            opts.interactive = true;
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

    const char *path = (yetty_yplatform_optind < argc) ? argv[yetty_yplatform_optind] : NULL;
    FILE *in = stdin;
    if (path) {
        in = fopen(path, "rb");
        if (!in) {
            fprintf(stderr, "yflame: cannot open %s\n", path);
            return 1;
        }
    }

    size_t input_len = 0;
    char *input = read_all(in, &input_len);
    if (path) {
        fclose(in);
    }
    if (!input) {
        fprintf(stderr, "yflame: failed to read input\n");
        return 1;
    }

    uint32_t flags = 0;
    if (!opts.no_labels) {
        flags |= YETTY_YFLAME_FLAG_LABELS;
    }
    if (opts.icicle) {
        flags |= YETTY_YFLAME_FLAG_ICICLE;
    }

    /* Interactive mode: ship a live yview figure and drive it from mouse/keys. */
    if (opts.interactive) {
        int rc = yflame_interactive_run(input, input_len, opts.min_width, flags);
        free(input);
        return rc;
    }

    /* Register the yclass, build a flame, parse the folded input, and render
     * the one-shot drawable list into a YDRAW_BIN envelope on stdout. */
    struct yetty_ycore_void_result reg = yetty_yflame_register();
    if (YETTY_IS_ERR(reg)) {
        fprintf(stderr, "yflame: class register failed: %s\n", reg.error.msg);
        yetty_ycore_error_destroy(reg.error);
        free(input);
        return 1;
    }

    struct yetty_yclass_object_ptr_result obj_r = yetty_yflame_flame_create(NULL);
    if (YETTY_IS_ERR(obj_r)) {
        fprintf(stderr, "yflame: create failed: %s\n", obj_r.error.msg);
        yetty_ycore_error_destroy(obj_r.error);
        free(input);
        return 1;
    }
    struct yetty_yclass_object *flame = obj_r.value;

    yetty_yflame_configure(flame, opts.width, opts.frame_height, opts.min_width, flags);

    struct yetty_ycore_void_result pr = yetty_yflame_parse(flame, input, input_len);
    free(input);
    if (YETTY_IS_ERR(pr)) {
        fprintf(stderr, "yflame: parse failed: %s\n", pr.error.msg);
        for (const struct yetty_ycore_error *e = pr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(pr.error);
        (void)yetty_yflame_destroy(flame);
        return 1;
    }

    struct yetty_ydraw_drawable_list_result rr = yetty_yflame_render(flame);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "yflame: render failed: %s\n", rr.error.msg);
        for (const struct yetty_ycore_error *e = rr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(rr.error);
        (void)yetty_yflame_destroy(flame);
        return 1;
    }

    int rc = 0;
    struct yetty_ycore_void_result wr = yetty_yflame_emit_osc(rr.value, YFLAME_STDOUT_FD);
    yetty_ydraw_drawable_list_destroy(rr.value);
    if (YETTY_IS_ERR(wr)) {
        fprintf(stderr, "yflame: OSC emit failed: %s\n", wr.error.msg);
        for (const struct yetty_ycore_error *e = wr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(wr.error);
        rc = 1;
    }

    (void)yetty_yflame_destroy(flame);

    if (!opts.no_newline) {
        fputc('\n', stdout);
    }
    fflush(stdout);
    return rc;
}
