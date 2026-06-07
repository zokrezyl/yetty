/*
 * ysheet — spreadsheet editor.
 *
 * Thin entry: builds (or loads) a yetty_yrich_spreadsheet and hands it to
 * the shared yrich app host, which opens a window and runs the
 * ygui-decorated editor (edit toolbar + scrolling grid + statusbar),
 * rendered through the in-process yfigure container — same path as the
 * other ygui apps (no OSC).
 *
 * Press 'q' / Esc / close the window to quit.
 *
 * Usage:
 *   ysheet                              # built-in demo grid
 *   ysheet path/to/sample.ysheet.yaml
 */

#include <yetty/yrich/yrich-app.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-yaml.h>
#include <yetty/yrich/yspreadsheet.h>

#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [file.ysheet.yaml]\n"
            "  With no file, opens a built-in demo grid.\n",
            prog);
}

static void set_cell(struct yetty_yrich_spreadsheet *s, int row, int col, const char *value)
{
    struct yetty_yrich_cell_addr addr = {row, col};
    struct yetty_ycore_void_result r =
        yetty_yrich_spreadsheet_set_cell_value(s, addr, value, strlen(value));
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static void seed_demo(struct yetty_yrich_spreadsheet *s)
{
    yetty_yrich_spreadsheet_set_grid_size(s, 50, 20);
    set_cell(s, 0, 0, "yrich spreadsheet");
    set_cell(s, 1, 0, "A");
    set_cell(s, 1, 1, "B");
    set_cell(s, 1, 2, "C");
    set_cell(s, 2, 0, "100");
    set_cell(s, 2, 1, "200");
    set_cell(s, 2, 2, "300");
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

    struct yetty_yrich_spreadsheet *sheet = NULL;
    if (file_path) {
        struct yetty_yrich_spreadsheet_ptr_result lr =
            yetty_yrich_spreadsheet_load_yaml_file(file_path);
        if (YETTY_IS_ERR(lr)) {
            fprintf(stderr, "ysheet: load %s: %s\n", file_path, lr.error.msg);
            yetty_ycore_error_destroy(lr.error);
            return 1;
        }
        sheet = lr.value;
    } else {
        struct yetty_yrich_spreadsheet_ptr_result sr = yetty_yrich_spreadsheet_create();
        if (YETTY_IS_ERR(sr)) {
            fprintf(stderr, "ysheet: %s\n", sr.error.msg);
            yetty_ycore_error_destroy(sr.error);
            return 1;
        }
        sheet = sr.value;
        seed_demo(sheet);
    }

    struct yetty_ycore_int_result run_result =
        yetty_yrich_app_run(argc, argv, &sheet->base, YETTY_YRICH_APP_YSHEET);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "ysheet: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
