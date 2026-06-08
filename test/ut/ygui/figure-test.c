/*
 * ygui-figure-test.c — Phase 9 sanity test.
 *
 * Drives the two-pass emit machinery against a stub pty (writes
 * discarded) and walks the engine's internal per-pass buffers to
 * verify:
 *   - Pass 1 emits CREATE_CHILD for the chrome (container + ygrid) and
 *     a CREATE_CHILD(kind=YIMAGE) for the yimage widget.
 *   - Pass 2 emits a record with id = yimage's obj id whose body is the
 *     rendered drawable_list for the image we handed to set_bytes (the wire
 *     body is the rendered drawable_list, not the encoded image bytes).
 *   - Second emit doesn't re-emit chrome.
 *   - Destroying a widget queues DELETE_CHILD for the next envelope.
 *   - Malformed image bytes surface as a Result error from emit rather
 *     than being silently dropped.
 *
 * The yface envelope encoding (b64 + LZ4F) is opaque here; the test
 * looks at the per-pass buffers directly. NDEBUG release builds elide
 * assert(), so this test uses explicit if-error checks instead.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yfigure/wire.h>

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

/*-----------------------------------------------------------------------------
 * Stub pty: implements just the write op, drops every byte. The engine
 * needs a non-NULL output_pty + a working write op to flush, but the
 * test doesn't care about the wire bytes (it walks the engine's
 * per-pass buffers directly).
 *---------------------------------------------------------------------------*/

static struct yetty_ycore_size_result stub_pty_write(struct yetty_platform_pty *self,
                                                     const char *data, size_t len)
{
    (void)self;
    (void)data;
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result stub_pty_destroy(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result stub_pty_read(struct yetty_platform_pty *self, char *buf,
                                                    size_t max_len)
{
    (void)self;
    (void)buf;
    (void)max_len;
    return YETTY_OK(yetty_ycore_size, 0);
}

static struct yetty_ycore_void_result stub_pty_resize(struct yetty_platform_pty *self,
                                                      uint32_t cols, uint32_t rows,
                                                      uint32_t pixel_w, uint32_t pixel_h)
{
    (void)self;
    (void)cols;
    (void)rows;
    (void)pixel_w;
    (void)pixel_h;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stub_pty_stop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *stub_pty_pipe_source(struct yetty_platform_pty *self)
{
    (void)self;
    return NULL;
}

static const struct yetty_platform_pty_ops *stub_pty_ops_get(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = stub_pty_destroy,
        .read = stub_pty_read,
        .write = stub_pty_write,
        .resize = stub_pty_resize,
        .stop = stub_pty_stop,
        .pipe_source = stub_pty_pipe_source,
    };
    return &ops;
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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

/* Build a headless engine whose root panel holds one 100x100 yimage.
 * The stub pty must outlive the returned engine — the caller owns it. */
static struct yetty_ygui_framework *make_engine_with_yimage(struct yetty_platform_pty *stub,
                                                            struct yetty_ygui_object **out_img)
{
    struct yetty_ygui_framework_ptr_result er = yetty_ygui_framework_create(stub);
    CHECK(YETTY_IS_OK(er), "engine_create");
    struct yetty_ygui_framework *engine = er.value;

    struct yetty_ygui_object_ptr_result rr =
        yetty_ygui_add(yetty_ygui_panel_class_get().value, NULL);
    CHECK(YETTY_IS_OK(rr), "add panel");
    struct yetty_ygui_object *root = rr.value;
    struct yetty_ycore_void_result sr = yetty_ygui_framework_set_root(engine, root);
    CHECK(YETTY_IS_OK(sr), "engine_set_root");

    struct yetty_ygui_object_ptr_result ir =
        yetty_ygui_add(yetty_ygui_yimage_class_get().value, root);
    CHECK(YETTY_IS_OK(ir), "add yimage");
    struct yetty_ygui_object *img = ir.value;

    /* Give yimage an explicit width/height so the layout pass produces
     * a non-empty rect for emit_container to ship. */
    struct yetty_ygui_layout l = *yetty_ygui_widget_layout_get(img);
    l.width = 100.0f;
    l.height = 100.0f;
    yetty_ygui_widget_layout_set(img, &l);

    *out_img = img;
    return engine;
}

static void test_yimage_emit(void)
{
    /* Stub pty lives on the stack of this function. */
    struct yetty_platform_pty stub = {.ops = stub_pty_ops_get()};
    struct yetty_ygui_object *img = NULL;
    struct yetty_ygui_framework *engine = make_engine_with_yimage(&stub, &img);

    struct yetty_ycore_void_result br =
        yetty_ygui_yimage_set_bytes(img, k_bmp_2x2, sizeof(k_bmp_2x2));
    CHECK(YETTY_IS_OK(br), "yimage_set_bytes");

    /* Emit and inspect the per-pass buffers. */
    struct yetty_ycore_void_result rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_OK(rer), "engine_emit #1");

    /* figure_bodies holds one record: {u32 len, u32 id, body[len]}. The
     * body is the rendered drawable_list (CMD_ZERO + one yimage prim), not
     * the encoded image bytes — so we assert the record framing and id
     * rather than an exact byte match. */
    CHECK(engine->figure_bodies.size > 8, "figure_bodies has one record");
    uint32_t rec_len = read_u32_le(engine->figure_bodies.data);
    uint32_t rec_id = read_u32_le(engine->figure_bodies.data + 4);
    CHECK(rec_len > 0, "figure record body non-empty");
    CHECK(engine->figure_bodies.size == 8 + (size_t)rec_len, "figure record framing");
    CHECK(rec_id == yetty_ygui_object_id(img), "figure record id");

    /* container_records should contain admin records for YGRID and
     * YIMAGE CREATE_CHILDs (the receiver IS the root container — no
     * synthetic engine container is emitted). The panel has
     * figure_kind=0 so its default emit_container is a no-op. */
    const uint8_t *cr = engine->container_records.data;
    size_t cr_size = engine->container_records.size;
    int seen_kinds[10] = {0};
    size_t off = 0;
    int saw_yimage = 0;
    while (off + 12 <= cr_size) {
        uint32_t len = read_u32_le(cr + off);
        uint32_t id = read_u32_le(cr + off + 4);
        uint32_t admin_op = read_u32_le(cr + off + 8);
        CHECK(id == 0, "admin record id == 0");
        if (admin_op == YETTY_YFIGURE_ADMIN_CREATE_CHILD) {
            uint32_t kind = read_u32_le(cr + off + 12 + 4);
            if (kind < 10) {
                seen_kinds[kind]++;
            }
            if (kind == YETTY_YFIGURE_KIND_YIMAGE) {
                /* framework_emit runs layout_compute first which
                 * overrides any set_rect — so we just check the rect
                 * is non-empty rather than a specific value. */
                uint32_t r0 = read_u32_le(cr + off + 12 + 8);
                uint32_t r2 = read_u32_le(cr + off + 12 + 16);
                float min_x, max_x;
                memcpy(&min_x, &r0, 4);
                memcpy(&max_x, &r2, 4);
                CHECK(max_x > min_x, "yimage rect non-empty");
                saw_yimage = 1;
            }
        }
        off += 8 + len;
    }
    CHECK(saw_yimage, "saw_yimage CREATE_CHILD");
    CHECK(seen_kinds[YETTY_YFIGURE_KIND_YGRID] == 1, "ygrid CREATE_CHILD count");
    CHECK(seen_kinds[YETTY_YFIGURE_KIND_YIMAGE] == 1, "yimage CREATE_CHILD count");

    /* Second emit: ygrid already exists (the engine sends
     * SET_CHILD_RECT instead of CREATE_CHILD). */
    rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_OK(rer), "engine_emit #2");
    int ygrid_creates = 0;
    off = 0;
    cr = engine->container_records.data;
    cr_size = engine->container_records.size;
    while (off + 12 <= cr_size) {
        uint32_t len = read_u32_le(cr + off);
        uint32_t admin_op = read_u32_le(cr + off + 8);
        if (admin_op == YETTY_YFIGURE_ADMIN_CREATE_CHILD) {
            uint32_t kind = read_u32_le(cr + off + 12 + 4);
            if (kind == YETTY_YFIGURE_KIND_YGRID) {
                ygrid_creates++;
            }
        }
        off += 8 + len;
    }
    CHECK(ygrid_creates == 0, "no ygrid re-emit on second emit");

    /* Destroy yimage — engine should queue a DELETE for next emit. */
    yetty_ygui_del(img);
    rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_OK(rer), "engine_emit #3");
    int delete_count = 0;
    off = 0;
    cr = engine->container_records.data;
    cr_size = engine->container_records.size;
    while (off + 12 <= cr_size) {
        uint32_t len = read_u32_le(cr + off);
        uint32_t admin_op = read_u32_le(cr + off + 8);
        if (admin_op == YETTY_YFIGURE_ADMIN_DELETE_CHILD) {
            delete_count++;
        }
        off += 8 + len;
    }
    CHECK(delete_count == 1, "one DELETE_CHILD after del");

    yetty_ygui_framework_destroy(engine);
}

/* Incremental figure-body emit: once a figure has been minted on the
 * receiver, an emit where nothing in its body subtree changed must NOT
 * re-ship the body — the receiver keeps the last one. A subsequent
 * content change must re-ship it. This is what stops an unchanged page
 * (a scrollarea figure) from being re-serialized on every emit. */
static void test_incremental_figure_skip(void)
{
    struct yetty_platform_pty stub = {.ops = stub_pty_ops_get()};
    struct yetty_ygui_framework_ptr_result er = yetty_ygui_framework_create(&stub);
    CHECK(YETTY_IS_OK(er), "skip: framework_create");
    struct yetty_ygui_framework *engine = er.value;

    struct yetty_ygui_object_ptr_result rr =
        yetty_ygui_add(yetty_ygui_panel_class_get().value, NULL);
    CHECK(YETTY_IS_OK(rr), "skip: add panel");
    struct yetty_ygui_object *root = rr.value;
    CHECK(YETTY_IS_OK(yetty_ygui_framework_set_root(engine, root)), "skip: set_root");

    /* A scrollarea promotes itself to its own YGRID figure (figure_kind !=
     * 0) — the same figure mechanism the browser's page area uses. Its
     * child label paints into that figure's body. */
    struct yetty_ygui_object_ptr_result sr =
        yetty_ygui_add(yetty_ygui_scrollarea_class_get().value, root);
    CHECK(YETTY_IS_OK(sr), "skip: add scrollarea");
    struct yetty_ygui_object *scroll = sr.value;
    struct yetty_ygui_layout sl = *yetty_ygui_widget_layout_get(scroll);
    sl.width = 200.0f;
    sl.height = 200.0f;
    CHECK(YETTY_IS_OK(yetty_ygui_widget_layout_set(scroll, &sl)), "skip: scroll layout");

    struct yetty_ygui_object_ptr_result lr =
        yetty_ygui_add(yetty_ygui_label_class_get().value, scroll);
    CHECK(YETTY_IS_OK(lr), "skip: add label");
    struct yetty_ygui_object *label = lr.value;
    CHECK(YETTY_IS_OK(yetty_ygui_label_set_text(label, "page content")), "skip: label text");
    struct yetty_ygui_layout ll = *yetty_ygui_widget_layout_get(label);
    ll.width = 180.0f;
    ll.height = 22.0f;
    CHECK(YETTY_IS_OK(yetty_ygui_widget_layout_set(label, &ll)), "skip: label layout");

    /* Emit #1: scrollarea figure not yet minted → body shipped. */
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #1");
    CHECK(engine->figure_bodies.size > 8, "skip: emit #1 ships scrollarea figure body");

    /* Emit #2: nothing changed and the figure is minted → body skipped.
     * figure_bodies (reset at the start of each emit) holds no records. */
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #2");
    CHECK(engine->figure_bodies.size == 0, "skip: emit #2 omits clean figure body");

    /* A content change re-dirties the figure → it re-ships on next emit. */
    CHECK(YETTY_IS_OK(yetty_ygui_label_set_text(label, "different content")), "skip: change text");
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #3");
    CHECK(engine->figure_bodies.size > 8, "skip: emit #3 re-ships dirtied figure body");

    /* And once clean again, it is skipped once more. */
    CHECK(YETTY_IS_OK(yetty_ygui_framework_emit(engine)), "skip: emit #4");
    CHECK(engine->figure_bodies.size == 0, "skip: emit #4 omits clean figure body");

    yetty_ygui_framework_destroy(engine);
}

/* Malformed image bytes must surface as a Result error from emit — not
 * be silently dropped or truncated. Exercises the strict-rejection path
 * in yimage_emit_body / yetty_yimage_render (issue #243 findings 4/5). */
static void test_yimage_emit_rejects_malformed(void)
{
    struct yetty_platform_pty stub = {.ops = stub_pty_ops_get()};
    struct yetty_ygui_object *img = NULL;
    struct yetty_ygui_framework *engine = make_engine_with_yimage(&stub, &img);

    uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    struct yetty_ycore_void_result br = yetty_ygui_yimage_set_bytes(img, garbage, sizeof(garbage));
    CHECK(YETTY_IS_OK(br), "yimage_set_bytes (garbage)");

    struct yetty_ycore_void_result rer = yetty_ygui_framework_emit(engine);
    CHECK(YETTY_IS_ERR(rer), "engine_emit rejects malformed image payload");
    yetty_ycore_error_destroy(rer.error);

    yetty_ygui_framework_destroy(engine);
}

int main(void)
{
    test_yimage_emit();
    test_incremental_figure_skip();
    test_yimage_emit_rejects_malformed();
    puts("ygui-figure-test: OK");
    return 0;
}
