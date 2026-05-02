/*
 * ypdf render smoke test.
 *
 * Loads test-comprehensive.pdf (3 pages) through yetty_ypdf_render_pdf and
 * validates the resulting ypaint buffer:
 *   - pages are counted correctly
 *   - scene bounds reflect the accumulated page heights
 *   - at least one FONT prim was emitted (test PDF embeds fonts)
 *   - at least one TEXT_SPAN prim was emitted
 *
 * After the buffer/handler refactor, fonts and text spans live in the
 * primitive byte stream alongside SDF prims. We count them by iterating
 * with a flyweight registry that has the FONT/TEXT_SPAN handlers
 * registered (yetty_ypaint_flyweight_create() does that).
 */

#include <pdfio.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ypaint-core/font-prim.h>
#include <yetty/ypaint-core/text-span-prim.h>
#include <yetty/ypaint/flyweight.h>
#include <yetty/ypdf/ypdf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(cond, msg)                                                     \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#ifndef YPDF_TEST_PDF
#define YPDF_TEST_PDF "test-comprehensive.pdf"
#endif

static bool error_cb(pdfio_file_t *f, const char *s, void *d) {
    (void)f; (void)d;
    fprintf(stderr, "pdfio: %s\n", s);
    return true;
}

struct prim_counts {
    uint32_t fonts;
    uint32_t text_spans;
    uint32_t other;
};

static struct prim_counts count_prims(struct yetty_ypaint_core_buffer *buf,
                                      struct yetty_ypaint_flyweight_registry *reg)
{
    struct prim_counts c = {0};
    struct yetty_ypaint_core_primitive_iter_result ir =
        yetty_ypaint_core_buffer_prim_first(buf, reg);
    if (YETTY_IS_ERR(ir))
        return c;
    struct yetty_ypaint_core_primitive_iter it = ir.value;
    for (;;) {
        uint32_t t = it.fw.data[0];
        if (t == YETTY_YPAINT_TYPE_FONT)            c.fonts++;
        else if (t == YETTY_YPAINT_TYPE_TEXT_SPAN)  c.text_spans++;
        else                                        c.other++;

        struct yetty_ypaint_core_primitive_iter_result nx =
            yetty_ypaint_core_buffer_prim_next(buf, reg, &it);
        if (YETTY_IS_ERR(nx)) break;
        it = nx.value;
    }
    return c;
}

int main(void) {
    pdfio_file_t *pdf = pdfioFileOpen(YPDF_TEST_PDF, NULL, NULL,
                                      error_cb, NULL);
    REQUIRE(pdf, "pdfioFileOpen failed");

    struct yetty_ypdf_render_result res = yetty_ypdf_render_pdf(pdf);
    REQUIRE(YETTY_IS_OK(res), "yetty_ypdf_render_pdf failed");

    struct yetty_ypdf_render_output *out = &res.value;
    REQUIRE(out->buffer, "buffer is NULL");
    REQUIRE(out->page_count == 3, "expected 3 pages");
    REQUIRE(out->total_height > 0.0f, "total_height not set");
    REQUIRE(out->max_width > 0.0f, "max_width not set");

    float sx = yetty_ypaint_core_buffer_scene_max_x(out->buffer);
    float sy = yetty_ypaint_core_buffer_scene_max_y(out->buffer);
    REQUIRE(sx == out->max_width, "scene max_x mismatch");
    REQUIRE(sy == out->total_height, "scene max_y mismatch");

    struct yetty_ypaint_flyweight_registry_ptr_result rr =
        yetty_ypaint_flyweight_create();
    REQUIRE(YETTY_IS_OK(rr), "flyweight_create failed");
    struct yetty_ypaint_flyweight_registry *reg = rr.value;

    struct prim_counts c = count_prims(out->buffer, reg);
    REQUIRE(c.fonts >= 1, "no FONT prims in buffer");
    REQUIRE(c.text_spans >= 1, "no TEXT_SPAN prims in buffer");

    printf("OK: %d pages, %u FONT prims, %u TEXT_SPAN prims, %u other, "
           "total_h=%.1f\n",
           out->page_count, c.fonts, c.text_spans, c.other,
           out->total_height);

    yetty_ypaint_flyweight_registry_destroy(reg);
    yetty_ypaint_core_buffer_destroy(out->buffer);
    pdfioFileClose(pdf);
    return 0;
}
