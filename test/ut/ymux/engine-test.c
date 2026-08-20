/*
 * ymux engine contract test (#695 phase 1) — headless, no fork, no GPU, no
 * yvterm. Drives the independent daemon-side terminal engine through its
 * generated API and asserts on semantic state, host-callback output, stable
 * row identity, and golden snapshots with chunk-partition invariance.
 *
 * This executable's link line is also the phase-1 closure proof: it links
 * the ymux module + libvterm + core libs only — no Dawn, no renderer, no
 * yfigure, no yvterm.
 */

#include <yetty/api/ymux/engine.h>
#include <yetty/ycore/types.h>

/* Module-private op stream (#699.1): the canonical screen-write operations
 * the engine records for the op-driven renderer. */
#include "../../../src/yetty/ymux/op-stream.h"

#include "ytest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Host capture: ordered log of output/clipboard/bell/title/scroll_out.
 *-------------------------------------------------------------------------*/
struct capture {
    char log[16384];
    size_t log_len;
    char output[4096]; /* raw PTY bytes, coalesced */
    size_t output_len;
    int scroll_out_count;
    uint64_t last_scroll_out_id;
};

static void capture_text(struct capture *cap, const char *text, size_t len)
{
    if (cap->log_len + len < sizeof(cap->log) - 1) {
        memcpy(cap->log + cap->log_len, text, len);
        cap->log_len += len;
        cap->log[cap->log_len] = 0;
    }
}

static void capture_flush_output(struct capture *cap)
{
    if (!cap->output_len) {
        return;
    }
    capture_text(cap, "out ", 4);
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < cap->output_len; ++i) {
        char hex[2] = {digits[((unsigned char)cap->output[i]) >> 4],
                       digits[((unsigned char)cap->output[i]) & 0xF]};
        capture_text(cap, hex, 2);
    }
    capture_text(cap, "\n", 1);
    cap->output_len = 0;
}

static struct yetty_ycore_void_result capture_output(const char *bytes, size_t len, void *userdata)
{
    struct capture *cap = userdata;
    for (size_t i = 0; i < len && cap->output_len < sizeof(cap->output); ++i) {
        cap->output[cap->output_len++] = bytes[i];
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result capture_clipboard(const char *text, size_t len, int clipboard,
                                                        void *userdata)
{
    struct capture *cap = userdata;
    capture_flush_output(cap);
    char head[32];
    int wrote = snprintf(head, sizeof(head), "clip flag=%d ", clipboard);
    capture_text(cap, head, (size_t)(wrote > 0 ? wrote : 0));
    capture_text(cap, text, len);
    capture_text(cap, "\n", 1);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result capture_bell(void *userdata)
{
    struct capture *cap = userdata;
    capture_flush_output(cap);
    capture_text(cap, "bell\n", 5);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result capture_title(const char *title, size_t len, void *userdata)
{
    struct capture *cap = userdata;
    capture_flush_output(cap);
    capture_text(cap, "title ", 6);
    capture_text(cap, title, len);
    capture_text(cap, "\n", 1);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result capture_scroll_out(const struct yetty_ymux_cell *cells,
                                                         uint32_t cols, uint64_t logical_line_id,
                                                         uint32_t logical_cell_start,
                                                         int continuation, void *userdata)
{
    struct capture *cap = userdata;
    capture_flush_output(cap);
    cap->scroll_out_count++;
    cap->last_scroll_out_id = logical_line_id;
    char head[96];
    int wrote = snprintf(head, sizeof(head), "scroll_out id=%llu start=%u cont=%d text=",
                         (unsigned long long)logical_line_id, logical_cell_start, continuation);
    capture_text(cap, head, (size_t)(wrote > 0 ? wrote : 0));
    for (uint32_t col = 0; col < cols; ++col) {
        uint32_t codepoint = cells[col].codepoint;
        char ch = (codepoint >= 0x20 && codepoint < 0x7F) ? (char)codepoint : '.';
        capture_text(cap, &ch, 1);
    }
    /* Trim is not needed for the log; rows are narrow in these tests. */
    capture_text(cap, "\n", 1);
    return YETTY_OK_VOID();
}

static struct yetty_ymux_engine_host capture_host(struct capture *cap)
{
    struct yetty_ymux_engine_host host = {
        .output = capture_output,
        .clipboard = capture_clipboard,
        .bell = capture_bell,
        .title = capture_title,
        .scroll_out = capture_scroll_out,
        .userdata = cap,
    };
    return host;
}

static struct yetty_yclass_object *make_engine(struct ytest *test, uint32_t rows, uint32_t cols,
                                               struct capture *cap)
{
    struct yetty_yclass_object_ptr_result engine_res =
        yetty_ymux_engine_make(rows, cols, cap ? &(struct yetty_ymux_engine_host){0} : NULL);
    /* Re-make with the real host when capturing (compound literal above only
     * proves NULL-host support). */
    if (cap) {
        if (YETTY_IS_OK(engine_res)) {
            yetty_ymux_engine_dispose(engine_res.value);
        } else {
            yetty_ycore_error_destroy(engine_res.error);
        }
        struct yetty_ymux_engine_host host = capture_host(cap);
        engine_res = yetty_ymux_engine_make(rows, cols, &host);
    }
    YTEST_REQUIRE_OK(test, engine_res);
    return engine_res.value;
}

static void feed(struct ytest *test, struct yetty_yclass_object *engine, const char *bytes)
{
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_feed(engine, bytes, strlen(bytes)));
}

static const struct yetty_ymux_cell *cell_at(struct ytest *test, struct yetty_yclass_object *engine,
                                             uint32_t row, uint32_t col)
{
    struct yetty_ymux_cell_const_ptr_result cells_res = yetty_ymux_engine_row_cells(engine, row);
    YTEST_REQUIRE_OK(test, cells_res);
    YTEST_REQUIRE_NOT_NULL(test, cells_res.value);
    return &cells_res.value[col];
}

static uint64_t row_id(struct ytest *test, struct yetty_yclass_object *engine, uint32_t row)
{
    uint64_t logical_line_id = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_engine_row_identity(engine, row, &logical_line_id, NULL, NULL));
    return logical_line_id;
}

/*---------------------------------------------------------------------------
 * Basic ingestion, cursor, attributes.
 *-------------------------------------------------------------------------*/
static void test_basic_ingestion(struct ytest *test)
{
    struct yetty_yclass_object *engine = make_engine(test, 6, 20, NULL);
    feed(test, engine, "Hi!\r\n\x1b[1;4mB\x1b[0mN");
    YTEST_CHECK_EQ_INT(test, cell_at(test, engine, 0, 0)->codepoint, 'H');
    YTEST_CHECK_EQ_INT(test, cell_at(test, engine, 0, 2)->codepoint, '!');
    const struct yetty_ymux_cell *bold = cell_at(test, engine, 1, 0);
    YTEST_CHECK_EQ_INT(test, bold->codepoint, 'B');
    YTEST_CHECK(test, bold->attrs & YETTY_YMUX_ATTR_BOLD);
    YTEST_CHECK(test, bold->attrs & YETTY_YMUX_ATTR_UNDERLINE);
    const struct yetty_ymux_cell *normal = cell_at(test, engine, 1, 1);
    YTEST_CHECK_EQ_INT(test, normal->codepoint, 'N');
    YTEST_CHECK_EQ_INT(test, normal->attrs, 0);
    uint32_t row = 0, col = 0;
    int visible = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_cursor(engine, &row, &col, &visible));
    YTEST_CHECK_EQ_INT(test, row, 1);
    YTEST_CHECK_EQ_INT(test, col, 2);
    YTEST_CHECK_EQ_INT(test, visible, 1);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * Wide + combining clusters.
 *-------------------------------------------------------------------------*/
static void test_wide_combining(struct ytest *test)
{
    struct yetty_yclass_object *engine = make_engine(test, 4, 20, NULL);
    feed(test, engine,
         "\xE4\xB8\xAD"
         "A\xCC\x80\xCC\x81");
    const struct yetty_ymux_cell *wide = cell_at(test, engine, 0, 0);
    YTEST_CHECK_EQ_INT(test, wide->codepoint, 0x4E2D);
    YTEST_CHECK_EQ_INT(test, wide->width, 2);
    YTEST_CHECK_EQ_INT(test, cell_at(test, engine, 0, 1)->width, 0); /* spill */
    const struct yetty_ymux_cell *marked = cell_at(test, engine, 0, 2);
    YTEST_CHECK_EQ_INT(test, marked->codepoint, 'A');
    YTEST_CHECK_EQ_INT(test, marked->mark_count, 2);
    YTEST_CHECK_EQ_INT(test, marked->marks[0], 0x300);
    YTEST_CHECK_EQ_INT(test, marked->marks[1], 0x301);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * Alt screen enter/restore; primary content survives.
 *-------------------------------------------------------------------------*/
static void test_alt_screen(struct ytest *test)
{
    struct yetty_yclass_object *engine = make_engine(test, 4, 20, NULL);
    feed(test, engine, "primary");
    uint64_t primary_row0 = row_id(test, engine, 0);
    feed(test, engine, "\x1b[?1049h\x1b[HALT");
    YTEST_CHECK_EQ_INT(test, yetty_ymux_engine_alt_active(engine).value, 1);
    YTEST_CHECK_EQ_INT(test, cell_at(test, engine, 0, 0)->codepoint, 'A');
    feed(test, engine, "\x1b[?1049l");
    YTEST_CHECK_EQ_INT(test, yetty_ymux_engine_alt_active(engine).value, 0);
    YTEST_CHECK_EQ_INT(test, cell_at(test, engine, 0, 0)->codepoint, 'p');
    YTEST_CHECK(test, row_id(test, engine, 0) == primary_row0);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * Identity: scroll rotation keeps ids on surviving rows, streams the top
 * row to the history intake, and mints for vacated rows. Autowrap joins the
 * head row's logical line.
 *-------------------------------------------------------------------------*/
static void test_identity_scroll_and_wrap(struct ytest *test)
{
    struct capture cap = {0};
    struct yetty_yclass_object *engine = make_engine(test, 3, 10, &cap);
    feed(test, engine, "AAA\r\nBBB\r\nCCC");
    uint64_t id_row0 = row_id(test, engine, 0);
    uint64_t id_row1 = row_id(test, engine, 1);
    uint64_t id_row2 = row_id(test, engine, 2);

    feed(test, engine, "\r\nDDD"); /* scrolls AAA out */
    YTEST_CHECK_EQ_INT(test, cap.scroll_out_count, 1);
    YTEST_CHECK(test, cap.last_scroll_out_id == id_row0);
    YTEST_CHECK(test, row_id(test, engine, 0) == id_row1);
    YTEST_CHECK(test, row_id(test, engine, 1) == id_row2);
    /* The vacated bottom row minted a fresh id. */
    YTEST_CHECK(test, row_id(test, engine, 2) != id_row0);
    YTEST_CHECK(test, row_id(test, engine, 2) != id_row1);
    YTEST_CHECK(test, row_id(test, engine, 2) != id_row2);

    /* Autowrap: the continuation row joins the head's logical line. */
    feed(test, engine, "\x1b[2J\x1b[H");
    uint64_t head_id = row_id(test, engine, 0);
    feed(test, engine, "0123456789WRAP");
    int continuation = 0;
    uint64_t wrap_id = 0;
    uint32_t wrap_start = 0;
    YTEST_REQUIRE_OK(
        test, yetty_ymux_engine_row_identity(engine, 1, &wrap_id, &wrap_start, &continuation));
    YTEST_CHECK_EQ_INT(test, continuation, 1);
    YTEST_CHECK(test, wrap_id == head_id);
    YTEST_CHECK_EQ_INT(test, wrap_start, 10);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * DA/DSR replies + OSC 10/11 report reach the host output.
 *-------------------------------------------------------------------------*/
static void test_query_replies(struct ytest *test)
{
    struct capture cap = {0};
    struct yetty_yclass_object *engine = make_engine(test, 4, 20, &cap);
    feed(test, engine, "\x1b[c\x1b[6n");
    capture_flush_output(&cap);
    /* DA reply starts ESC [ ? ... c ; DSR cursor report ESC [ 1 ; 1 R. */
    YTEST_CHECK(test, cap.log_len > 0);
    YTEST_CHECK(test, strstr(cap.log, "1B5B") != NULL);

    cap.log_len = 0;
    cap.log[0] = 0;
    feed(test, engine, "\x1b]10;?\x07");
    capture_flush_output(&cap);
    /* OSC 10 report: ESC ] 1 0 ; rgb : ... — 1B5D31303B7267623A hex head. */
    YTEST_CHECK(test, strstr(cap.log, "1B5D31303B7267623A") != NULL);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * OSC 52 clipboard decode reaches the host.
 *-------------------------------------------------------------------------*/
static void test_osc52_clipboard(struct ytest *test)
{
    struct capture cap = {0};
    struct yetty_yclass_object *engine = make_engine(test, 4, 20, &cap);
    /* base64("yank!") = eWFuayE= */
    feed(test, engine, "\x1b]52;c;eWFuayE=\x07");
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "clip flag=1 yank!") != NULL);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * Input encoding is mode-aware: cursor keys switch with DECCKM; mouse
 * reports only after the app enables a mouse mode; paste brackets only
 * under 2004.
 *-------------------------------------------------------------------------*/
static void test_input_encoding(struct ytest *test)
{
    struct capture cap = {0};
    struct yetty_yclass_object *engine = make_engine(test, 4, 20, &cap);

    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_UP, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B41") != NULL); /* ESC [ A */

    cap.log_len = 0;
    cap.log[0] = 0;
    feed(test, engine, "\x1b[?1h"); /* DECCKM: application cursor keys */
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_UP, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B4F41") != NULL); /* ESC O A */

    /* Cycle-22: the full cursor/nav set the bridge's tty-key decoder maps must
     * encode mode-aware under DECCKM — DOWN/LEFT/RIGHT/HOME become SS3, and the
     * `~` editing keys keep their CSI form (DECCKM does not affect those). This
     * is the encoding the bridge now routes arrows through (structured
     * INPUT_KEY) instead of forwarding pre-encoded CSI verbatim. */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_DOWN, 0));
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_HOME, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B4F42") != NULL); /* ESC O B (Down SS3) */
    YTEST_CHECK(test, strstr(cap.log, "1B4F48") != NULL); /* ESC O H (Home SS3) */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_DELETE, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B337E") != NULL); /* ESC [ 3 ~ (Delete) */

    /* cdx-3 #5: the extended grammar re-encodes end-to-end through libvterm —
     * the daemon half of the bridge's decode → structured → re-encode path.
     * F1-F4 emit SS3 P..S; F5-F12 emit CSI n~; a modifier parameter rides an
     * arrow (Ctrl-Up -> CSI 1;5A, unaffected by DECCKM); a Ctrl-modified letter
     * folds to its control byte. The F-key value maps arithmetically onto
     * VTERM_KEY_FUNCTION(1..12) — this pins that mapping against an off-by-one. */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_F1, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B4F50") != NULL); /* ESC O P (F1) */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_F5, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B31357E") != NULL); /* ESC [ 1 5 ~ (F5) */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_F12, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B32347E") != NULL); /* ESC [ 2 4 ~ (F12) */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_UP, YETTY_YMUX_MOD_CTRL));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B313B3541") != NULL); /* ESC [ 1 ; 5 A (Ctrl-Up) */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_char(engine, 'a', YETTY_YMUX_MOD_CTRL));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "01") != NULL); /* Ctrl-A -> control byte 0x01 */

    /* Normal key mode folds Ctrl+char to its C0 byte exactly as tmux
     * input_key_vt10x does — NOT the CSI-u form libvterm emits for i/j/m/[ and
     * the punctuation keys. These are the cases a Ctrl-A-only test misses. */
    struct {
        uint32_t codepoint;
        const char *hex; /* the whole emitted output, so CSI-u would not match */
    } fold_cases[] = {
        {'i', "09"}, /* Ctrl-I -> TAB (not \e[105;5u) */
        {'m', "0D"}, /* Ctrl-M -> CR */
        {'j', "0A"}, /* Ctrl-J -> LF */
        {'[', "1B"}, /* Ctrl-[ -> ESC */
        {'/', "1F"}, /* Ctrl-/ -> 0x1f */
        {'8', "7F"}, /* Ctrl-8 -> DEL */
        {' ', "00"}, /* Ctrl-Space -> NUL */
    };
    for (size_t index = 0; index < sizeof(fold_cases) / sizeof(fold_cases[0]); ++index) {
        cap.log_len = 0;
        cap.log[0] = 0;
        YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_char(engine, fold_cases[index].codepoint,
                                                            YETTY_YMUX_MOD_CTRL));
        capture_flush_output(&cap);
        /* The full emitted run is "out <hex>\n"; the fold produces exactly the C0
         * byte, so the CSI-u lead-in "1B5B" must be ABSENT. */
        YTEST_CHECK(test, strstr(cap.log, fold_cases[index].hex) != NULL);
        YTEST_CHECK(test, strstr(cap.log, "1B5B") == NULL); /* no CSI-u */
    }

    /* Back to NORMAL cursor mode (DECCKM reset): arrows return to CSI. */
    feed(test, engine, "\x1b[?1l");
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_key(engine, YETTY_YMUX_KEY_RIGHT, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B43") != NULL); /* ESC [ C (Right CSI) */
    /* Re-enable DECCKM for any later assertions that expect it. */
    feed(test, engine, "\x1b[?1h");

    /* No mouse mode: button events encode nothing. */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_mouse_button(engine, 1, 1, 0));
    capture_flush_output(&cap);
    YTEST_CHECK_EQ_SIZE(test, strlen(cap.log), 0);
    /* Release so the later press is a fresh button transition (the emulator
     * tracks button state even while unreported). */
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_mouse_button(engine, 1, 0, 0));

    /* SGR mouse mode on: press encodes ESC [ < 0 ; ... M. */
    feed(test, engine, "\x1b[?1000h\x1b[?1006h");
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_mouse_move(engine, 2, 3, 0));
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_mouse_button(engine, 1, 1, 0));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B3C") != NULL);

    /* Paste: unbracketed by default, bracketed under 2004. */
    cap.log_len = 0;
    cap.log[0] = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_paste(engine, "pp", 2));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "7070") != NULL);
    YTEST_CHECK(test, strstr(cap.log, "1B5B3230307E") == NULL);

    cap.log_len = 0;
    cap.log[0] = 0;
    feed(test, engine, "\x1b[?2004h");
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_input_paste(engine, "pp", 2));
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "1B5B3230307E") != NULL); /* ESC [ 200 ~ */
    YTEST_CHECK(test, strstr(cap.log, "1B5B3230317E") != NULL); /* ESC [ 201 ~ */
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * Title + bell host callbacks.
 *-------------------------------------------------------------------------*/
static void test_title_and_bell(struct ytest *test)
{
    struct capture cap = {0};
    struct yetty_yclass_object *engine = make_engine(test, 4, 20, &cap);
    feed(test, engine, "\x1b]2;hello title\x07\x07");
    capture_flush_output(&cap);
    YTEST_CHECK(test, strstr(cap.log, "title hello title") != NULL);
    YTEST_CHECK(test, strstr(cap.log, "bell") != NULL);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * Resize preserves surviving content and identity.
 *-------------------------------------------------------------------------*/
static void test_resize(struct ytest *test)
{
    struct yetty_yclass_object *engine = make_engine(test, 6, 20, NULL);
    feed(test, engine, "keep");
    uint64_t id_row0 = row_id(test, engine, 0);
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_resize(engine, 4, 30));
    uint32_t rows = 0, cols = 0;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_dims(engine, &rows, &cols));
    YTEST_CHECK_EQ_INT(test, rows, 4);
    YTEST_CHECK_EQ_INT(test, cols, 30);
    YTEST_CHECK_EQ_INT(test, cell_at(test, engine, 0, 0)->codepoint, 'k');
    YTEST_CHECK(test, row_id(test, engine, 0) == id_row0);
    yetty_ymux_engine_dispose(engine);
}

/*---------------------------------------------------------------------------
 * Golden snapshots with chunk-partition invariance.
 *-------------------------------------------------------------------------*/
#ifndef YMUX_GOLDEN_DIR
#define YMUX_GOLDEN_DIR "golden"
#endif

struct golden_case {
    const char *name;
    const char *bytes;
};

static const struct golden_case golden_cases[] = {
    {"basic", "Hello world!\r\nsecond \x1b[7mrev\x1b[0m line"},
    {"sgr", "\x1b[31mred \x1b[1;44mboldbg\x1b[0m \x1b[38;5;123midx\x1b[0m "
            "\x1b[38;2;1;2;3mtrue\x1b[0m \x1b[4mu\x1b[21muu\x1b[24m"},
    {"cursor-scroll", "L1\r\nL2\r\nL3\r\nL4\r\nL5\r\n\x1b[2;4r\x1b[2;1HR1\r\nR2\r\nR3\r\nR4"
                      "\x1b[r\x1b[6;1Hbottom"},
    {"wide-combining", "\xE4\xB8\xAD\xE6\x96\x87 A\xCC\x80 \xF0\x9F\x98\x80 end"},
    {"alt", "primary-content\x1b[?1049h\x1b[Halt-content\x1b[?1049lback"},
    {"erase-insert", "ABCDEFGH\r\n01234567\x1b[1;3H\x1b[2K\x1b[2;2H\x1b[0K\x1b[2;1H\x1b[3@X"},
    {"osc-colors", "pre\x1b]10;#a0b0c0\x07\x1b]11;#102030\x07post\x1b]110\x07\x1b]111\x07end"},
    {"modes", "\x1b[?1000h\x1b[?25l\x1b[?2004htext\x1b[?25h"},
    {"queries", "\x1b[c\x1b[6n\x1b[5n"},
    {"charset", "\x1b(0lqqk\x1b(Bplain"},
    {"wrap", "0123456789012345678901234567890123456789WRAPPED-TAIL"},
    {"utf8-split-tail", "A\xE4\xB8\xAD tail"},
};

static void run_golden_case(struct ytest *test, const struct golden_case *golden_case, size_t chunk,
                            struct yetty_ycore_buffer *out)
{
    struct capture cap = {0};
    struct yetty_yclass_object *engine = make_engine(test, 6, 40, &cap);
    const char *bytes = golden_case->bytes;
    size_t len = strlen(bytes);
    if (chunk == 0) {
        YTEST_REQUIRE_OK(test, yetty_ymux_engine_feed(engine, bytes, len));
    } else {
        for (size_t offset = 0; offset < len; offset += chunk) {
            size_t take = len - offset < chunk ? len - offset : chunk;
            YTEST_REQUIRE_OK(test, yetty_ymux_engine_feed(engine, bytes + offset, take));
        }
    }
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_snapshot(engine, out));
    capture_flush_output(&cap);
    YTEST_REQUIRE_OK(test, yetty_ycore_buffer_write(out, "callbacks\n", 10));
    if (cap.log_len) {
        YTEST_REQUIRE_OK(test, yetty_ycore_buffer_write(out, cap.log, cap.log_len));
    }
    yetty_ymux_engine_dispose(engine);
}

static void test_golden_snapshots(struct ytest *test)
{
    for (size_t index = 0; index < sizeof(golden_cases) / sizeof(golden_cases[0]); ++index) {
        const struct golden_case *golden_case = &golden_cases[index];
        struct yetty_ycore_buffer_result reference_res = yetty_ycore_buffer_create(16384);
        YTEST_REQUIRE_OK(test, reference_res);
        struct yetty_ycore_buffer reference = reference_res.value;
        run_golden_case(test, golden_case, 0, &reference);

        static const size_t chunks[] = {1, 2, 3, 7};
        for (size_t chunk_index = 0; chunk_index < sizeof(chunks) / sizeof(chunks[0]);
             ++chunk_index) {
            struct yetty_ycore_buffer_result chunked_res = yetty_ycore_buffer_create(16384);
            YTEST_REQUIRE_OK(test, chunked_res);
            struct yetty_ycore_buffer chunked = chunked_res.value;
            run_golden_case(test, golden_case, chunks[chunk_index], &chunked);
            int same =
                chunked.size == reference.size &&
                (reference.size == 0 || memcmp(chunked.data, reference.data, reference.size) == 0);
            if (!same) {
                fprintf(stderr, "[%s] chunk=%zu diverges\n--- one-shot\n%.*s\n--- chunked\n%.*s\n",
                        golden_case->name, chunks[chunk_index], (int)reference.size,
                        (char *)reference.data, (int)chunked.size, (char *)chunked.data);
            }
            YTEST_CHECK(test, same);
            yetty_ycore_buffer_destroy(&chunked);
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.golden", YMUX_GOLDEN_DIR, golden_case->name);
        if (getenv("YMUX_GOLDEN_RECORD")) {
            FILE *file = fopen(path, "wb");
            YTEST_REQUIRE_NOT_NULL(test, file);
            if (reference.size) {
                fwrite(reference.data, 1, reference.size, file);
            }
            fclose(file);
            fprintf(stderr, "[%s] golden recorded: %s\n", golden_case->name, path);
        } else {
            FILE *file = fopen(path, "rb");
            if (!file) {
                fprintf(stderr, "[%s] missing golden %s — run with YMUX_GOLDEN_RECORD=1\n",
                        golden_case->name, path);
            }
            YTEST_REQUIRE_NOT_NULL(test, file);
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            fseek(file, 0, SEEK_SET);
            char *expected = malloc(file_size > 0 ? (size_t)file_size : 1);
            YTEST_REQUIRE_NOT_NULL(test, expected);
            size_t got = fread(expected, 1, (size_t)file_size, file);
            fclose(file);
            int same =
                got == reference.size && (got == 0 || memcmp(expected, reference.data, got) == 0);
            if (!same) {
                fprintf(stderr, "[%s] golden mismatch\n--- expected\n%.*s\n--- actual\n%.*s\n",
                        golden_case->name, (int)got, expected, (int)reference.size,
                        (char *)reference.data);
            }
            YTEST_CHECK(test, same);
            free(expected);
        }
        yetty_ycore_buffer_destroy(&reference);
    }
}

/*---------------------------------------------------------------------------
 * Screen-write op stream (#699.1): the engine records the CANONICAL mutations
 * (putglyph runs, cursor moves, semantic scrollrect incl. the fork's ICH/DCH
 * decomposition source, erase, invalidate on resize) in order with a monotonic
 * head, and evicts consumers that fall behind the ring.
 *-------------------------------------------------------------------------*/
static void test_op_stream(struct ytest *test)
{
    struct yetty_yclass_object *engine = make_engine(test, 4, 20, NULL);

    uint64_t base = yetty_ymux_engine_op_head(engine).value;
    feed(test, engine, "ab");
    uint64_t after_text = yetty_ymux_engine_op_head(engine).value;
    YTEST_CHECK(test, after_text > base);

    /* The two written cells appear as PUTGLYPH ops in order, with the pen
     * applied and correct positions. */
    int putglyphs = 0;
    uint64_t last_cursor_col = 0;
    for (uint64_t seq = base; seq < after_text; ++seq) {
        const struct yetty_ymux_engine_op *op = yetty_ymux_engine_op_at(engine, seq);
        YTEST_REQUIRE_NOT_NULL(test, (void *)op);
        if (op->type == YMUX_ENGINE_OP_PUTGLYPH) {
            YTEST_CHECK_EQ_INT(test, op->a, 0); /* row 0 */
            YTEST_CHECK_EQ_INT(test, op->b, putglyphs);
            YTEST_CHECK_EQ_INT(test, (int)op->cell.codepoint, putglyphs == 0 ? 'a' : 'b');
            ++putglyphs;
        } else if (op->type == YMUX_ENGINE_OP_MOVECURSOR) {
            last_cursor_col = (uint64_t)op->b;
        }
    }
    YTEST_CHECK_EQ_INT(test, putglyphs, 2);
    YTEST_CHECK_EQ_SIZE(test, last_cursor_col, 2); /* cursor after 'b' */

    /* ICH arrives as the SEMANTIC scrollrect op (rightward != 0) — the source
     * tmux consumes, not an inference from settled grids. */
    uint64_t before_ich = yetty_ymux_engine_op_head(engine).value;
    feed(test, engine, "\x1b[1;1H\x1b[1@");
    uint64_t after_ich = yetty_ymux_engine_op_head(engine).value;
    int saw_hshift = 0;
    for (uint64_t seq = before_ich; seq < after_ich; ++seq) {
        const struct yetty_ymux_engine_op *op = yetty_ymux_engine_op_at(engine, seq);
        YTEST_REQUIRE_NOT_NULL(test, (void *)op);
        if (op->type == YMUX_ENGINE_OP_SCROLLRECT && op->b != 0) {
            saw_hshift = 1;
        }
    }
    YTEST_CHECK(test, saw_hshift);

    /* A clear records ERASE. */
    uint64_t before_clear = yetty_ymux_engine_op_head(engine).value;
    feed(test, engine, "\x1b[2J");
    uint64_t after_clear = yetty_ymux_engine_op_head(engine).value;
    int saw_erase = 0;
    for (uint64_t seq = before_clear; seq < after_clear; ++seq) {
        const struct yetty_ymux_engine_op *op = yetty_ymux_engine_op_at(engine, seq);
        if (op && op->type == YMUX_ENGINE_OP_ERASE) {
            saw_erase = 1;
        }
    }
    YTEST_CHECK(test, saw_erase);

    /* Resize records INVALIDATE (consumers must full-redraw). */
    uint64_t before_resize = yetty_ymux_engine_op_head(engine).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_engine_resize(engine, 5, 24));
    const struct yetty_ymux_engine_op *invalidate_op =
        yetty_ymux_engine_op_at(engine, before_resize);
    YTEST_REQUIRE_NOT_NULL(test, (void *)invalidate_op);
    YTEST_CHECK_EQ_INT(test, (int)invalidate_op->type, (int)YMUX_ENGINE_OP_INVALIDATE);

    /* Eviction: flooding the ring makes old sequences unreadable (NULL). */
    for (int line = 0; line < 80; ++line) {
        feed(test, engine, "0123456789012345678\r\n");
    }
    YTEST_CHECK(test, yetty_ymux_engine_op_at(engine, base) == NULL);
    uint64_t head_now = yetty_ymux_engine_op_head(engine).value;
    YTEST_CHECK(test, yetty_ymux_engine_op_at(engine, head_now - 1) != NULL);

    yetty_ymux_engine_dispose(engine);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_engine");
    YTEST_RUN(&test, test_basic_ingestion);
    YTEST_RUN(&test, test_wide_combining);
    YTEST_RUN(&test, test_alt_screen);
    YTEST_RUN(&test, test_identity_scroll_and_wrap);
    YTEST_RUN(&test, test_query_replies);
    YTEST_RUN(&test, test_osc52_clipboard);
    YTEST_RUN(&test, test_input_encoding);
    YTEST_RUN(&test, test_title_and_bell);
    YTEST_RUN(&test, test_resize);
    YTEST_RUN(&test, test_golden_snapshots);
    YTEST_RUN(&test, test_op_stream);
    return ytest_end(&test);
}
