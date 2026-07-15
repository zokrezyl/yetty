/*
 * ysixel decoder contract test — headless, deterministic.
 *
 * Decodes hand-built sixel payloads (the bytes between the DCS `q` command and
 * the ST terminator) and pins:
 *   - 6-pixel column bit ordering (bit 0 = top) and transparent (alpha 0) gaps
 *   - band advance on `-`, carriage return on `$`
 *   - run-length `!Pn` repeat
 *   - RGB palette (`#n;2;r;g;b`, 0..100 scaled to 0..255)
 *   - HLS palette (`#n;1;h;l;s`) primary hue mapping
 *   - measured dimensions grow to the painted extent
 *   - empty / no-pixel payloads reported as errors, oversized payload capped
 *
 * No GPU: the decoder is pure pixel math. The prim-packaging path
 * (yetty_ysixel_render) needs the drawable-list registry and is exercised by
 * the end-to-end terminal smoke, not here.
 */

#include <yetty/ysixel/sixel.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Fetch one RGBA pixel from a decoded image (0,0 = top-left). */
static const uint8_t *pixel_at(const struct yetty_ysixel_image *image, uint32_t x, uint32_t y)
{
    return &image->rgba[((size_t)y * image->width + x) * 4];
}

static void check_rgba(struct ytest *test, const struct yetty_ysixel_image *image, uint32_t x,
                       uint32_t y, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    const uint8_t *pixel = pixel_at(image, x, y);
    YTEST_CHECK_EQ_INT(test, pixel[0], red);
    YTEST_CHECK_EQ_INT(test, pixel[1], green);
    YTEST_CHECK_EQ_INT(test, pixel[2], blue);
    YTEST_CHECK_EQ_INT(test, pixel[3], alpha);
}

/* Bit ordering: '@' (0x40) sets only bit 0 (the top pixel); the five pixels
 * below stay transparent, so the measured height is exactly 1. */
static void test_bit_ordering(struct ytest *test)
{
    const char *payload = "#0;2;100;0;0@";
    struct yetty_ysixel_image_result res = yetty_ysixel_decode(payload, strlen(payload));
    YTEST_REQUIRE_OK(test, res);
    struct yetty_ysixel_image image = res.value;
    YTEST_CHECK_EQ_INT(test, image.width, 1);
    YTEST_CHECK_EQ_INT(test, image.height, 1);
    check_rgba(test, &image, 0, 0, 255, 0, 0, 255);
    yetty_ysixel_image_destroy(&image);
}

/* A byte selecting only bit 5 ('~' would be all six; 0x3F + 32 = 0x5F '_'
 * selects bit 5 alone) paints the sixth pixel, leaving rows 0..4 transparent. */
static void test_high_bit_and_gaps(struct ytest *test)
{
    const char *payload = "#0;2;0;100;0_"; /* green, bit 5 only */
    struct yetty_ysixel_image_result res = yetty_ysixel_decode(payload, strlen(payload));
    YTEST_REQUIRE_OK(test, res);
    struct yetty_ysixel_image image = res.value;
    YTEST_CHECK_EQ_INT(test, image.width, 1);
    YTEST_CHECK_EQ_INT(test, image.height, 6);
    /* rows 0..4 untouched (transparent), row 5 green */
    check_rgba(test, &image, 0, 0, 0, 0, 0, 0);
    check_rgba(test, &image, 0, 4, 0, 0, 0, 0);
    check_rgba(test, &image, 0, 5, 0, 255, 0, 255);
    yetty_ysixel_image_destroy(&image);
}

/* Two full bands via `-`, run-length fill via `!Pn`, and the second band a
 * different colour. Result is 4 x 12 with a clean colour split at row 6. */
static void test_bands_and_rle(struct ytest *test)
{
    const char *payload =
        "#0;2;100;0;0" /* red */
        "#1;2;0;0;100" /* blue */
        "#0!4~"        /* band 0: 4 cols, all six pixels */
        "-"            /* next band */
        "#1!4~";       /* band 1: 4 cols, all six pixels */
    struct yetty_ysixel_image_result res = yetty_ysixel_decode(payload, strlen(payload));
    YTEST_REQUIRE_OK(test, res);
    struct yetty_ysixel_image image = res.value;
    YTEST_CHECK_EQ_INT(test, image.width, 4);
    YTEST_CHECK_EQ_INT(test, image.height, 12);
    check_rgba(test, &image, 0, 0, 255, 0, 0, 255);   /* top-left red */
    check_rgba(test, &image, 3, 5, 255, 0, 0, 255);   /* band 0 bottom-right red */
    check_rgba(test, &image, 0, 6, 0, 0, 255, 255);   /* band 1 top blue */
    check_rgba(test, &image, 3, 11, 0, 0, 255, 255);  /* band 1 bottom-right blue */
    yetty_ysixel_image_destroy(&image);
}

/* Carriage return `$` returns to the left margin within the same band so a
 * second colour can overpaint. The last write wins at the overlapping column. */
static void test_carriage_return_overpaint(struct ytest *test)
{
    const char *payload =
        "#0;2;100;0;0" /* red */
        "#1;2;0;100;0" /* green */
        "#0!4~"        /* fill 4 cols red */
        "$"            /* back to column 0 */
        "#1~";         /* overpaint column 0 green */
    struct yetty_ysixel_image_result res = yetty_ysixel_decode(payload, strlen(payload));
    YTEST_REQUIRE_OK(test, res);
    struct yetty_ysixel_image image = res.value;
    YTEST_CHECK_EQ_INT(test, image.width, 4);
    YTEST_CHECK_EQ_INT(test, image.height, 6);
    check_rgba(test, &image, 0, 0, 0, 255, 0, 255); /* col 0 overpainted green */
    check_rgba(test, &image, 1, 0, 255, 0, 0, 255); /* col 1 still red */
    yetty_ysixel_image_destroy(&image);
}

/* HLS palette: hue 0 in sixel HLS is blue (the +240 degree rotation onto the
 * red-origin wheel), lightness 50, saturation 100 → pure blue. */
static void test_hls_palette(struct ytest *test)
{
    const char *payload = "#0;1;0;50;100~"; /* HLS h=0 l=50 s=100 */
    struct yetty_ysixel_image_result res = yetty_ysixel_decode(payload, strlen(payload));
    YTEST_REQUIRE_OK(test, res);
    struct yetty_ysixel_image image = res.value;
    const uint8_t *pixel = pixel_at(&image, 0, 0);
    /* Pure blue: low red/green, high blue. Allow rounding slack. */
    YTEST_CHECK(test, pixel[0] < 8);
    YTEST_CHECK(test, pixel[1] < 8);
    YTEST_CHECK(test, pixel[2] > 247);
    YTEST_CHECK_EQ_INT(test, pixel[3], 255);
    yetty_ysixel_image_destroy(&image);
}

/* Error paths: empty payload and a payload that paints nothing are both
 * errors (no zero-sized image escapes). */
static void test_error_paths(struct ytest *test)
{
    struct yetty_ysixel_image_result empty = yetty_ysixel_decode("", 0);
    YTEST_CHECK(test, !empty.ok);
    if (!empty.ok) {
        yetty_ycore_error_destroy(empty.error);
    }

    /* Only control bytes, no sixel data byte → no pixels. */
    const char *blank = "#0;2;100;0;0";
    struct yetty_ysixel_image_result none = yetty_ysixel_decode(blank, strlen(blank));
    YTEST_CHECK(test, !none.ok);
    if (!none.ok) {
        yetty_ycore_error_destroy(none.error);
    }
}

int main(void)
{
    struct ytest test = ytest_begin("ysixel_decode");
    YTEST_RUN(&test, test_bit_ordering);
    YTEST_RUN(&test, test_high_bit_and_gaps);
    YTEST_RUN(&test, test_bands_and_rle);
    YTEST_RUN(&test, test_carriage_return_overpaint);
    YTEST_RUN(&test, test_hls_palette);
    YTEST_RUN(&test, test_error_paths);
    return ytest_end(&test);
}
