/*
 * yslide — presentation editor.
 *
 * Thin entry: builds (or loads) a yetty_yrich_slides deck and hands it to
 * the shared yrich app host, which opens a window and runs the
 * ygui-decorated editor (navigation toolbar + scrolling slide canvas +
 * statusbar), rendered through the in-process yfigure container — same
 * path as the other ygui apps (no OSC).
 *
 * Press 'q' / Esc / close the window to quit.
 *
 * Usage:
 *   yslide                              # built-in demo deck
 *   yslide path/to/sample.yslide.yaml
 */

#include <yetty/yrich/yrich-app.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-yaml.h>
#include <yetty/yrich/yslides.h>

#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [file.yslide.yaml]\n"
            "  With no file, opens a built-in demo deck.\n",
            prog);
}

static void seed_demo(struct yetty_yrich_slides *s)
{
    /* Slide 1 — title. */
    yetty_yrich_slides_add_textbox(s, 200, 150, 560, 80, "Welcome to yslide",
                                   strlen("Welcome to yslide"));
    yetty_yrich_slides_add_textbox(s, 200, 250, 560, 40, "Presentations on the yetty canvas.",
                                   strlen("Presentations on the yetty canvas."));

    /* Slide 2 — shapes. */
    yetty_yrich_slides_add_slide(s);
    yetty_yrich_slides_set_current(s, 1);
    yetty_yrich_slides_add_textbox(s, 200, 50, 560, 50, "Shape examples", strlen("Shape examples"));
    yetty_yrich_slides_add_rectangle(s, 100, 150, 200, 150);
    yetty_yrich_slides_add_ellipse(s, 400, 150, 200, 150);
    yetty_yrich_slides_add_line(s, 150, 400, 550, 400);

    /* Slide 3 — features. */
    yetty_yrich_slides_add_slide(s);
    yetty_yrich_slides_set_current(s, 2);
    yetty_yrich_slides_add_textbox(s, 200, 50, 560, 50, "Features", strlen("Features"));
    const char *bullets[] = {
        "- Multiple shapes",
        "- Text editing",
        "- Keyboard navigation",
    };
    float y = 150.0f;
    for (size_t i = 0; i < sizeof(bullets) / sizeof(bullets[0]); i++) {
        yetty_yrich_slides_add_textbox(s, 100, y, 400, 40, bullets[i], strlen(bullets[i]));
        y += 50.0f;
    }

    yetty_yrich_slides_set_current(s, 0);
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

    struct yetty_yrich_slides *deck = NULL;
    if (file_path) {
        struct yetty_yrich_slides_ptr_result lr = yetty_yrich_slides_load_yaml_file(file_path);
        if (YETTY_IS_ERR(lr)) {
            fprintf(stderr, "yslide: load %s: %s\n", file_path, lr.error.msg);
            yetty_ycore_error_destroy(lr.error);
            return 1;
        }
        deck = lr.value;
    } else {
        struct yetty_yrich_slides_ptr_result sr = yetty_yrich_slides_create();
        if (YETTY_IS_ERR(sr)) {
            fprintf(stderr, "yslide: %s\n", sr.error.msg);
            yetty_ycore_error_destroy(sr.error);
            return 1;
        }
        deck = sr.value;
        seed_demo(deck);
    }

    struct yetty_ycore_int_result run_result =
        yetty_yrich_app_run(argc, argv, &deck->base, YETTY_YRICH_APP_YSLIDE);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yslide: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
