/*
 * yplot — emit a yplot composite DCS envelope for a function expression.
 *
 * Inside a yetty terminal the DCS is routed to the ydraw scrolling layer,
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
    bool x_log;
    bool y_log;
    bool no_grid;
    bool no_axes;
    bool no_labels;
    bool no_newline;
    const char *title;
    const char *x_label;
    const char *y_label;
    enum yetty_yplot_legend_mode legend_mode;
    enum yetty_yplot_colormap colormap;
    float field_min, field_max;
    const char *data_specs[8]; /* raw --data values: [name=]file[:column] */
    size_t data_count;
    const char *band_specs[4]; /* raw --band values: curve=lo_spec,hi_spec */
    size_t band_count;
    const char *err_specs[4]; /* raw --err values: curve=err_spec */
    size_t err_count;
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [options] <expression> [<expression> ...]\n"
            "\n"
            "Emit a YPlot DCS envelope (YETTY_DCS_YDRAW_BIN, 600001) consumed\n"
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
            "      --xlog                logarithmic X axis (range must be > 0;\n"
            "                            default range becomes 0.1..100)\n"
            "      --ylog                logarithmic Y axis (same rules)\n"
            "      --title=TEXT          figure title above the plot\n"
            "      --xlabel=TEXT         X axis name under the tick labels\n"
            "      --ylabel=TEXT         Y axis name above the tick column\n"
            "      --legend              show the legend even for one curve\n"
            "      --no-legend           never show the legend\n"
            "      --colormap=NAME       field colormap: viridis (default),\n"
            "                            plasma, magma, inferno\n"
            "      --field-range=lo..hi  value range a field maps onto the\n"
            "                            colormap (default -1..1); sets the\n"
            "                            colorbar labels\n"
            "      --data=[NAME=]SPEC    plot samples from a data file (may\n"
            "                            repeat, up to 8). SPEC is\n"
            "                            file[:column] — .npy (1-D f4/f8/\n"
            "                            i4/i8) or CSV/TSV/whitespace text;\n"
            "                            column by header name or 0-based\n"
            "                            index (default: second column).\n"
            "                            NAME labels the curve in the\n"
            "                            legend. With --data alone the\n"
            "                            expression may be omitted; axis\n"
            "                            ranges default to the data extent\n"
            "                            and sample index.\n"
            "      --band=CURVE=LO,HI    shaded envelope around --data curve\n"
            "                            CURVE, between two data specs\n"
            "                            (lower, upper), e.g.\n"
            "                            --band 'temp=run.csv:2,run.csv:3'\n"
            "      --err=CURVE=SPEC      whisker error bars on --data curve\n"
            "                            CURVE from a symmetric-error data\n"
            "                            spec (drawn at curve +- err)\n"
            "      --no-grid             disable the grid overlay\n"
            "      --no-axes             disable the axes overlay\n"
            "      --no-labels           disable axis labels (and all figure text)\n"
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
    OPT_XLOG,
    OPT_YLOG,
    OPT_TITLE,
    OPT_XLABEL,
    OPT_YLABEL,
    OPT_LEGEND,
    OPT_NO_LEGEND,
    OPT_COLORMAP,
    OPT_FIELD_RANGE,
    OPT_DATA,
    OPT_BAND,
    OPT_ERR,
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
        .x_max = 3.14159f,
        .y_min = -1.5f,
        .y_max = 1.5f,
    };

    static const struct yetty_yplatform_option long_opts[] = {
        {"width", required_argument, NULL, 'w'},
        {"height", required_argument, NULL, 'H'},
        {"xrange", required_argument, NULL, OPT_XRANGE},
        {"yrange", required_argument, NULL, OPT_YRANGE},
        {"xlog", no_argument, NULL, OPT_XLOG},
        {"ylog", no_argument, NULL, OPT_YLOG},
        {"title", required_argument, NULL, OPT_TITLE},
        {"xlabel", required_argument, NULL, OPT_XLABEL},
        {"ylabel", required_argument, NULL, OPT_YLABEL},
        {"legend", no_argument, NULL, OPT_LEGEND},
        {"no-legend", no_argument, NULL, OPT_NO_LEGEND},
        {"colormap", required_argument, NULL, OPT_COLORMAP},
        {"field-range", required_argument, NULL, OPT_FIELD_RANGE},
        {"data", required_argument, NULL, OPT_DATA},
        {"band", required_argument, NULL, OPT_BAND},
        {"err", required_argument, NULL, OPT_ERR},
        {"no-grid", no_argument, NULL, OPT_NO_GRID},
        {"no-axes", no_argument, NULL, OPT_NO_AXES},
        {"no-labels", no_argument, NULL, OPT_NO_LABELS},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int c;
    while ((c = yetty_yplatform_getopt_long(argc, argv, "w:H:nh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'w':
            opts.w = (float)atof(yetty_yplatform_optarg);
            break;
        case 'H':
            opts.h = (float)atof(yetty_yplatform_optarg);
            break;
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
        case OPT_XLOG:
            opts.x_log = true;
            break;
        case OPT_YLOG:
            opts.y_log = true;
            break;
        case OPT_TITLE:
            opts.title = yetty_yplatform_optarg;
            break;
        case OPT_XLABEL:
            opts.x_label = yetty_yplatform_optarg;
            break;
        case OPT_YLABEL:
            opts.y_label = yetty_yplatform_optarg;
            break;
        case OPT_LEGEND:
            opts.legend_mode = YETTY_YPLOT_LEGEND_ON;
            break;
        case OPT_NO_LEGEND:
            opts.legend_mode = YETTY_YPLOT_LEGEND_OFF;
            break;
        case OPT_COLORMAP:
            if (strcmp(yetty_yplatform_optarg, "viridis") == 0) {
                opts.colormap = YETTY_YPLOT_COLORMAP_VIRIDIS;
            } else if (strcmp(yetty_yplatform_optarg, "plasma") == 0) {
                opts.colormap = YETTY_YPLOT_COLORMAP_PLASMA;
            } else if (strcmp(yetty_yplatform_optarg, "magma") == 0) {
                opts.colormap = YETTY_YPLOT_COLORMAP_MAGMA;
            } else if (strcmp(yetty_yplatform_optarg, "inferno") == 0) {
                opts.colormap = YETTY_YPLOT_COLORMAP_INFERNO;
            } else {
                fprintf(stderr, "yplot: unknown colormap %s\n", yetty_yplatform_optarg);
                return 2;
            }
            break;
        case OPT_FIELD_RANGE:
            if (parse_range(yetty_yplatform_optarg, &opts.field_min, &opts.field_max) < 0) {
                fprintf(stderr, "yplot: invalid field-range %s\n", yetty_yplatform_optarg);
                return 2;
            }
            break;
        case OPT_DATA:
            if (opts.data_count >= 8) {
                fprintf(stderr, "yplot: too many --data curves (max 8)\n");
                return 2;
            }
            opts.data_specs[opts.data_count++] = yetty_yplatform_optarg;
            break;
        case OPT_BAND:
            if (opts.band_count >= 4) {
                fprintf(stderr, "yplot: too many --band envelopes (max 4)\n");
                return 2;
            }
            opts.band_specs[opts.band_count++] = yetty_yplatform_optarg;
            break;
        case OPT_ERR:
            if (opts.err_count >= 4) {
                fprintf(stderr, "yplot: too many --err specs (max 4)\n");
                return 2;
            }
            opts.err_specs[opts.err_count++] = yetty_yplatform_optarg;
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

    if (yetty_yplatform_optind >= argc && opts.data_count == 0) {
        fprintf(stderr, "yplot: missing expression (or --data)\n");
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

    /* Load --data curves. The name prefix (before '=') labels the curve in
     * the legend; the rest is the loader spec (file[:column]). */
    struct yetty_yplot_buffer_input data_buffers[8];
    struct yetty_yplot_loaded_samples loaded[8];
    char name_scratch[8][64];
    size_t loaded_count = 0;
    float data_min = 0.0f, data_max = 0.0f;
    size_t longest_series = 0;
    for (size_t i = 0; i < opts.data_count; i++) {
        const char *spec = opts.data_specs[i];
        const char *name = NULL;
        const char *equals = strchr(spec, '=');
        if (equals && equals != spec && (size_t)(equals - spec) < sizeof(name_scratch[0])) {
            memcpy(name_scratch[i], spec, (size_t)(equals - spec));
            name_scratch[i][equals - spec] = '\0';
            name = name_scratch[i];
            spec = equals + 1;
        }
        struct yetty_yplot_loaded_samples_result load_res = yetty_yplot_load_samples(spec);
        if (YETTY_IS_ERR(load_res)) {
            fprintf(stderr, "yplot: --data %s: %s\n", opts.data_specs[i], load_res.error.msg);
            for (const struct yetty_ycore_error *e = load_res.error.cause; e; e = e->cause) {
                fprintf(stderr, "  caused by: %s\n", e->msg);
            }
            yetty_ycore_error_destroy(load_res.error);
            for (size_t j = 0; j < loaded_count; j++) {
                free(loaded[j].samples);
            }
            free(source);
            return 1;
        }
        loaded[loaded_count] = load_res.value;
        memset(&data_buffers[loaded_count], 0, sizeof(data_buffers[loaded_count]));
        data_buffers[loaded_count].samples = load_res.value.samples;
        data_buffers[loaded_count].count = load_res.value.count;
        data_buffers[loaded_count].name = name;
        for (size_t j = 0; j < load_res.value.count; j++) {
            float sample = load_res.value.samples[j];
            if (loaded_count == 0 && j == 0) {
                data_min = data_max = sample;
            } else {
                if (sample < data_min) {
                    data_min = sample;
                }
                if (sample > data_max) {
                    data_max = sample;
                }
            }
        }
        if (load_res.value.count > longest_series) {
            longest_series = load_res.value.count;
        }
        loaded_count++;
    }

    /* Attach --band / --err envelopes to their owner curves. Envelope
     * buffers are appended hidden (band input only); all data buffers
     * together must fit the shader's 8 sampler slots. */
    for (size_t i = 0; i < opts.band_count + opts.err_count; i++) {
        bool is_err = i >= opts.band_count;
        const char *raw_spec = is_err ? opts.err_specs[i - opts.band_count] : opts.band_specs[i];
        const char *equals = strchr(raw_spec, '=');
        if (!equals || equals == raw_spec) {
            fprintf(stderr, "yplot: %s needs CURVE=SPEC form: %s\n", is_err ? "--err" : "--band",
                    raw_spec);
            goto data_cleanup_error;
        }
        char curve_name[64];
        size_t curve_name_len = (size_t)(equals - raw_spec);
        if (curve_name_len >= sizeof(curve_name)) {
            fprintf(stderr, "yplot: curve name too long: %s\n", raw_spec);
            goto data_cleanup_error;
        }
        memcpy(curve_name, raw_spec, curve_name_len);
        curve_name[curve_name_len] = '\0';

        size_t owner = loaded_count;
        for (size_t j = 0; j < loaded_count; j++) {
            if (data_buffers[j].name && strcmp(data_buffers[j].name, curve_name) == 0) {
                owner = j;
                break;
            }
        }
        if (owner == loaded_count) {
            fprintf(stderr, "yplot: %s: no --data curve named %s\n", is_err ? "--err" : "--band",
                    curve_name);
            goto data_cleanup_error;
        }
        if (loaded_count + 2 > 8) {
            fprintf(stderr, "yplot: too many data buffers (curves + envelopes > 8)\n");
            goto data_cleanup_error;
        }

        float *lo_samples = NULL;
        float *hi_samples = NULL;
        size_t envelope_count = 0;
        if (is_err) {
            struct yetty_yplot_loaded_samples_result err_res = yetty_yplot_load_samples(equals + 1);
            if (YETTY_IS_ERR(err_res)) {
                fprintf(stderr, "yplot: --err %s: %s\n", raw_spec, err_res.error.msg);
                yetty_ycore_error_destroy(err_res.error);
                goto data_cleanup_error;
            }
            envelope_count = err_res.value.count < data_buffers[owner].count
                                 ? err_res.value.count
                                 : data_buffers[owner].count;
            lo_samples = malloc(envelope_count * sizeof(float));
            hi_samples = malloc(envelope_count * sizeof(float));
            if (!lo_samples || !hi_samples) {
                free(lo_samples);
                free(hi_samples);
                free(err_res.value.samples);
                fprintf(stderr, "yplot: envelope alloc failed\n");
                goto data_cleanup_error;
            }
            for (size_t j = 0; j < envelope_count; j++) {
                lo_samples[j] = data_buffers[owner].samples[j] - err_res.value.samples[j];
                hi_samples[j] = data_buffers[owner].samples[j] + err_res.value.samples[j];
            }
            free(err_res.value.samples);
        } else {
            /* --band CURVE=LO_SPEC,HI_SPEC — split on the last comma so
             * column specs keep their own colons. */
            const char *comma = strrchr(equals + 1, ',');
            if (!comma || comma == equals + 1 || !comma[1]) {
                fprintf(stderr, "yplot: --band needs CURVE=LO,HI form: %s\n", raw_spec);
                goto data_cleanup_error;
            }
            char lo_spec[512];
            size_t lo_spec_len = (size_t)(comma - (equals + 1));
            if (lo_spec_len >= sizeof(lo_spec)) {
                fprintf(stderr, "yplot: --band spec too long: %s\n", raw_spec);
                goto data_cleanup_error;
            }
            memcpy(lo_spec, equals + 1, lo_spec_len);
            lo_spec[lo_spec_len] = '\0';
            struct yetty_yplot_loaded_samples_result lo_res = yetty_yplot_load_samples(lo_spec);
            if (YETTY_IS_ERR(lo_res)) {
                fprintf(stderr, "yplot: --band %s (lower): %s\n", raw_spec, lo_res.error.msg);
                yetty_ycore_error_destroy(lo_res.error);
                goto data_cleanup_error;
            }
            struct yetty_yplot_loaded_samples_result hi_res = yetty_yplot_load_samples(comma + 1);
            if (YETTY_IS_ERR(hi_res)) {
                fprintf(stderr, "yplot: --band %s (upper): %s\n", raw_spec, hi_res.error.msg);
                yetty_ycore_error_destroy(hi_res.error);
                free(lo_res.value.samples);
                goto data_cleanup_error;
            }
            lo_samples = lo_res.value.samples;
            hi_samples = hi_res.value.samples;
            envelope_count =
                lo_res.value.count < hi_res.value.count ? lo_res.value.count : hi_res.value.count;
        }

        /* Auto-range must cover the envelope, not just the curves. */
        for (size_t j = 0; j < envelope_count; j++) {
            if (lo_samples[j] < data_min) {
                data_min = lo_samples[j];
            }
            if (hi_samples[j] > data_max) {
                data_max = hi_samples[j];
            }
        }

        memset(&data_buffers[loaded_count], 0, sizeof(data_buffers[loaded_count]));
        data_buffers[loaded_count].samples = lo_samples;
        data_buffers[loaded_count].count = envelope_count;
        data_buffers[loaded_count].hidden = true;
        loaded[loaded_count].samples = lo_samples;
        loaded[loaded_count].count = envelope_count;
        size_t lo_index = loaded_count++;

        memset(&data_buffers[loaded_count], 0, sizeof(data_buffers[loaded_count]));
        data_buffers[loaded_count].samples = hi_samples;
        data_buffers[loaded_count].count = envelope_count;
        data_buffers[loaded_count].hidden = true;
        loaded[loaded_count].samples = hi_samples;
        loaded[loaded_count].count = envelope_count;
        size_t hi_index = loaded_count++;

        data_buffers[owner].has_band = true;
        data_buffers[owner].band_lo = (int)lo_index;
        data_buffers[owner].band_hi = (int)hi_index;
        data_buffers[owner].band_style = is_err ? YETTY_YPLOT_BAND_WHISKERS : YETTY_YPLOT_BAND_FILL;
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
    if (opts.x_log) {
        flags |= YETTY_YPLOT_FLAG_XLOG;
    }
    if (opts.y_log) {
        flags |= YETTY_YPLOT_FLAG_YLOG;
    }

    struct yetty_yplot_render_config cfg = {
        .bounds_w = opts.w,
        .bounds_h = opts.h,
        .x_min = opts.x_min,
        .x_max = opts.x_max,
        .y_min = opts.y_min,
        .y_max = opts.y_max,
        .flags = flags,
        .title = opts.title,
        .x_label = opts.x_label,
        .y_label = opts.y_label,
        .legend_mode = opts.legend_mode,
        .colormap = opts.colormap,
        .field_min = opts.field_min,
        .field_max = opts.field_max,
    };
    /* Force range presence so render() doesn't silently fall back to
     * defaults when user passed e.g. yrange=-1..1 (both endpoints non-zero
     * trivially survive the heuristic, but yrange=0..something would be
     * eaten — better to be explicit). A log axis without an explicit range
     * gets a positive decade default; the signed linear default would be
     * rejected by the renderer. Loaded data curves auto-range to the data
     * extent (5% padding) and to the sample index along x. */
    if (!opts.x_range_set) {
        if (loaded_count > 0 && longest_series > 1) {
            cfg.x_min = 0.0f;
            cfg.x_max = (float)(longest_series - 1);
        } else {
            cfg.x_min = opts.x_log ? 0.1f : -3.14159f;
            cfg.x_max = opts.x_log ? 100.0f : 3.14159f;
        }
    }
    if (!opts.y_range_set) {
        if (loaded_count > 0 && !opts.y_log) {
            float span = data_max - data_min;
            float padding = span > 0.0f ? span * 0.05f : 1.0f;
            cfg.y_min = data_min - padding;
            cfg.y_max = data_max + padding;
        } else {
            cfg.y_min = opts.y_log ? 0.1f : -1.5f;
            cfg.y_max = opts.y_log ? 100.0f : 1.5f;
        }
    }

    struct yetty_ydraw_drawable_list_result rr = yetty_yplot_render_with_buffers(
        source, source_len, loaded_count > 0 ? data_buffers : NULL, loaded_count, &cfg);
    free(source);
    for (size_t i = 0; i < loaded_count; i++) {
        free(loaded[i].samples);
    }
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "yplot: render failed: %s\n", rr.error.msg);
        for (const struct yetty_ycore_error *e = rr.error.cause; e; e = e->cause) {
            fprintf(stderr, "  caused by: %s\n", e->msg);
        }
        yetty_ycore_error_destroy(rr.error);
        return 1;
    }

    int rc = 0;
    struct yetty_ycore_size_result wr = yetty_yplot_dcs_bin_emit(rr.value, stdout);
    yetty_ydraw_drawable_list_destroy(rr.value);
    if (YETTY_IS_ERR(wr)) {
        fprintf(stderr, "yplot: DCS emit failed: %s\n", wr.error.msg);
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

data_cleanup_error:
    /* Reached only before render: loaded sample arrays and the joined
     * source string are still owned here. */
    for (size_t i = 0; i < loaded_count; i++) {
        free(loaded[i].samples);
    }
    free(source);
    return 1;
}
