/*
 * ysheet — spreadsheet editor.
 *
 * Thin entry: builds (or loads) a yrich:spreadsheet document object and
 * hands it to the shared yrich app host, which opens a window and runs
 * the ygui-decorated editor (edit toolbar + scrolling grid + statusbar),
 * rendered through the in-process yfigure container — same path as the
 * other ygui apps (no OSC).
 *
 * Press Esc or close the window to quit.
 *
 * Usage:
 *   ysheet                              # built-in demo grid
 *   ysheet path/to/sample.ysheet.yaml
 */

#include <yetty/yrich/yrich-app.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/spreadsheet.h>
#include <yetty/yrich/yrich-yaml.h>

#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [file.ysheet.yaml]\n"
            "  With no file, opens a built-in demo grid.\n",
            prog);
}

static struct yetty_ycore_void_result set_cell(struct yetty_yclass_object *sheet_obj, int32_t row,
                                               int32_t col, const char *value)
{
    struct yetty_ycore_buffer text = {
        .data = (uint8_t *)value,
        .size = strlen(value),
        .capacity = strlen(value),
    };
    struct yetty_ycore_void_result set_res =
        yetty_yrich_spreadsheet_set_cell_value(sheet_obj, row, col, text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_res, "seed_demo: set_cell_value failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result seed_demo(struct yetty_yclass_object *sheet_obj)
{
    struct yetty_ycore_void_result grid_res =
        yetty_yrich_spreadsheet_set_grid_size(sheet_obj, 50, 20);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "seed_demo: set_grid_size failed");
    struct yetty_ycore_void_result cell_res = set_cell(sheet_obj, 0, 0, "yrich spreadsheet");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "seed_demo");
    cell_res = set_cell(sheet_obj, 1, 0, "A");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "seed_demo");
    cell_res = set_cell(sheet_obj, 1, 1, "B");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "seed_demo");
    cell_res = set_cell(sheet_obj, 1, 2, "C");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "seed_demo");
    cell_res = set_cell(sheet_obj, 2, 0, "100");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "seed_demo");
    cell_res = set_cell(sheet_obj, 2, 1, "200");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "seed_demo");
    cell_res = set_cell(sheet_obj, 2, 2, "300");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "seed_demo");
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

    struct yetty_yclass_object *sheet_obj = NULL;
    if (file_path) {
        struct yetty_yclass_object_ptr_result load_res =
            yetty_yrich_spreadsheet_load_yaml_file(file_path);
        if (YETTY_IS_ERR(load_res)) {
            yetty_ycore_error_print(stderr, "ysheet: load failed", load_res.error);
            yetty_ycore_error_destroy(load_res.error);
            return 1;
        }
        sheet_obj = load_res.value;
    } else {
        struct yetty_yclass_object_ptr_result create_res = yetty_yrich_spreadsheet_create(NULL);
        if (YETTY_IS_ERR(create_res)) {
            yetty_ycore_error_print(stderr, "ysheet: create failed", create_res.error);
            yetty_ycore_error_destroy(create_res.error);
            return 1;
        }
        sheet_obj = create_res.value;
        struct yetty_ycore_void_result seed_res = seed_demo(sheet_obj);
        if (YETTY_IS_ERR(seed_res)) {
            yetty_ycore_error_print(stderr, "ysheet: seed demo", seed_res.error);
            yetty_ycore_error_destroy(seed_res.error);
            struct yetty_ycore_void_result destroy_res =
                yetty_yrich_document_destroy(sheet_obj);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            return 1;
        }
    }

    struct yetty_ycore_int_result run_result =
        yetty_yrich_app_run(argc, argv, sheet_obj, YETTY_YRICH_APP_YSHEET);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "ysheet: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
