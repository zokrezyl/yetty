/*
 * ydrawlist2/ysdf2 byte-identity contract test — headless, deterministic.
 *
 * The migration pin of the v2 client interface: a record packed through the
 * yclass classes (yetty_ydrawlist2_pack dispatching the concrete override)
 * must be BYTE-IDENTICAL to the same record packed through the plain-C
 * producer surface (the ysdf funcs.gen.c builders, add_font_named,
 * add_text). Both derive from the same schema; any divergence is a
 * generator bug. Also pins:
 *   - the HAS_ID flag path (id != 0 inserts the id word and sets the flag)
 *   - the explicit-font-id record (add_font_with_id vs the record parser)
 *   - abstract pack on the bare drawable base fails
 *   - drawable_list add()/destroy() drive the same pack path
 */

#include <yetty/api/ydrawlist2/drawable.h> /* drawable + font + text classes */
#include <yetty/api/ydrawlist2/list.h>
#include <yetty/api/ydrawlist2/shape.h>
#include <yetty/api/ysdf2/shapes.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ydraw-list/font-resource.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

#include <stdint.h>
#include <string.h>

static struct yetty_ydraw_drawable_list *fresh_list(struct ytest *test)
{
    struct yetty_ydraw_drawable_list_result res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YTEST_REQUIRE_OK(test, res);
    return res.value;
}

static void require_identical(struct ytest *test, struct yetty_ydraw_drawable_list *via_class,
                              struct yetty_ydraw_drawable_list *via_builder)
{
    const uint8_t *class_raw = NULL;
    const uint8_t *builder_raw = NULL;
    size_t class_size = yetty_ydraw_drawable_list_serialize(via_class, &class_raw);
    size_t builder_size = yetty_ydraw_drawable_list_serialize(via_builder, &builder_raw);
    YTEST_REQUIRE(test, class_size > 0);
    YTEST_REQUIRE(test, class_size == builder_size);
    YTEST_REQUIRE(test, memcmp(class_raw, builder_raw, class_size) == 0);
}

/* circle — floats only, anonymous (id 0: no HAS_ID flag, no id word). */
static void test_circle_identity(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *via_class = fresh_list(test);
    struct yetty_ydraw_drawable_list *via_builder = fresh_list(test);

    struct yetty_yclass_object_ptr_result circle_res = yetty_ysdf2_circle_create(NULL);
    YTEST_REQUIRE_OK(test, circle_res);
    struct yetty_yclass_object *circle = circle_res.value;
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_shape_z_set(circle, 3u));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_shape_fill_set(circle, 0xFF6BA892u));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_shape_stroke_set(circle, 0xFF364A47u));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_shape_stroke_width_set(circle, 2.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_circle_center_x_set(circle, 96.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_circle_center_y_set(circle, 97.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_circle_radius_set(circle, 64.0f));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_pack(circle, via_class));
    YTEST_REQUIRE_OK(test, yetty_yclass_object_free(circle));

    struct yetty_ysdf_circle geom = {.center_x = 96.0f, .center_y = 97.0f, .radius = 64.0f};
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_add_circle(
                               via_builder, 0u, 3u, 0xFF6BA892u, 0xFF364A47u, 2.0f, &geom));

    require_identical(test, via_class, via_builder);
    yetty_ydraw_drawable_list_destroy(via_class);
    yetty_ydraw_drawable_list_destroy(via_builder);
}

/* rounded box with a nonzero id — the HAS_ID flag + id word path. */
static void test_box_with_id_identity(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *via_class = fresh_list(test);
    struct yetty_ydraw_drawable_list *via_builder = fresh_list(test);

    struct yetty_yclass_object_ptr_result box_res = yetty_ysdf2_box_create(NULL);
    YTEST_REQUIRE_OK(test, box_res);
    struct yetty_yclass_object *box = box_res.value;
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_shape_id_set(box, 42u));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_shape_fill_set(box, 0xFF1E262Cu));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_box_center_x_set(box, 280.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_box_center_y_set(box, 96.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_box_half_width_set(box, 72.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_box_half_height_set(box, 48.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_box_corner_radius_set(box, 8.0f));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_pack(box, via_class));
    YTEST_REQUIRE_OK(test, yetty_yclass_object_free(box));

    struct yetty_ysdf_box geom = {.center_x = 280.0f,
                                  .center_y = 96.0f,
                                  .half_width = 72.0f,
                                  .half_height = 48.0f,
                                  .corner_radius = 8.0f};
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_add_box(via_builder, 42u, 0u,
                                                                     0xFF1E262Cu, 0u, 0.0f, &geom));

    require_identical(test, via_class, via_builder);
    yetty_ydraw_drawable_list_destroy(via_class);
    yetty_ydraw_drawable_list_destroy(via_builder);
}

/* star — mixed float fields including the num_points/inner_ratio pair. */
static void test_star_identity(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *via_class = fresh_list(test);
    struct yetty_ydraw_drawable_list *via_builder = fresh_list(test);

    struct yetty_yclass_object_ptr_result star_res = yetty_ysdf2_star_create(NULL);
    YTEST_REQUIRE_OK(test, star_res);
    struct yetty_yclass_object *star = star_res.value;
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_shape_fill_set(star, 0xFF74C5A5u));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_star_center_x_set(star, 460.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_star_center_y_set(star, 96.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_star_radius_set(star, 56.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_star_num_points_set(star, 5.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_star_inner_ratio_set(star, 0.45f));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_pack(star, via_class));
    YTEST_REQUIRE_OK(test, yetty_yclass_object_free(star));

    struct yetty_ysdf_star geom = {.center_x = 460.0f,
                                   .center_y = 96.0f,
                                   .radius = 56.0f,
                                   .num_points = 5.0f,
                                   .inner_ratio = 0.45f};
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_cmd_add_star(via_builder, 0u, 0u,
                                                                      0xFF74C5A5u, 0u, 0.0f, &geom));

    require_identical(test, via_class, via_builder);
    yetty_ydraw_drawable_list_destroy(via_class);
    yetty_ydraw_drawable_list_destroy(via_builder);
}

/* font — v2 with the auto id the old builder would have assigned (0) must be
 * byte-identical to add_font_named; a nonzero explicit id round-trips
 * through the record parser. */
static void test_font_identity_and_explicit_id(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *via_class = fresh_list(test);
    struct yetty_ydraw_drawable_list *via_builder = fresh_list(test);

    struct yetty_yclass_object_ptr_result font_res = yetty_ydrawlist2_font_create(NULL);
    YTEST_REQUIRE_OK(test, font_res);
    struct yetty_yclass_object *font = font_res.value;
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_set_name(font, "Emmentaler"));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_pack(font, via_class)); /* font_id defaults to 0 */

    struct yetty_ycore_int_result named_res =
        yetty_ydraw_drawable_list_add_font_named(via_builder, "Emmentaler");
    YTEST_REQUIRE_OK(test, named_res);
    YTEST_REQUIRE(test, named_res.value == 0);

    require_identical(test, via_class, via_builder);

    /* Explicit user id: pack with font_id 7, parse the second record back.
     * The serialized stream is [header][records…] and the header size is
     * constant, so the second record begins where the first serialization
     * ended. */
    const uint8_t *raw = NULL;
    size_t size_before = yetty_ydraw_drawable_list_serialize(via_class, &raw);
    YTEST_REQUIRE(test, size_before > 0);
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_font_font_id_set(font, 7));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_pack(font, via_class));
    size_t raw_size = yetty_ydraw_drawable_list_serialize(via_class, &raw);
    YTEST_REQUIRE(test, raw_size > size_before);
    struct yetty_ydraw_font_resource_view view;
    YTEST_REQUIRE(test,
                  yetty_ydraw_font_resource_parse((const uint32_t *)(raw + size_before), &view) == 0);
    YTEST_REQUIRE(test, view.font_id == 7);
    YTEST_REQUIRE(test, view.ttf_len == 0);
    YTEST_REQUIRE(test, view.name_len == strlen("Emmentaler"));

    YTEST_REQUIRE_OK(test, yetty_yclass_object_free(font));
    yetty_ydraw_drawable_list_destroy(via_class);
    yetty_ydraw_drawable_list_destroy(via_builder);
}

/* text — same record through text_pack and add_text. */
static void test_text_identity(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *via_class = fresh_list(test);
    struct yetty_ydraw_drawable_list *via_builder = fresh_list(test);

    struct yetty_yclass_object_ptr_result text_res = yetty_ydrawlist2_text_create(NULL);
    YTEST_REQUIRE_OK(test, text_res);
    struct yetty_yclass_object *text = text_res.value;
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_set_body(text, "hello ydraw"));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_text_x_set(text, 40.0f));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_text_y_set(text, 240.0f));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_text_font_size_set(text, 24.0f));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_text_color_set(text, 0xFFE0E5E4u));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_text_font_id_set(text, -1));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_pack(text, via_class));
    YTEST_REQUIRE_OK(test, yetty_yclass_object_free(text));

    const char *body = "hello ydraw";
    struct yetty_ycore_buffer body_buf = {.data = (uint8_t *)(uintptr_t)body,
                                          .size = strlen(body),
                                          .capacity = strlen(body)};
    YTEST_REQUIRE_OK(test, yetty_ydraw_drawable_list_add_text(via_builder, 40.0f, 240.0f, &body_buf,
                                                              24.0f, 0xFFE0E5E4u, 0u, -1, 0.0f));

    require_identical(test, via_class, via_builder);
    yetty_ydraw_drawable_list_destroy(via_class);
    yetty_ydraw_drawable_list_destroy(via_builder);
}

/* the abstract base refuses to pack; drawable_list add() dispatches packs. */
static void test_dispatch_paths(struct ytest *test)
{
    struct yetty_ydraw_drawable_list *raw = fresh_list(test);
    struct yetty_yclass_object_ptr_result base_res = yetty_ydrawlist2_drawable_create(NULL);
    YTEST_REQUIRE_OK(test, base_res);
    YTEST_CHECK_ERR(test, yetty_ydrawlist2_pack(base_res.value, raw));
    YTEST_REQUIRE_OK(test, yetty_yclass_object_free(base_res.value));
    yetty_ydraw_drawable_list_destroy(raw);

    struct yetty_yclass_object_ptr_result list_res = yetty_ydrawlist2_drawable_list_create(NULL);
    YTEST_REQUIRE_OK(test, list_res);
    struct yetty_yclass_object *list = list_res.value;
    struct yetty_yclass_object_ptr_result seg_res = yetty_ysdf2_segment_create(NULL);
    YTEST_REQUIRE_OK(test, seg_res);
    struct yetty_yclass_object *segment = seg_res.value;
    YTEST_REQUIRE_OK(test, yetty_ysdf2_segment_start_x_set(segment, 40.0f));
    YTEST_REQUIRE_OK(test, yetty_ysdf2_segment_end_x_set(segment, 600.0f));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_add(list, segment));
    YTEST_REQUIRE_OK(test, yetty_yclass_object_free(segment));
    YTEST_REQUIRE_OK(test, yetty_ydrawlist2_destroy(list));
}

int main(void)
{
    struct ytest test = ytest_begin("ydrawlist2_byte_identity");
    YTEST_RUN(&test, test_circle_identity);
    YTEST_RUN(&test, test_box_with_id_identity);
    YTEST_RUN(&test, test_star_identity);
    YTEST_RUN(&test, test_font_identity_and_explicit_id);
    YTEST_RUN(&test, test_text_identity);
    YTEST_RUN(&test, test_dispatch_paths);
    return ytest_end(&test);
}
