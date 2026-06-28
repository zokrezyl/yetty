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
 * NDEBUG release builds elide assert(), so this test uses explicit if-error
 * checks instead.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>

/* Direct access to engine internals for assertions. */
#include "yetty/ygui/internal.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, label)                                                                         \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", label, __FILE__, __LINE__);                      \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

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
    CHECK(YETTY_IS_OK(parent_r), "stub_figure parent class");
    struct yetty_yclass_ptr_result r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), parent_r.value, NULL, 0);
    if (YETTY_IS_OK(r)) {
        cls = r.value;
    }
    return r;
}

static struct yetty_yfigure_figure_ptr_result stub_figure_factory(
    struct yetty_ycore_rectangle rect, const struct yetty_context *ctx, void *user)
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
static struct yetty_yfigure_registry *make_registry(void)
{
    struct yetty_yfigure_registry_ptr_result r = yetty_yfigure_registry_create();
    CHECK(YETTY_IS_OK(r), "registry_create");
    const char *const kinds[] = {"ygrid", "yimage"};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        struct yetty_ycore_void_result rr = yetty_yfigure_registry_register(
            r.value, yetty_yfigure_kind_token(kinds[i]), stub_figure_factory, NULL);
        CHECK(YETTY_IS_OK(rr), "registry_register");
    }
    return r.value;
}

/* Look up a minted stub figure by id and return its bytes_seen, or SIZE_MAX
 * if no child is bound to that id. */
static size_t child_bytes_seen(struct yetty_yclass_object *container, uint32_t id)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(container, id);
    CHECK(YETTY_IS_OK(child_res), "find_child_by_id");
    if (!child_res.value) {
        return (size_t)-1;
    }
    struct yetty_yclass_object *child_obj = (struct yetty_yclass_object *)(child_res.value) - 1;
    struct stub_figure *figure = stub_figure_from_obj(child_obj);
    return figure->bytes_seen;
}

static int child_present(struct yetty_yclass_object *container, uint32_t id)
{
    struct yetty_yfigure_figure_ptr_result child_res =
        yetty_yfigure_container_find_child_by_id(container, id);
    CHECK(YETTY_IS_OK(child_res), "find_child_by_id");
    return child_res.value != NULL;
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
static struct yetty_ygui_framework *make_engine_with_yimage(
    struct yetty_yfigure_registry **out_registry, struct yetty_yclass_object **out_container,
    struct yetty_yclass_object **out_img)
{
    struct yetty_yfigure_registry *registry = make_registry();
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result cont_res = yetty_yfigure_container_create(&yclass_ctx);
    CHECK(YETTY_IS_OK(cont_res), "container_create");
    struct yetty_yclass_object *container = cont_res.value;
    struct yetty_ycore_rectangle container_rect = {{0, 0}, {800, 600}};
    yetty_yfigure_container_set_registry(container, registry);
    yetty_yfigure_container_set_rect(container, container_rect);

    struct yetty_ygui_framework_ptr_result er = yetty_ygui_framework_create(NULL);
    CHECK(YETTY_IS_OK(er), "engine_create");
    struct yetty_ygui_framework *engine = er.value;
    CHECK(YETTY_IS_OK(yetty_ygui_framework_set_container_obj(engine, container)),
          "engine_set_container_obj");

    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_panel_class_get().value);
    CHECK(YETTY_IS_OK(rr), "add panel");
    struct yetty_yclass_object *root = rr.value;
    CHECK(YETTY_IS_OK(yetty_ygui_framework_set_root(engine, root)), "engine_set_root");

    struct yetty_yclass_object_ptr_result ir =
        yetty_ygui_widget_add(root, yetty_ygui_yimage_class_get().value);
    CHECK(YETTY_IS_OK(ir), "add yimage");
    struct yetty_yclass_object *img = ir.value;

    /* Give yimage an explicit width/height so the layout pass produces
     * a non-empty rect for emit_container to ship. */
    struct yetty_ygui_layout_const_ptr_result img_layout_res = yetty_ygui_widget_layout_get(img);
    CHECK(YETTY_IS_OK(img_layout_res), "img layout_get");
    struct yetty_ygui_layout l = *img_layout_res.value;
    l.width = 100.0f;
    l.height = 100.0f;
    yetty_ygui_widget_layout_set(img, &l);

    *out_registry = registry;
    *out_container = container;
    *out_img = img;
    return engine;
}

static void test_yimage_emit(void)
{
    struct yetty_yfigure_registry *registry = NULL;
    struct yetty_yclass_object *container = NULL;
    struct yetty_yclass_object *img = NULL;
    struct yetty_ygui_framework *engine =
        make_engine_with_yimage(&registry, &container, &img);

    struct yetty_ycore_void_result br =
        yetty_ygui_yimage_set_bytes(img, k_bmp_2x2, sizeof(k_bmp_2x2));
    CHECK(YETTY_IS_OK(br), "yimage_set_bytes");

    /* Emit and inspect the container's resulting figure tree. */
    struct yetty_ycore_void_result rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_OK(rer), "engine_emit #1");

    uint32_t ygrid_id = yetty_ygui_framework_ygrid_id(engine);
    struct yetty_ycore_uint32_result img_id_res = yetty_ygui_widget_id(img);
    CHECK(YETTY_IS_OK(img_id_res), "img widget_id");
    uint32_t img_id = img_id_res.value;

    /* Pass 1 minted the chrome ygrid and the yimage child (the receiver IS
     * the root container — no synthetic engine container is created; the
     * panel has figure_kind=0 so its default emit_container is a no-op). */
    CHECK(child_present(container, ygrid_id), "ygrid child minted");
    CHECK(child_present(container, img_id), "yimage child minted");

    /* Pass 2 applied the yimage's rendered drawable_list (CMD_ZERO + one
     * yimage prim) to its child as a body — the child saw non-zero bytes. */
    size_t img_bytes = child_bytes_seen(container, img_id);
    CHECK(img_bytes != (size_t)-1, "yimage child bound");
    CHECK(img_bytes > 0, "yimage body applied to child");

    /* Second emit: ygrid already exists — emit sends SET_CHILD_RECT, not a
     * fresh CREATE_CHILD, so the container keeps the same single ygrid child. */
    rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_OK(rer), "engine_emit #2");
    CHECK(child_present(container, ygrid_id), "ygrid child still present after emit #2");
    CHECK(child_present(container, img_id), "yimage child still present after emit #2");

    /* Destroy yimage — the engine queues a delete that drops the child from
     * the container on the next emit. */
    yetty_ygui_widget_destroy(img);
    rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_OK(rer), "engine_emit #3");
    CHECK(!child_present(container, img_id), "yimage child removed after destroy");
    CHECK(child_present(container, ygrid_id), "ygrid child survives yimage destroy");

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

/* Incremental figure-body emit: once a figure has been minted on the
 * receiver, an emit where nothing in its body subtree changed must NOT
 * re-apply the body — the child keeps its last bytes. A subsequent content
 * change must re-apply it. This is what stops an unchanged page (a scrollarea
 * figure) from being re-serialized on every emit. */
static void test_incremental_figure_skip(void)
{
    struct yetty_yfigure_registry *registry = make_registry();
    /* The scrollarea promotes itself to a YGRID figure — already in the
     * registry above. The label inside it paints into that figure's body. */
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result cont_res = yetty_yfigure_container_create(&yclass_ctx);
    CHECK(YETTY_IS_OK(cont_res), "skip: container_create");
    struct yetty_yclass_object *container = cont_res.value;
    struct yetty_ycore_rectangle container_rect = {{0, 0}, {800, 600}};
    yetty_yfigure_container_set_registry(container, registry);
    yetty_yfigure_container_set_rect(container, container_rect);

    struct yetty_ygui_framework_ptr_result er = yetty_ygui_framework_create(NULL);
    CHECK(YETTY_IS_OK(er), "skip: framework_create");
    struct yetty_ygui_framework *engine = er.value;
    CHECK(YETTY_IS_OK(yetty_ygui_framework_set_container_obj(engine, container)),
          "skip: set_container_obj");

    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_panel_class_get().value);
    CHECK(YETTY_IS_OK(rr), "skip: add panel");
    struct yetty_yclass_object *root = rr.value;
    CHECK(YETTY_IS_OK(yetty_ygui_framework_set_root(engine, root)), "skip: set_root");

    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(root, yetty_ygui_scrollarea_class_get().value);
    CHECK(YETTY_IS_OK(sr), "skip: add scrollarea");
    struct yetty_yclass_object *scroll = sr.value;
    struct yetty_ygui_layout_const_ptr_result scroll_layout_res =
        yetty_ygui_widget_layout_get(scroll);
    CHECK(YETTY_IS_OK(scroll_layout_res), "skip: scroll layout_get");
    struct yetty_ygui_layout sl = *scroll_layout_res.value;
    sl.width = 200.0f;
    sl.height = 200.0f;
    CHECK(YETTY_IS_OK(yetty_ygui_widget_layout_set(scroll, &sl)), "skip: scroll layout");

    struct yetty_yclass_object_ptr_result lr =
        yetty_ygui_widget_add(scroll, yetty_ygui_label_class_get().value);
    CHECK(YETTY_IS_OK(lr), "skip: add label");
    struct yetty_yclass_object *label = lr.value;
    CHECK(YETTY_IS_OK(yetty_ygui_label_set_text(label, "page content")), "skip: label text");
    struct yetty_ygui_layout_const_ptr_result label_layout_res =
        yetty_ygui_widget_layout_get(label);
    CHECK(YETTY_IS_OK(label_layout_res), "skip: label layout_get");
    struct yetty_ygui_layout ll = *label_layout_res.value;
    ll.width = 180.0f;
    ll.height = 22.0f;
    CHECK(YETTY_IS_OK(yetty_ygui_widget_layout_set(label, &ll)), "skip: label layout");

    struct yetty_ycore_uint32_result scroll_id_res = yetty_ygui_widget_id(scroll);
    CHECK(YETTY_IS_OK(scroll_id_res), "skip: scroll widget_id");
    uint32_t scroll_id = scroll_id_res.value;

    /* Emit #1: scrollarea figure minted → body applied. */
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #1");
    size_t after1 = child_bytes_seen(container, scroll_id);
    CHECK(after1 != (size_t)-1, "skip: scrollarea child minted");
    CHECK(after1 > 0, "skip: emit #1 applies scrollarea figure body");

    /* Emit #2: nothing changed and the figure is minted → body skipped, so
     * the child's accumulated bytes do not grow. */
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #2");
    size_t after2 = child_bytes_seen(container, scroll_id);
    CHECK(after2 == after1, "skip: emit #2 omits clean figure body");

    /* A content change re-dirties the figure → it re-applies on next emit. */
    CHECK(YETTY_IS_OK(yetty_ygui_label_set_text(label, "different content")), "skip: change text");
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #3");
    size_t after3 = child_bytes_seen(container, scroll_id);
    CHECK(after3 > after2, "skip: emit #3 re-applies dirtied figure body");

    /* And once clean again, it is skipped once more. */
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #4");
    size_t after4 = child_bytes_seen(container, scroll_id);
    CHECK(after4 == after3, "skip: emit #4 omits clean figure body");

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

/* Malformed image bytes must surface as a Result error from emit — not
 * be silently dropped or truncated. Exercises the strict-rejection path
 * in yimage_emit_body / yetty_yimage_render (issue #243 findings 4/5). */
static void test_yimage_emit_rejects_malformed(void)
{
    struct yetty_yfigure_registry *registry = NULL;
    struct yetty_yclass_object *container = NULL;
    struct yetty_yclass_object *img = NULL;
    struct yetty_ygui_framework *engine =
        make_engine_with_yimage(&registry, &container, &img);

    uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    struct yetty_ycore_void_result br = yetty_ygui_yimage_set_bytes(img, garbage, sizeof(garbage));
    CHECK(YETTY_IS_OK(br), "yimage_set_bytes (garbage)");

    struct yetty_ycore_void_result rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_ERR(rer), "engine_emit rejects malformed image payload");
    yetty_ycore_error_destroy(rer.error);

    yetty_ygui_framework_destroy(engine);
    yetty_yfigure_destroy(container);
    yetty_yfigure_registry_destroy(registry);
}

int main(void)
{
    test_yimage_emit();
    test_incremental_figure_skip();
    test_yimage_emit_rejects_malformed();
    puts("ygui-figure-test: OK");
    return 0;
}
