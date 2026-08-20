/*
 * fallback-test.c - ms-raster fallback-chain contract test (#89)
 *
 * The yscene terminal grid resolves glyphs against its MSDF base face and
 * falls back to an ms-raster font whose FreeType chain loads every non-color
 * face in the fonts dir (Noto CJK among them). This pins the chain itself,
 * headlessly: a raster ms-font created for the DejaVu family must resolve
 *
 *   - Basic Latin from the primary face,
 *   - a CJK ideograph (U+6F22) through the fallback chain (DejaVu lacks it),
 *   - a cluster (e + combining acute) to a composited slot distinct from the
 *     bare base glyph's slot.
 *
 * Fonts + shader come from the build-dir embed-data staging; the test
 * self-skips (exit 77) when they are not present.
 */

#include <ytest.h>

#include <yetty/yconfig/config.h>
#include <yetty/yfont/ms-font.h>
#include <yetty/yfont/ms-raster-font.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FALLBACK_TEST_YAML "tmp-yfont-fallback-config.yaml"

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

static void test_fallback_chain(struct ytest *test)
{
    char primary_font_path[768];
    snprintf(primary_font_path, sizeof(primary_font_path), "%s/NotoSans-Regular.ttf",
             YFONT_TEST_FONT_DIR);
    char cjk_font_path[768];
    snprintf(cjk_font_path, sizeof(cjk_font_path), "%s/NotoSansCJK-Regular.ttf",
             YFONT_TEST_FONT_DIR);
    char shader_path[768];
    snprintf(shader_path, sizeof(shader_path), "%s/ms-raster-font.wgsl", YFONT_TEST_SHADER_DIR);
    if (!file_exists(primary_font_path) || !file_exists(cjk_font_path) ||
        !file_exists(shader_path)) {
        printf("SKIP: staged fonts/shader missing under %s\n", YFONT_TEST_FONT_DIR);
        exit(77);
    }

    /* Point a real config at the staged assets. */
    FILE *yaml = fopen(FALLBACK_TEST_YAML, "w");
    YTEST_REQUIRE(test, yaml != NULL);
    fprintf(yaml,
            "paths:\n"
            "  fonts: \"%s\"\n"
            "  shaders: \"%s\"\n",
            YFONT_TEST_FONT_DIR, YFONT_TEST_SHADER_DIR);
    fclose(yaml);
    char *argv[] = {"yfont-fallback-test", "-c", (char *)FALLBACK_TEST_YAML, NULL};
    struct yetty_yconfig_result config_res = yetty_yconfig_create(3, argv);
    YTEST_REQUIRE_OK(test, config_res);
    struct yetty_yconfig_config *config = config_res.value;

    /* The glob name is what populates the fallback chain (a plain family
     * name loads only its four style faces) — same as the grid's setup. */
    struct yetty_font_ms_font_result font_res =
        yetty_yfont_ms_raster_font_create_named(config, "NotoSans*", 9.0f, 18.0f);
    YTEST_REQUIRE_OK(test, font_res);
    struct yetty_yfont_ms_font *font = font_res.value;

    /* Basic Latin: the primary face covers it. */
    struct uint32_result latin_res = font->ops->get_glyph_index(font, (uint32_t)'A');
    YTEST_REQUIRE_OK(test, latin_res);

    /* CJK ideograph U+6F22 (漢): DejaVu lacks it — only the fallback chain
     * (NotoSansCJK) can resolve it. This is the grid's tofu case. */
    struct uint32_result cjk_res = font->ops->get_glyph_index(font, 0x6F22u);
    YTEST_REQUIRE_OK(test, cjk_res);
    YTEST_CHECK(test, cjk_res.value != latin_res.value);

    /* Cluster compositing: e + combining acute resolves to its own slot,
     * distinct from the bare 'e' (the grid uses this for fallback cells so
     * marks survive without separate mark slots). */
    if (font->ops->get_glyph_index_cluster) {
        struct uint32_result base_res = font->ops->get_glyph_index(font, (uint32_t)'e');
        YTEST_REQUIRE_OK(test, base_res);
        uint32_t acute = 0x0301u;
        struct uint32_result cluster_res = font->ops->get_glyph_index_cluster(
            font, (uint32_t)'e', &acute, 1, YETTY_YFONT_MS_STYLE_REGULAR);
        YTEST_REQUIRE_OK(test, cluster_res);
        YTEST_CHECK(test, cluster_res.value != base_res.value);
    }

    font->ops->destroy(font);
    config->ops->destroy(config);
    remove(FALLBACK_TEST_YAML);
}

int main(void)
{
    struct ytest test = ytest_begin("yfont_fallback");
    test_fallback_chain(&test);
    return ytest_end(&test);
}
