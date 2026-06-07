/*
 * ydoc — rich-text document editor.
 *
 * Thin entry: builds (or loads) a yetty_yrich_ydoc and hands it to the
 * shared yrich app host, which opens a window and runs the ygui-decorated
 * editor (formatting toolbar + scrolling document view + statusbar),
 * rendered through the in-process yfigure container — same path as the
 * other ygui apps (no OSC).
 *
 * Press 'q' / Esc / close the window to quit.
 *
 * Usage:
 *   ydoc                       # built-in demo content
 *   ydoc path/to/sample.ydoc.yaml
 */

#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-app.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-yaml.h>

#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [file.ydoc.yaml]\n"
            "  With no file, opens built-in demo content.\n",
            prog);
}

static void seed_demo(struct yetty_yrich_ydoc *d)
{
    const char *paras[] = {
        "Welcome to ydoc — a rich text editor.",
        "",
        "Click in the toolbar to undo / redo or add a paragraph.",
        "",
        "Press 'q' or Esc to quit.",
    };
    for (size_t i = 0; i < sizeof(paras) / sizeof(paras[0]); i++) {
        yetty_yrich_ydoc_add_paragraph(d, paras[i], strlen(paras[i]));
    }
}

int main(int argc, char **argv)
{
    const char *file_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        }
        if (argv[i][0] != '-' && !file_path) {
            file_path = argv[i];
        }
    }

    struct yetty_yrich_ydoc *doc = NULL;
    if (file_path) {
        struct yetty_yrich_ydoc_ptr_result lr = yetty_yrich_ydoc_load_yaml_file(file_path);
        if (YETTY_IS_ERR(lr)) {
            fprintf(stderr, "ydoc: load %s: %s\n", file_path, lr.error.msg);
            yetty_ycore_error_destroy(lr.error);
            return 1;
        }
        doc = lr.value;
    } else {
        struct yetty_yrich_ydoc_ptr_result dr = yetty_yrich_ydoc_create();
        if (YETTY_IS_ERR(dr)) {
            fprintf(stderr, "ydoc: %s\n", dr.error.msg);
            yetty_ycore_error_destroy(dr.error);
            return 1;
        }
        doc = dr.value;
        seed_demo(doc);
    }

    struct yetty_ycore_int_result run_result =
        yetty_yrich_app_run(argc, argv, &doc->base, YETTY_YRICH_APP_YDOC);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "ydoc: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
