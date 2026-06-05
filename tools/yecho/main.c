/*
 * yecho - echo text with embedded glyphs and styled blocks.
 *
 * Inside a yetty terminal, the parsed input is rendered into a ydraw-core
 * buffer and emitted as a YETTY_DCS_YDRAW_BIN sequence (same wire format
 * ycat uses). Outside a yetty terminal there's nothing meaningful to do
 * with rich content; we fall back to writing the raw input through —
 * unless --osc is forced.
 */

#include <yetty/yecho/yecho.h>
#include <yetty/yfont/shader-glyph.h>
#include <yetty/yplatform/getopt.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/terminal-detect.h>

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
#include <sys/ioctl.h>
#endif

struct yecho_opts {
    bool no_newline;
    bool list_glyphs;
    bool force_osc;
    bool force_raw;
    int width_cells;
    int height_cells;
    float font_size;
};

static int terminal_columns(void)
{
#ifdef __unix__
    struct winsize ws = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
#endif
    return 80;
}

static bool in_yetty_terminal(void)
{
    return yetty_running_under_yetty() != 0;
}

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options] [text...]\n"
        "\n"
        "Echo text with embedded glyphs and styled blocks.\n"
        "\n"
        "Grammar:\n"
        "  @name           shader glyph (e.g. @spinner, @heart)\n"
        "  {attrs: text}   styled block (e.g. {color=#ff0000: hello})\n"
        "  \\@ \\{ \\}        escaped literals\n"
        "\n"
        "Block attributes (semicolon-separated):\n"
        "  color=#RRGGBB   text color\n"
        "  bg=#RRGGBB      background color\n"
        "  style=bold      bold | italic | underline (combinable with '|')\n"
        "\n"
        "Inside a yetty terminal, the input is rendered to a ydraw buffer\n"
        "and emitted via OSC 600001 (YDRAW_BIN). Otherwise the raw input\n"
        "is written through.\n"
        "\n"
        "Options:\n"
        "  -w, --width=N        layout width in cells (default: term cols)\n"
        "  -H, --height=N       layout height in cells (default: 0)\n"
        "      --font-size=F    text size in pixels (default: 16)\n"
        "  -n                   no trailing newline\n"
        "  -l, --list           list available glyphs and exit\n"
        "      --osc            force OSC emission even outside a yetty terminal\n"
        "  -r, --raw            force raw passthrough (no rendering)\n"
        "  -h, --help           show this help\n"
        "\n"
        "  Use '-' or no args to read from stdin.\n",
        prog);
}

static void list_glyphs(FILE *out)
{
    size_t n = yetty_yfont_shader_glyph_count();
    const struct yetty_yfont_shader_glyph_entry *t = yetty_yfont_shader_glyph_table();
    fprintf(out, "Available glyphs (%zu):\n", n);
    for (size_t i = 0; i < n; i++) {
        fprintf(out, "  @%s (U+%04X)\n", t[i].name, t[i].codepoint);
    }
}

static int read_all_stdin(char **out, size_t *out_len)
{
    char *buf = NULL;
    size_t cap = 0, len = 0;
    char chunk[65536];
    for (;;) {
        size_t n = fread(chunk, 1, sizeof(chunk), stdin);
        if (n > 0) {
            if (len + n > cap) {
                size_t nc = cap ? cap * 2 : 65536;
                while (nc < len + n) {
                    nc *= 2;
                }
                char *nb = realloc(buf, nc);
                if (!nb) {
                    free(buf);
                    return -1;
                }
                buf = nb;
                cap = nc;
            }
            memcpy(buf + len, chunk, n);
            len += n;
        }
        if (n < sizeof(chunk)) {
            if (ferror(stdin)) {
                free(buf);
                return -1;
            }
            break;
        }
    }
    *out = buf;
    *out_len = len;
    return 0;
}

enum {
    OPT_FONT_SIZE = 1000,
    OPT_OSC,
};

int main(int argc, char **argv)
{
    struct yecho_opts opts = {0};

    static const struct yetty_yplatform_option long_opts[] = {
        {"width",     required_argument, NULL, 'w'},
        {"height",    required_argument, NULL, 'H'},
        {"font-size", required_argument, NULL, OPT_FONT_SIZE},
        {"list",      no_argument,       NULL, 'l'},
        {"osc",       no_argument,       NULL, OPT_OSC},
        {"raw",       no_argument,       NULL, 'r'},
        {"help",      no_argument,       NULL, 'h'},
        {NULL,        0,                 NULL, 0  },
    };

    int c;
    while ((c = yetty_yplatform_getopt_long(argc, argv, "w:H:nlrh", long_opts, NULL)) != -1) {
        switch (c) {
        case 'w':           opts.width_cells = atoi(yetty_yplatform_optarg); break;
        case 'H':           opts.height_cells = atoi(yetty_yplatform_optarg); break;
        case OPT_FONT_SIZE: opts.font_size = (float)atof(yetty_yplatform_optarg); break;
        case 'n':           opts.no_newline = true; break;
        case 'l':           opts.list_glyphs = true; break;
        case OPT_OSC:       opts.force_osc = true; break;
        case 'r':           opts.force_raw = true; break;
        case 'h':           usage(stdout, argv[0]); return 0;
        default:            usage(stderr, argv[0]); return 2;
        }
    }

    if (opts.list_glyphs) {
        list_glyphs(stdout);
        return 0;
    }

    /* Collect input: positional args joined with spaces, or stdin. */
    char *input = NULL;
    size_t input_len = 0;

    if (yetty_yplatform_optind < argc) {
        size_t total = 0;
        for (int i = yetty_yplatform_optind; i < argc; i++) {
            total += strlen(argv[i]) + 1;
        }
        input = malloc(total + 1);
        if (!input) {
            fprintf(stderr, "yecho: out of memory\n");
            return 1;
        }
        size_t pos = 0;
        for (int i = yetty_yplatform_optind; i < argc; i++) {
            if (i > yetty_yplatform_optind) {
                input[pos++] = ' ';
            }
            size_t l = strlen(argv[i]);
            memcpy(input + pos, argv[i], l);
            pos += l;
        }
        input[pos] = '\0';
        input_len = pos;
    } else {
        if (read_all_stdin(&input, &input_len) < 0) {
            fprintf(stderr, "yecho: failed to read stdin\n");
            return 1;
        }
    }

    int rc = 0;
    const bool emit_osc = opts.force_osc || (in_yetty_terminal() && !opts.force_raw);

    if (emit_osc) {
        if (opts.width_cells == 0) {
            opts.width_cells = terminal_columns();
        }
        struct yetty_yecho_render_config cfg = {
            .cell_width = 8,
            .cell_height = 16,
            .width_cells = (uint32_t)opts.width_cells,
            .font_size = opts.font_size,
            .default_fg = 0,
            .line_spacing = 0,
        };
        struct yetty_ydraw_drawable_list_result r =
            yetty_yecho_render_string(input, input_len, &cfg);
        if (YETTY_IS_ERR(r)) {
            fprintf(stderr, "yecho: render failed: %s\n", r.error.msg);
            for (const struct yetty_ycore_error *e = r.error.cause; e; e = e->cause) {
                fprintf(stderr, "  caused by: %s\n", e->msg);
            }
            yetty_ycore_error_destroy(r.error);
            rc = 1;
        } else {
            struct yetty_ycore_size_result wr = yetty_yecho_osc_bin_emit(r.value, stdout);
            yetty_ydraw_drawable_list_destroy(r.value);
            if (YETTY_IS_ERR(wr)) {
                fprintf(stderr, "yecho: OSC emit failed: %s\n", wr.error.msg);
                for (const struct yetty_ycore_error *e = wr.error.cause; e; e = e->cause) {
                    fprintf(stderr, "  caused by: %s\n", e->msg);
                }
                yetty_ycore_error_destroy(wr.error);
                rc = 1;
            }
        }
    } else {
        /* Raw passthrough. */
        if (input_len > 0) {
            fwrite(input, 1, input_len, stdout);
        }
    }

    if (!opts.no_newline) {
        fputc('\n', stdout);
    }
    fflush(stdout);
    free(input);
    return rc;
}
