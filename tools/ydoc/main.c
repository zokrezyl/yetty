/*
 * ydoc — rich-text document editor.
 *
 * Thin entry: builds (or loads) a yrich:ydoc document object and hands it
 * to the shared yrich app host, which opens a window and runs the
 * ygui-decorated editor (formatting toolbar + scrolling document view +
 * statusbar), rendered through the in-process yfigure container — same
 * path as the other ygui apps (no OSC).
 *
 * Press Esc or close the window to quit.
 *
 * Usage:
 *   ydoc                       # built-in demo content
 *   ydoc path/to/sample.ydoc.yaml
 */

#include <yetty/yrich/yrich-app.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-export.h>
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

static struct yetty_ycore_void_result seed_demo(struct yetty_yclass_object *doc_obj)
{
    const char *paras[] = {
        "Welcome to ydoc — a rich text editor.",
        "",
        "Use the menus to format text, or the toolbar to undo / redo.",
        "",
        "Press Ctrl+Q or click the ✕ Quit button to exit.",
    };
    for (size_t i = 0; i < sizeof(paras) / sizeof(paras[0]); i++) {
        struct yetty_yclass_object_ptr_result para_res =
            yetty_yrich_ydoc_add_paragraph(doc_obj, paras[i], strlen(paras[i]));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, para_res, "seed_demo: add_paragraph failed");
    }
    return YETTY_OK_VOID();
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

    struct yetty_yclass_object *doc_obj = NULL;
    if (file_path) {
        /* Pick the reader by extension: .md/.markdown and .txt import through
         * the compatibility layer; everything else is native YAML. */
        size_t path_len = strlen(file_path);
        struct yetty_yclass_object_ptr_result load_res;
        if ((path_len > 3 && strcmp(file_path + path_len - 3, ".md") == 0) ||
            (path_len > 9 && strcmp(file_path + path_len - 9, ".markdown") == 0)) {
            load_res = yetty_yrich_ydoc_import_markdown_file(file_path);
        } else if (path_len > 4 && strcmp(file_path + path_len - 4, ".txt") == 0) {
            load_res = yetty_yrich_ydoc_import_text_file(file_path);
        } else if ((path_len > 5 && strcmp(file_path + path_len - 5, ".html") == 0) ||
                   (path_len > 4 && strcmp(file_path + path_len - 4, ".htm") == 0)) {
            load_res = yetty_yrich_ydoc_import_html_file(file_path);
        } else if (path_len > 4 && strcmp(file_path + path_len - 4, ".rtf") == 0) {
            load_res = yetty_yrich_ydoc_import_rtf_file(file_path);
        } else {
            load_res = yetty_yrich_ydoc_load_yaml_file(file_path);
        }
        if (YETTY_IS_ERR(load_res)) {
            yetty_ycore_error_print(stderr, "ydoc: load failed", load_res.error);
            yetty_ycore_error_destroy(load_res.error);
            return 1;
        }
        doc_obj = load_res.value;
        struct yetty_ycore_void_result path_res =
            yetty_yrich_ydoc_set_source_path(doc_obj, file_path);
        if (YETTY_IS_ERR(path_res)) {
            yetty_ycore_error_destroy(path_res.error);
        }
    } else {
        struct yetty_yclass_object_ptr_result create_res = yetty_yrich_ydoc_create(NULL);
        if (YETTY_IS_ERR(create_res)) {
            yetty_ycore_error_print(stderr, "ydoc: create failed", create_res.error);
            yetty_ycore_error_destroy(create_res.error);
            return 1;
        }
        doc_obj = create_res.value;
        struct yetty_ycore_void_result seed_res = seed_demo(doc_obj);
        if (YETTY_IS_ERR(seed_res)) {
            yetty_ycore_error_print(stderr, "ydoc: seed demo", seed_res.error);
            yetty_ycore_error_destroy(seed_res.error);
            struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(doc_obj);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            return 1;
        }
    }

    struct yetty_ycore_int_result run_result =
        yetty_yrich_app_run(argc, argv, doc_obj, YETTY_YRICH_APP_YDOC);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "ydoc: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
