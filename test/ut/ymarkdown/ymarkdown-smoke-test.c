/*
 * ymarkdown smoke test.
 *
 * Renders markdown into a ydraw buffer and asserts that the new block
 * constructs emit the right primitive families. The buffer holds bare SDF
 * prims (box = YETTY_YSDF_BOX, segment = YETTY_YSDF_SEGMENT) and TEXT_SPAN
 * prims, each led by a u32 type tag — we scan the raw byte stream for those
 * tags rather than standing up a full drawable-list registry.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/ymarkdown/ymarkdown.h>
#include <yetty/ysdf/types.gen.h>

#define REQUIRE(cond, msg)                                                       \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);        \
            return 1;                                                            \
        }                                                                        \
    } while (0)

/* Count word-pattern occurrences of `tag` (host byte order) anywhere in the
 * byte stream. The tag values used here (0x7FFFFFFE/D, 0x40000002) do not
 * collide with the ASCII text, colours, or float coordinates in these inputs,
 * so a plain byte scan is a reliable proxy for "how many such prims". */
static size_t count_tag(const uint8_t *data, size_t size, uint32_t tag)
{
    uint8_t pat[4];
    memcpy(pat, &tag, sizeof(pat));
    size_t n = 0;
    if (size < sizeof(pat)) {
        return 0;
    }
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

/* Render `md` and tally the primitive families. Returns 0 on success. */
static int render_counts(const char *md, struct counts *out)
{
    struct yetty_ymarkdown_render_config cfg = {
        .cell_width = 8,
        .cell_height = 16,
        .width_cells = 80,
        .height_cells = 24,
    };
    struct yetty_ymarkdown_render_result r =
        yetty_ymarkdown_render(md, strlen(md), NULL, 0, &cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return -1;
    }
    const uint8_t *data = yetty_ydraw_drawable_list_data(r.value.buffer);
    size_t size = yetty_ydraw_drawable_list_size(r.value.buffer);
    out->boxes = count_tag(data, size, (uint32_t)YETTY_YSDF_BOX);
    out->segments = count_tag(data, size, (uint32_t)YETTY_YSDF_SEGMENT);
    out->text_spans = count_tag(data, size, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST);
    out->bytes = size;
    yetty_ydraw_drawable_list_destroy(r.value.buffer);
    return 0;
}

int main(void)
{
    struct counts c;

    /* 1. A plain paragraph: text spans only — no boxes, no segments. */
    REQUIRE(render_counts("Just a plain paragraph with no markup.\n", &c) == 0, "plain render");
    REQUIRE(c.bytes > 0, "plain buffer non-empty");
    REQUIRE(c.text_spans >= 1, "plain emits a text span");
    REQUIRE(c.segments == 0, "plain emits no segments");
    REQUIRE(c.boxes == 0, "plain emits no boxes");

    /* 2. A 2x2 table draws a grid (3 horizontal + 3 vertical = 6 segments),
     *    a header background box, and one text span per non-empty cell. */
    REQUIRE(render_counts("| A | B |\n|---|---|\n| 1 | 2 |\n", &c) == 0, "table render");
    REQUIRE(c.segments >= 6, "table emits grid segments");
    REQUIRE(c.boxes >= 1, "table emits a header background box");
    REQUIRE(c.text_spans >= 4, "table emits a text span per cell");

    /* 3. Column alignment markers parse without disturbing the grid. */
    REQUIRE(render_counts("| L | C | R |\n|:--|:-:|--:|\n| 1 | 2 | 3 |\n", &c) == 0,
            "aligned table render");
    REQUIRE(c.segments >= 7, "3-col table emits >=7 grid segments"); /* 3 h + 4 v */

    /* 4. A fenced code block draws a background panel per line (>=2 boxes). */
    REQUIRE(render_counts("```\nint x = 1;\nint y = 2;\n```\n", &c) == 0, "code render");
    REQUIRE(c.boxes >= 2, "code block emits a panel per line");
    REQUIRE(c.text_spans >= 2, "code block emits code text");

    /* 5. A horizontal rule draws a filled bar (one box). */
    REQUIRE(render_counts("above\n\n---\n\nbelow\n", &c) == 0, "hrule render");
    REQUIRE(c.boxes >= 1, "hrule emits a bar box");

    /* 6. A blockquote draws an accent gutter bar (one box) plus its text. */
    REQUIRE(render_counts("> quoted line\n", &c) == 0, "blockquote render");
    REQUIRE(c.boxes >= 1, "blockquote emits a gutter bar");
    REQUIRE(c.text_spans >= 1, "blockquote emits text");

    /* 7. Inline code still draws its tight background box. */
    REQUIRE(render_counts("use the `printf` call\n", &c) == 0, "inline code render");
    REQUIRE(c.boxes >= 1, "inline code emits a background box");

    /* 8. Strikethrough draws a line-through box over the struck text. */
    REQUIRE(render_counts("this is ~~gone~~ now\n", &c) == 0, "strike render");
    REQUIRE(c.boxes >= 1, "strikethrough emits a line box");

    /* 9. The kitchen-sink document renders cleanly and is non-trivial. */
    const char *all =
        "# Title\n"
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
    REQUIRE(render_counts(all, &c) == 0, "kitchen-sink render");
    REQUIRE(c.bytes > 0, "kitchen-sink non-empty");
    REQUIRE(c.segments >= 6, "kitchen-sink table grid present");
    REQUIRE(c.text_spans >= 8, "kitchen-sink has plenty of text");

    printf("OK: ymarkdown smoke test (table=%zu segs, code panels & quotes ok)\n", c.segments);
    return 0;
}
