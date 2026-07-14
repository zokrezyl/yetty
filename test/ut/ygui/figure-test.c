/*
 * ygui-figure-test.c — figure-emit sanity test.
 *
 * Wires the ygui framework to a real yfigure_container (the supported
 * in-process path: framework_set_container_obj, no PTY) and asserts the
 * figure-tree state the two-pass emit produces, by inspecting the container's
 * children directly. The container's registry maps the chrome ("ygrid") and
 * figure-widget ("yimage") kinds to a stub figure that counts the bytes it
 * receives — keeping the test free of any GPU / font dependency while still
 * exercising the real typed-slot dispatch (create_child / set_child_rect /
 * apply_child_body / delete_child) the framework now drives.
 *
 * Verifies:
 *   - Pass 1 mints the chrome ygrid child and the yimage child.
 *   - Pass 2 applies the yimage's rendered drawable_list to its child as a
 *     body (the wire body is the rendered drawable_list, not the encoded
 *     image bytes), so the child sees non-zero bytes.
 *   - A second emit does not re-mint the chrome ygrid.
 *   - Destroying a widget removes its child from the container on next emit.
 *   - Malformed image bytes surface as a Result error from emit rather than
 *     being silently dropped.
 *
 * Assertions use the shared ytest.h harness so the checks stay live under
 * Release/NDEBUG.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>

/* Direct access to engine internals for assertions. */
#include "yetty/ygui/internal.h"

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*===========================================================================
 * Stub figure: counts the bytes its process_bytes receives. Registered under
 * both the "ygrid" and "yimage" kind tokens so the container can mint a
 * placeholder for the chrome and for the yimage widget without pulling in the
 * real (GPU-backed) figure factories.
 *===========================================================================*/

struct stub_figure {
    struct yetty_yfigure_figure *base;
    size_t bytes_seen;
    uint32_t call_count;
};

static struct yetty_yclass_ptr_result stub_figure_class_get(void);

static struct stub_figure *stub_figure_from_obj(struct yetty_yclass_object *obj)
{
    return (struct stub_figure *)yetty_yclass_object_data(obj, stub_figure_class_get().value).value;
}

static struct yetty_ycore_void_result stub_figure_destroy(struct yetty_yclass_object *obj)
{
    return yetty_yclass_object_free(obj);
}

static struct yetty_ycore_void_result stub_figure_render(struct yetty_yclass_object *obj,
                                                         struct yetty_ydraw_target *target)
{
    (void)obj;
    (void)target;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stub_figure_process_bytes(struct yetty_yclass_object *obj,
                                                                const uint8_t *bytes,
                                                                size_t bytes_len)
{
    (void)bytes;
    struct stub_figure *figure = stub_figure_from_obj(obj);
    figure->bytes_seen += bytes_len;
    figure->call_count++;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stub_figure_reset_content(struct yetty_yclass_object *obj)
{
    struct stub_figure *figure = stub_figure_from_obj(obj);
    figure->bytes_seen = 0;
    figure->call_count = 0;
    return YETTY_OK_VOID();
}

static struct yetty_yclass_ptr_result stub_figure_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    static const struct yetty_yclass_descriptor desc = {
        .name = "stub_figure",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct stub_figure),
        .data_align = _Alignof(struct stub_figure),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)stub_figure_render},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)stub_figure_destroy},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)stub_figure_process_bytes},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content,
         (yetty_yclass_impl_t)stub_figure_reset_content},
    };
    struct yetty_yclass_ptr_result parent_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_r)) {
        return parent_r;
    }
    struct yetty_yclass_ptr_result r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), parent_r.value, NULL, 0);
    if (YETTY_IS_OK(r)) {
        cls = r.value;
    }
    return r;
}

static struct yetty_yfigure_figure_ptr_result stub_figure_factory(struct yetty_ycore_rectangle rect,
                                                                  const struct yetty_context *ctx,
                                                                  void *user)
{
    (void)ctx;
    (void)user;
    struct yetty_yclass_ptr_result cls_r = stub_figure_class_get();
    if (YETTY_IS_ERR(cls_r)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "stub_figure_factory: class", cls_r);
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_yclass_object_alloc(cls_r.value);
    if (YETTY_IS_ERR(obj_r)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "stub_figure_factory: alloc", obj_r);
    }
    struct stub_figure *figure = stub_figure_from_obj(obj_r.value);
    figure->base = (struct yetty_yfigure_figure *)(obj_r.value + 1);
    yetty_yfigure_figure_rect_set(obj_r.value, rect);
    yetty_yfigure_figure_dirty_set(obj_r.value, 1);
    return YETTY_OK(yetty_yfigure_figure_ptr, figure->base);
}

/* Build a registry that mints a stub figure for the chrome ygrid and for
 * the yimage widget kind. Caller frees. */
static struct yetty_yfigure_registry *make_registry(struct ytest *test)
{
    struct yetty_yfigure_registry_ptr_result r = yetty_yfigure_registry_create();
    YTEST_REQUIRE_OK(test, r);
    const char *const kinds[] = {"ygrid", "yimage"};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        struct yetty_ycore_void_result rr = yetty_yfigure_registry_register(
            r.value, yetty_yfigure_kind_token(kinds[i]), stub_figure_factory, NULL);
        YTEST_REQUIRE_OK(test, rr);
    }
    return r.value;
}

/* Look up a minted stub figure by id and return its bytes_seen, or SIZE_MAX
 * if no child is bound to that id. */
static size_t child_bytes_seen(struct ytest *test, struct yetty_yclass_object *container,
                               uint32_t id)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(container, id);
    YTEST_REQUIRE_OK(test, child_res);
    if (!child_res.value) {
        return (size_t)-1;
    }
    struct yetty_yclass_object *child_obj = (struct yetty_yclass_object *)(child_res.value) - 1;
    struct stub_figure *figure = stub_figure_from_obj(child_obj);
    return figure->bytes_seen;
}

static int child_present(struct ytest *test, struct yetty_yclass_object *container, uint32_t id)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(container, id);
    YTEST_REQUIRE_OK(test, child_res);
    return child_res.value != NULL;
}

static struct yetty_ycore_void_result count_click(struct yetty_yclass_object *obj, void *userdata)
{
    (void)obj;
    (*(int *)userdata)++;
    return YETTY_OK_VOID();
}

static void make_headless_framework(struct ytest *test,
                                    struct yetty_yfigure_registry **out_registry,
                                    struct yetty_yclass_object **out_container,
                                    struct yetty_yclass_object **out_engine,
                                    struct yetty_yclass_object **out_root)
{
    struct yetty_yfigure_registry *registry = make_registry(test);
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result cont_res = yetty_yfigure_container_create(&yclass_ctx);
    YTEST_REQUIRE_OK(test, cont_res);
    struct yetty_yclass_object *container = cont_res.value;
    struct yetty_ycore_rectangle container_rect = {{0, 0}, {800, 600}};
    yetty_yfigure_container_set_registry(container, registry);
    yetty_yfigure_container_set_rect(container, container_rect);

    struct yetty_yclass_object_ptr_result er = yetty_ygui_framework_create(NULL);
    YTEST_REQUIRE_OK(test, er);
    struct yetty_yclass_object *engine = er.value;
    struct yetty_ycore_void_result set_container_res =
        yetty_ygui_framework_set_container_obj(engine, container);
    YTEST_REQUIRE_OK(test, set_container_res);

    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_panel_class_get().value);
    YTEST_REQUIRE_OK(test, rr);
    struct yetty_yclass_object *root = rr.value;
    struct yetty_ycore_void_result set_root_res = yetty_ygui_framework_set_root(engine, root);
    YTEST_REQUIRE_OK(test, set_root_res);

    *out_registry = registry;
    *out_container = container;
    *out_engine = engine;
    *out_root = root;
}

/* Minimal valid 2x2 24-bit BMP. stb_image (used by yetty_yimage_render)
 * decodes this into RGBA8, so the emit path produces a real drawable_list —
 * handcrafted inline so the headless test needs no on-disk asset. */
static const uint8_t k_bmp_2x2[] = {
    /* BITMAPFILEHEADER (14) */
    0x42, 0x4D,             /* "BM"                       */
    0x46, 0x00, 0x00, 0x00, /* file size = 70             */
    0x00, 0x00, 0x00, 0x00, /* reserved                   */
    0x36, 0x00, 0x00, 0x00, /* pixel data offset = 54     */
    /* BITMAPINFOHEADER (40) */
    0x28, 0x00, 0x00, 0x00, /* header size = 40           */
    0x02, 0x00, 0x00, 0x00, /* width = 2                  */
    0x02, 0x00, 0x00, 0x00, /* height = 2                 */
    0x01, 0x00,             /* planes = 1                 */
    0x18, 0x00,             /* bpp = 24                   */
    0x00, 0x00, 0x00, 0x00, /* compression = BI_RGB       */
    0x10, 0x00, 0x00, 0x00, /* image size = 16            */
    0x13, 0x0B, 0x00, 0x00, /* x ppm                      */
    0x13, 0x0B, 0x00, 0x00, /* y ppm                      */
    0x00, 0x00, 0x00, 0x00, /* colors used                */
    0x00, 0x00, 0x00, 0x00, /* colors important           */
    /* pixel data: bottom-up, BGR, rows padded to 4 bytes */
    0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, /* row0: blue, green + pad */
    0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, /* row1: red,  white + pad */
};

/* Create a headless container + framework wired together, whose root panel
 * holds one 100x100 yimage. The container and registry are returned through
 * out-params; the caller owns and destroys all three. */
static struct yetty_yclass_object *make_engine_with_yimage(
    struct ytest *test, struct yetty_yfigure_registry **out_registry,
    struct yetty_yclass_object **out_container, struct yetty_yclass_object **out_img)
{
    struct yetty_yfigure_registry *registry = make_registry(test);
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result cont_res = yetty_yfigure_container_create(&yclass_ctx);
    YTEST_REQUIRE_OK(test, cont_res);
    struct yetty_yclass_object *container = cont_res.value;
    struct yetty_ycore_rectangle container_rect = {{0, 0}, {800, 600}};
    yetty_yfigure_container_set_registry(container, registry);
    yetty_yfigure_container_set_rect(container, container_rect);

    struct yetty_yclass_object_ptr_result er = yetty_ygui_framework_create(NULL);
    YTEST_REQUIRE_OK(test, er);
    struct yetty_yclass_object *engine = er.value;
    struct yetty_ycore_void_result set_container_res =
        yetty_ygui_framework_set_container_obj(engine, container);
    YTEST_REQUIRE_OK(test, set_container_res);

    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_panel_class_get().value);
    YTEST_REQUIRE_OK(test, rr);
    struct yetty_yclass_object *root = rr.value;
    struct yetty_ycore_void_result set_root_res = yetty_ygui_framework_set_root(engine, root);
    YTEST_REQUIRE_OK(test, set_root_res);

    struct yetty_yclass_object_ptr_result ir =
        yetty_ygui_widget_add(root, yetty_ygui_yimage_class_get().value);
    YTEST_REQUIRE_OK(test, ir);
    struct yetty_yclass_object *img = ir.value;

    /* Give yimage an explicit width/height so the layout pass produces
     * a non-empty rect for emit_container to ship. */
    struct yetty_ygui_layout_const_ptr_result img_layout_res = yetty_ygui_widget_layout_get(img);
    YTEST_REQUIRE_OK(test, img_layout_res);
    struct yetty_ygui_layout l = *img_layout_res.value;
    l.width = 100.0f;
    l.height = 100.0f;
    yetty_ygui_widget_layout_set(img, &l);

    *out_registry = registry;
    *out_container = container;
    *out_img = img;
    return engine;
}

static void test_layout_set_marks_framework_dirty(struct ytest *test)
{
    struct yetty_yfigure_registry *registry = NULL;
    struct yetty_yclass_object *container = NULL;
    struct yetty_yclass_object *engine = NULL;
    struct yetty_yclass_object *root = NULL;
    make_headless_framework(test, &registry, &container, &engine, &root);

    struct yetty_ycore_void_result emit_res = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, emit_res);
    YTEST_CHECK_EQ_INT(test, yetty_ygui_framework_is_dirty(engine), 0);

    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(root);
    YTEST_REQUIRE_OK(test, layout_res);
    struct yetty_ygui_layout layout = *layout_res.value;
    layout.gap = 7.0f;
    struct yetty_ycore_void_result set_res = yetty_ygui_widget_layout_set(root, &layout);
    YTEST_REQUIRE_OK(test, set_res);

    YTEST_CHECK_EQ_INT(test, yetty_ygui_framework_is_dirty(engine), 1);

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

/* A scrollarea child positioned outside the scrollarea's viewport rect is not
 * clickable; the same child moved inside the viewport is. This locks the input
 * side of scrollarea clipping, which hit_test already enforces structurally:
 * the recursive rect check prunes the scrollarea subtree whenever the pointer
 * is outside the scrollarea's own rect, so an off-viewport child is never
 * reached (see hit_test in framework.c). It is a regression guard for that
 * containment, not for any separate scissor-propagation code. */
static void test_scrollarea_children_clipped_by_viewport(struct ytest *test)
{
    struct yetty_yfigure_registry *registry = NULL;
    struct yetty_yclass_object *container = NULL;
    struct yetty_yclass_object *engine = NULL;
    struct yetty_yclass_object *root = NULL;
    make_headless_framework(test, &registry, &container, &engine, &root);

    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(root, yetty_ygui_scrollarea_class_get().value);
    YTEST_REQUIRE_OK(test, sr);
    struct yetty_yclass_object *scroll = sr.value;
    struct yetty_ygui_layout_const_ptr_result scroll_layout_res =
        yetty_ygui_widget_layout_get(scroll);
    YTEST_REQUIRE_OK(test, scroll_layout_res);
    struct yetty_ygui_layout scroll_layout = *scroll_layout_res.value;
    scroll_layout.width = 100.0f;
    scroll_layout.height = 100.0f;
    struct yetty_ycore_void_result scroll_set_res =
        yetty_ygui_widget_layout_set(scroll, &scroll_layout);
    YTEST_REQUIRE_OK(test, scroll_set_res);

    struct yetty_yclass_object_ptr_result br =
        yetty_ygui_widget_add(scroll, yetty_ygui_button_class_get().value);
    YTEST_REQUIRE_OK(test, br);
    struct yetty_yclass_object *button = br.value;
    int clicks = 0;
    struct yetty_ycore_void_result cb_res =
        yetty_ygui_clickable_on_click_set(button, count_click, &clicks);
    YTEST_REQUIRE_OK(test, cb_res);

    struct yetty_ygui_layout_const_ptr_result button_layout_res =
        yetty_ygui_widget_layout_get(button);
    YTEST_REQUIRE_OK(test, button_layout_res);
    struct yetty_ygui_layout button_layout = *button_layout_res.value;
    button_layout.absolute = 1;
    button_layout.pos_x = 0.0f;
    button_layout.pos_y = 150.0f;
    button_layout.width = 50.0f;
    button_layout.height = 20.0f;
    struct yetty_ycore_void_result button_set_res =
        yetty_ygui_widget_layout_set(button, &button_layout);
    YTEST_REQUIRE_OK(test, button_set_res);

    struct yetty_ycore_void_result emit1 = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, emit1);

    struct yetty_ycore_int_result press_out =
        yetty_ygui_framework_feed_mouse_button(engine, 10.0f, 160.0f, 0, 1, 0);
    YTEST_REQUIRE_OK(test, press_out);
    YTEST_CHECK_EQ_INT(test, press_out.value, 0);
    struct yetty_ycore_int_result release_out =
        yetty_ygui_framework_feed_mouse_button(engine, 10.0f, 160.0f, 0, 0, 0);
    YTEST_REQUIRE_OK(test, release_out);
    YTEST_CHECK_EQ_INT(test, release_out.value, 0);
    YTEST_CHECK_EQ_INT(test, clicks, 0);

    button_layout.pos_y = 10.0f;
    button_set_res = yetty_ygui_widget_layout_set(button, &button_layout);
    YTEST_REQUIRE_OK(test, button_set_res);
    struct yetty_ycore_void_result emit2 = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, emit2);

    struct yetty_ycore_int_result press_in =
        yetty_ygui_framework_feed_mouse_button(engine, 10.0f, 20.0f, 0, 1, 0);
    YTEST_REQUIRE_OK(test, press_in);
    YTEST_CHECK_EQ_INT(test, press_in.value, 1);
    struct yetty_ycore_int_result release_in =
        yetty_ygui_framework_feed_mouse_button(engine, 10.0f, 20.0f, 0, 0, 0);
    YTEST_REQUIRE_OK(test, release_in);
    YTEST_CHECK_EQ_INT(test, release_in.value, 1);
    YTEST_CHECK_EQ_INT(test, clicks, 1);

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

static void test_yimage_emit(struct ytest *test)
{
    struct yetty_yfigure_registry *registry = NULL;
    struct yetty_yclass_object *container = NULL;
    struct yetty_yclass_object *img = NULL;
    struct yetty_yclass_object *engine = make_engine_with_yimage(test, &registry, &container, &img);

    struct yetty_ycore_void_result br =
        yetty_ygui_yimage_set_bytes(img, k_bmp_2x2, sizeof(k_bmp_2x2));
    YTEST_REQUIRE_OK(test, br);

    /* Emit and inspect the container's resulting figure tree. */
    struct yetty_ycore_void_result rer = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, rer);

    uint32_t ygrid_id = yetty_ygui_framework_ygrid_id(engine);
    struct yetty_ycore_uint32_result img_id_res = yetty_ygui_widget_id(img);
    YTEST_REQUIRE_OK(test, img_id_res);
    uint32_t img_id = img_id_res.value;

    /* Pass 1 minted the chrome ygrid and the yimage child (the receiver IS
     * the root container — no synthetic engine container is created; the
     * panel has figure_kind=0 so its default emit_container is a no-op). */
    YTEST_CHECK(test, child_present(test, container, ygrid_id));
    YTEST_CHECK(test, child_present(test, container, img_id));

    /* Pass 2 applied the yimage's rendered drawable_list (CMD_ZERO + one
     * yimage prim) to its child as a body — the child saw non-zero bytes. */
    size_t img_bytes = child_bytes_seen(test, container, img_id);
    YTEST_CHECK(test, img_bytes != (size_t)-1);
    YTEST_CHECK(test, img_bytes > 0);

    /* Second emit: ygrid already exists — emit sends SET_CHILD_RECT, not a
     * fresh CREATE_CHILD, so the container keeps the same single ygrid child. */
    rer = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, rer);
    YTEST_CHECK(test, child_present(test, container, ygrid_id));
    YTEST_CHECK(test, child_present(test, container, img_id));

    /* Destroy yimage — the engine queues a delete that drops the child from
     * the container on the next emit. */
    yetty_ygui_widget_destroy(img);
    rer = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, rer);
    YTEST_CHECK(test, !child_present(test, container, img_id));
    YTEST_CHECK(test, child_present(test, container, ygrid_id));

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

/* Incremental figure-body emit: once a figure has been minted on the
 * receiver, an emit where nothing in its body subtree changed must NOT
 * re-apply the body — the child keeps its last bytes. A subsequent content
 * change must re-apply it. This is what stops an unchanged page (a scrollarea
 * figure) from being re-serialized on every emit. */
static void test_incremental_figure_skip(struct ytest *test)
{
    struct yetty_yfigure_registry *registry = make_registry(test);
    /* The scrollarea promotes itself to a YGRID figure — already in the
     * registry above. The label inside it paints into that figure's body. */
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result cont_res = yetty_yfigure_container_create(&yclass_ctx);
    YTEST_REQUIRE_OK(test, cont_res);
    struct yetty_yclass_object *container = cont_res.value;
    struct yetty_ycore_rectangle container_rect = {{0, 0}, {800, 600}};
    yetty_yfigure_container_set_registry(container, registry);
    yetty_yfigure_container_set_rect(container, container_rect);

    struct yetty_yclass_object_ptr_result er = yetty_ygui_framework_create(NULL);
    YTEST_REQUIRE_OK(test, er);
    struct yetty_yclass_object *engine = er.value;
    struct yetty_ycore_void_result set_container_res =
        yetty_ygui_framework_set_container_obj(engine, container);
    YTEST_REQUIRE_OK(test, set_container_res);

    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_panel_class_get().value);
    YTEST_REQUIRE_OK(test, rr);
    struct yetty_yclass_object *root = rr.value;
    struct yetty_ycore_void_result set_root_res = yetty_ygui_framework_set_root(engine, root);
    YTEST_REQUIRE_OK(test, set_root_res);

    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(root, yetty_ygui_scrollarea_class_get().value);
    YTEST_REQUIRE_OK(test, sr);
    struct yetty_yclass_object *scroll = sr.value;
    struct yetty_ygui_layout_const_ptr_result scroll_layout_res =
        yetty_ygui_widget_layout_get(scroll);
    YTEST_REQUIRE_OK(test, scroll_layout_res);
    struct yetty_ygui_layout sl = *scroll_layout_res.value;
    sl.width = 200.0f;
    sl.height = 200.0f;
    struct yetty_ycore_void_result scroll_layout_set_res =
        yetty_ygui_widget_layout_set(scroll, &sl);
    YTEST_REQUIRE_OK(test, scroll_layout_set_res);

    struct yetty_yclass_object_ptr_result lr =
        yetty_ygui_widget_add(scroll, yetty_ygui_label_class_get().value);
    YTEST_REQUIRE_OK(test, lr);
    struct yetty_yclass_object *label = lr.value;
    struct yetty_ycore_void_result label_text_res =
        yetty_ygui_label_set_text(label, "page content");
    YTEST_REQUIRE_OK(test, label_text_res);
    struct yetty_ygui_layout_const_ptr_result label_layout_res =
        yetty_ygui_widget_layout_get(label);
    YTEST_REQUIRE_OK(test, label_layout_res);
    struct yetty_ygui_layout ll = *label_layout_res.value;
    ll.width = 180.0f;
    ll.height = 22.0f;
    struct yetty_ycore_void_result label_layout_set_res = yetty_ygui_widget_layout_set(label, &ll);
    YTEST_REQUIRE_OK(test, label_layout_set_res);

    struct yetty_ycore_uint32_result scroll_id_res = yetty_ygui_widget_id(scroll);
    YTEST_REQUIRE_OK(test, scroll_id_res);
    uint32_t scroll_id = scroll_id_res.value;

    /* Emit #1: scrollarea figure minted → body applied. */
    struct yetty_ycore_void_result emit1 = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, emit1);
    size_t after1 = child_bytes_seen(test, container, scroll_id);
    YTEST_CHECK(test, after1 != (size_t)-1);
    YTEST_CHECK(test, after1 > 0);

    /* Emit #2: nothing changed and the figure is minted → body skipped, so
     * the child's accumulated bytes do not grow. */
    struct yetty_ycore_void_result emit2 = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, emit2);
    size_t after2 = child_bytes_seen(test, container, scroll_id);
    YTEST_CHECK_EQ_SIZE(test, after2, after1);

    /* A content change re-dirties the figure → it re-applies on next emit. */
    struct yetty_ycore_void_result change_res =
        yetty_ygui_label_set_text(label, "different content");
    YTEST_REQUIRE_OK(test, change_res);
    struct yetty_ycore_void_result emit3 = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, emit3);
    size_t after3 = child_bytes_seen(test, container, scroll_id);
    YTEST_CHECK(test, after3 > after2);

    /* And once clean again, it is skipped once more. */
    struct yetty_ycore_void_result emit4 = yetty_ygui_framework_emit(engine);
    YTEST_REQUIRE_OK(test, emit4);
    size_t after4 = child_bytes_seen(test, container, scroll_id);
    YTEST_CHECK_EQ_SIZE(test, after4, after3);

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

/* Malformed image bytes must surface as a Result error from emit — not
 * be silently dropped or truncated. Exercises the strict-rejection path
 * in yimage_emit_body / yetty_yimage_render (issue #243 findings 4/5). */
static void test_yimage_emit_rejects_malformed(struct ytest *test)
{
    struct yetty_yfigure_registry *registry = NULL;
    struct yetty_yclass_object *container = NULL;
    struct yetty_yclass_object *img = NULL;
    struct yetty_yclass_object *engine = make_engine_with_yimage(test, &registry, &container, &img);

    uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    struct yetty_ycore_void_result br = yetty_ygui_yimage_set_bytes(img, garbage, sizeof(garbage));
    YTEST_REQUIRE_OK(test, br);

    struct yetty_ycore_void_result rer = yetty_ygui_framework_emit(engine);
    YTEST_CHECK_ERR(test, rer);

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

int main(void)
{
    struct ytest test = ytest_begin("ygui_figure");
    YTEST_RUN(&test, test_layout_set_marks_framework_dirty);
    YTEST_RUN(&test, test_scrollarea_children_clipped_by_viewport);
    YTEST_RUN(&test, test_yimage_emit);
    YTEST_RUN(&test, test_incremental_figure_skip);
    YTEST_RUN(&test, test_yimage_emit_rejects_malformed);
    return ytest_end(&test);
}
