/*
 * svg-diag — file-driven ysvg diagnostic harness.
 *
 * Not a golden test: it takes SVG file paths on argv, runs each through
 * yetty_ysvg_render, and prints OK (drawable count + scene size) or the FULL
 * error cause chain. Used to triage which real-world SVGs the parser rejects
 * and why, so failing files can be captured as regression assets.
 *
 *   ./ysvg_diag-test file1.svg file2.svg ...
 *
 * With no args it renders a trivial inline SVG and returns 0, so it stays a
 * harmless no-op under ctest.
 */

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysvg/ysvg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    size_t got = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[got] = '\0';
    *out_len = got;
    return buffer;
}

static int diagnose(const char *path)
{
    size_t len = 0;
    char *content = read_file(path, &len);
    if (!content) {
        fprintf(stderr, "READERR  %s (cannot open)\n", path);
        return 2;
    }

    struct yetty_ysvg_render_config config = {
        .cell_width = 8,
        .cell_height = 16,
        .width_cells = 80,
        .height_cells = 24,
    };
    struct yetty_ysvg_render_result result = yetty_ysvg_render(content, len, NULL, 0, &config);
    free(content);

    if (YETTY_IS_ERR(result)) {
        char headline[512];
        snprintf(headline, sizeof(headline), "FAIL     %s", path);
        yetty_ycore_error_print(stdout, headline, result.error);
        yetty_ycore_error_destroy(result.error);
        return 1;
    }

    size_t drawables = yetty_ydraw_drawable_list_size(result.value.buffer);
    printf("OK       %s  drawables=%zu scene=%.0fx%.0f\n", path, drawables,
           result.value.scene_width, result.value.scene_height);
    yetty_ydraw_drawable_list_destroy(result.value.buffer);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        const char *svg = "<svg viewBox=\"0 0 10 10\">"
                          "<rect x=\"1\" y=\"1\" width=\"8\" height=\"8\" fill=\"#f00\"/></svg>";
        struct yetty_ysvg_render_config config = {0};
        struct yetty_ysvg_render_result result =
            yetty_ysvg_render(svg, strlen(svg), NULL, 0, &config);
        if (YETTY_IS_ERR(result)) {
            yetty_ycore_error_destroy(result.error);
            return 1;
        }
        yetty_ydraw_drawable_list_destroy(result.value.buffer);
        return 0;
    }

    int failures = 0;
    for (int i = 1; i < argc; i++) {
        if (diagnose(argv[i]) == 1) {
            failures++;
        }
    }
    printf("\n%d/%d file(s) failed to parse\n", failures, argc - 1);
    return 0;
}
