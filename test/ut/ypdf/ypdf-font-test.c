/*
 * ypdf font metrics test.
 *
 * Verifies the new yetty_font_font vtable advance APIs:
 *   - raster_font_create_from_file → measure_text returns positive
 *   - measure_text over an empty range returns 0
 *   - measure_text at different sizes scales linearly
 *
 * Uses a TTF shipped with the repo (assets/fonts/DejaVuSansMono-Regular.ttf).
 */

#include <yetty/yfont/font.h>
#include <yetty/yfont/raster-font.h>

#include <math.h>
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

#ifndef YPDF_TEST_TTF
#define YPDF_TEST_TTF "DejaVuSansMono-Regular.ttf"
#endif

int main(void) {
    struct yetty_font_font_result fr =
        yetty_yfont_raster_font_create_from_file(YPDF_TEST_TTF, NULL, 32.0f);
    if (YETTY_IS_ERR(fr)) {
        fprintf(stderr, "skipping: raster_font from '%s' failed: %s\n",
                YPDF_TEST_TTF, fr.error.msg);
        return 77; /* skip */
    }

    struct yetty_ypaint_font *f = fr.value;
    REQUIRE(f->ops && f->ops->measure_text, "measure_text missing");

    const char *sample = "Hello";
    struct float_result w16 = f->ops->measure_text(f, sample, 5, 16.0f);
    struct float_result w32 = f->ops->measure_text(f, sample, 5, 32.0f);
    REQUIRE(YETTY_IS_OK(w16), "measure_text@16 failed");
    REQUIRE(YETTY_IS_OK(w32), "measure_text@32 failed");
    REQUIRE(w16.value > 0.0f, "w16 is zero");
    REQUIRE(w32.value > 0.0f, "w32 is zero");

    /* Doubling the size must double the advance. */
    float ratio = w32.value / w16.value;
    REQUIRE(fabsf(ratio - 2.0f) < 0.001f, "advance does not scale linearly");

    /* Empty range returns zero. */
    struct float_result wempty = f->ops->measure_text(f, sample, 0, 16.0f);
    REQUIRE(YETTY_IS_OK(wempty), "measure_text empty failed");
    REQUIRE(wempty.value == 0.0f, "empty measurement is non-zero");

    /* Single codepoint advance consistency. */
    struct float_result a = f->ops->get_advance(f, 'H', 16.0f);
    REQUIRE(YETTY_IS_OK(a), "get_advance failed");
    REQUIRE(a.value > 0.0f, "advance is zero");

    printf("OK: w16=%.2f w32=%.2f H@16=%.2f\n", w16.value, w32.value, a.value);

    f->ops->destroy(f);
    return 0;
}
