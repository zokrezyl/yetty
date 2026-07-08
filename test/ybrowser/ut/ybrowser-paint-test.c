/* ybrowser-paint-test — paint-output regression layer.
 *
 * Where the layout tests pin box *geometry*, these tests pin the
 * serialized draw stream `ybrowser` actually emits: the ydraw drawable
 * list. They run with no Chrome and no GPU — paint into an in-memory
 * drawable list, walk the primitives via the standard registry, and assert
 * on their type + bounds.
 *
 * Covered:
 *   - an <img> emits a yimage primitive whose bounds exactly match its
 *     laid-out box (guards the image-position contract at the emission
 *     boundary);
 *   - text-decoration underline / line-through / overline each emit a thin
 *     decoration rect in the expected vertical band;
 *   - plain text emits no decoration rect.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ybrowser/ybrowser.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ydraw/drawable-list-registry.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/ysdf/types.gen.h>

static int g_failures = 0;
static int g_passed = 0;

#define EPS 0.5f

#define ASSERT_NEAR(name, got, expect)                                                             \
    do {                                                                                           \
        float got_value = (got);                                                                   \
        float expect_value = (expect);                                                             \
        if (fabsf(got_value - expect_value) > EPS) {                                               \
            fprintf(stderr, "  FAIL %s: got %.2f expected %.2f\n", (name), got_value,              \
                    expect_value);                                                                 \
            g_failures++;                                                                          \
        } else {                                                                                   \
            g_passed++;                                                                            \
        }                                                                                          \
    } while (0)

#define ASSERT_TRUE(name, cond)                                                                    \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "  FAIL %s: condition false\n", (name));                               \
            g_failures++;                                                                          \
        } else {                                                                                   \
            g_passed++;                                                                            \
        }                                                                                          \
    } while (0)

/* The default font size the fixtures load with; the decoration bands below
 * are derived from it. */
#define FONT_SIZE 16.0f

/* A 4x4 solid-red PNG as a data URI — decodes inline (no network) so the
 * image path exercises the real yimage emission, not the grey placeholder. */
static const char RED_PNG_DATA_URI[] =
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAEklEQVR42mP4z8DwHxkzkC4AADxAH+Ea86VIAAAAAElFTk"
    "SuQmCC";

struct prim {
    uint32_t type;
    float x0, y0, x1, y1;
};

/* Load + lay out an HTML fragment with a monospace-matched advance so paint
 * positions are deterministic. Exits the process on engine errors — a
 * harness failure, not a test failure. */
static struct yetty_ylexbor *load(const char *html, int viewport_w, int viewport_h)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = viewport_w,
        .viewport_height = viewport_h,
        .default_font_size = FONT_SIZE,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "ylexbor_create failed: %s\n", r.error.msg);
        exit(2);
    }
    struct yetty_ylexbor *yl = r.value;
    yetty_ylexbor_set_glyph_advance_ratio(yl, 0.602f);
    struct yetty_ycore_void_result lr = yetty_ylexbor_load_html(yl, html, strlen(html));
    if (YETTY_IS_ERR(lr)) {
        fprintf(stderr, "load_html failed: %s\n", lr.error.msg);
        yetty_ylexbor_destroy(yl);
        exit(3);
    }
    return yl;
}

/* Walk every primitive in `buf`, collecting (type, bounds) into `out`.
 * Descends into CMD_GROUP records — since #507 the paint pass wraps each
 * <img> box's prims in a stable CMD_GROUP so a landed image can ship as a
 * per-group delta; the flat iterator strides OVER a group's body, so grouped
 * prims are only reached by re-iterating the body (a bare prim stream) as its
 * own sub-list. Ungrouped and grouped prims are collected identically. */
static void collect_buf(struct yetty_ydraw_drawable_list *buf,
                        struct yetty_ydraw_drawable_list_registry *reg, struct prim *out, int max,
                        int *count)
{
    struct yetty_ydraw_drawable_iter_result it = yetty_ydraw_drawable_list_drawable_first(buf, reg);
    while (!YETTY_IS_ERR(it)) {
        const struct yetty_ydraw_drawable_list_entry *fw = &it.value.fw;
        if (fw->data && fw->data[0] == YETTY_YDRAW_CMD_GROUP) {
            uint32_t payload = fw->data[2];
            const uint8_t *body = (const uint8_t *)(fw->data + 3);
            struct yetty_ydraw_drawable_list_result sub =
                yetty_ydraw_drawable_list_create_from_bytes(body, payload);
            if (!YETTY_IS_ERR(sub)) {
                collect_buf(sub.value, reg, out, max, count);
                yetty_ydraw_drawable_list_destroy(sub.value);
            }
        } else if (fw->data && fw->ops && fw->ops->aabb && *count < max) {
            struct rectangle_result ab = fw->ops->aabb(fw->data);
            if (!YETTY_IS_ERR(ab)) {
                out[*count].type = fw->data[0];
                out[*count].x0 = ab.value.min.x;
                out[*count].y0 = ab.value.min.y;
                out[*count].x1 = ab.value.max.x;
                out[*count].y1 = ab.value.max.y;
                (*count)++;
            }
        }
        it = yetty_ydraw_drawable_list_drawable_next(buf, reg, &it.value);
    }
}

/* Paint the laid-out document into a fresh drawable list and walk every
 * primitive, collecting (type, bounds) into `out`. Returns the count
 * (capped at max). */
static int paint_and_collect(struct yetty_ylexbor *yl, struct prim *out, int max)
{
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(br)) {
        fprintf(stderr, "buffer_create failed: %s\n", br.error.msg);
        exit(4);
    }
    struct yetty_ydraw_drawable_list *buf = br.value;
    struct yetty_ycore_void_result rr = yetty_ylexbor_render(yl, buf);
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "render failed: %s\n", rr.error.msg);
        exit(5);
    }

    struct yetty_ydraw_drawable_list_registry_ptr_result reg_res =
        yetty_ydraw_drawable_list_registry_create_default();
    if (YETTY_IS_ERR(reg_res)) {
        fprintf(stderr, "registry_create failed: %s\n", reg_res.error.msg);
        exit(6);
    }
    struct yetty_ydraw_drawable_list_registry *reg = reg_res.value;

    int count = 0;
    collect_buf(buf, reg, out, max, &count);

    yetty_ydraw_drawable_list_registry_destroy(reg);
    yetty_ydraw_drawable_list_destroy(buf);
    return count;
}

/* Geometry of the first box whose tag matches `tag` (e.g. "img"). Returns 0
 * and fills the x, y, w, h out-params, or -1 if none. */
static int find_box(struct yetty_ylexbor *yl, const char *tag, float *x, float *y, float *w,
                    float *h)
{
    int total = yetty_ylexbor_test_box_count(yl);
    for (int i = 0; i < total; i++) {
        char t[16] = {0};
        if (yetty_ylexbor_test_box_at(yl, i, x, y, w, h, t, sizeof(t)) != 0) {
            continue;
        }
        if (strcmp(t, tag) == 0) {
            return 0;
        }
    }
    return -1;
}

/* Geometry of the first laid-out inline-text fragment. Returns 0 or -1. */
static int find_first_text_box(struct yetty_ylexbor *yl, float *x, float *y, float *w, float *h)
{
    int total = yetty_ylexbor_test_box_count(yl);
    for (int i = 0; i < total; i++) {
        int kind = -1;
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, NULL, NULL, NULL, NULL, 0) != 0) {
            continue;
        }
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT) {
            char t[16] = {0};
            (void)yetty_ylexbor_test_box_at(yl, i, x, y, w, h, t, sizeof(t));
            return 0;
        }
    }
    return -1;
}

/* Count thin SDF boxes (decoration rects) and report the center_y of the
 * single one found. A decoration rect is a YETTY_YSDF_BOX no taller than a
 * couple of pixels — page backgrounds and borders are far taller. */
static int count_decoration_rects(const struct prim *prims, int n, float *out_center_y)
{
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (prims[i].type != (uint32_t)YETTY_YSDF_BOX) {
            continue;
        }
        float half_height = (prims[i].y1 - prims[i].y0) * 0.5f;
        if (half_height <= 2.0f) {
            if (out_center_y) {
                *out_center_y = (prims[i].y0 + prims[i].y1) * 0.5f;
            }
            found++;
        }
    }
    return found;
}

static void test_image_bounds_match_box(void)
{
    fprintf(stderr, "[test_image_bounds_match_box]\n");
    char html[512];
    snprintf(html, sizeof(html),
             "<html><body style='margin:0'>"
             "<img data-test='pic' src='%s' style='width:100px;height:60px'>"
             "</body></html>",
             RED_PNG_DATA_URI);
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    float bx = 0, by = 0, bw = 0, bh = 0;
    if (find_box(yl, "img", &bx, &by, &bw, &bh) != 0) {
        fprintf(stderr, "  missing img box\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }

    struct prim prims[2048];
    int n = paint_and_collect(yl, prims, 2048);

    int image_prims = 0;
    struct prim img = {0};
    for (int i = 0; i < n; i++) {
        if (prims[i].type == YETTY_YIMAGE_TYPE_ID) {
            img = prims[i];
            image_prims++;
        }
    }
    ASSERT_TRUE("exactly one yimage primitive emitted", image_prims == 1);
    if (image_prims == 1) {
        /* The yimage bounds must match the laid-out box exactly — this is
         * the contract a downstream offset bug would violate. */
        ASSERT_NEAR("yimage min.x == box.x", img.x0, bx);
        ASSERT_NEAR("yimage min.y == box.y", img.y0, by);
        ASSERT_NEAR("yimage width == box.w", img.x1 - img.x0, bw);
        ASSERT_NEAR("yimage height == box.h", img.y1 - img.y0, bh);
    }

    yetty_ylexbor_destroy(yl);
}

/* Shared driver for the three decoration fixtures. `decoration` is a CSS
 * text-decoration value; band_lo/band_hi bound the expected center_y of the
 * emitted rect relative to the text fragment's top. */
static void run_decoration_test(const char *label, const char *decoration, float band_lo,
                                float band_hi)
{
    fprintf(stderr, "[%s]\n", label);
    char html[256];
    snprintf(html, sizeof(html),
             "<html><body style='margin:0'>"
             "<div style='text-decoration:%s'>abcdef</div>"
             "</body></html>",
             decoration);
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    float tx = 0, ty = 0, tw = 0, th = 0;
    if (find_first_text_box(yl, &tx, &ty, &tw, &th) != 0) {
        fprintf(stderr, "  missing text box\n");
        g_failures++;
        yetty_ylexbor_destroy(yl);
        return;
    }
    (void)tx;
    (void)tw;
    (void)th;

    struct prim prims[2048];
    int n = paint_and_collect(yl, prims, 2048);

    float center_y = 0;
    int rects = count_decoration_rects(prims, n, &center_y);
    ASSERT_TRUE("exactly one decoration rect emitted", rects == 1);
    if (rects == 1) {
        ASSERT_TRUE("decoration rect in expected vertical band",
                    center_y >= ty + band_lo && center_y <= ty + band_hi);
    }

    yetty_ylexbor_destroy(yl);
}

static void test_no_decoration_for_plain_text(void)
{
    fprintf(stderr, "[test_no_decoration_for_plain_text]\n");
    static const char html[] = "<html><body style='margin:0'>"
                               "<span>abcdef</span>"
                               "</body></html>";
    struct yetty_ylexbor *yl = load(html, 1000, 600);

    struct prim prims[2048];
    int n = paint_and_collect(yl, prims, 2048);

    int rects = count_decoration_rects(prims, n, NULL);
    ASSERT_TRUE("plain text emits no decoration rect", rects == 0);

    yetty_ylexbor_destroy(yl);
}

int main(void)
{
    test_image_bounds_match_box();
    /* Bands are fractions of FONT_SIZE (16): overline near the cap line,
     * line-through through the middle, underline below the baseline. */
    run_decoration_test("test_overline_emits_decoration", "overline", 0.0f, 5.0f);
    run_decoration_test("test_line_through_emits_decoration", "line-through", 6.0f, 11.0f);
    run_decoration_test("test_underline_emits_decoration", "underline", 12.0f, 18.0f);
    test_no_decoration_for_plain_text();

    fprintf(stderr, "results: %d passed, %d failed\n", g_passed, g_failures);
    return g_failures == 0 ? 0 : 1;
}
