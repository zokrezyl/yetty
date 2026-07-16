/*
 * shaping-render-test.c - End-to-end shaped-glyph rasterization
 *
 * Drives the whole complex-script pipeline: HarfBuzz shapes a run, each shaped
 * glyph id is rasterized through the (glyph_id, face) atlas, and the resulting
 * pixels are composited at their shaped pen positions (advances + GPOS offsets).
 * The composite proves the shaped output is renderable, not just that the shaper
 * returns glyph ids. Runs in full (atlas) mode so real pixels exist.
 *
 * Set YFONT_SHAPING_DUMP=<path.ppm> to write the composited canvas for visual
 * inspection (Arabic on the top line, Devanagari below). Without it the test is
 * headless and only asserts that shaped ink was produced.
 */

#include <ytest.h>

#include <yetty/yfont/font.h>
#include <yetty/yfont/raster-font.h>
#include <yetty/yrender/gpu-resource-set.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHAPE_MAX 128

struct canvas {
    uint8_t *pixels; /* grayscale, 0 = black ink, 255 = white paper */
    int width;
    int height;
};

static struct canvas canvas_create(int width, int height)
{
    struct canvas canvas = {.width = width, .height = height};
    canvas.pixels = malloc((size_t)width * height);
    if (canvas.pixels) {
        memset(canvas.pixels, 0xFF, (size_t)width * height);
    }
    return canvas;
}

static void canvas_write_ppm(const struct canvas *canvas, const char *path)
{
    FILE *out = fopen(path, "wb");
    if (!out) {
        return;
    }
    fprintf(out, "P6\n%d %d\n255\n", canvas->width, canvas->height);
    for (int i = 0; i < canvas->width * canvas->height; i++) {
        uint8_t value = canvas->pixels[i];
        uint8_t rgb[3] = {value, value, value};
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
}

/* Composite one shaped run onto the canvas at the given baseline, returning the
 * count of ink (non-white) pixels drawn. */
static long render_run(struct ytest *test, struct canvas *canvas, struct yetty_yfont_font *font,
                       const uint32_t *codepoints, size_t count, float pen_x, float baseline)
{
    struct yetty_yfont_shaped_glyph shaped[SHAPE_MAX];
    struct uint32_result shape_res = font->ops->shape_run(font, codepoints, count, shaped, SHAPE_MAX);
    YTEST_REQUIRE_OK(test, shape_res);
    uint32_t glyph_count = shape_res.value;
    YTEST_REQUIRE(test, glyph_count > 0);

    long ink = 0;
    for (uint32_t gi = 0; gi < glyph_count; gi++) {
        struct uint32_result slot_res = font->ops->get_glyph_index_by_gid(font, shaped[gi].gid);
        YTEST_CHECK_OK(test, slot_res);
        if (YETTY_IS_ERR(slot_res)) {
            continue;
        }
        uint32_t slot = slot_res.value;

        struct yetty_yrender_gpu_resource_set_result rs_res = font->ops->get_gpu_resource_set(font);
        YTEST_REQUIRE_OK(test, rs_res);
        const struct yetty_yrender_gpu_resource_set *rs = rs_res.value;
        const float *meta = (const float *)rs->buffers[0].data;
        uint32_t cell_size = rs->uniforms[1].u32;
        uint32_t atlas_cols = rs->uniforms[2].u32;
        uint32_t atlas_width = rs->textures[0].width;
        const uint8_t *atlas = rs->textures[0].data;

        const float *gm = meta + (size_t)slot * 6u;
        int size_x = (int)gm[0];
        int size_y = (int)gm[1];
        float bearing_x = gm[2];
        float bearing_y = gm[3];
        float cell_idx = gm[5];

        if (cell_idx >= 0.0f && size_x > 0 && size_y > 0) {
            uint32_t cell = (uint32_t)cell_idx;
            uint32_t col = cell % atlas_cols;
            uint32_t row = cell / atlas_cols;
            uint32_t ox = (cell_size - (uint32_t)size_x) / 2u;
            uint32_t oy = (cell_size - (uint32_t)size_y) / 2u;

            int glyph_left = (int)lroundf(pen_x + bearing_x + shaped[gi].x_offset);
            int glyph_top = (int)lroundf(baseline - bearing_y - shaped[gi].y_offset);

            for (int gy = 0; gy < size_y; gy++) {
                for (int gx = 0; gx < size_x; gx++) {
                    uint32_t ax = col * cell_size + ox + (uint32_t)gx;
                    uint32_t ay = row * cell_size + oy + (uint32_t)gy;
                    uint8_t coverage = atlas[(size_t)ay * atlas_width + ax];
                    if (coverage == 0) {
                        continue;
                    }
                    int cx = glyph_left + gx;
                    int cy = glyph_top + gy;
                    if (cx < 0 || cy < 0 || cx >= canvas->width || cy >= canvas->height) {
                        continue;
                    }
                    uint8_t *dst = &canvas->pixels[(size_t)cy * canvas->width + cx];
                    uint8_t darkened = (uint8_t)(255 - coverage);
                    if (darkened < *dst) {
                        *dst = darkened;
                    }
                    ink++;
                }
            }
        }
        pen_x += shaped[gi].x_advance;
    }
    return ink;
}

/* Load a raster font in full (atlas) mode, or return NULL if it is not staged
 * so a caller can skip just that line. */
static struct yetty_yfont_font *load_font_opt(struct ytest *test, const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", YFONT_TEST_FONT_DIR, name);
    FILE *probe = fopen(path, "rb");
    if (!probe) {
        fprintf(stderr, "skip: %s not staged at %s\n", name, YFONT_TEST_FONT_DIR);
        return NULL;
    }
    fclose(probe);

    struct yetty_font_font_result font_res =
        yetty_yfont_raster_font_create_from_file(path, YFONT_RASTER_SHADER, 48.0f);
    YTEST_REQUIRE_OK(test, font_res);
    return font_res.value;
}

int main(void)
{
    struct ytest test = ytest_begin("yfont_shaping_render");

    struct canvas canvas = canvas_create(1200, 480);
    YTEST_REQUIRE_NOT_NULL(&test, canvas.pixels);

    /* One canonical word per shaping family, matching demo/scripts/harfbuzz/.
     * Arabic "العربية" (joined), Devanagari "हिन्दी" (reordered matra + न्द
     * conjunct), Bengali "বাংলা", Tamil "தமிழ்" (split/reordered signs), Thai
     * "สวัสดี" (stacked marks). */
    struct sample {
        const char *font;
        uint32_t codepoints[16];
        size_t count;
        float baseline;
    };
    const struct sample samples[] = {
        {"NotoNaskhArabic-Regular.ttf",
         {0x0627, 0x0644, 0x0639, 0x0631, 0x0628, 0x064A, 0x0629}, 7, 70.0f},
        {"NotoSansDevanagari-Regular.ttf",
         {0x0939, 0x093F, 0x0928, 0x094D, 0x0926, 0x0940}, 6, 160.0f},
        {"NotoSansBengali-Regular.ttf", {0x09AC, 0x09BE, 0x0982, 0x09B2, 0x09BE}, 5, 250.0f},
        {"NotoSansTamil-Regular.ttf", {0x0BA4, 0x0BAE, 0x0BBF, 0x0BB4, 0x0BCD}, 5, 340.0f},
        {"NotoSansThai-Regular.ttf",
         {0x0E2A, 0x0E27, 0x0E31, 0x0E2A, 0x0E14, 0x0E35}, 6, 420.0f},
    };

    int rendered = 0;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        struct yetty_yfont_font *font = load_font_opt(&test, samples[i].font);
        if (!font) {
            continue;
        }
        YTEST_REQUIRE_NOT_NULL(&test, (void *)font->ops->shape_run);
        long ink = render_run(&test, &canvas, font, samples[i].codepoints, samples[i].count, 40.0f,
                              samples[i].baseline);
        YTEST_CHECK(&test, ink > 0);
        font->ops->destroy(font);
        rendered++;
    }
    if (rendered == 0) {
        YTEST_SKIP(&test, "no complex-script fonts staged at %s", YFONT_TEST_FONT_DIR);
    }

    const char *dump = getenv("YFONT_SHAPING_DUMP");
    if (dump && dump[0]) {
        canvas_write_ppm(&canvas, dump);
        fprintf(stderr, "wrote shaped-render canvas to %s\n", dump);
    }
    free(canvas.pixels);

    return ytest_end(&test);
}
