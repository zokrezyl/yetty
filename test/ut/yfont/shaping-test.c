/*
 * shaping-test.c - Complex-script shaping (HarfBuzz) contract test
 *
 * Proves the raster-font shaper + glyph-id atlas path produce correct
 * OpenType shaping with the bundled Noto fonts, headlessly (metrics-only mode,
 * no GPU/atlas). Assertions are reference-free — they compare shaping outputs
 * against each other rather than against hard-coded glyph ids, so they stay
 * stable across font revisions:
 *
 *   - Arabic contextual joining: a letter shaped in medial context gets a
 *     different glyph id than the same letter shaped in isolation.
 *   - Devanagari reordering: a pre-base matra typed after its consonant comes
 *     out before it, so the shaped cluster indices are non-monotonic.
 *   - The (glyph_id, face) atlas resolves shaped gids to slots and caches them.
 *
 * The fonts come from the 3rdparty fetch (build-dir embed-data/fonts); the test
 * self-skips (exit 77) when a required font is not staged.
 */

#include <ytest.h>

#include <yetty/yfont/font.h>
#include <yetty/yfont/raster-font.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHAPE_MAX 64

/* Build "<dir>/<name>" into caller storage. */
static void font_path(char *out, size_t out_size, const char *name)
{
    snprintf(out, out_size, "%s/%s", YFONT_TEST_FONT_DIR, name);
}

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

/* Find the first shaped glyph whose cluster matches, return its index or -1. */
static int find_by_cluster(const struct yetty_yfont_shaped_glyph *glyphs, uint32_t count,
                           uint32_t cluster)
{
    for (uint32_t i = 0; i < count; i++) {
        if (glyphs[i].cluster == cluster) {
            return (int)i;
        }
    }
    return -1;
}

static void test_script_classifier(struct ytest *test)
{
    YTEST_CHECK_EQ_INT(test, yetty_yfont_shaping_script_for_codepoint('A'),
                       YETTY_YFONT_SHAPING_NONE);
    YTEST_CHECK_EQ_INT(test, yetty_yfont_shaping_script_for_codepoint(0x0645 /* Arabic meem */),
                       YETTY_YFONT_SHAPING_ARABIC);
    YTEST_CHECK_EQ_INT(test, yetty_yfont_shaping_script_for_codepoint(0x0915 /* Deva ka */),
                       YETTY_YFONT_SHAPING_INDIC);
    YTEST_CHECK_EQ_INT(test, yetty_yfont_shaping_script_for_codepoint(0x0E01 /* Thai ko kai */),
                       YETTY_YFONT_SHAPING_BRAHMIC);
    YTEST_CHECK_EQ_INT(test, yetty_yfont_shaping_script_for_codepoint(0x4E00 /* CJK */),
                       YETTY_YFONT_SHAPING_NONE);
}

static void test_arabic_joining(struct ytest *test)
{
    char path[1024];
    font_path(path, sizeof(path), "NotoNaskhArabic-Regular.ttf");
    if (!file_exists(path)) {
        YTEST_SKIP(test, "NotoNaskhArabic-Regular.ttf not staged at %s", YFONT_TEST_FONT_DIR);
    }

    struct yetty_font_font_result font_res =
        yetty_yfont_raster_font_create_from_file(path, NULL, 32.0f);
    YTEST_REQUIRE_OK(test, font_res);
    struct yetty_yfont_font *font = font_res.value;

    YTEST_REQUIRE_NOT_NULL(test, (void *)font->ops->shape_run);
    YTEST_REQUIRE_NOT_NULL(test, (void *)font->ops->get_glyph_index_by_gid);

    struct yetty_yfont_shaped_glyph shaped[SHAPE_MAX];

    /* Kaf (U+0643) shaped in isolation. */
    const uint32_t isolated[] = {0x0643};
    struct uint32_result iso_res = font->ops->shape_run(font, isolated, 1, shaped, SHAPE_MAX);
    YTEST_REQUIRE_OK(test, iso_res);
    YTEST_REQUIRE_EQ_INT(test, iso_res.value, 1);
    uint32_t isolated_kaf_gid = shaped[0].gid;
    YTEST_CHECK(test, isolated_kaf_gid != 0);

    /* "مكتب" (meem, kaf, teh, beh) — the kaf (source index 1) is now medial. */
    const uint32_t word[] = {0x0645, 0x0643, 0x062A, 0x0628};
    struct uint32_result run_res =
        font->ops->shape_run(font, word, sizeof(word) / sizeof(word[0]), shaped, SHAPE_MAX);
    YTEST_REQUIRE_OK(test, run_res);
    YTEST_CHECK(test, run_res.value >= 1);

    int kaf_pos = find_by_cluster(shaped, run_res.value, 1);
    YTEST_REQUIRE(test, kaf_pos >= 0);
    uint32_t medial_kaf_gid = shaped[kaf_pos].gid;

    /* Contextual joining must have picked a different glyph for the medial kaf
     * than for the isolated one. This is the whole point of shaping Arabic. */
    YTEST_CHECK(test, medial_kaf_gid != isolated_kaf_gid);

    /* Every shaped gid resolves to an atlas slot, and the resolution caches. */
    for (uint32_t i = 0; i < run_res.value; i++) {
        struct uint32_result slot_res = font->ops->get_glyph_index_by_gid(font, shaped[i].gid);
        YTEST_CHECK_OK(test, slot_res);
        struct uint32_result slot_again = font->ops->get_glyph_index_by_gid(font, shaped[i].gid);
        YTEST_CHECK_OK(test, slot_again);
        YTEST_CHECK_EQ_INT(test, slot_res.value, slot_again.value);
    }

    /* Advances should be positive (kaf is not zero-width). */
    YTEST_CHECK(test, shaped[kaf_pos].x_advance > 0.0f);

    font->ops->destroy(font);
}

static void test_devanagari_reordering(struct ytest *test)
{
    char path[1024];
    font_path(path, sizeof(path), "NotoSansDevanagari-Regular.ttf");
    if (!file_exists(path)) {
        YTEST_SKIP(test, "NotoSansDevanagari-Regular.ttf not staged at %s", YFONT_TEST_FONT_DIR);
    }

    struct yetty_font_font_result font_res =
        yetty_yfont_raster_font_create_from_file(path, NULL, 32.0f);
    YTEST_REQUIRE_OK(test, font_res);
    struct yetty_yfont_font *font = font_res.value;
    YTEST_REQUIRE_NOT_NULL(test, (void *)font->ops->shape_run);

    struct yetty_yfont_shaped_glyph shaped[SHAPE_MAX];

    /* ka (U+0915) shaped alone gives its nominal base glyph. */
    const uint32_t ka_only[] = {0x0915};
    struct uint32_result ka_res = font->ops->shape_run(font, ka_only, 1, shaped, SHAPE_MAX);
    YTEST_REQUIRE_OK(test, ka_res);
    YTEST_REQUIRE_EQ_INT(test, ka_res.value, 1);
    uint32_t ka_gid = shaped[0].gid;
    YTEST_CHECK(test, ka_gid != 0);

    /* "कि" = ka (U+0915) + pre-base i-matra (U+093F). Typed ka-then-matra, but
     * the matra is a pre-base vowel sign — it renders before the consonant. The
     * HarfBuzz Indic shaper reorders the run so the matra glyph precedes ka.
     * (Cluster indices can't show this: the default cluster level merges the
     * syllable into one cluster, so detect reordering by glyph order instead.) */
    const uint32_t syllable[] = {0x0915, 0x093F};
    struct uint32_result run_res = font->ops->shape_run(font, syllable, 2, shaped, SHAPE_MAX);
    YTEST_REQUIRE_OK(test, run_res);
    YTEST_REQUIRE(test, run_res.value >= 2);

    int ka_pos = -1;
    for (uint32_t i = 0; i < run_res.value; i++) {
        if (shaped[i].gid == ka_gid) {
            ka_pos = (int)i;
            break;
        }
    }
    /* Reordering: at least one glyph (the pre-base matra) comes before ka. */
    YTEST_REQUIRE(test, ka_pos >= 1);

    font->ops->destroy(font);
}

int main(void)
{
    struct ytest test = ytest_begin("yfont_shaping");
    test_script_classifier(&test);
    test_arabic_joining(&test);
    test_devanagari_reordering(&test);
    return ytest_end(&test);
}
