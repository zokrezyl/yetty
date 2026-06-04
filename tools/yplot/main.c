/*
 * yplot — emit a yplot complex-prim OSC envelope for a function expression.
 *
 * Inside a yetty terminal the OSC is routed to the ydraw scrolling layer,
 * which renders the plot via the yplot pipeline. Outside a yetty terminal
 * the bytes are still printed (mostly garbage on a vt100), so the typical
 * usage is `yetty -e 'yplot ...'` or invocation from a script running in
 * a yetty session.
 *
 * Multi-function syntax (yexpr-plot) examples:
 *   yplot 'sin(x)'
 *   yplot 'f=sin(x); g=cos(x)' --xrange=-3.14..3.14
 *   yplot 'f=x*x; g=2*x+1; @f.color=#ffe66d; @g.color=#aa96da' \
 *         -w 480 -H 220 --yrange=-2..10
 */

#include <yetty/yplot/yplot.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#endif

struct yplot_opts {
    float w;
    float h;
    float x_min, x_max;
    float y_min, y_max;
    bool x_range_set;
    bool y_range_set;
    bool no_grid;
    bool no_axes;
    bool no_labels;
    bool no_newline;
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options] <expression> [<expression> ...]\n"
        "\n"
        "Emit a YPlot OSC envelope (YETTY_OSC_YDRAW_BIN, 600001) consumed\n"
        "by the yetty ydraw scrolling layer.\n"
        "\n"
        "Multi-function syntax (yexpr-plot):\n"
        "  '<expr>'                     single function (auto-named plot1)\n"
        "  'f=expr; g=expr; ...'        named functions\n"
        "  '@f.color=#RRGGBB; ...'      per-plot color overrides\n"
        "\n"
        "Options:\n"
        "  -w, --width=N             plot width  in pixels (default 400)\n"
        "  -H, --height=N            plot height in pixels (default 200)\n"
        "      --xrange=lo..hi       X axis range (default -3.14..3.14)\n"
        "      --yrange=lo..hi       Y axis range (default -1.5..1.5)\n"
        "      --no-grid             disable the grid overlay\n"
        "      --no-axes             disable the axes overlay\n"
        "      --no-labels           disable axis labels\n"
        "  -n                        no trailing newline after the OSC\n"
        "  -h, --help                show this help\n"
        "\n"
        "Multiple <expression> args are joined with '; ' before parsing,\n"
        "so quoting commas / semicolons inside one arg vs across many is\n"
        "equivalent.\n",
        prog);
}

/* Parse "lo..hi" into two floats. Returns 0 on success, -1 on error. */
static int parse_range(const char *s, float *lo, float *hi)
{
    if (!s) {
        return -1;
    }
    const char *dots = strstr(s, "..");
    if (!dots) {
        return -1;
    }
    char buf[64];
    size_t prefix_len = (size_t)(dots - s);
    if (prefix_len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, s, prefix_len);
    buf[prefix_len] = '\0';
    char *endp = NULL;
    float low = strtof(buf, &endp);
    if (!endp || *endp != '\0') {
        return -1;
    }
    const char *tail = dots + 2;
    float high = strtof(tail, &endp);
    if (!endp || *endp != '\0') {
        return -1;
    }
    *lo = low;
    *hi = high;
    return 0;
}

enum {
    OPT_XRANGE = 1000,
    OPT_YRANGE,
    OPT_NO_GRID,
    OPT_NO_AXES,
    OPT_NO_LABELS,
};

int main(int argc, char **argv)
{
    struct yplot_opts opts = {
        .w = 400.0f,
        .h = 200.0f,
        .x_min = -3.14159f,
        .x_max =  3.14159f,
        .y_min = -1.5f,
        .y_max =  1.5f,
    };

    static const struct yetty_yplatform_option long_opts[] = {
        {"width",     required_argument, NULL, 'w'},
        {"height",    required_argument, NULL, 'H'},
        {"xrange",    required_argument, NULL, OPT_XRANGE},
        {"yrange",    required_argument, NULL, OPT_YRANGE},
        {"no-grid",   no_argument,       NULL, OPT_NO_GRID},
        {"no-axes",   no_argument,       NULL, OPT_NO_AXES},
        {"no-labels", no_argument,       NULL, OPT_NO_LABELS},
        {"help",      no_argument,       NULL, 'h'},
        {NULL,        0,                 NULL, 0  },
    };

    int c;
    while ((c = yetty_yplatform_getopt_long(argc, argv, "w:H:nh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'w': opts.w = (float)atof(yetty_yplatform_optarg); break;
        case 'H': opts.h = (float)atof(yetty_yplatform_optarg); break;
        case OPT_XRANGE:
            if (parse_range(yetty_yplatform_optarg, &opts.x_min, &opts.x_max) < 0) {
                fprintf(stderr, "yplot: invalid xrange %s\n", yetty_yplatform_optarg);
                return 2;
            }
            opts.x_range_set = true;
            break;
        case OPT_YRANGE:
            if (parse_range(yetty_yplatform_optarg, &opts.y_min, &opts.y_max) < 0) {
                fprintf(stderr, "yplot: invalid yrange %s\n", yetty_yplatform_optarg);
                return 2;
            }
            opts.y_range_set = true;
            break;
        case OPT_NO_GRID:   opts.no_grid = true; break;
        case OPT_NO_AXES:   opts.no_axes = true; break;
        case OPT_NO_LABELS: opts.no_labels = true; break;
        case 'n':           opts.no_newline = true; break;
        case 'h':           usage(stdout, argv[0]); return 0;
        default:            usage(stderr, argv[0]); return 2;
        }
    }

    if (yetty_yplatform_optind >= argc) {
        fprintf(stderr, "yplot: missing expression\n");
        usage(stderr, argv[0]);
        return 2;
    }

    /* Join positional args with "; " so multi-function plots can be
     * passed either as one quoted arg or several. */
    size_t total = 0;
    for (int i = yetty_yplatform_optind; i < argc; i++) {
        total += strlen(argv[i]) + 2;
    }
    char *source = malloc(total + 1);
    if (!source) {
        fprintf(stderr, "yplot: out of memory\n");
        return 1;
    }
    size_t pos = 0;
    for (int i = yetty_yplatform_optind; i < argc; i++) {
        if (i > yetty_yplatform_optind) {
            source[pos++] = ';';
            source[pos++] = ' ';
        }
        size_t l = strlen(argv[i]);
        memcpy(source + pos, argv[i], l);
        pos += l;
    }
    source[pos] = '\0';
    size_t source_len = pos;

    uint32_t flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS;
    if (opts.no_grid)   flags &= ~YETTY_YPLOT_FLAG_GRID;
    if (opts.no_axes)   flags &= ~YETTY_YPLOT_FLAG_AXES;
    if (opts.no_labels) flags &= ~YETTY_YPLOT_FLAG_LABELS;

    struct yetty_yplot_render_config cfg = {
        .bounds_w = opts.w,
        .bounds_h = opts.h,
        .x_min = opts.x_min,
        .x_max = opts.x_max,
        .y_min = opts.y_min,
        .y_max = opts.y_max,
        .flags = flags,
    };
    /* Force range presence so render() doesn't silently fall back to
     * defaults when user passed e.g. yrange=-1..1 (both endpoints non-zero
     * trivially survive the heuristic, but yrange=0..something would be
     * eaten — better to be explicit). */
    if (!opts.x_range_set) {
        cfg.x_min = -3.14159f;
        cfg.x_max =  3.14159f;
    }
    if (!opts.y_range_set) {
        cfg.y_min = -1.5f;
        cfg.y_max =  1.5f;
    }

    struct yetty_ydraw_drawable_list_result rr =
        yetty_yplot_render(source, source_len, &cfg);
    free(source);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "yplot: render failed: %s\n", rr.error.msg);
        for (const struct yetty_ycore_error *e = rr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(rr.error);
        return 1;
    }

    int rc = 0;
    struct yetty_ycore_size_result wr = yetty_yplot_osc_bin_emit(rr.value, stdout);
    yetty_ydraw_drawable_list_destroy(rr.value);
    if (YETTY_IS_ERR(wr)) {
        fprintf(stderr, "yplot: OSC emit failed: %s\n", wr.error.msg);
        for (const struct yetty_ycore_error *e = wr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
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
