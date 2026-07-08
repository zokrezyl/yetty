/*
 * render.c — scrollback rendering for yai.
 */
#include "render.h"

#include <yetty/yfont/shader-glyph.h>

/* In-process markdown → YDRAW_BIN: yai renders the answer with the same
 * ymarkdown library ycat uses and frames it as a YDRAW_BIN OSC envelope
 * itself, instead of shelling out to the ycat binary. The terminal anchors
 * the drawable-list's text records to the cursor's grid line, so the rich
 * block scrolls with the conversation text. */
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/ymarkdown/ymarkdown.h>
#include <yetty/ycore/types.h>
#include <yetty/yterminal/dcs-codes.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* Sentinels the yetty MCP server emits under YETTY_MCP_VIA_PARENT: yai is
 * the single PTY writer, so it writes the envelope itself. The FILE form
 * carries a temp-file path (envelopes can be 100+ KB — music scores embed
 * font glyphs — far past the agent's tool-result token cap, so the bytes
 * must not travel through the tool result). The inline-base64 form is the
 * legacy variant, still accepted from older servers. The marker strings
 * are wire constants shared with the server — do not rename them. */
#define YAI_FIGURE_FILE_SENTINEL_OPEN "<<CCLOOP_FIGURE_FILE "
#define YAI_FIGURE_FILE_SENTINEL_CLOSE " CCLOOP_FIGURE_FILE>>"
#define YAI_FIGURE_SENTINEL_OPEN "<<CCLOOP_FIGURE "
#define YAI_FIGURE_SENTINEL_CLOSE " CCLOOP_FIGURE>>"

/* The spool files the FILE sentinel may name: tempfile output of the MCP
 * server. Anything else in tool output is untrusted text, not a path. */
#define YAI_FIGURE_SPOOL_PREFIX "yetty-figure-"
#define YAI_FIGURE_SPOOL_SUFFIX ".bin"
#define YAI_FIGURE_SPOOL_MAX_BYTES (64u * 1024u * 1024u)

/* The YAI:DRAW sentinel: the agent (via the yetty MCP server in parent
 * mode) wrote RAW content to a temp file and tells yai which tool to
 * render it with. yai — the single PTY writer, and the side that knows
 * the real terminal width — runs that tool itself. Format:
 *   <<YAI:DRAW kind=<kind> path=</abs/tmp/yetty-draw-XXXX>>>
 * The temp file uses this prefix so the path validator can recognise it. */
#define YAI_DRAW_SENTINEL_OPEN "<<YAI:DRAW "
#define YAI_DRAW_SENTINEL_CLOSE ">>"
#define YAI_DRAW_SPOOL_PREFIX "yetty-draw-"

#define YAI_SUMMARY_MAX 100

#define YAI_STREAM_NONE 0
#define YAI_STREAM_TEXT 1
#define YAI_STREAM_THINKING 2

/* "you ▸ " — 6 display columns; the glyph prefix adds 2 more. */
#define YAI_PROMPT_COLUMNS 6

/* Upper bound on the visual rows the wrapped prompt may occupy. A taller
 * prompt scrolls vertically inside this window, keeping the cursor visible.
 * The effective cap is also clamped to the room left on screen. */
#define YAI_PROMPT_MAX_ROWS 12

/* Render `len` bytes of markdown to a yetty figure (via ycat) and write
 * it under the engine label. Defined below; forward-declared so the
 * stream finish path (above its definition) can reach it. Value 1 =
 * rendered; 0 = could not (ycat absent/failed) → caller prints plain. */
static struct yetty_ycore_int_result render_markdown_buffer(struct yai_renderer *renderer,
                                                            const char *bytes, size_t len);

/* Flush stdio's stdout buffer to the tty. stdout is NON-BLOCKING here
 * (the uv_poll on stdin put the shared tty file description in
 * non-blocking mode), so EAGAIN is a normal transient: wait for
 * writability and retry — glibc keeps the unwritten tail buffered and
 * resumes on the next fflush. Anything else is a real error. */
struct yetty_ycore_void_result yai_render_flush_stdout(void)
{
    while (fflush(stdout) != 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            if (errno != EINTR) {
                struct pollfd ready = {.fd = STDOUT_FILENO, .events = POLLOUT};
                (void)poll(&ready, 1, -1);
            }
            continue;
        }
        return YETTY_ERR(yetty_ycore_void, "yai_render_flush_stdout: fflush(stdout) failed");
    }
    return YETTY_OK_VOID();
}

void yai_renderer_init(struct yai_renderer *renderer, int fold_lines, int show_thinking,
                       const char *engine_label)
{
    memset(renderer, 0, sizeof(*renderer));
    renderer->fold_lines = fold_lines;
    renderer->show_thinking = show_thinking;
    renderer->engine_label = (engine_label && engine_label[0]) ? engine_label : "engine";
    /* The pinned zone uses in-place erase; only a tty can take it. The
     * animated glyph additionally needs yetty's text layer behind the
     * tty — any other terminal would show PUA-B tofu. */
    renderer->pin_enabled = isatty(STDOUT_FILENO);
    const char *term_program = getenv("TERM_PROGRAM");
    renderer->pin_shader_glyphs = term_program && strcmp(term_program, "yetty") == 0;
    /* render_markdown, animate_glyph and text_hud are opt-in flags set by
     * main from --markdown / --animate / --hud after init. All default off:
     * plain text, static glyph, and — crucially — NO bottom status bar, so
     * the conversation uses the full normal screen and stays in the
     * terminal's scrollback. The bar needs a DECSTBM scroll region, which
     * drops scrolled-off lines from scrollback in most terminals; hence
     * opt-in. */
}

void yai_renderer_destroy(struct yai_renderer *renderer)
{
    free(renderer->stream_buf);
    renderer->stream_buf = NULL;
}

/*---------------------------------------------------------------------------
 * Pinned zone — stream ticker row + prompt row at the scrollback bottom
 *---------------------------------------------------------------------------*/

static size_t encode_codepoint_utf8(uint32_t codepoint, char *out)
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

static int terminal_columns(void)
{
    struct winsize size = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_col == 0) {
        return 80;
    }
    return size.ws_col;
}

static int terminal_rows(void)
{
    struct winsize size = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_row == 0) {
        return 24;
    }
    return size.ws_row;
}

/* Tail of [bytes, bytes+len) that fits `max_columns` display cells
 * (counting UTF-8 codepoints as one cell each — same simplification as
 * the rest of the editor). Sets *clipped when something was cut. */
static const char *clip_tail(const char *bytes, size_t len, int max_columns, size_t *out_len,
                             int *clipped)
{
    size_t chars = 0;
    size_t start = len;
    while (start > 0 && chars < (size_t)max_columns) {
        start--;
        if (((unsigned char)bytes[start] & 0xC0) != 0x80) {
            chars++;
        }
    }
    *clipped = start > 0;
    *out_len = len - start;
    return bytes + start;
}

/* Move to the next visual prompt row. When drawing, a `\n` + line-erase is
 * emitted for every visible row past the first one in the window — the first
 * visible row is where the caller already parked the cursor (after the prefix
 * for row 0, or column 0 for a scrolled continuation row). */
static void prompt_row_break(int *row, int *col, int draw, int win_first, int win_count)
{
    (*row)++;
    *col = 0;
    if (draw && *row > win_first && *row < win_first + win_count) {
        fputs("\n\033[2K", stdout);
    }
}

/* Walk the editor buffer as it wraps onto the prompt rows. One cell per
 * codepoint (same simplification as the rest of the editor); a literal '\n'
 * or reaching `max_columns` starts a new visual row. Row 0 begins at column
 * `prefix_columns` (just past "you ▸ "); continuation rows begin at column 0.
 * Returns the total visual row count and reports the cursor's row and 0-based
 * screen column. When `draw` is set, the buffer text for rows in
 * [win_first, win_first + win_count) is emitted to stdout. */
static int prompt_walk(const struct yai_renderer *renderer, int prefix_columns, int max_columns,
                       int draw, int win_first, int win_count, int *out_cursor_row,
                       int *out_cursor_col)
{
    const char *buffer = renderer->edit_buffer;
    size_t length = (renderer->edit_buffer && renderer->edit_length) ? *renderer->edit_length : 0;
    size_t cursor = renderer->edit_cursor ? *renderer->edit_cursor : length;
    if (cursor > length) {
        cursor = length;
    }
    if (max_columns < 1) {
        max_columns = 1;
    }

    int row = 0;
    int col = prefix_columns;
    int cursor_row = 0;
    int cursor_col = prefix_columns;
    size_t index = 0;
    while (1) {
        if (index == cursor) {
            cursor_row = row;
            cursor_col = col;
        }
        if (index >= length) {
            break;
        }
        if ((unsigned char)buffer[index] == '\n') {
            prompt_row_break(&row, &col, draw, win_first, win_count);
            index++;
            continue;
        }
        if (col >= max_columns) {
            prompt_row_break(&row, &col, draw, win_first, win_count);
        }
        size_t glyph_start = index;
        index++;
        while (index < length && ((unsigned char)buffer[index] & 0xC0) == 0x80) {
            index++;
        }
        if (draw && row >= win_first && row < win_first + win_count) {
            fwrite(buffer + glyph_start, 1, index - glyph_start, stdout);
        }
        col++;
    }
    *out_cursor_row = cursor_row;
    *out_cursor_col = cursor_col;
    return row + 1;
}

/* The current in-progress streamed line, shown as a single clipped
 * ticker row. Only the LAST line of stream_buf is shown: in markdown
 * mode the buffer holds the whole multi-line answer, and a '\n' printed
 * in this single row would move the cursor and corrupt the pinned zone. */
static void zone_draw_ticker(const struct yai_renderer *renderer, int columns)
{
    size_t line_start = renderer->stream_len;
    while (line_start > 0 && renderer->stream_buf[line_start - 1] != '\n') {
        line_start--;
    }
    size_t tail_len = 0;
    int clipped = 0;
    const char *tail =
        clip_tail(renderer->stream_buf + line_start, renderer->stream_len - line_start, columns - 2,
                  &tail_len, &clipped);
    const char *style = (renderer->stream_kind == YAI_STREAM_THINKING) ? YAI_DIM : "";
    printf("%s%s%.*s" YAI_RESET, style, clipped ? "…" : "", (int)tail_len, tail);
}

/* Display width of the mode indicator prefix ("[N] " etc.), 0 when the
 * editor has no modal status (emacs). The status string is ASCII, so
 * byte length == column count; plus one trailing space. */
static int status_columns(const struct yai_renderer *renderer)
{
    if (renderer->edit_status_ptr && renderer->edit_status_ptr[0]) {
        return (int)strlen(renderer->edit_status_ptr) + 1;
    }
    return 0;
}

/* Draw the prompt, wrapping the editor buffer across as many rows as it needs
 * (hard-wrap at the terminal width, plus any newlines the user inserted). A
 * prompt taller than the on-screen budget scrolls vertically so the cursor row
 * stays visible. Records the row count and the cursor's row in the renderer
 * (for the zone bookkeeping) and returns the 1-based screen column the cursor
 * parks on. Auto-wrap is disabled while drawing so a full-width row never spills
 * onto an extra screen line and desyncs the row count — the `\n`s this emits are
 * the only row breaks. */
static int zone_draw_prompt(struct yai_renderer *renderer, int columns)
{
    int prefix_columns =
        status_columns(renderer) + YAI_PROMPT_COLUMNS + (renderer->activity_glyph[0] ? 2 : 0);

    /* Measure the full wrapped layout first, so the visible window can be slid
     * to keep the cursor row on screen. */
    int total_rows = 0;
    int cursor_row = 0;
    int cursor_col = 0;
    total_rows = prompt_walk(renderer, prefix_columns, columns, /*draw=*/0, 0, 0, &cursor_row,
                             &cursor_col);

    /* The menu / HUD rows are drawn below the prompt (after this call), so
     * count them from their sources rather than the not-yet-set zone_rows_below. */
    int rows_below = (int)renderer->menu_row_count + (renderer->text_hud ? 1 : 0);
    int rows_budget = terminal_rows() - renderer->zone_rows_above - rows_below - 1;
    int cap = rows_budget < 1 ? 1 : rows_budget;
    if (cap > YAI_PROMPT_MAX_ROWS) {
        cap = YAI_PROMPT_MAX_ROWS;
    }
    int visible_rows = total_rows < cap ? total_rows : cap;
    int window_first = 0;
    if (cursor_row >= visible_rows) {
        window_first = cursor_row - visible_rows + 1;
    }
    if (window_first > total_rows - visible_rows) {
        window_first = total_rows - visible_rows;
    }
    if (window_first < 0) {
        window_first = 0;
    }

    fputs("\033[?7l", stdout); /* DECAWM off: our own '\n's are the only breaks */
    /* The prefix sits on the first visible row's screen line; a scrolled
     * window (window_first > 0) opens on a continuation row, which carries no
     * prefix, so its text starts at column 0. */
    if (window_first == 0) {
        if (status_columns(renderer) > 0) {
            printf(YAI_MUTED "%s" YAI_RESET " ", renderer->edit_status_ptr);
        }
        if (renderer->activity_glyph[0]) {
            printf(YAI_MINT "%s" YAI_RESET " ", renderer->activity_glyph);
        }
        fputs(YAI_MINT "you ▸ " YAI_RESET, stdout);
    }
    prompt_walk(renderer, prefix_columns, columns, /*draw=*/1, window_first, visible_rows,
                &cursor_row, &cursor_col);
    fputs("\033[?7h", stdout); /* DECAWM back on */

    renderer->zone_prompt_rows = visible_rows;
    renderer->zone_prompt_cursor_row = cursor_row - window_first;
    return cursor_col + 1; /* 0-based screen column → 1-based for CHA */
}

/* Display columns of `s`: one cell per UTF-8 codepoint, skipping CSI
 * escape sequences so the bar's embedded color codes don't count toward
 * width. */
static int text_hud_cols(const char *s)
{
    int cols = 0;
    for (const unsigned char *cursor = (const unsigned char *)s; *cursor;) {
        if (cursor[0] == 0x1b && cursor[1] == '[') {
            cursor += 2;
            while (*cursor && (*cursor < '@' || *cursor > '~')) {
                cursor++;
            }
            if (*cursor) {
                cursor++;
            }
            continue;
        }
        if ((*cursor & 0xC0) != 0x80) {
            cols++;
        }
        cursor++;
    }
    return cols;
}

/* Draw the text-HUD status band at the cursor's CURRENT position — it is the
 * bottom row of the pinned zone (see zone_draw), not a fixed last row carved
 * out with a DECSTBM scroll region. A scroll region confines the conversation
 * to rows 1..N-1 and DROPS lines scrolled off its top — and the rich figures
 * anchored to those lines — instead of pushing them into the terminal's
 * scrollback history. As a zone row the bar rides the same erase/redraw the
 * prompt does (drawn after a real `\n`, which scrolls retired content into
 * normal history), so the whole conversation scrolls naturally.
 *
 * Auto-wrap (DECAWM) is disabled around the band so filling the full width
 * never spills onto a new row — that would desync the zone's row bookkeeping.
 * A dark-mint band: bright-mint activity left, quota centered, stats right. */
static void zone_draw_hud_bar(const struct yai_renderer *renderer, int columns)
{
    if (columns < 2) {
        return;
    }
    int left_cols = text_hud_cols(renderer->hud_state);
    int right_cols = text_hud_cols(renderer->hud_stats);
    int quota_cols = text_hud_cols(renderer->hud_quota);
    fputs("\033[?7l" YAI_HUD_BG, stdout); /* DECAWM off, then the mint band */
    printf(YAI_MINT_BRIGHT "%s" YAI_FG_DEFAULT, renderer->hud_state);
    int gap = columns - left_cols - right_cols - quota_cols;
    if (quota_cols > 0 && gap >= 2) {
        int left_gap = gap / 2;
        printf("%*s" YAI_MINT "%s" YAI_FG_DEFAULT "%*s", left_gap, "", renderer->hud_quota,
               gap - left_gap, "");
    } else {
        int plain_gap = columns - left_cols - right_cols;
        if (plain_gap >= 1) {
            printf("%*s", plain_gap, "");
        } else {
            fputs("  ", stdout);
        }
    }
    fputs(renderer->hud_stats, stdout);
    fputs(YAI_RESET "\033[?7h", stdout); /* reset colors, DECAWM back on */
}

/* Set up / refresh the text-HUD bar. NO DECSTBM scroll region — the bar is
 * the bottom row of the pinned zone (zone_draw), so the conversation scrolls
 * into the terminal's normal history. See the header for anchor_top. */
struct yetty_ycore_void_result yai_renderer_text_hud_reserve(struct yai_renderer *renderer,
                                                             int anchor_top)
{
    if (!renderer->text_hud) {
        return YETTY_OK_VOID();
    }
    if (anchor_top) {
        /* Startup: clear the screen and home the cursor so the banner + prompt
         * (printed by the caller next) start at the top. The bar appears when
         * the pinned zone is first drawn. No scroll region is set. */
        printf("\033[2J\033[H");
        return yai_render_flush_stdout();
    }
    /* Mid-session (resume / resize / config close): repaint the zone — prompt
     * plus the bar as its bottom row — at the bottom of the conversation. */
    return yai_renderer_pin_redraw(renderer);
}

/* Erase the bar. With no scroll region to drop, this just tears the pinned
 * zone (prompt + bar) down so a shell / alt screen gets a clean surface; the
 * caller re-shows the zone afterwards (text_hud_reserve / pin_show). */
struct yetty_ycore_void_result yai_renderer_text_hud_release(struct yai_renderer *renderer)
{
    if (!renderer->text_hud) {
        return YETTY_OK_VOID();
    }
    return yai_renderer_zone_suspend(renderer);
}

/* Paint the overlay rows over the top screen rows (absolute addressing,
 * cursor saved/restored), erasing leftovers when the row count shrank.
 * The caller flushes. */
static void overlay_paint(struct yai_renderer *renderer)
{
    if (renderer->overlay_row_count == 0 && renderer->overlay_rows_drawn == 0) {
        return;
    }
    fputs("\0337", stdout);
    for (size_t row = 0; row < renderer->overlay_row_count; row++) {
        printf("\033[%zu;1H%s\033[0m", row + 1, renderer->overlay_rows[row]);
    }
    for (int row = (int)renderer->overlay_row_count; row < renderer->overlay_rows_drawn; row++) {
        printf("\033[%d;1H\033[2K", row + 1);
    }
    fputs("\0338", stdout);
    renderer->overlay_rows_drawn = (int)renderer->overlay_row_count;
}

/* Erase whatever overlay rows are on screen. The caller flushes. */
static void overlay_erase(struct yai_renderer *renderer)
{
    if (renderer->overlay_rows_drawn == 0) {
        return;
    }
    fputs("\0337", stdout);
    for (int row = 0; row < renderer->overlay_rows_drawn; row++) {
        printf("\033[%d;1H\033[2K", row + 1);
    }
    fputs("\0338", stdout);
    renderer->overlay_rows_drawn = 0;
}

static struct yetty_ycore_void_result zone_draw(struct yai_renderer *renderer)
{
    if (!renderer->pin_enabled || !renderer->pin_active || renderer->zone_visible) {
        return YETTY_OK_VOID();
    }
    int columns = terminal_columns();
    renderer->zone_rows_above = 0;
    if (renderer->stream_kind != YAI_STREAM_NONE && renderer->stream_len > 0) {
        zone_draw_ticker(renderer, columns);
        fputc('\n', stdout);
        renderer->zone_rows_above++;
    }
    int prompt_cursor_col = zone_draw_prompt(renderer, columns);
    /* Menu rows walk down with newlines (scrolling at the screen
     * bottom), then the cursor comes back up — scroll-proof. */
    renderer->zone_rows_below = 0;
    for (size_t row = 0; row < renderer->menu_row_count; row++) {
        printf("\n\033[2K%s", renderer->menu_rows[row]);
        renderer->zone_rows_below++;
    }
    /* The text-HUD status bar is the bottom-most zone row: a real `\n` retires
     * the row above into the terminal's normal scrollback (unlike a DECSTBM
     * region, which would drop it), then the cursor walks back up below. */
    if (renderer->text_hud) {
        printf("\n\033[2K");
        zone_draw_hud_bar(renderer, columns);
        renderer->zone_rows_below++;
    }
    /* Park the cursor on its prompt row: walk up from the bottom-most zone row
     * past the menu/HUD rows and any prompt rows below the cursor's row. */
    int rows_below_cursor =
        (renderer->zone_prompt_rows - 1 - renderer->zone_prompt_cursor_row) +
        renderer->zone_rows_below;
    if (rows_below_cursor > 0) {
        printf("\033[%dA", rows_below_cursor);
    }
    printf("\033[%dG", prompt_cursor_col);
    renderer->zone_visible = 1;
    /* The overlay was erased with the zone (zone_suspend) — repaint it. */
    overlay_paint(renderer);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "zone_draw: flush");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_zone_suspend(struct yai_renderer *renderer)
{
    /* The overlay must vanish before any scrolling write, zone or not —
     * otherwise its rows scroll into the terminal's scrollback. */
    int overlay_was_drawn = renderer->overlay_rows_drawn > 0;
    overlay_erase(renderer);
    if (!renderer->zone_visible) {
        if (overlay_was_drawn) {
            struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "zone_suspend: flush overlay");
        }
        return YETTY_OK_VOID();
    }
    /* The cursor sits on its prompt row. Erase it, then everything below it
     * (the rest of the prompt rows, the menu rows, the HUD), then everything
     * above it (the earlier prompt rows, the ticker); end at the top zone row,
     * column 0. */
    int rows_below_cursor =
        (renderer->zone_prompt_rows - 1 - renderer->zone_prompt_cursor_row) +
        renderer->zone_rows_below;
    int rows_above_cursor = renderer->zone_rows_above + renderer->zone_prompt_cursor_row;
    fputs("\r\033[2K", stdout);
    for (int row = 0; row < rows_below_cursor; row++) {
        fputs("\033[1B\033[2K", stdout);
    }
    if (rows_below_cursor > 0) {
        printf("\033[%dA", rows_below_cursor);
    }
    for (int row = 0; row < rows_above_cursor; row++) {
        fputs("\033[1A\033[2K", stdout);
    }
    renderer->zone_visible = 0;
    renderer->zone_rows_above = 0;
    renderer->zone_rows_below = 0;
    renderer->zone_prompt_rows = 0;
    renderer->zone_prompt_cursor_row = 0;
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "zone_suspend: flush");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_zone_resume(struct yai_renderer *renderer)
{
    struct yetty_ycore_void_result draw_res = zone_draw(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, draw_res, "zone_resume: draw");
    return YETTY_OK_VOID();
}

void yai_renderer_pin_setup(struct yai_renderer *renderer, const char *edit_buffer,
                            const size_t *edit_length, const size_t *edit_cursor)
{
    renderer->edit_buffer = edit_buffer;
    renderer->edit_length = edit_length;
    renderer->edit_cursor = edit_cursor;
}

struct yetty_ycore_void_result yai_renderer_pin_show(struct yai_renderer *renderer)
{
    if (!renderer->pin_enabled) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "pin_show: suspend");
    renderer->pin_active = 1;
    struct yetty_ycore_void_result draw_res = zone_draw(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, draw_res, "pin_show: draw");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_pin_hide(struct yai_renderer *renderer)
{
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    renderer->pin_active = 0;
    renderer->activity_glyph[0] = '\0';
    renderer->menu_row_count = 0;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "pin_hide: suspend");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_pin_redraw(struct yai_renderer *renderer)
{
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "pin_redraw: suspend");
    struct yetty_ycore_void_result draw_res = zone_draw(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, draw_res, "pin_redraw: draw");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_activity_set(struct yai_renderer *renderer,
                                                         const char *glyph_name)
{
    if (!renderer->pin_enabled) {
        return YETTY_OK_VOID();
    }
    char glyph_bytes[8] = "";
    size_t glyph_len = 0;
    /* Only emit the animated shader glyph when explicitly opted in — it
     * keeps yetty re-rendering every frame (high CPU). Otherwise fall
     * through to the static glyph below so yetty idles. */
    if (renderer->pin_shader_glyphs && renderer->animate_glyph && glyph_name) {
        struct uint32_result codepoint_res = yetty_yfont_shader_glyph_codepoint(glyph_name);
        if (YETTY_IS_ERR(codepoint_res)) {
            /* Unknown glyph name is not an error — the static fallback
             * below stands in. Consume the chain here. */
            yetty_ycore_error_destroy(codepoint_res.error);
        } else {
            glyph_len = encode_codepoint_utf8(codepoint_res.value, glyph_bytes);
        }
    }
    if (glyph_len == 0) {
        glyph_len = strlen("✳");
        memcpy(glyph_bytes, "✳", glyph_len);
    }
    glyph_bytes[glyph_len] = '\0';
    snprintf(renderer->activity_glyph, sizeof(renderer->activity_glyph), "%s", glyph_bytes);
    struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "activity_set: redraw");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_activity_clear(struct yai_renderer *renderer)
{
    renderer->activity_glyph[0] = '\0';
    struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "activity_clear: redraw");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_hud_state(struct yai_renderer *renderer,
                                                      const char *text)
{
    snprintf(renderer->hud_state, sizeof(renderer->hud_state), "%s", text ? text : "");
    if (!renderer->text_hud) {
        return YETTY_OK_VOID(); /* no text bar to repaint (ygui HUD owns it) */
    }
    return yai_renderer_pin_redraw(renderer);
}

struct yetty_ycore_void_result yai_renderer_hud_stats(struct yai_renderer *renderer,
                                                      const char *text)
{
    snprintf(renderer->hud_stats, sizeof(renderer->hud_stats), "%s", text ? text : "");
    if (!renderer->text_hud) {
        return YETTY_OK_VOID(); /* no text bar to repaint (ygui HUD owns it) */
    }
    return yai_renderer_pin_redraw(renderer);
}

struct yetty_ycore_void_result yai_renderer_hud_quota(struct yai_renderer *renderer,
                                                      const char *text)
{
    snprintf(renderer->hud_quota, sizeof(renderer->hud_quota), "%s", text ? text : "");
    if (!renderer->text_hud) {
        return YETTY_OK_VOID(); /* no text bar to repaint (ygui HUD owns it) */
    }
    return yai_renderer_pin_redraw(renderer);
}

struct yetty_ycore_void_result yai_renderer_menu_set(struct yai_renderer *renderer,
                                                     const char *const *rows, size_t count)
{
    if (count > YAI_RENDERER_MENU_ROWS) {
        count = YAI_RENDERER_MENU_ROWS;
    }
    for (size_t row = 0; row < count; row++) {
        snprintf(renderer->menu_rows[row], sizeof(renderer->menu_rows[row]), "%s", rows[row]);
    }
    renderer->menu_row_count = count;
    struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "menu_set: redraw");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_menu_clear(struct yai_renderer *renderer)
{
    if (renderer->menu_row_count == 0) {
        return YETTY_OK_VOID();
    }
    renderer->menu_row_count = 0;
    struct yetty_ycore_void_result redraw_res = yai_renderer_pin_redraw(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, redraw_res, "menu_clear: redraw");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_overlay_set(struct yai_renderer *renderer,
                                                        const char *const *rows, size_t count)
{
    if (!renderer->pin_enabled) {
        return YETTY_OK_VOID();
    }
    if (count > YAI_RENDERER_OVERLAY_MAX_ROWS) {
        count = YAI_RENDERER_OVERLAY_MAX_ROWS;
    }
    for (size_t row = 0; row < count; row++) {
        snprintf(renderer->overlay_rows[row], sizeof(renderer->overlay_rows[row]), "%s", rows[row]);
    }
    renderer->overlay_row_count = count;
    overlay_paint(renderer);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "overlay_set: flush");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_overlay_clear(struct yai_renderer *renderer)
{
    renderer->overlay_row_count = 0;
    if (renderer->overlay_rows_drawn == 0) {
        return YETTY_OK_VOID();
    }
    overlay_erase(renderer);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "overlay_clear: flush");
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Streaming — deltas accumulate per line; complete lines go to history,
 * the in-progress remainder rides the zone's ticker row.
 *---------------------------------------------------------------------------*/

/* A streamed line / tool result this large is pathological; reject
 * rather than risk a capacity-doubling overflow. */
#define YAI_RENDER_MAX_BUF (256u * 1024u * 1024u)

static struct yetty_ycore_void_result stream_reserve(struct yai_renderer *renderer, size_t wanted)
{
    if (wanted <= renderer->stream_cap) {
        return YETTY_OK_VOID();
    }
    if (wanted > YAI_RENDER_MAX_BUF) {
        return YETTY_ERR(yetty_ycore_void, "stream_reserve: requested size exceeds cap");
    }
    size_t new_cap = renderer->stream_cap ? renderer->stream_cap : 4096;
    while (new_cap < wanted) {
        new_cap *= 2; /* bounded: wanted <= YAI_RENDER_MAX_BUF, so no wrap */
    }
    char *grown = realloc(renderer->stream_buf, new_cap);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "stream_reserve: realloc failed");
    }
    renderer->stream_buf = grown;
    renderer->stream_cap = new_cap;
    return YETTY_OK_VOID();
}

/* Print one completed streamed line into history. The zone must be
 * suspended; failures surface at the caller's flush. The first line of
 * a message carries its label. */
static void stream_print_line(struct yai_renderer *renderer, const char *line, size_t len)
{
    if (!renderer->stream_labeled) {
        if (renderer->stream_kind == YAI_STREAM_THINKING) {
            fputs("\n" YAI_MUTED "thinking" YAI_RESET " ", stdout);
        } else {
            printf("\n" YAI_MINT YAI_BOLD "%s" YAI_RESET " ", renderer->engine_label);
        }
        renderer->stream_labeled = 1;
    }
    if (renderer->stream_kind == YAI_STREAM_THINKING) {
        fputs(YAI_DIM, stdout);
    }
    fwrite(line, 1, len, stdout);
    fputs(YAI_RESET "\n", stdout);
}

/* Flush completed lines out of stream_buf; keep the partial tail. */
static void stream_flush_lines(struct yai_renderer *renderer)
{
    /* Markdown mode keeps the whole assistant message buffered and
     * renders it as one figure at finish — don't commit plain lines. */
    if (renderer->render_markdown && renderer->stream_kind == YAI_STREAM_TEXT) {
        return;
    }
    size_t start = 0;
    for (size_t index = 0; index < renderer->stream_len; index++) {
        if (renderer->stream_buf[index] != '\n') {
            continue;
        }
        stream_print_line(renderer, renderer->stream_buf + start, index - start);
        start = index + 1;
    }
    if (start > 0) {
        memmove(renderer->stream_buf, renderer->stream_buf + start, renderer->stream_len - start);
        renderer->stream_len -= start;
    }
}

/* End of a streamed run. In markdown mode, render the whole accumulated
 * assistant message as one yetty figure; otherwise flush the
 * unterminated remainder as plain text. */
static struct yetty_ycore_void_result stream_finish(struct yai_renderer *renderer)
{
    if (renderer->stream_kind == YAI_STREAM_NONE) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "stream_finish: suspend");

    int rendered_markdown = 0;
    if (renderer->render_markdown && renderer->stream_kind == YAI_STREAM_TEXT &&
        renderer->stream_len > 0) {
        struct yetty_ycore_int_result markdown_res =
            render_markdown_buffer(renderer, renderer->stream_buf, renderer->stream_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, markdown_res, "stream_finish: markdown render");
        rendered_markdown = markdown_res.value;
    }
    if (!rendered_markdown && renderer->stream_len > 0) {
        /* Plain text — either non-markdown mode (just the tail), or the
         * markdown fallback (the buffer holds whole lines; split them). */
        size_t start = 0;
        for (size_t index = 0; index < renderer->stream_len; index++) {
            if (renderer->stream_buf[index] != '\n') {
                continue;
            }
            stream_print_line(renderer, renderer->stream_buf + start, index - start);
            start = index + 1;
        }
        if (start < renderer->stream_len) {
            stream_print_line(renderer, renderer->stream_buf + start, renderer->stream_len - start);
        }
    }
    renderer->stream_len = 0;
    renderer->stream_kind = YAI_STREAM_NONE;
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "stream_finish: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "stream_finish: resume");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result stream_delta(struct yai_renderer *renderer, int kind,
                                                   const char *text, size_t len)
{
    if (renderer->stream_kind != YAI_STREAM_NONE && renderer->stream_kind != kind) {
        struct yetty_ycore_void_result finish_res = stream_finish(renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, finish_res, "stream_delta: finish previous kind");
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "stream_delta: suspend");
    renderer->stream_kind = kind;
    struct yetty_ycore_void_result reserve_res =
        stream_reserve(renderer, renderer->stream_len + len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "stream_delta: reserve");
    memcpy(renderer->stream_buf + renderer->stream_len, text, len);
    renderer->stream_len += len;
    stream_flush_lines(renderer);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "stream_delta: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "stream_delta: resume");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_begin_message(struct yai_renderer *renderer)
{
    /* A leftover unterminated line from the previous message must not
     * merge into this one. */
    struct yetty_ycore_void_result finish_res = stream_finish(renderer);
    renderer->stream_labeled = 0;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, finish_res, "begin_message: finish previous");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_text_delta(struct yai_renderer *renderer,
                                                       const char *text, size_t len)
{
    struct yetty_ycore_void_result delta_res = stream_delta(renderer, YAI_STREAM_TEXT, text, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res, "text_delta");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_thinking_delta(struct yai_renderer *renderer,
                                                           const char *text, size_t len)
{
    if (!renderer->show_thinking) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result delta_res =
        stream_delta(renderer, YAI_STREAM_THINKING, text, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, delta_res, "thinking_delta");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_end_thinking(struct yai_renderer *renderer)
{
    if (renderer->stream_kind != YAI_STREAM_THINKING) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result finish_res = stream_finish(renderer);
    renderer->stream_labeled = 0;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, finish_res, "end_thinking: finish");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_renderer_finish_turn(struct yai_renderer *renderer)
{
    struct yetty_ycore_void_result finish_res = stream_finish(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, finish_res, "finish_turn");
    return YETTY_OK_VOID();
}

/* Pick the most informative one-line summary for a tool call. */
void yai_summarize_tool_input(yyjson_val *input, char *out, size_t out_size)
{
    static const char *const preferred_keys[] = {
        "command", "file_path", "pattern", "path", "url", "query",
    };
    out[0] = '\0';
    if (!yyjson_is_obj(input)) {
        return;
    }
    for (size_t key_index = 0; key_index < sizeof(preferred_keys) / sizeof(preferred_keys[0]);
         key_index++) {
        yyjson_val *value = yyjson_obj_get(input, preferred_keys[key_index]);
        if (value && yyjson_is_str(value)) {
            snprintf(out, out_size, "%s", yyjson_get_str(value));
            break;
        }
    }
    if (!out[0]) {
        size_t json_len = 0;
        char *json_text = yyjson_val_write(input, 0, &json_len);
        if (json_text) {
            snprintf(out, out_size, "%s", json_text);
            free(json_text);
        }
    }
    /* Truncate with an ellipsis, and keep it one line. */
    for (char *cursor = out; *cursor; cursor++) {
        if (*cursor == '\n' || *cursor == '\r') {
            *cursor = ' ';
        }
    }
    if (strlen(out) >= out_size - 1 && out_size > 4) {
        strcpy(out + out_size - 5, "...");
    }
}

struct yetty_ycore_void_result yai_render_tool_call(struct yai_renderer *renderer, const char *name,
                                                    yyjson_val *input)
{
    /* Markdown mode buffers the assistant text; render it before the
     * tool line so ordering holds (text, then its tool call). */
    if (renderer->render_markdown) {
        struct yetty_ycore_void_result finish_res = stream_finish(renderer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, finish_res, "render_tool_call: finish text");
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "render_tool_call: suspend");
    char summary[YAI_SUMMARY_MAX + 1];
    yai_summarize_tool_input(input, summary, sizeof(summary));
    printf("\n" YAI_MUTED "⚙ " YAI_BOLD "%s" YAI_RESET " " YAI_DIM "%s" YAI_RESET "\n", name,
           summary);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "render_tool_call: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "render_tool_call: resume");
    return YETTY_OK_VOID();
}

/* Normalize a tool_result content field (string or block list) to one
 * heap string. Caller frees. Returns NULL on allocation failure. */
static char *tool_result_text(yyjson_val *content)
{
    if (yyjson_is_str(content)) {
        return strdup(yyjson_get_str(content));
    }
    if (!yyjson_is_arr(content)) {
        size_t json_len = 0;
        char *json_text = content ? yyjson_val_write(content, 0, &json_len) : NULL;
        return json_text ? json_text : strdup("");
    }
    size_t total = 0;
    yyjson_val *block;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(content, &iter);
    while ((block = yyjson_arr_iter_next(&iter)) != NULL) {
        yyjson_val *text = yyjson_is_obj(block) ? yyjson_obj_get(block, "text") : NULL;
        size_t piece = (text && yyjson_is_str(text)) ? strlen(yyjson_get_str(text)) + 1 : 64;
        /* Saturating add against a hard cap: adversarial tool output
         * must not wrap `total` into a small allocation. */
        if (piece > YAI_RENDER_MAX_BUF - total) {
            return NULL;
        }
        total += piece;
    }
    char *joined = calloc(1, total + 1);
    if (!joined) {
        return NULL;
    }
    size_t used = 0;
    yyjson_arr_iter_init(content, &iter);
    while ((block = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *piece = NULL;
        yyjson_val *text = yyjson_is_obj(block) ? yyjson_obj_get(block, "text") : NULL;
        if (text && yyjson_is_str(text)) {
            piece = yyjson_get_str(text);
        }
        if (used > 0) {
            joined[used++] = '\n';
        }
        if (piece) {
            size_t piece_len = strlen(piece);
            if (used + piece_len > total) {
                piece_len = total - used;
            }
            memcpy(joined + used, piece, piece_len);
            used += piece_len;
        } else {
            int wrote = snprintf(joined + used, total + 1 - used, "(non-text block)");
            used += (wrote > 0) ? (size_t)wrote : 0;
        }
    }
    joined[used] = '\0';
    return joined;
}

static int base64_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

/* Decode base64 in [encoded, encoded+len) into a heap buffer. Caller
 * frees. Returns NULL on malformed input or allocation failure. */
static unsigned char *base64_decode(const char *encoded, size_t len, size_t *out_len)
{
    unsigned char *decoded = malloc((len / 4 + 1) * 3);
    if (!decoded) {
        return NULL;
    }
    size_t produced = 0;
    int accumulator = 0;
    int bits = 0;
    for (size_t index = 0; index < len; index++) {
        char ch = encoded[index];
        if (ch == '=') {
            break;
        }
        int value = base64_value(ch);
        if (value < 0) {
            free(decoded);
            return NULL;
        }
        accumulator = (accumulator << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded[produced++] = (unsigned char)((accumulator >> bits) & 0xFF);
        }
    }
    *out_len = produced;
    return decoded;
}

/* Write all `len` bytes to `fd`, polling through EAGAIN. The uv_poll on
 * STDIN_FILENO put the tty in non-blocking mode, and fd 0/1 share the
 * tty's open file description — so stdout is NON-BLOCKING here. A plain
 * fwrite of an envelope larger than the kernel PTY buffer (~64 KB; music
 * scores with embedded fonts are ~100 KB) hits EAGAIN mid-stream and
 * silently truncates the DCS, which the terminal then discards whole. */
static struct yetty_ycore_void_result write_all_to_terminal(int fd, const unsigned char *bytes,
                                                            size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t wrote = write(fd, bytes + written, len - written);
        if (wrote > 0) {
            written += (size_t)wrote;
            continue;
        }
        if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd ready = {.fd = fd, .events = POLLOUT};
            (void)poll(&ready, 1, -1);
            continue;
        }
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        /* A real write error mid-envelope — the figure is lost either
         * way; surface it. */
        return YETTY_ERR(yetty_ycore_void, "write_all_to_terminal: write failed");
    }
    return YETTY_OK_VOID();
}

/* Write `envelope_len` figure bytes to the terminal, preceded by the
 * tool checkmark line. Flushes around the raw write so the envelope
 * can't be cut in half by buffered text (single-writer discipline). */
static struct yetty_ycore_void_result write_figure_envelope(const unsigned char *envelope,
                                                            size_t envelope_len,
                                                            const char *tool_name)
{
    printf("  " YAI_MINT "✓" YAI_RESET " " YAI_MUTED "%s" YAI_RESET "\n",
           tool_name ? tool_name : "figure");
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "write_figure_envelope: pre-flush");
    struct yetty_ycore_void_result write_res =
        write_all_to_terminal(STDOUT_FILENO, envelope, envelope_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "write_figure_envelope: envelope write");
    return YETTY_OK_VOID();
}

/* Run `ycat -w <cols> -c <kind> <path>` with TERM_PROGRAM=yetty and
 * capture its stdout — the OSC figure envelope — into a malloc'd buffer.
 * yai is the single PTY writer, so the child's stdout is captured here
 * (a pipe), never written to the tty directly. Value 1 with out and
 * out_len set on success; value 0 when ycat is absent or produced
 * nothing, so the caller falls back to plain text. */
static struct yetty_ycore_int_result run_render_tool(const char *kind, const char *path,
                                                     unsigned char **out, size_t *out_len)
{
    char columns_text[16];
    snprintf(columns_text, sizeof(columns_text), "%d", terminal_columns());
    const char *bin_dir = getenv("YETTY_BIN_DIR");
    char tool_path[1024];
    if (bin_dir && bin_dir[0]) {
        snprintf(tool_path, sizeof(tool_path), "%s/ycat", bin_dir);
    } else {
        snprintf(tool_path, sizeof(tool_path), "ycat");
    }
    char *const argv[] = {tool_path, "-w", columns_text, "-c", (char *)kind, (char *)path, NULL};

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return YETTY_ERR(yetty_ycore_int, "run_render_tool: pipe failed");
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return YETTY_ERR(yetty_ycore_int, "run_render_tool: fork failed");
    }
    if (pid == 0) {
        /* Child: stdout -> pipe, stderr -> /dev/null (its chatter must
         * never reach our tty), and TERM_PROGRAM=yetty so ycat emits the
         * envelope instead of plain text. */
        dup2(pipe_fds[1], STDOUT_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        setenv("TERM_PROGRAM", "yetty", 1);
        execvp(tool_path, argv);
        _exit(127);
    }
    close(pipe_fds[1]);
    size_t cap = 64 * 1024;
    size_t len = 0;
    unsigned char *buffer = malloc(cap);
    if (!buffer) {
        close(pipe_fds[0]);
        waitpid(pid, NULL, 0);
        return YETTY_ERR(yetty_ycore_int, "run_render_tool: alloc failed");
    }
    int overflow = 0;
    for (;;) {
        if (len == cap) {
            if (cap >= YAI_FIGURE_SPOOL_MAX_BYTES) {
                overflow = 1;
                break;
            }
            cap *= 2;
            unsigned char *grown = realloc(buffer, cap);
            if (!grown) {
                free(buffer);
                close(pipe_fds[0]);
                waitpid(pid, NULL, 0);
                return YETTY_ERR(yetty_ycore_int, "run_render_tool: realloc failed");
            }
            buffer = grown;
        }
        ssize_t chunk = read(pipe_fds[0], buffer + len, cap - len);
        if (chunk < 0 && errno == EINTR) {
            continue;
        }
        if (chunk <= 0) {
            break;
        }
        len += (size_t)chunk;
    }
    /* Closing the read end unblocks the child if it overflowed our cap
     * (its next write gets EPIPE), so waitpid can never hang. */
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (overflow || len == 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buffer);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    *out = buffer;
    *out_len = len;
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_int_result render_markdown_buffer(struct yai_renderer *renderer,
                                                            const char *bytes, size_t len)
{
    /* Render the answer to a ydraw drawable list in-process — the same
     * yetty_ymarkdown path ycat drives — and frame it as a YDRAW_BIN OSC
     * envelope ourselves (no ycat subprocess). The terminal ingests the
     * envelope at the cursor's grid line and reserves rows with real
     * newlines, so the rich text scrolls with the conversation. Cell metrics
     * mirror ycat's defaults (8x16, width = terminal columns). */
    struct yetty_ymarkdown_render_config config = {
        .cell_width = 8,
        .cell_height = 16,
        .width_cells = (uint32_t)terminal_columns(),
        .height_cells = 0,
    };
    struct yetty_ymarkdown_render_result render_res =
        yetty_ymarkdown_render(bytes, len, NULL, 0, &config);
    if (YETTY_IS_ERR(render_res)) {
        /* Best-effort: a render failure falls back to plain text. */
        yetty_ycore_error_destroy(render_res.error);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ydraw_drawable_list *drawables = render_res.value.buffer;

    /* Serialize + frame as a YDRAW_BIN envelope (mirrors
     * yetty_ycat_osc_bin_emit, writing through yai's non-blocking PTY
     * writer). */
    const uint8_t *raw_bytes = NULL;
    size_t raw_size = yetty_ydraw_drawable_list_serialize(drawables, &raw_bytes);
    if (raw_size == 0 || !raw_bytes) {
        yetty_ydraw_drawable_list_destroy(drawables);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw_bytes, raw_size, &envelope);
    yetty_ydraw_drawable_list_destroy(drawables);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_int, "render_markdown_buffer: yface_emit", emit_res);
    }
    if (envelope.size == 0) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Label it like a normal answer, then the figure, then a newline so
     * the next write starts below the figure. */
    printf("\n" YAI_MINT YAI_BOLD "%s" YAI_RESET "\n", renderer->engine_label);
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    if (YETTY_IS_ERR(flush_res)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_int, "render_markdown_buffer: pre-flush", flush_res);
    }
    struct yetty_ycore_void_result write_res =
        write_all_to_terminal(STDOUT_FILENO, envelope.data, envelope.size);
    yetty_ycore_buffer_destroy(&envelope);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, write_res, "render_markdown_buffer: envelope write");
    if (fputc('\n', stdout) == EOF) {
        return YETTY_ERR(yetty_ycore_int, "render_markdown_buffer: trailing newline");
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/* A sentinel path arrives inside model-visible tool output — UNTRUSTED.
 * Accept only what the MCP server's tempfile actually produces: an
 * absolute path in the temp directory whose basename is
 * <prefix>*<suffix>, with no ".." anywhere. An empty suffix matches any. */
static int spool_path_acceptable(const char *path, const char *prefix, const char *suffix)
{
    if (path[0] != '/' || strstr(path, "..")) {
        return 0;
    }
    const char *basename_start = strrchr(path, '/') + 1;
    size_t basename_len = strlen(basename_start);
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);
    if (basename_len <= prefix_len + suffix_len ||
        strncmp(basename_start, prefix, prefix_len) != 0 ||
        strcmp(basename_start + basename_len - suffix_len, suffix) != 0) {
        return 0;
    }
    /* Directory must be the temp dir (same resolution order as the
     * server's tempfile: TMPDIR, TEMP, TMP, /tmp). The server inherits
     * our environment, so the values agree. */
    const char *temp_dir = NULL;
    static const char *const temp_env_names[] = {"TMPDIR", "TEMP", "TMP"};
    for (size_t name_index = 0; name_index < 3; name_index++) {
        const char *value = getenv(temp_env_names[name_index]);
        if (value && value[0]) {
            temp_dir = value;
            break;
        }
    }
    if (!temp_dir) {
        temp_dir = "/tmp";
    }
    size_t dir_len = (size_t)(basename_start - path) - 1; /* exclude the slash */
    size_t temp_len = strlen(temp_dir);
    while (temp_len > 1 && temp_dir[temp_len - 1] == '/') {
        temp_len--;
    }
    return dir_len == temp_len && strncmp(path, temp_dir, dir_len) == 0;
}

/* FILE sentinel: the payload between the markers is a path to a temp file
 * holding the raw envelope bytes. Validate, read, write to the terminal,
 * unlink. Value 1 = a figure was emitted, 0 = no (caller falls back to
 * the text preview); real write failures are errors. */
static struct yetty_ycore_int_result try_render_figure_file(const char *raw, const char *tool_name)
{
    const char *open_marker = strstr(raw, YAI_FIGURE_FILE_SENTINEL_OPEN);
    if (!open_marker) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const char *payload = open_marker + strlen(YAI_FIGURE_FILE_SENTINEL_OPEN);
    const char *close_marker = strstr(payload, YAI_FIGURE_FILE_SENTINEL_CLOSE);
    if (!close_marker) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    size_t path_len = (size_t)(close_marker - payload);
    char path[1024];
    if (path_len == 0 || path_len >= sizeof(path)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    memcpy(path, payload, path_len);
    path[path_len] = '\0';
    if (!spool_path_acceptable(path, YAI_FIGURE_SPOOL_PREFIX, YAI_FIGURE_SPOOL_SUFFIX)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    int spool_fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (spool_fd < 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct stat spool_stat;
    if (fstat(spool_fd, &spool_stat) != 0 || !S_ISREG(spool_stat.st_mode) ||
        spool_stat.st_uid != getuid() || spool_stat.st_size <= 0 ||
        (uint64_t)spool_stat.st_size > YAI_FIGURE_SPOOL_MAX_BYTES) {
        close(spool_fd);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    size_t envelope_len = (size_t)spool_stat.st_size;
    unsigned char *envelope = malloc(envelope_len);
    if (!envelope) {
        close(spool_fd);
        return YETTY_ERR(yetty_ycore_int, "try_render_figure_file: envelope alloc failed");
    }
    size_t filled = 0;
    while (filled < envelope_len) {
        ssize_t chunk = read(spool_fd, envelope + filled, envelope_len - filled);
        if (chunk < 0 && errno == EINTR) {
            continue;
        }
        if (chunk <= 0) {
            break;
        }
        filled += (size_t)chunk;
    }
    close(spool_fd);
    unlink(path);
    if (filled == 0) {
        free(envelope);
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_void_result emit_res = write_figure_envelope(envelope, filled, tool_name);
    free(envelope);
    if (YETTY_IS_ERR(emit_res)) {
        return YETTY_ERR(yetty_ycore_int, "try_render_figure_file: emit", emit_res);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/* If `raw` carries a parent-render figure sentinel (FILE or legacy
 * inline-base64), write the OSC envelope bytes straight to the terminal.
 * Value 1 = a figure was emitted. */
static struct yetty_ycore_int_result try_render_figure(const char *raw, const char *tool_name)
{
    struct yetty_ycore_int_result file_res = try_render_figure_file(raw, tool_name);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, file_res, "try_render_figure: file sentinel");
    if (file_res.value) {
        return file_res;
    }
    const char *open_marker = strstr(raw, YAI_FIGURE_SENTINEL_OPEN);
    if (!open_marker) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const char *payload = open_marker + strlen(YAI_FIGURE_SENTINEL_OPEN);
    const char *close_marker = strstr(payload, YAI_FIGURE_SENTINEL_CLOSE);
    if (!close_marker) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    size_t decoded_len = 0;
    unsigned char *decoded = base64_decode(payload, (size_t)(close_marker - payload), &decoded_len);
    if (!decoded) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_void_result emit_res =
        write_figure_envelope(decoded, decoded_len, tool_name);
    free(decoded);
    if (YETTY_IS_ERR(emit_res)) {
        return YETTY_ERR(yetty_ycore_int, "try_render_figure: emit", emit_res);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Copy the value of `key` (e.g. "kind=") from the marker body [begin,end)
 * into `out`, stopping at whitespace or `end`. Value 1 if a non-empty
 * value was found and fit. */
static int extract_marker_field(const char *begin, const char *end, const char *key, char *out,
                                size_t out_size)
{
    const char *found = strstr(begin, key);
    if (!found || found >= end) {
        return 0;
    }
    const char *value = found + strlen(key);
    size_t len = 0;
    while (value + len < end && value[len] != ' ' && value[len] != '\t' && len < out_size - 1) {
        len++;
    }
    if (len == 0) {
        return 0;
    }
    memcpy(out, value, len);
    out[len] = '\0';
    return 1;
}

/* A render kind is passed as a literal argv element to ycat (no shell),
 * so injection is impossible; still bound it to lowercase letters so a
 * malformed marker can't smuggle flags or paths. */
static int draw_kind_acceptable(const char *kind)
{
    if (!kind[0]) {
        return 0;
    }
    for (size_t index = 0; kind[index]; index++) {
        if (kind[index] < 'a' || kind[index] > 'z') {
            return 0;
        }
    }
    return 1;
}

/* YAI:DRAW sentinel: the agent staged raw content in a temp file and
 * named the render kind. Validate, run the tool (ycat -c <kind>), emit
 * the envelope, unlink. Value 1 = a figure was emitted. */
static struct yetty_ycore_int_result try_render_draw(const char *raw, const char *tool_name)
{
    const char *open_marker = strstr(raw, YAI_DRAW_SENTINEL_OPEN);
    if (!open_marker) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    const char *body = open_marker + strlen(YAI_DRAW_SENTINEL_OPEN);
    const char *close_marker = strstr(body, YAI_DRAW_SENTINEL_CLOSE);
    if (!close_marker || close_marker == body) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    char kind[32] = "";
    char path[1024] = "";
    if (!extract_marker_field(body, close_marker, "kind=", kind, sizeof(kind)) ||
        !extract_marker_field(body, close_marker, "path=", path, sizeof(path))) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (!draw_kind_acceptable(kind) || !spool_path_acceptable(path, YAI_DRAW_SPOOL_PREFIX, "")) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    unsigned char *envelope = NULL;
    size_t envelope_len = 0;
    struct yetty_ycore_int_result run_res = run_render_tool(kind, path, &envelope, &envelope_len);
    unlink(path);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, run_res, "try_render_draw: run tool");
    if (!run_res.value) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_void_result emit_res =
        write_figure_envelope(envelope, envelope_len, tool_name);
    free(envelope);
    if (YETTY_IS_ERR(emit_res)) {
        return YETTY_ERR(yetty_ycore_int, "try_render_draw: emit", emit_res);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

struct yetty_ycore_void_result yai_render_tool_result(struct yai_renderer *renderer,
                                                      yyjson_val *content, int is_error,
                                                      const char *tool_name)
{
    char *raw = tool_result_text(content);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "render_tool_result: tool_result_text alloc failed");
    }
    /* Render any buffered assistant text before the result (markdown mode). */
    if (renderer->render_markdown) {
        struct yetty_ycore_void_result finish_res = stream_finish(renderer);
        if (YETTY_IS_ERR(finish_res)) {
            free(raw);
            return YETTY_ERR(yetty_ycore_void, "render_tool_result: finish text", finish_res);
        }
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    if (YETTY_IS_ERR(suspend_res)) {
        free(raw);
        return YETTY_ERR(yetty_ycore_void, "render_tool_result: suspend", suspend_res);
    }
    if (!is_error) {
        /* Two parent-render paths: YAI:DRAW (raw content + kind — yai
         * runs the tool) and the figure sentinel (a pre-rendered
         * envelope yai just relays). Either emits and we're done. */
        struct yetty_ycore_int_result draw_res = try_render_draw(raw, tool_name);
        if (YETTY_IS_ERR(draw_res)) {
            free(raw);
            struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(renderer);
            if (YETTY_IS_ERR(resume_res)) {
                yetty_ycore_error_destroy(resume_res.error);
            }
            return YETTY_ERR(yetty_ycore_void, "render_tool_result: draw", draw_res);
        }
        struct yetty_ycore_int_result figure_res = {.ok = 1, .value = 0};
        if (!draw_res.value) {
            figure_res = try_render_figure(raw, tool_name);
        }
        if (YETTY_IS_ERR(figure_res)) {
            free(raw);
            /* Never leave the zone suspended: restore it before
             * surfacing the figure error (best-effort). */
            struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(renderer);
            if (YETTY_IS_ERR(resume_res)) {
                yetty_ycore_error_destroy(resume_res.error);
            }
            return YETTY_ERR(yetty_ycore_void, "render_tool_result: figure", figure_res);
        }
        if (draw_res.value || figure_res.value) {
            free(raw);
            struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(renderer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "render_tool_result: resume");
            return YETTY_OK_VOID();
        }
    }

    /* Trim trailing newlines; empty result becomes a placeholder. */
    size_t raw_len = strlen(raw);
    while (raw_len > 0 && (raw[raw_len - 1] == '\n' || raw[raw_len - 1] == '\r')) {
        raw[--raw_len] = '\0';
    }
    const char *body = raw;
    int only_whitespace = 1;
    for (const char *cursor = body; *cursor; cursor++) {
        if (*cursor != ' ' && *cursor != '\t' && *cursor != '\n' && *cursor != '\r') {
            only_whitespace = 0;
            break;
        }
    }
    if (only_whitespace) {
        body = "(no output)";
    }

    printf("  %s" YAI_RESET, is_error ? YAI_RED "✗" : YAI_MINT "✓");
    if (tool_name) {
        printf(" " YAI_MUTED "%s" YAI_RESET, tool_name);
    }
    fputc('\n', stdout);

    /* First fold_lines lines, each indented two spaces; note the rest. */
    const char *style = is_error ? YAI_RED : YAI_DIM;
    int shown = 0;
    size_t hidden = 0;
    const char *line = body;
    while (*line) {
        const char *line_end = strchr(line, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);
        if (shown < renderer->fold_lines) {
            printf("%s  %.*s" YAI_RESET "\n", style, (int)line_len, line);
            shown++;
        } else {
            hidden++;
        }
        if (!line_end) {
            break;
        }
        line = line_end + 1;
    }
    if (hidden > 0) {
        printf(YAI_DIM "    … %zu more line(s) (full in transcript)" YAI_RESET "\n", hidden);
    }
    free(raw);
    /* Best-effort: the zone must be restored even if the flush failed,
     * else the prompt stays hidden. Chain both, surface the first. */
    struct yetty_ycore_void_result tail = yai_render_flush_stdout();
    tail = yetty_ycore_void_chain(tail, yai_renderer_zone_resume(renderer));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tail, "render_tool_result: flush/resume");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_render_hook(struct yai_renderer *renderer, yyjson_val *event)
{
    const char *name = NULL;
    static const char *const name_keys[] = {"hook_event_name", "subtype", "event"};
    for (size_t key_index = 0; key_index < sizeof(name_keys) / sizeof(name_keys[0]); key_index++) {
        yyjson_val *value = yyjson_obj_get(event, name_keys[key_index]);
        if (value && yyjson_is_str(value)) {
            name = yyjson_get_str(value);
            break;
        }
    }
    struct yetty_ycore_void_result suspend_res = yai_renderer_zone_suspend(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, suspend_res, "render_hook: suspend");
    printf(YAI_MUTED "⤷ hook: %s" YAI_RESET "\n", name ? name : "hook");
    struct yetty_ycore_void_result flush_res = yai_render_flush_stdout();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "render_hook: flush");
    struct yetty_ycore_void_result resume_res = yai_renderer_zone_resume(renderer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resume_res, "render_hook: resume");
    return YETTY_OK_VOID();
}

void yai_format_tokens(uint64_t count, char *out, size_t out_size)
{
    if (count >= 1000) {
        snprintf(out, out_size, "%.1fk", (double)count / 1000.0);
    } else {
        snprintf(out, out_size, "%llu", (unsigned long long)count);
    }
}
