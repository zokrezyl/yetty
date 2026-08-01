/*
 * yscene-demo — minimal producer for the `yscene` retained-scene figure
 * kind (#691). Run INSIDE a yetty terminal: it ships a small scene —
 * a page background, one positioned group with content, one nested
 * group — as a `yscene` card via yview, then exits, leaving the card
 * on screen (yetty keeps it until the pane clears).
 *
 * This is the reference producer shape for the yscene wire adapter:
 * the drawable list carries plain records (runs) and CMD_GROUP bodies;
 * the receiving scene maps them onto its retained tree (groups become
 * nodes, runs become content batches, paint order by declaration seq).
 *
 *   usage: yscene-demo [x y w h]     (default 40 40 680 520)
 */
#include <stdio.h>
#include <stdlib.h>

#include <yetty/api/yview/view.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfigure/kind.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#ifndef _WIN32
#include <unistd.h>
#define YSCENE_DEMO_GETPID() ((uint32_t)getpid())
#else
#include <process.h>
#define YSCENE_DEMO_GETPID() ((uint32_t)_getpid())
#endif

/* Brand palette (docs/logo-2.jpeg roles), 0xAARRGGBB. */
#define DEMO_COLOR_BG 0xFF0B1014u
#define DEMO_COLOR_SURFACE 0xFF1E262Cu
#define DEMO_COLOR_ACCENT 0xFF6BA892u
#define DEMO_COLOR_ACCENT_BRIGHT 0xFF74C5A5u
#define DEMO_COLOR_TEXT 0xFFE0E5E4u

static int add_box(struct yetty_ydraw_drawable_list *list, float x, float y, float w, float h,
                   uint32_t fill, float corner_radius)
{
    struct yetty_ysdf_rounded_box geometry = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .radius_top_right = corner_radius,
        .radius_bottom_right = corner_radius,
        .radius_top_left = corner_radius,
        .radius_bottom_left = corner_radius,
    };
    struct yetty_ycore_void_result add_res = yetty_ydraw_drawable_list_add_cmd_add_rounded_box(
        list, /*id=*/0, /*z_order=*/0, fill, /*stroke=*/0, /*stroke_w=*/0.0f, &geometry);
    if (YETTY_IS_ERR(add_res)) {
        fprintf(stderr, "yscene-demo: add box failed: %s\n", add_res.error.msg);
        yetty_ycore_error_destroy(add_res.error);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    float origin_x = 40.0f;
    float origin_y = 40.0f;
    float width = 680.0f;
    float height = 520.0f;
    if (argc == 5) {
        origin_x = (float)atof(argv[1]);
        origin_y = (float)atof(argv[2]);
        width = (float)atof(argv[3]);
        height = (float)atof(argv[4]);
    }

    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(list_res)) {
        fprintf(stderr, "yscene-demo: list create failed\n");
        yetty_ycore_error_destroy(list_res.error);
        return 1;
    }
    struct yetty_ydraw_drawable_list *list = list_res.value;

    /* Root run: the page background (document coords, 0,0 at the card's
     * top-left). */
    if (add_box(list, 0, 0, width, height, DEMO_COLOR_BG, 0.0f) != 0) {
        return 1;
    }

    /* Group 1: a raised panel with content. Its records land on node 1;
     * the group declares AFTER the background run, so it paints above
     * it by seq — no z juggling needed. */
    struct yetty_ydraw_id_result panel_res = yetty_ydraw_drawable_list_begin_group(list, 1);
    if (YETTY_IS_ERR(panel_res)) {
        fprintf(stderr, "yscene-demo: begin_group failed\n");
        yetty_ycore_error_destroy(panel_res.error);
        return 1;
    }
    if (add_box(list, 40, 40, width - 80.0f, height - 80.0f, DEMO_COLOR_SURFACE, 12.0f) != 0) {
        return 1;
    }
    /* Nested group 2: an accent chip inside the panel — a child node,
     * always above the panel body. */
    {
        struct yetty_ydraw_id_result chip_res = yetty_ydraw_drawable_list_begin_group(list, 2);
        if (YETTY_IS_ERR(chip_res)) {
            yetty_ycore_error_destroy(chip_res.error);
            return 1;
        }
        if (add_box(list, 80, 80, 160, 48, DEMO_COLOR_ACCENT, 8.0f) != 0) {
            return 1;
        }
        if (add_box(list, 92, 92, 24, 24, DEMO_COLOR_ACCENT_BRIGHT, 12.0f) != 0) {
            return 1;
        }
        yetty_ydraw_drawable_list_end_group(list, chip_res.value);
    }
    /* A trailing run on the panel AFTER the nested group — exercises
     * the run/child/run interleave (batch B sorts above the chip). */
    if (add_box(list, 80, 160, width - 240.0f, 8, DEMO_COLOR_TEXT, 4.0f) != 0) {
        return 1;
    }
    yetty_ydraw_drawable_list_end_group(list, panel_res.value);

    struct yetty_yclass_object_ptr_result view_res = yetty_yview_view_create(NULL);
    if (YETTY_IS_ERR(view_res)) {
        fprintf(stderr, "yscene-demo: view create failed: %s\n", view_res.error.msg);
        yetty_ycore_error_destroy(view_res.error);
        return 1;
    }
    struct yetty_yclass_object *view = view_res.value;

    struct yetty_ycore_void_result configure_res = yetty_yview_configure(
        view, /*fd=*/1, YSCENE_DEMO_GETPID(), yetty_yfigure_kind_token("yscene"),
        /*bg_color=*/0, origin_x, origin_y, origin_x + width, origin_y + height);
    if (YETTY_IS_ERR(configure_res)) {
        fprintf(stderr, "yscene-demo: configure failed: %s\n", configure_res.error.msg);
        yetty_ycore_error_destroy(configure_res.error);
        return 1;
    }
    struct yetty_ycore_void_result content_res = yetty_yview_set_content(view, list);
    yetty_ydraw_drawable_list_destroy(list);
    if (YETTY_IS_ERR(content_res)) {
        fprintf(stderr, "yscene-demo: set_content failed: %s\n", content_res.error.msg);
        yetty_ycore_error_destroy(content_res.error);
        return 1;
    }
    fprintf(stderr, "yscene-demo: scene card shipped (%.0fx%.0f at %.0f,%.0f)\n", width, height,
            origin_x, origin_y);
    /* Deliberately no destroy: the card stays on screen after exit. */
    return 0;
}
