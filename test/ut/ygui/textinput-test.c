/*
 * ygui textinput (single-line edit box) behaviour test.
 *
 * Drives the widget's public editing/selection API directly — no GPU, no
 * display, no clipboard — to pin down the edit-box semantics the ybrowser
 * address bar relies on:
 *   - typing / backspace / delete / Home / End caret editing,
 *   - Shift+Arrow and Shift+Home/End keyboard selection (and plain-arrow
 *     collapse),
 *   - Ctrl-A select-all,
 *   - Ctrl+Arrow word motion and Ctrl+Backspace word delete,
 *   - insert_text: paste-replaces the selection, drops non-printable bytes,
 *     and insert_text("") deletes the selection (the cut primitive),
 *   - mouse press→drag→release grows a selection.
 */

#include <yetty/ygui/framework-defs.h>
#include <yetty/ygui/widgets/textinput.h>
#include <yetty/ygui/ygui.h>

#include "ytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct yetty_yclass_object *make_input(struct ytest *test)
{
    struct yetty_yclass_ptr_result cls = yetty_ygui_textinput_class_get();
    YTEST_REQUIRE_OK(test, cls);
    struct yetty_yclass_object_ptr_result r = yetty_ygui_widget_new(cls.value);
    YTEST_REQUIRE_OK(test, r);
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_focus(r.value, 1));
    return r.value;
}

static const char *text_of(struct ytest *test, struct yetty_yclass_object *in)
{
    struct yetty_ycore_const_char_ptr_result r = yetty_ygui_textinput_get_text(in);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void key(struct ytest *test, struct yetty_yclass_object *in, uint32_t code, int mods)
{
    struct yetty_ycore_int_result r = yetty_ygui_textinput_handle_key(in, code, mods);
    YTEST_REQUIRE_OK(test, r);
}

static void type_str(struct ytest *test, struct yetty_yclass_object *in, const char *s)
{
    for (const char *p = s; *p; ++p) {
        key(test, in, (uint32_t)(unsigned char)*p, 0);
    }
}

/* Returns the selection as a fresh heap string (caller frees), or NULL. */
static char *sel_dup(struct ytest *test, struct yetty_yclass_object *in)
{
    struct yetty_ycore_char_ptr_result r = yetty_ygui_textinput_get_selection(in);
    YTEST_REQUIRE_OK(test, r);
    return r.value;
}

static void test_basic_editing(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    type_str(test, in, "hello");
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "hello");
    key(test, in, 0x08, 0); /* backspace */
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "hell");
    key(test, in, YETTY_YGUI_KEY_HOME, 0);
    key(test, in, YETTY_YGUI_KEY_DELETE, 0); /* delete the leading 'h' */
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "ell");
    key(test, in, YETTY_YGUI_KEY_END, 0);
    type_str(test, in, "!");
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "ell!");
    yetty_ygui_widget_destroy(in);
}

static void test_shift_arrow_selection(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello world"));
    key(test, in, YETTY_YGUI_KEY_HOME, 0);
    for (int i = 0; i < 5; i++) {
        key(test, in, YETTY_YGUI_KEY_ARROW_RIGHT, YETTY_YGUI_MOD_SHIFT);
    }
    char *sel = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel);
    if (sel) {
        YTEST_CHECK_STR_EQ(test, sel, "hello");
        free(sel);
    }
    /* Typing over a selection replaces it. */
    type_str(test, in, "X");
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "X world");
    yetty_ygui_widget_destroy(in);
}

static void test_shift_home_then_collapse(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "abcdef"));
    key(test, in, YETTY_YGUI_KEY_HOME, YETTY_YGUI_MOD_SHIFT); /* caret at end → select all */
    char *sel = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel);
    if (sel) {
        YTEST_CHECK_STR_EQ(test, sel, "abcdef");
        free(sel);
    }
    /* A plain (unshifted) arrow collapses the selection instead of moving one. */
    key(test, in, YETTY_YGUI_KEY_ARROW_LEFT, 0);
    char *none = sel_dup(test, in);
    YTEST_CHECK_NULL(test, none);
    free(none);
    yetty_ygui_widget_destroy(in);
}

static void test_select_all(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "select me"));
    key(test, in, 0x01, 0); /* Ctrl-A */
    char *sel = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel);
    if (sel) {
        YTEST_CHECK_STR_EQ(test, sel, "select me");
        free(sel);
    }
    yetty_ygui_widget_destroy(in);
}

static void test_word_motion_and_delete(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "foo bar"));
    /* Caret at end; Ctrl+Backspace deletes the trailing word. */
    key(test, in, 0x08, YETTY_YGUI_MOD_CTRL);
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "foo ");
    /* Home, then Ctrl+Shift+Right selects the whole first word. */
    key(test, in, YETTY_YGUI_KEY_HOME, 0);
    key(test, in, YETTY_YGUI_KEY_ARROW_RIGHT, YETTY_YGUI_MOD_CTRL | YETTY_YGUI_MOD_SHIFT);
    char *sel = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel);
    if (sel) {
        YTEST_CHECK_STR_EQ(test, sel, "foo");
        free(sel);
    }
    yetty_ygui_widget_destroy(in);
}

static void test_insert_text_paste_and_cut(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello"));
    key(test, in, 0x01, 0);                                                /* select all */
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_insert_text(in, "WORLD")); /* paste replaces */
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "WORLD");

    /* A multi-line clipboard paste is flattened: control bytes are dropped. */
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, ""));
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_insert_text(in, "a\nb\tc"));
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "abc");

    /* Cut = copy (elsewhere) then insert_text("") to drop the selection. */
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello world"));
    key(test, in, YETTY_YGUI_KEY_HOME, 0);
    for (int i = 0; i < 5; i++) {
        key(test, in, YETTY_YGUI_KEY_ARROW_RIGHT, YETTY_YGUI_MOD_SHIFT);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_insert_text(in, ""));
    YTEST_CHECK_STR_EQ(test, text_of(test, in), " world");
    yetty_ygui_widget_destroy(in);
}

static void test_mouse_drag_selection(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    struct yetty_ycore_rectangle rect = {{0, 0}, {200, 28}};
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_set_rect(in, rect));
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello world"));
    /* Press at the far left drops the caret at the start; dragging right grows
     * a selection. The exact hit index depends on glyph metrics (a fixed
     * advance here, since no measurement font is wired), so assert the shape:
     * the selection is a non-empty prefix of the text. */
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(in, 10.0f, 14.0f, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_motion(in, 120.0f, 14.0f));
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(in, 120.0f, 14.0f, 0));
    char *sel = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel);
    if (sel) {
        size_t n = strlen(sel);
        YTEST_CHECK(test, n > 0);
        YTEST_CHECK(test, strncmp(sel, "hello world", n) == 0);
        free(sel);
    }
    yetty_ygui_widget_destroy(in);
}

/* Click-to-caret: a press at x places the caret at the byte offset nearest that
 * pixel. No measurement font is wired here, so the widget falls back to a fixed
 * per-char advance — deterministic. The key case is the reported regression:
 * a click PAST the last glyph must reach the very end, not stop a char short. */
static void press_release(struct ytest *test, struct yetty_yclass_object *in, float x)
{
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(in, x, 15.0f, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(in, x, 15.0f, 0));
}

static void test_click_to_caret(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    struct yetty_ycore_rectangle rect = {{0.0f, 0.0f}, {400.0f, 30.0f}};
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_set_rect(in, rect));

    /* Click WELL PAST the text → caret at the end; a typed char appends. */
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello"));
    press_release(test, in, 380.0f);
    key(test, in, 'X', 0);
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "helloX");

    /* Click just a few px past the last glyph (the exact spot the reporter
     * could not reach) → still the end. */
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello"));
    press_release(test, in, 55.0f); /* pad(10) + 5*~7.7 ≈ 48.5, so 55 is just past */
    key(test, in, 'Z', 0);
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "helloZ");

    /* Click at the far left → caret at the start; a typed char prepends. */
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello"));
    press_release(test, in, 0.0f);
    key(test, in, 'Y', 0);
    YTEST_CHECK_STR_EQ(test, text_of(test, in), "Yhello");

    /* Click near the front interior → caret lands between early chars, not at an
     * edge (guards against the caret sticking at 0 or the end). */
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "hello"));
    press_release(test, in, 26.0f); /* pad(10) + ~2 chars */
    key(test, in, '.', 0);
    const char *after = text_of(test, in);
    YTEST_CHECK(test, strcmp(after, "hello") != 0);     /* something inserted */
    YTEST_CHECK(test, strncmp(after, "he", 2) == 0);    /* after the first chars */
    YTEST_CHECK(test, after[strlen(after) - 1] == 'o'); /* not at the very end */

    yetty_ygui_widget_destroy(in);
}

/* Monotonicity: pressing at increasing x never moves the caret left. Catches
 * advance-metric drift that made the tail of the field unreachable. */
static void test_click_monotonic(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    struct yetty_ycore_rectangle rect = {{0.0f, 0.0f}, {400.0f, 30.0f}};
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_set_rect(in, rect));
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "abcdefghij"));

    size_t prev_marker = 0;
    int first = 1;
    for (float x = 12.0f; x <= 200.0f; x += 12.0f) {
        YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "abcdefghij"));
        press_release(test, in, x);
        /* Mark the caret by inserting '#', then read its index from the text. */
        YTEST_REQUIRE_OK(test, yetty_ygui_textinput_insert_text(in, "#"));
        const char *marked = text_of(test, in);
        const char *hash = strchr(marked, '#');
        YTEST_REQUIRE_NOT_NULL(test, hash);
        size_t marker = (size_t)(hash - marked);
        if (!first) {
            YTEST_CHECK(test, marker >= prev_marker); /* never moves left */
        }
        prev_marker = marker;
        first = 0;
    }
    /* And a click far to the right reached the true end (index 10). */
    YTEST_CHECK_EQ_SIZE(test, prev_marker, 10);

    yetty_ygui_widget_destroy(in);
}

/* Pixel x that lands inside the glyph at byte index `p` under the fixed fallback
 * advance (no measurement font wired): pad(10) + (p + 0.5) * ~7.7. */
static float x_at_index(size_t p)
{
    return 10.0f + ((float)p + 0.5f) * (14.0f * 0.55f);
}

/* Two rapid left presses on the same spot = a double-click. The unit test uses
 * the real monotonic clock (platform_time shim); calls a few microseconds apart
 * fall well inside the multi-click window, so a second press here is always seen
 * as a double-click. */
static void test_double_click_selects_word(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    struct yetty_ycore_rectangle rect = {{0.0f, 0.0f}, {400.0f, 30.0f}};
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_set_rect(in, rect));
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "the quick brown fox"));

    float x_brown = x_at_index(12); /* 'o' inside "brown" (bytes 10..14) */
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(in, x_brown, 15.0f, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(in, x_brown, 15.0f, 0));
    /* Second press on the same spot = double-click → the whole word. */
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(in, x_brown, 15.0f, 0));
    char *sel = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel);
    if (sel) {
        YTEST_CHECK_STR_EQ(test, sel, "brown");
        free(sel);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(in, x_brown, 15.0f, 0));
    yetty_ygui_widget_destroy(in);
}

/* After a double-click, dragging grows the selection a whole word at a time — to
 * the right past later words, and to the left past the pivot word. */
static void test_double_click_drag_extends_by_word(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    struct yetty_ycore_rectangle rect = {{0.0f, 0.0f}, {400.0f, 30.0f}};
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_set_rect(in, rect));
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "the quick brown fox"));

    /* Double-click "brown" (pivot), then drag right into "fox". */
    float x_brown = x_at_index(12);
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(in, x_brown, 15.0f, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(in, x_brown, 15.0f, 0));
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(in, x_brown, 15.0f, 0));

    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_motion(in, x_at_index(17), 15.0f)); /* 'o' in fox */
    char *sel_right = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel_right);
    if (sel_right) {
        YTEST_CHECK_STR_EQ(test, sel_right, "brown fox"); /* snaps to whole words */
        free(sel_right);
    }

    /* Drag left past the pivot into "the" → whole words the other way. */
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_motion(in, x_at_index(1), 15.0f)); /* 'h' in the */
    char *sel_left = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel_left);
    if (sel_left) {
        YTEST_CHECK_STR_EQ(test, sel_left, "the quick brown");
        free(sel_left);
    }
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(in, x_at_index(1), 15.0f, 0));
    yetty_ygui_widget_destroy(in);
}

/* Triple-click selects the whole field (single-line stand-in for select-line). */
static void test_triple_click_selects_all(struct ytest *test)
{
    struct yetty_yclass_object *in = make_input(test);
    struct yetty_ycore_rectangle rect = {{0.0f, 0.0f}, {400.0f, 30.0f}};
    YTEST_REQUIRE_OK(test, yetty_ygui_widget_set_rect(in, rect));
    YTEST_REQUIRE_OK(test, yetty_ygui_textinput_set_text(in, "the quick brown fox"));

    float x = x_at_index(12);
    for (int i = 0; i < 3; i++) {
        YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_press(in, x, 15.0f, 0));
        YTEST_REQUIRE_OK(test, yetty_ygui_widget_on_release(in, x, 15.0f, 0));
    }
    char *sel = sel_dup(test, in);
    YTEST_CHECK_NOT_NULL(test, sel);
    if (sel) {
        YTEST_CHECK_STR_EQ(test, sel, "the quick brown fox");
        free(sel);
    }
    yetty_ygui_widget_destroy(in);
}

int main(void)
{
    struct ytest test = ytest_begin("ygui_textinput");
    YTEST_RUN(&test, test_basic_editing);
    YTEST_RUN(&test, test_shift_arrow_selection);
    YTEST_RUN(&test, test_shift_home_then_collapse);
    YTEST_RUN(&test, test_select_all);
    YTEST_RUN(&test, test_word_motion_and_delete);
    YTEST_RUN(&test, test_insert_text_paste_and_cut);
    YTEST_RUN(&test, test_mouse_drag_selection);
    YTEST_RUN(&test, test_click_to_caret);
    YTEST_RUN(&test, test_click_monotonic);
    YTEST_RUN(&test, test_double_click_selects_word);
    YTEST_RUN(&test, test_double_click_drag_extends_by_word);
    YTEST_RUN(&test, test_triple_click_selects_all);
    return ytest_end(&test);
}
