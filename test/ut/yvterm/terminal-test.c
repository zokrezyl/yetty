/*
 * yvterm terminal-core contract test — headless, no process fork, no GPU.
 *
 * Drives the CPU terminal model (yetty_yvterm_grid, libvterm-backed) directly
 * and through the in-process memory-PTY seam, then asserts on the resulting
 * cell grid / cursor / scrollback / alt-screen state. Also exercises the wire
 * statemachine's split of a byte stream into terminal text vs DCS/OSC figure
 * envelopes.
 *
 * Every failure names the input escape stream and the expected terminal state.
 * Mouse-report encoding and visual selection are intentionally out of scope —
 * they live in the vterm figure / UI layer, not this pure grid model.
 */

#include <yetty/yplatform/pty.h>
#include <yetty/yvterm/grid.h>
#include <yetty/ywire/wire-statemachine.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct yetty_yclass_object *make_grid(struct ytest *test, uint32_t cols, uint32_t rows,
                                             uint32_t scrollback)
{
    struct yetty_yclass_object_ptr_result r =
        yetty_yvterm_grid_make(cols, rows, scrollback, /*hot_rows=*/0);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void feed(struct ytest *test, struct yetty_yclass_object *grid, const char *bytes)
{
    struct yetty_ycore_void_result r = yetty_yvterm_grid_feed(grid, bytes, strlen(bytes));
    YTEST_REQUIRE_OK(test, r);
}

/* Codepoint at visible (row, col). */
static uint32_t cp_at(struct ytest *test, struct yetty_yclass_object *grid, uint32_t row,
                      uint32_t col)
{
    struct yetty_yvterm_text_cell_const_ptr_result cells = yetty_yvterm_grid_line_cells(grid, row);
    YTEST_REQUIRE_OK(test, cells);
    YTEST_REQUIRE_NOT_NULL(test, cells.value);
    return cells.value[col].codepoint;
}

static const struct yetty_yvterm_text_cell *cell_at(struct ytest *test,
                                                    struct yetty_yclass_object *grid, uint32_t row,
                                                    uint32_t col)
{
    struct yetty_yvterm_text_cell_const_ptr_result cells = yetty_yvterm_grid_line_cells(grid, row);
    YTEST_REQUIRE_OK(test, cells);
    YTEST_REQUIRE_NOT_NULL(test, cells.value);
    return &cells.value[col];
}

/*---------------------------------------------------------------------------
 * Byte ingestion.
 *-------------------------------------------------------------------------*/
static void test_byte_ingestion(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    feed(test, grid, "Hi!");
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'H');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 1), 'i');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 2), '!');

    uint32_t row = 99, col = 99, vis = 0;
    struct yetty_ycore_void_result cur = yetty_yvterm_grid_cursor(grid, &row, &col, &vis);
    YTEST_REQUIRE_OK(test, cur);
    YTEST_CHECK_EQ_SIZE(test, row, 0);
    YTEST_CHECK_EQ_SIZE(test, col, 3);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Memory-PTY seam: program output written to endpoint A is read from endpoint
 * B and fed to the terminal — no fd, no fork.
 *-------------------------------------------------------------------------*/
static void test_memory_pty_transport(struct ytest *test)
{
    struct yetty_yplatform_memory_pty_pair_result pair_res =
        yetty_yplatform_memory_pty_pair_create(0);
    YTEST_REQUIRE_OK(test, pair_res);
    struct yetty_platform_pty *master = pair_res.value.a;
    struct yetty_platform_pty *slave = pair_res.value.b;
    YTEST_REQUIRE_NOT_NULL(test, master);
    YTEST_REQUIRE_NOT_NULL(test, slave);

    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);

    const char *out = "PTY";
    struct yetty_ycore_size_result wrote = master->ops->write(master, out, strlen(out));
    YTEST_REQUIRE_OK(test, wrote);
    YTEST_CHECK_EQ_SIZE(test, wrote.value, strlen(out));

    char buf[64];
    struct yetty_ycore_size_result got = slave->ops->read(slave, buf, sizeof(buf));
    YTEST_REQUIRE_OK(test, got);
    YTEST_REQUIRE_EQ_SIZE(test, got.value, strlen(out));
    struct yetty_ycore_void_result fed = yetty_yvterm_grid_feed(grid, buf, got.value);
    YTEST_REQUIRE_OK(test, fed);

    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'P');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 1), 'T');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 2), 'Y');

    yetty_yvterm_grid_dispose(grid);
    master->ops->destroy(master);
    slave->ops->destroy(slave);
}

/*---------------------------------------------------------------------------
 * ANSI SGR: attributes toggle and reset; foreground changes and resets.
 *-------------------------------------------------------------------------*/
static void test_ansi_attrs_and_color(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    /* A(default) B(red) C(reset) D(bold) E(reset) F(underline) */
    feed(test, grid, "A\033[31mB\033[0mC\033[1mD\033[0mE\033[4mF");

    uint32_t default_fg = cell_at(test, grid, 0, 0)->fg; /* 'A' */
    /* Red 'B' differs from default; reset 'C' returns to default. */
    YTEST_CHECK(test, cell_at(test, grid, 0, 1)->fg != default_fg);
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 0, 2)->fg, default_fg);
    /* Bold 'D', not-bold 'E', underline 'F'. */
    YTEST_CHECK(test, cell_at(test, grid, 0, 3)->attrs & YETTY_YVTERM_ATTR_BOLD);
    YTEST_CHECK(test, !(cell_at(test, grid, 0, 4)->attrs & YETTY_YVTERM_ATTR_BOLD));
    YTEST_CHECK(test, cell_at(test, grid, 0, 5)->attrs & YETTY_YVTERM_ATTR_UNDERLINE);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Configurable colours: the 16-entry ANSI palette and the default fg/bg
 * installed via the grid setters (the terminal/colors config path) must be
 * what indexed SGR colours and blank cells resolve to.
 *-------------------------------------------------------------------------*/
static void test_palette_and_default_colors(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);

    /* Web-style 0xRRGGBB in; cells store ABGR-packed (red in the low byte). */
    struct yetty_ycore_void_result palette_res =
        yetty_yvterm_grid_set_palette_color(grid, 1 /* red */, 0x112233);
    YTEST_REQUIRE_OK(test, palette_res);
    struct yetty_ycore_void_result defaults_res =
        yetty_yvterm_grid_set_default_colors(grid, 0xF0F0F0, 0x000000);
    YTEST_REQUIRE_OK(test, defaults_res);

    /* Blank cells already carry the new default colours. */
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 0, 0)->fg, 0xFFF0F0F0u);
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 0, 0)->bg, 0xFF000000u);

    /* A(default fg) B(SGR 31 → palette slot 1) C(reset → default fg) */
    feed(test, grid, "A\033[31mB\033[0mC");
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 0, 0)->fg, 0xFFF0F0F0u);
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 0, 1)->fg, 0xFF332211u);
    YTEST_CHECK_EQ_SIZE(test, cell_at(test, grid, 0, 2)->fg, 0xFFF0F0F0u);

    /* Out-of-range palette index is rejected. */
    struct yetty_ycore_void_result bad_res = yetty_yvterm_grid_set_palette_color(grid, 16, 0);
    YTEST_CHECK(test, YETTY_IS_ERR(bad_res));
    if (YETTY_IS_ERR(bad_res)) {
        yetty_ycore_error_destroy(bad_res.error);
    }

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Cursor movement: CUP, print, CR, LF, CUU, CUF.
 *-------------------------------------------------------------------------*/
static void test_cursor_movement(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    uint32_t row, col, vis;

    feed(test, grid, "\033[3;5H"); /* CUP row 3 col 5 (1-based) → 0-based (2,4) */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, row, 2);
    YTEST_CHECK_EQ_SIZE(test, col, 4);

    feed(test, grid, "X"); /* advances col */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 5);

    feed(test, grid, "\r"); /* CR → col 0 */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 0);

    feed(test, grid, "\n"); /* LF → row + 1 */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, row, 3);

    feed(test, grid, "\033[2A"); /* CUU 2 → row 1 */
    feed(test, grid, "\033[6C"); /* CUF 6 → col 6 */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, row, 1);
    YTEST_CHECK_EQ_SIZE(test, col, 6);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Unicode width: cursor advance must match the pinned modern Unicode
 * version (regenerated combining.inc / fullwidth.inc), not the historical
 * Unicode 5.0 tables — post-5.0 wide codepoints and combining marks are
 * the cases that regressed before the regeneration.
 *-------------------------------------------------------------------------*/
static void test_unicode_widths(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    uint32_t row, col, vis;

    feed(test, grid, "\rA"); /* ASCII stays narrow */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 1);

    feed(test, grid, "\r\xEF\xBC\xA1"); /* U+FF21 fullwidth A — classic wide */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 2);

    feed(test, grid, "\r\xF0\x9F\x98\x80"); /* U+1F600 grinning face (Unicode 8) */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 2);

    feed(test, grid, "\r\xF0\x9F\xAA\xB2"); /* U+1FAB2 beetle (Unicode 13) */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 2);

    feed(test, grid, "\r\xF0\xB0\x80\x80"); /* U+30000 CJK extension G (Unicode 13) */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 2);

    feed(test, grid, "\rA\xCC\x80"); /* A + U+0300 combining grave — no advance */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 1);

    feed(test, grid, "\rA\xF0\x9E\xA5\x84"); /* A + U+1E944 Adlam combining (Unicode 9) */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 1);

    feed(test, grid, "\rA\xE1\x85\xA0"); /* A + U+1160 hangul jungseong filler — zero width */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 1);

    /* U+0B95 tamil KA + U+0BBE spacing vowel sign (Mc) — the spacing mark
     * advances one cell so the cluster measures 2, matching the reference. */
    feed(test, grid, "\r\xE0\xAE\x95\xE0\xAE\xBE");
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_cursor(grid, &row, &col, &vis));
    YTEST_CHECK_EQ_SIZE(test, col, 2);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Scrollback: printing past the last row scrolls content off the top.
 *-------------------------------------------------------------------------*/
static void test_scrollback(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 5, 100);

    struct yetty_ycore_uint64_result before = yetty_yvterm_grid_scroll_origin(grid);
    YTEST_REQUIRE_OK(test, before);
    YTEST_CHECK_EQ_SIZE(test, before.value, 0);

    /* 12 lines into a 5-row screen forces the top lines into scrollback. */
    feed(test, grid, "L0\nL1\nL2\nL3\nL4\nL5\nL6\nL7\nL8\nL9\nL10\nL11");

    struct yetty_ycore_uint64_result after = yetty_yvterm_grid_scroll_origin(grid);
    YTEST_REQUIRE_OK(test, after);
    YTEST_CHECK(test, after.value > 0); /* content scrolled off the top */

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Alt screen. Entering (?1049h) sets the alt-screen mode and clears the visible
 * surface; content written there lands on it. This smoke test covers entry
 * only; the full enter/scroll/exit-restore contract (primary contents and
 * cursor come back on ?1049l, alt scrolls stay out of the primary scrollback)
 * lives in state-matrix-test.c (test_alt_screen_restore).
 *-------------------------------------------------------------------------*/
static void test_alt_screen(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    feed(test, grid, "primary");
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'p');

    /* Entering the (cleared) alt screen hides the primary content at row 0. */
    feed(test, grid, "\033[?1049h");
    YTEST_CHECK(test, cp_at(test, grid, 0, 0) != 'p');

    /* Home the cursor, then write on the alt surface. */
    feed(test, grid,
         "\033[H"
         "ALT");
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'A');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 1), 'L');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 2), 'T');

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Resize changes the reported grid dimensions.
 *-------------------------------------------------------------------------*/
static void test_resize(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    feed(test, grid, "before resize");

    struct yetty_ycore_void_result rz = yetty_yvterm_grid_resize(grid, 40, 10);
    YTEST_REQUIRE_OK(test, rz);

    uint32_t cols = 0, rows = 0, base = 0;
    struct yetty_ycore_void_result dims = yetty_yvterm_grid_dims(grid, &cols, &rows, &base);
    YTEST_REQUIRE_OK(test, dims);
    YTEST_CHECK_EQ_SIZE(test, cols, 40);
    YTEST_CHECK_EQ_SIZE(test, rows, 10);

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * Malformed escapes are absorbed; following text still lands.
 *-------------------------------------------------------------------------*/
static void test_malformed_recovery(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    /* Bogus CSI params, an unknown APC string, then real text. */
    feed(test, grid, "\033[38;5;999m\033_unknown apc\033\\\033[qOK");
    /* The parser resynced and printed "OK". */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'O');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 1), 'K');

    yetty_yvterm_grid_dispose(grid);
}

/*---------------------------------------------------------------------------
 * DCS/OSC routing: the wire statemachine splits a stream into terminal text
 * (→ libvterm grid) and yetty DCS/OSC envelopes (→ figure handlers).
 *-------------------------------------------------------------------------*/
struct route_cap {
    int calls;
    int code;
    size_t payload_len;
    int payload_ok;
};

static struct yetty_ycore_void_result on_envelope(void *userdata,
                                                  enum yetty_ywire_envelope_kind kind, int code,
                                                  const uint8_t *args, size_t args_len,
                                                  const uint8_t *payload, size_t payload_len)
{
    (void)kind;
    (void)args;
    (void)args_len;
    struct route_cap *cap = userdata;
    cap->calls++;
    cap->code = code;
    cap->payload_len = payload_len;
    cap->payload_ok = payload_len == 3 && payload && memcmp(payload, "fig", 3) == 0;
    return YETTY_OK_VOID();
}

static void test_dcs_osc_routing(struct ytest *test)
{
    struct yetty_yclass_object *grid = make_grid(test, 80, 24, 100);
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm_res);
    struct yetty_ywire_wire_statemachine *sm = sm_res.value;

    /* Grid becomes the raw-text sink for the SM. */
    YTEST_REQUIRE_OK(test, yetty_yvterm_grid_register_wire(grid, sm));

    struct route_cap cap = {0};
    YTEST_REQUIRE_OK(
        test, yetty_ywire_wire_statemachine_register_buffered(sm, YETTY_YWIRE_ENVELOPE_DCS, 777,
                                                              /*has_args=*/0, on_envelope, &cap));

    /* Build: raw "AB" + DCS(777,"fig") envelope + raw "CD". */
    const char *pre = "AB";
    struct yetty_ycore_void_result e1 = yetty_ywire_wire_statemachine_feed(sm, pre, strlen(pre));
    YTEST_REQUIRE_OK(test, e1);
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, 777, 0, 0, NULL, 0, "fig", 3, &env);
    YTEST_REQUIRE_OK(test, emit);
    struct yetty_ycore_void_result e2 =
        yetty_ywire_wire_statemachine_feed(sm, (const char *)env.data, env.size);
    YTEST_REQUIRE_OK(test, e2);
    const char *post = "CD";
    struct yetty_ycore_void_result e3 = yetty_ywire_wire_statemachine_feed(sm, post, strlen(post));
    YTEST_REQUIRE_OK(test, e3);
    struct yetty_ycore_void_result proc = yetty_ywire_wire_statemachine_process(sm);
    YTEST_REQUIRE_OK(test, proc);

    /* Envelope reached the figure handler. */
    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_INT(test, cap.code, 777);
    YTEST_CHECK(test, cap.payload_ok);

    /* Raw text reached the terminal grid: "ABCD" on row 0. */
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 0), 'A');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 1), 'B');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 2), 'C');
    YTEST_CHECK_EQ_INT(test, cp_at(test, grid, 0, 3), 'D');

    yetty_ycore_buffer_destroy(&env);
    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_yvterm_grid_dispose(grid);
}

int main(void)
{
    struct ytest test = ytest_begin("yvterm_terminal");
    YTEST_RUN(&test, test_byte_ingestion);
    YTEST_RUN(&test, test_memory_pty_transport);
    YTEST_RUN(&test, test_ansi_attrs_and_color);
    YTEST_RUN(&test, test_palette_and_default_colors);
    YTEST_RUN(&test, test_cursor_movement);
    YTEST_RUN(&test, test_unicode_widths);
    YTEST_RUN(&test, test_scrollback);
    YTEST_RUN(&test, test_alt_screen);
    YTEST_RUN(&test, test_resize);
    YTEST_RUN(&test, test_malformed_recovery);
    YTEST_RUN(&test, test_dcs_osc_routing);
    return ytest_end(&test);
}
