/*
 * ymath — render a TeX-subset math expression inline in the terminal.
 *
 *   ymath 'E = mc^2'
 *   ymath '\frac{\partial u}{\partial t} = \alpha \nabla^2 u'
 *   ymath --size 36 '\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}'
 *   ymath -f equation.tex
 *
 * Emits a YDRAW_BIN DCS envelope (same wire as ycat / yplot).
 */

#include <yetty/ymath/ymath.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [options] '<math>'\n"
            "\n"
            "Render TeX-subset math inline: scripts (x^2, a_i), \\frac,\n"
            "\\sqrt, \\sum/\\int/\\prod with limits, greek letters and the\n"
            "common relation/operator symbols.\n"
            "\n"
            "Options:\n"
            "  -s, --size=N   base glyph size in pixels (default 28)\n"
            "  -f, --file=F   read the expression from a file\n"
            "  -h, --help     show this help\n",
            prog);
}

int main(int argc, char **argv)
{
    float font_size = 0.0f;
    const char *file_path = NULL;
    const char *expression = NULL;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else if (strncmp(arg, "--size=", 7) == 0) {
            font_size = strtof(arg + 7, NULL);
        } else if (strcmp(arg, "-s") == 0 && i + 1 < argc) {
            font_size = strtof(argv[++i], NULL);
        } else if (strncmp(arg, "--file=", 7) == 0) {
            file_path = arg + 7;
        } else if (strcmp(arg, "-f") == 0 && i + 1 < argc) {
            file_path = argv[++i];
        } else if (!expression) {
            expression = arg;
        } else {
            fprintf(stderr, "ymath: unexpected argument %s\n", arg);
            usage(stderr, argv[0]);
            return 2;
        }
    }

    char *file_text = NULL;
    if (file_path) {
        FILE *file = fopen(file_path, "rb");
        if (!file) {
            fprintf(stderr, "ymath: cannot open %s\n", file_path);
            return 1;
        }
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        rewind(file);
        file_text = size > 0 ? malloc((size_t)size + 1) : NULL;
        if (!file_text || fread(file_text, 1, (size_t)size, file) != (size_t)size) {
            fprintf(stderr, "ymath: cannot read %s\n", file_path);
            free(file_text);
            fclose(file);
            return 1;
        }
        fclose(file);
        file_text[size] = '\0';
        expression = file_text;
    }
    if (!expression || !expression[0]) {
        fprintf(stderr, "ymath: missing expression\n");
        usage(stderr, argv[0]);
        free(file_text);
        return 2;
    }

    struct yetty_ymath_render_config config = {
        .font_size = font_size,
    };
    struct yetty_ydraw_drawable_list_result render_res =
        yetty_ymath_render(expression, strlen(expression), &config);
    free(file_text);
    if (YETTY_IS_ERR(render_res)) {
        fprintf(stderr, "ymath: render failed: %s\n", render_res.error.msg);
        for (const struct yetty_ycore_error *cause = render_res.error.cause; cause;
             cause = cause->cause) {
            fprintf(stderr, "  caused by: %s\n", cause->msg);
        }
        yetty_ycore_error_destroy(render_res.error);
        return 1;
    }

    int exit_code = 0;
    struct yetty_ycore_size_result emit_res = yetty_ymath_dcs_emit(render_res.value, stdout);
    yetty_ydraw_drawable_list_destroy(render_res.value);
    if (YETTY_IS_ERR(emit_res)) {
        fprintf(stderr, "ymath: emit failed: %s\n", emit_res.error.msg);
        yetty_ycore_error_destroy(emit_res.error);
        exit_code = 1;
    }
    fputc('\n', stdout);
    fflush(stdout);
    return exit_code;
}
