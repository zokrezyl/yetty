/*
 * yslide — presentation editor.
 *
 * Thin entry: builds (or loads) a yrich:slides deck object and hands it
 * to the shared yrich app host, which opens a window and runs the
 * ygui-decorated editor (navigation toolbar + scrolling slide canvas +
 * statusbar), rendered through the in-process yfigure container — same
 * path as the other ygui apps (no OSC).
 *
 * Press Esc or close the window to quit.
 *
 * Usage:
 *   yslide                              # built-in demo deck
 *   yslide path/to/sample.yslide.yaml
 */

#include <yetty/yrich/yrich-app.h>
#include <yetty/yrich/yrich-types.h>

#include <yetty/api/yrich/document.h>
#include <yetty/api/yrich/slides.h>
#include <yetty/yrich/yrich-yaml.h>

#include <stdio.h>
#include <string.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
            "Usage: %s [file.yslide.yaml]\n"
            "  With no file, opens a built-in demo deck.\n",
            prog);
}

static struct yetty_ycore_void_result add_textbox(struct yetty_yclass_object *deck_obj, float x,
                                                  float y, float w, float h, const char *text)
{
    struct yetty_yclass_object_ptr_result shape_res =
        yetty_yrich_slides_add_textbox(deck_obj, x, y, w, h, text, strlen(text));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "seed_demo: add_textbox failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result seed_demo(struct yetty_yclass_object *deck_obj)
{
    /* Slide 1 — title. */
    struct yetty_ycore_void_result text_res =
        add_textbox(deck_obj, 200, 150, 560, 80, "Welcome to yslide");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "seed_demo: slide 1");
    text_res = add_textbox(deck_obj, 200, 250, 560, 40, "Presentations on the yetty canvas.");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "seed_demo: slide 1");

    /* Slide 2 — shapes. */
    struct yetty_yrich_slide_ptr_result slide_res = yetty_yrich_slides_add_slide(deck_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slide_res, "seed_demo: slide 2 create");
    struct yetty_ycore_void_result current_res = yetty_yrich_slides_set_current(deck_obj, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, current_res, "seed_demo: slide 2 current");
    text_res = add_textbox(deck_obj, 200, 50, 560, 50, "Shape examples");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "seed_demo: slide 2");
    struct yetty_yclass_object_ptr_result shape_res =
        yetty_yrich_slides_add_rectangle(deck_obj, 100, 150, 200, 150);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "seed_demo: rectangle");
    shape_res = yetty_yrich_slides_add_ellipse(deck_obj, 400, 150, 200, 150);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "seed_demo: ellipse");
    shape_res = yetty_yrich_slides_add_line(deck_obj, 150, 400, 550, 400);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_res, "seed_demo: line");

    /* Slide 3 — features. */
    slide_res = yetty_yrich_slides_add_slide(deck_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slide_res, "seed_demo: slide 3 create");
    current_res = yetty_yrich_slides_set_current(deck_obj, 2);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, current_res, "seed_demo: slide 3 current");
    text_res = add_textbox(deck_obj, 200, 50, 560, 50, "Features");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "seed_demo: slide 3");
    const char *bullets[] = {
        "- Multiple shapes",
        "- Text editing",
        "- Keyboard navigation",
    };
    float y = 150.0f;
    for (size_t i = 0; i < sizeof(bullets) / sizeof(bullets[0]); i++) {
        text_res = add_textbox(deck_obj, 100, y, 400, 40, bullets[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "seed_demo: bullet");
        y += 50.0f;
    }

    return yetty_yrich_slides_set_current(deck_obj, 0);
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

    struct yetty_yclass_object *deck_obj = NULL;
    if (file_path) {
        struct yetty_yclass_object_ptr_result load_res =
            yetty_yrich_slides_load_yaml_file(file_path);
        if (YETTY_IS_ERR(load_res)) {
            yetty_ycore_error_print(stderr, "yslide: load failed", load_res.error);
            yetty_ycore_error_destroy(load_res.error);
            return 1;
        }
        deck_obj = load_res.value;
    } else {
        struct yetty_yclass_object_ptr_result create_res = yetty_yrich_slides_create(NULL);
        if (YETTY_IS_ERR(create_res)) {
            yetty_ycore_error_print(stderr, "yslide: create failed", create_res.error);
            yetty_ycore_error_destroy(create_res.error);
            return 1;
        }
        deck_obj = create_res.value;
        struct yetty_ycore_void_result seed_res = seed_demo(deck_obj);
        if (YETTY_IS_ERR(seed_res)) {
            yetty_ycore_error_print(stderr, "yslide: seed demo", seed_res.error);
            yetty_ycore_error_destroy(seed_res.error);
            struct yetty_ycore_void_result destroy_res = yetty_yrich_document_destroy(deck_obj);
            if (YETTY_IS_ERR(destroy_res)) {
                yetty_ycore_error_destroy(destroy_res.error);
            }
            return 1;
        }
    }

    struct yetty_ycore_int_result run_result =
        yetty_yrich_app_run(argc, argv, deck_obj, YETTY_YRICH_APP_YSLIDE);
    if (YETTY_IS_ERR(run_result)) {
        yetty_ycore_error_print(stderr, "yslide: run", run_result.error);
        yetty_ycore_error_destroy(run_result.error);
        return 1;
    }
    return run_result.value;
}
