/*
 * ymarkdown golden test (#421 — promoted from a smoke test).
 *
 * Renders markdown into a ydraw buffer and pins which primitive families each
 * block construct emits (SDF box = YETTY_YSDF_BOX, segment = YETTY_YSDF_SEGMENT,
 * and TEXT_DRAWABLE_LIST spans, each led by a u32 type tag scanned from the raw
 * byte stream), PLUS byte-exact determinism: the same markdown renders to
 * identical bytes twice. Layout is fixed-geometry (no font metrics), so the
 * output is deterministic and headless.
 */

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ymarkdown/ymarkdown.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Count host-byte-order occurrences of `tag` anywhere in the stream. The tag
 * values (0x7FFFFFFE/D, TEXT_DRAWABLE_LIST) don't collide with the ASCII text,
 * colours, or float coordinates in these inputs, so a byte scan is a reliable
 * proxy for "how many such prims". */
static size_t count_tag(const uint8_t *data, size_t size, uint32_t tag)
{
    uint8_t pat[4];
    memcpy(pat, &tag, sizeof(pat));
    size_t n = 0;
    for (size_t i = 0; i + sizeof(pat) <= size; i++) {
        if (memcmp(data + i, pat, sizeof(pat)) == 0) {
            n++;
        }
    }
    return n;
}

struct counts {
    size_t boxes;
    size_t segments;
    size_t text_spans;
    size_t bytes;
};

/* Render `md`; caller must destroy the returned buffer. */
static struct yetty_ydraw_drawable_list *render_buf(struct ytest *test, const char *md)
{
    struct yetty_ymarkdown_render_config cfg = {
        .cell_width = 8,
        .cell_height = 16,
        .width_cells = 80,
        .height_cells = 24,
    };
    struct yetty_ymarkdown_render_result r = yetty_ymarkdown_render(md, strlen(md), NULL, 0, &cfg);
    YTEST_REQUIRE_OK(test, r);
    YTEST_REQUIRE_NOT_NULL(test, r.value.buffer);
    return r.value.buffer;
}

static struct counts render_counts(struct ytest *test, const char *md)
{
    struct yetty_ydraw_drawable_list *buf = render_buf(test, md);
    const uint8_t *data = yetty_ydraw_drawable_list_data(buf);
    size_t size = yetty_ydraw_drawable_list_size(buf);
    struct counts c = {
        .boxes = count_tag(data, size, (uint32_t)YETTY_YSDF_BOX),
        .segments = count_tag(data, size, (uint32_t)YETTY_YSDF_SEGMENT),
        .text_spans = count_tag(data, size, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST),
        .bytes = size,
    };
    yetty_ydraw_drawable_list_destroy(buf);
    return c;
}

/*---------------------------------------------------------------------------
 * Each block construct emits the expected primitive families.
 *-------------------------------------------------------------------------*/
static void test_primitive_families(struct ytest *test)
{
    /* Plain paragraph: text spans only. */
    struct counts plain = render_counts(test, "Just a plain paragraph with no markup.\n");
    YTEST_CHECK(test, plain.bytes > 0);
    YTEST_CHECK(test, plain.text_spans >= 1);
    YTEST_CHECK_EQ_SIZE(test, plain.segments, 0u);
    YTEST_CHECK_EQ_SIZE(test, plain.boxes, 0u);

    /* 2x2 table: grid segments (>=6), a header background box, a span per cell. */
    struct counts table = render_counts(test, "| A | B |\n|---|---|\n| 1 | 2 |\n");
    YTEST_CHECK(test, table.segments >= 6);
    YTEST_CHECK(test, table.boxes >= 1);
    YTEST_CHECK(test, table.text_spans >= 4);

    /* Alignment markers don't disturb the 3-col grid (3 h + 4 v = 7 segments). */
    struct counts aligned = render_counts(test, "| L | C | R |\n|:--|:-:|--:|\n| 1 | 2 | 3 |\n");
    YTEST_CHECK(test, aligned.segments >= 7);

    /* Fenced code block: a panel per line (>=2 boxes) + code text. */
    struct counts code = render_counts(test, "```\nint x = 1;\nint y = 2;\n```\n");
    YTEST_CHECK(test, code.boxes >= 2);
    YTEST_CHECK(test, code.text_spans >= 2);

    /* Horizontal rule: a filled bar (one box). */
    struct counts hr = render_counts(test, "above\n\n---\n\nbelow\n");
    YTEST_CHECK(test, hr.boxes >= 1);

    /* Blockquote: a gutter bar box + text. */
    struct counts quote = render_counts(test, "> quoted line\n");
    YTEST_CHECK(test, quote.boxes >= 1);
    YTEST_CHECK(test, quote.text_spans >= 1);

    /* Inline code: a tight background box. */
    struct counts inline_code = render_counts(test, "use the `printf` call\n");
    YTEST_CHECK(test, inline_code.boxes >= 1);

    /* Strikethrough: a line-through box. */
    struct counts strike = render_counts(test, "this is ~~gone~~ now\n");
    YTEST_CHECK(test, strike.boxes >= 1);
}

/*---------------------------------------------------------------------------
 * The kitchen-sink document renders non-trivially and byte-identically twice.
 *-------------------------------------------------------------------------*/
static void test_kitchen_sink_deterministic(struct ytest *test)
{
    const char *all = "# Title\n"
                      "\n"
                      "Body with **bold**, *italic*, `code`, ~~strike~~ and a [link](http://x).\n"
                      "\n"
                      "1. one\n"
                      "2. two\n"
                      "\n"
                      "> a quote\n"
                      "\n"
                      "| key | value |\n"
                      "|:----|------:|\n"
                      "| a   | 1     |\n"
                      "| b   | 2     |\n"
                      "\n"
                      "```\ncode block\n```\n"
                      "\n"
                      "---\n";

    struct yetty_ydraw_drawable_list *a = render_buf(test, all);
    struct yetty_ydraw_drawable_list *b = render_buf(test, all);
    size_t sa = yetty_ydraw_drawable_list_size(a);
    size_t sb = yetty_ydraw_drawable_list_size(b);
    YTEST_CHECK(test, sa > 0);
    YTEST_REQUIRE_EQ_SIZE(test, sa, sb);
    YTEST_CHECK_MEM_EQ(test, yetty_ydraw_drawable_list_data(a), yetty_ydraw_drawable_list_data(b),
                       sa);

    /* And it is structurally rich. */
    const uint8_t *data = yetty_ydraw_drawable_list_data(a);
    YTEST_CHECK(test, count_tag(data, sa, (uint32_t)YETTY_YSDF_SEGMENT) >= 6); /* table grid */
    YTEST_CHECK(test, count_tag(data, sa, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) >= 8);

    yetty_ydraw_drawable_list_destroy(a);
    yetty_ydraw_drawable_list_destroy(b);
}

/*---------------------------------------------------------------------------
 * The serialized scene bounds carry the CONTENT extent. A receiver that
 * cannot walk record AABBs (the ymux daemon reserving figure rows) reads the
 * document height from the container header, so the bounds must cover the
 * laid-out document — not restate the viewport. height_cells=0 is ycat's
 * "unbounded document" shape, which used to serialize scene_max_y=0 and made
 * the daemon reserve nothing (the document overlapped the next prompt).
 *-------------------------------------------------------------------------*/
static void test_scene_bounds_carry_content_height(struct ytest *test)
{
    char many[4096] = {0};
    size_t used = 0;
    for (int i = 0; i < 40 && used + 32 < sizeof(many); i++) {
        used += (size_t)snprintf(many + used, sizeof(many) - used, "line %02d text\n\n", i);
    }
    struct yetty_ymarkdown_render_config unbounded = {
        .cell_width = 8,
        .cell_height = 16,
        .width_cells = 80,
        .height_cells = 0,
    };
    struct yetty_ymarkdown_render_result r =
        yetty_ymarkdown_render(many, strlen(many), NULL, 0, &unbounded);
    YTEST_REQUIRE_OK(test, r);
    float max_y = yetty_ydraw_drawable_list_scene_max_y(r.value.buffer);
    /* 40 paragraphs at 16 px per line: far taller than one 24-row screen. */
    YTEST_CHECK(test, max_y > 40.0f * 16.0f);
    yetty_ydraw_drawable_list_destroy(r.value.buffer);

    /* A short document's bounds are its content, not the 24-row viewport. */
    struct yetty_ydraw_drawable_list *small = render_buf(test, "one line\n");
    float small_max_y = yetty_ydraw_drawable_list_scene_max_y(small);
    YTEST_CHECK(test, small_max_y > 0.0f);
    YTEST_CHECK(test, small_max_y < 12.0f * 16.0f);
    yetty_ydraw_drawable_list_destroy(small);
}

int main(void)
{
    struct ytest test = ytest_begin("ymarkdown_golden");
    YTEST_RUN(&test, test_primitive_families);
    YTEST_RUN(&test, test_kitchen_sink_deterministic);
    YTEST_RUN(&test, test_scene_bounds_carry_content_height);
    return ytest_end(&test);
}
