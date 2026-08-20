/*
 * vtermgrid-test.c — class@yscene:vtermgrid behavioral tests (headless, no
 * GPU). The independent client terminal grid: feed ordinary terminal bytes,
 * read the resulting cell grid. Covers exact cells/attrs/colors/cursor, wide
 * cells, resize, reset, and byte-boundary fragmentation invariance (#699 test
 * requirements for the receiving grid).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yetty/gen/impl/yscene/vtermgrid.h"
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

static int g_failures;
static int g_checks;

#define CHECK(name, condition)                                                                     \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (condition) {                                                                           \
            fprintf(stderr, "ok   %s\n", (name));                                                  \
        } else {                                                                                   \
            fprintf(stderr, "FAIL %s (%s:%d)\n", (name), __FILE__, __LINE__);                      \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

/* Create a grid or abort the test binary — every case needs one. */
static struct yetty_yclass_object *make_grid(uint32_t rows, uint32_t cols)
{
    struct yetty_yclass_object_ptr_result res = yetty_yscene_vtermgrid_make(rows, cols);
    if (YETTY_IS_ERR(res)) {
        fprintf(stderr, "FATAL vtermgrid_make failed: %s\n", res.error.msg);
        yetty_ycore_error_destroy(res.error);
        exit(2);
    }
    return res.value;
}

static void feed_bytes(struct yetty_yclass_object *grid, const uint8_t *bytes, size_t len)
{
    struct yetty_ycore_void_result res = yetty_yscene_vtermgrid_write(grid, bytes, len);
    if (YETTY_IS_ERR(res)) {
        fprintf(stderr, "FATAL vtermgrid_write failed: %s\n", res.error.msg);
        yetty_ycore_error_destroy(res.error);
        exit(2);
    }
}

static void feed(struct yetty_yclass_object *grid, const char *bytes)
{
    feed_bytes(grid, (const uint8_t *)bytes, strlen(bytes));
}

static struct yetty_yscene_vtermgrid_cell cell_at(struct yetty_yclass_object *grid, uint32_t row,
                                                  uint32_t col)
{
    struct yetty_yscene_vtermgrid_cell cell;
    memset(&cell, 0, sizeof(cell));
    struct yetty_ycore_void_result res = yetty_yscene_vtermgrid_cell(grid, row, col, &cell);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    return cell;
}

/* Snapshot the whole grid's glyphs into a caller buffer, one uint32 per cell,
 * so two feed paths can be compared for identical final state. */
static void snapshot_glyphs(struct yetty_yclass_object *grid, uint32_t rows, uint32_t cols,
                            uint32_t *out)
{
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            out[r * cols + c] = cell_at(grid, r, c).glyph;
        }
    }
}

/*===========================================================================
 * Cases.
 *=========================================================================*/

static void test_basic_text(void)
{
    struct yetty_yclass_object *grid = make_grid(24, 80);
    feed(grid, "Hello\r\nWorld");

    CHECK("basic: H", cell_at(grid, 0, 0).glyph == 'H');
    CHECK("basic: e", cell_at(grid, 0, 1).glyph == 'e');
    CHECK("basic: l", cell_at(grid, 0, 2).glyph == 'l');
    CHECK("basic: l2", cell_at(grid, 0, 3).glyph == 'l');
    CHECK("basic: o", cell_at(grid, 0, 4).glyph == 'o');
    CHECK("basic: W row1", cell_at(grid, 1, 0).glyph == 'W');
    CHECK("basic: d row1", cell_at(grid, 1, 4).glyph == 'd');
    CHECK("basic: width 1", cell_at(grid, 0, 0).width == 1);

    uint32_t crow = 0, ccol = 0;
    int visible = 0;
    struct yetty_ycore_void_result cres =
        yetty_yscene_vtermgrid_cursor(grid, &crow, &ccol, &visible);
    CHECK("basic: cursor query ok", YETTY_IS_OK(cres));
    if (YETTY_IS_ERR(cres)) {
        yetty_ycore_error_destroy(cres.error);
    }
    /* After "World" the cursor sits at row 1, col 5. */
    CHECK("basic: cursor row", crow == 1);
    CHECK("basic: cursor col", ccol == 5);

    yetty_yscene_vtermgrid_dispose(grid);
}

static void test_attrs_and_color(void)
{
    struct yetty_yclass_object *grid = make_grid(10, 40);
    /* bold, then red foreground, then a char, then reset. */
    feed(grid, "\x1b[1mB\x1b[0m\x1b[31mR\x1b[0mN");

    struct yetty_yscene_vtermgrid_cell b = cell_at(grid, 0, 0);
    struct yetty_yscene_vtermgrid_cell r = cell_at(grid, 0, 1);
    struct yetty_yscene_vtermgrid_cell n = cell_at(grid, 0, 2);

    CHECK("attr: bold glyph", b.glyph == 'B');
    CHECK("attr: bold bit", (b.attrs & YETTY_YSCENE_VTERMGRID_ATTR_BOLD) != 0);
    CHECK("attr: red glyph", r.glyph == 'R');
    CHECK("attr: red not bold", (r.attrs & YETTY_YSCENE_VTERMGRID_ATTR_BOLD) == 0);

    uint32_t rr = r.fg & 0xFFu, rg = (r.fg >> 8) & 0xFFu, rb = (r.fg >> 16) & 0xFFu;
    CHECK("attr: red is reddish", rr > rg && rr > rb);

    CHECK("attr: normal glyph", n.glyph == 'N');
    /* After reset the fg returns to default (not red). */
    CHECK("attr: reset fg differs from red", n.fg != r.fg);

    yetty_yscene_vtermgrid_dispose(grid);
}

static void test_wide_cell(void)
{
    struct yetty_yclass_object *grid = make_grid(4, 20);
    /* U+4E16 (世), a double-width CJK ideograph: UTF-8 E4 B8 96. */
    feed(grid, "\xE4\xB8\x96X");

    struct yetty_yscene_vtermgrid_cell head = cell_at(grid, 0, 0);
    struct yetty_yscene_vtermgrid_cell cont = cell_at(grid, 0, 1);
    struct yetty_yscene_vtermgrid_cell after = cell_at(grid, 0, 2);

    CHECK("wide: head glyph", head.glyph == 0x4E16u);
    CHECK("wide: head width 2", head.width == 2);
    CHECK("wide: continuation width 0", cont.width == 0);
    CHECK("wide: next char after wide", after.glyph == 'X');

    yetty_yscene_vtermgrid_dispose(grid);
}

static void test_resize_and_reset(void)
{
    struct yetty_yclass_object *grid = make_grid(24, 80);
    feed(grid, "top-left");

    struct yetty_ycore_void_result rz = yetty_yscene_vtermgrid_resize(grid, 30, 100);
    CHECK("resize: ok", YETTY_IS_OK(rz));
    if (YETTY_IS_ERR(rz)) {
        yetty_ycore_error_destroy(rz.error);
    }
    uint32_t rows = 0, cols = 0;
    yetty_yscene_vtermgrid_dims(grid, &rows, &cols);
    CHECK("resize: rows", rows == 30);
    CHECK("resize: cols", cols == 100);
    /* Content survives a grow (row 0 unchanged). */
    CHECK("resize: content kept", cell_at(grid, 0, 0).glyph == 't');

    struct yetty_ycore_void_result rst = yetty_yscene_vtermgrid_reset(grid, 12, 50);
    CHECK("reset: ok", YETTY_IS_OK(rst));
    if (YETTY_IS_ERR(rst)) {
        yetty_ycore_error_destroy(rst.error);
    }
    yetty_yscene_vtermgrid_dims(grid, &rows, &cols);
    CHECK("reset: rows", rows == 12);
    CHECK("reset: cols", cols == 50);
    /* After reset the grid is blank. */
    CHECK("reset: blank", cell_at(grid, 0, 0).glyph == 0);

    yetty_yscene_vtermgrid_dispose(grid);
}

/* #699: "Fragment every terminal/UTF-8/control sequence at every byte boundary
 * ... assert the same final grid state." Feed the same stream in one shot and
 * one-byte-at-a-time; the final glyph grids must be identical. */
static void test_fragmentation_invariance(void)
{
    static const uint32_t ROWS = 8, COLS = 40;
    /* A stream mixing ASCII, an SGR sequence, CR/LF, and a multibyte glyph. */
    const char *stream = "Line1\r\n\x1b[32mgreen\x1b[0m \xE4\xB8\x96 end\r\nLine3";

    struct yetty_yclass_object *whole = make_grid(ROWS, COLS);
    feed(whole, stream);
    uint32_t *ref = calloc((size_t)ROWS * COLS, sizeof(uint32_t));
    snapshot_glyphs(whole, ROWS, COLS, ref);

    struct yetty_yclass_object *split = make_grid(ROWS, COLS);
    for (const char *p = stream; *p; ++p) {
        uint8_t byte = (uint8_t)*p;
        feed_bytes(split, &byte, 1);
    }
    uint32_t *got = calloc((size_t)ROWS * COLS, sizeof(uint32_t));
    snapshot_glyphs(split, ROWS, COLS, got);

    int identical = memcmp(ref, got, (size_t)ROWS * COLS * sizeof(uint32_t)) == 0;
    CHECK("fragmentation: byte-split grid == whole grid", identical);

    free(ref);
    free(got);
    yetty_yscene_vtermgrid_dispose(whole);
    yetty_yscene_vtermgrid_dispose(split);
}

/* Review #13: COMPLETE-state fragmentation invariance — every byte boundary
 * must yield the identical full grid state: glyph, fg, bg, attrs, width,
 * cursor (pos/visibility), and the alternate-screen flag, across vectors
 * covering SGR color runs, altscreen round trips, DECSCA/selective erase,
 * scrolling, and wide + combining text. */
struct grid_full_state {
    struct yetty_yscene_vtermgrid_cell cells[6 * 20];
    uint32_t cursor_row, cursor_col;
    int cursor_visible;
    int on_alt_screen;
    uint32_t mode_flags;    /* blink | reverse | cursor shape */
    uint32_t reply_pending; /* untaken terminal-reply bytes */
};

static void snapshot_full_state(struct yetty_yclass_object *grid, struct grid_full_state *out)
{
    memset(out, 0, sizeof(*out));
    for (uint32_t r = 0; r < 6; ++r) {
        for (uint32_t c = 0; c < 20; ++c) {
            out->cells[r * 20 + c] = cell_at(grid, r, c);
        }
    }
    struct yetty_ycore_void_result cres = yetty_yscene_vtermgrid_cursor(
        grid, &out->cursor_row, &out->cursor_col, &out->cursor_visible);
    if (YETTY_IS_ERR(cres)) {
        yetty_ycore_error_destroy(cres.error);
    }
    struct yetty_ycore_int_result alt_res = yetty_yscene_vtermgrid_on_alt_screen(grid);
    out->on_alt_screen = YETTY_IS_OK(alt_res) ? alt_res.value : -1;
    struct yetty_ycore_uint32_result flags_res = yetty_yscene_vtermgrid_mode_flags(grid);
    out->mode_flags = YETTY_IS_OK(flags_res) ? flags_res.value : 0xFFFFFFFFu;
    struct yetty_ycore_uint32_result reply_res = yetty_yscene_vtermgrid_reply_pending(grid);
    out->reply_pending = YETTY_IS_OK(reply_res) ? reply_res.value : 0xFFFFFFFFu;
}

static void test_full_state_fragmentation(void)
{
    static const char *vectors[] = {
        "\x1b[31mred\x1b[44mblue-bg\x1b[0m plain",
        "pri\x1b[?1049halt-\x1b[31mscreen\x1b[?1049l-back",
        "pri\x1b[?1049hSTILL-IN-ALT\x1b[31mred", /* ENDS inside the alt screen */
        "\x1b[1\"qP\x1b[0\"qU\x1b[1;1H\x1b[?K",
        "l1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\nl7\r\nl8",
        "wide:\xe6\xbc\xa2 combo:e\xcc\x81 tail",
        /* ENDS with modes ACTIVE (review #14): hidden blinking-bar cursor,
         * autowrap off, insert mode on — continuation exercises them. */
        "mode\x1b[?25l\x1b[5 q\x1b[?7l\x1b[4hX",
        /* ENDS in reverse video (DECSCNM) + a scroll region + origin mode. */
        "\x1b[?5m\x1b[2;5r\x1b[?6mtop",
        /* ENDS with an untaken DSR reply pending (cursor-position report). */
        "que\x1b[6n",
        /* ENDS mid-SGR pen state (bold red, never reset). */
        "pen\x1b[1;31mhot",
    };
    static const size_t chunks[] = {1, 2, 3, 7};
    for (size_t v = 0; v < sizeof(vectors) / sizeof(vectors[0]); ++v) {
        const char *vector = vectors[v];
        size_t len = strlen(vector);

        /* The continuation tail leans on carried state: wraps/inserts under
         * the active modes, colours under the live pen, movement under any
         * region/origin, and a second query on top of a pending reply. */
        static const char continuation[] = "-tail \x1b[32mgreen\x1b[6nZ";
        const size_t continuation_len = sizeof(continuation) - 1;
        struct yetty_yclass_object *reference = make_grid(6, 20);
        feed(reference, vector);
        struct grid_full_state expected;
        snapshot_full_state(reference, &expected);
        for (size_t off = 0; off < continuation_len; ++off) {
            struct yetty_ycore_void_result ref_cont_res =
                yetty_yscene_vtermgrid_write(reference, (const uint8_t *)continuation + off, 1);
            if (YETTY_IS_ERR(ref_cont_res)) {
                yetty_ycore_error_destroy(ref_cont_res.error);
            }
        }
        struct grid_full_state continued;
        snapshot_full_state(reference, &continued);
        yetty_yscene_vtermgrid_dispose(reference);

        for (size_t ci = 0; ci < sizeof(chunks) / sizeof(chunks[0]); ++ci) {
            struct yetty_yclass_object *grid = make_grid(6, 20);
            for (size_t off = 0; off < len; off += chunks[ci]) {
                size_t take = len - off < chunks[ci] ? len - off : chunks[ci];
                struct yetty_ycore_void_result wres =
                    yetty_yscene_vtermgrid_write(grid, (const uint8_t *)vector + off, take);
                if (YETTY_IS_ERR(wres)) {
                    yetty_ycore_error_destroy(wres.error);
                }
            }
            struct grid_full_state actual;
            snapshot_full_state(grid, &actual);
            char label[96];
            snprintf(label, sizeof(label), "full-state v%zu chunk %zu identical", v, chunks[ci]);
            CHECK(label, memcmp(&expected, &actual, sizeof(expected)) == 0);
            /* CONTINUATION (review #14): parser/pen/mode state carried across
             * the splits must keep producing identical results — feed a tail
             * that depends on the carried state and re-compare. */
            for (size_t off = 0; off < continuation_len; off += chunks[ci]) {
                size_t take =
                    continuation_len - off < chunks[ci] ? continuation_len - off : chunks[ci];
                struct yetty_ycore_void_result cont_res =
                    yetty_yscene_vtermgrid_write(grid, (const uint8_t *)continuation + off, take);
                if (YETTY_IS_ERR(cont_res)) {
                    yetty_ycore_error_destroy(cont_res.error);
                }
            }
            snapshot_full_state(grid, &actual);
            snprintf(label, sizeof(label), "continued v%zu chunk %zu identical", v, chunks[ci]);
            CHECK(label, memcmp(&continued, &actual, sizeof(continued)) == 0);
            yetty_yscene_vtermgrid_dispose(grid);
        }
    }
}

/* Review #14: the grid ACCUMULATES terminal replies (it no longer only
 * discards them) so the embedder can route them through attachment input.
 * A DSR (\e[6n) fed to the grid produces a cursor-position report the take
 * API returns. */
static void test_reply_accumulation(void)
{
    struct yetty_yclass_object *grid = make_grid(6, 20);
    feed(grid, "abc");     /* cursor now at col 3 */
    feed(grid, "\x1b[6n"); /* Device Status Report — cursor position */

    uint8_t replies[64];
    struct yetty_ycore_uint32_result take_res =
        yetty_yscene_vtermgrid_take_replies(grid, replies, sizeof(replies));
    CHECK("take ok", YETTY_IS_OK(take_res));
    uint32_t len = YETTY_IS_OK(take_res) ? take_res.value : 0;
    CHECK("reply captured (CPR)", len >= 6 && replies[0] == 0x1b && replies[1] == '[');
    /* A second take drains to empty. */
    struct yetty_ycore_uint32_result again =
        yetty_yscene_vtermgrid_take_replies(grid, replies, sizeof(replies));
    CHECK("second take empty", YETTY_IS_OK(again) && again.value == 0);
    yetty_yscene_vtermgrid_dispose(grid);
}

int main(void)
{
    test_basic_text();
    test_attrs_and_color();
    test_wide_cell();
    test_resize_and_reset();
    test_fragmentation_invariance();
    test_full_state_fragmentation();
    test_reply_accumulation();

    fprintf(stderr, "\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
