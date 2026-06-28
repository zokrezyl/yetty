/*
 * render.h — scrollback rendering for ccc.
 *
 * Everything here writes styled text to stdout. The terminal scrollback
 * stays the primary surface (it scrolls like any CLI); the non-scrolling
 * status lives in the HUD (hud.h).
 */
#ifndef CCC_RENDER_H
#define CCC_RENDER_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yyjson.h>

/* Completion-menu rows the pinned zone can host below the prompt. */
#define CCC_RENDERER_MENU_ROWS 5
#define CCC_RENDERER_MENU_ROW_BYTES 192

/* ANSI styling — ccc owns its pane, so it styles its own output. */
#define CCC_DIM "\033[2m"
#define CCC_BOLD "\033[1m"
#define CCC_MINT "\033[38;2;107;168;146m" /* brand accent */
#define CCC_MUTED "\033[38;2;133;141;143m"
#define CCC_RED "\033[38;2;220;100;100m"
#define CCC_RESET "\033[0m"

/* Per-message streaming state + render configuration. */
struct ccc_renderer {
    int fold_lines;    /* tool-output preview cap */
    int show_thinking; /* stream dim thinking text */

    /*
     * Pinned zone — transient rows at the bottom of the scrollback,
     * erased before any history write and redrawn after:
     *
     *   [stream ticker]   tail of the in-progress streamed line (clipped
     *                     to one row; completed lines go to history)
     *   [prompt]          "<glyph> you ▸ <editor window>" — always
     *                     visible, so the user can type/queue commands
     *                     while a turn is in flight; the cursor parks
     *                     here at the editor's cursor column
     *   [menu rows]       slash-command completion rows (prerendered by
     *                     main.c, owned here so async writes repaint
     *                     them safely)
     *
     * The glyph is an animated shader glyph (PUA-B codepoint, rendered
     * by the host's text layer); every zone row is clipped to one
     * terminal row so the erase sequence can never miss a wrapped row,
     * and no glyph is ever left animating in history (a visible shader
     * glyph pins the host's animation timer).
     */
    int pin_enabled;         /* stdout is a tty — the zone may be drawn */
    int pin_shader_glyphs;   /* host is yetty: PUA-B glyphs animate */
    int pin_active;          /* prompt pinned (off during shutdown/shell) */
    int zone_visible;        /* the zone is on screen right now */
    int zone_rows_above;     /* ticker rows drawn above the prompt row */
    int zone_rows_below;     /* menu rows drawn below the prompt row */
    char activity_glyph[8];  /* UTF-8 glyph; "" while idle */
    const char *edit_buffer; /* the line editor's buffer (borrowed) */
    const size_t *edit_length;
    const size_t *edit_cursor; /* byte offset of the editor cursor */
    char menu_rows[CCC_RENDERER_MENU_ROWS][CCC_RENDERER_MENU_ROW_BYTES];
    size_t menu_row_count;

    /* Streamed-line assembly: deltas accumulate here; complete lines
     * are flushed into history, the remainder feeds the ticker row. */
    char *stream_buf;
    size_t stream_len;
    size_t stream_cap;
    int stream_kind;    /* 0 none, 1 assistant text, 2 thinking */
    int stream_labeled; /* "claude"/"thinking" label already printed */
};

void ccc_renderer_init(struct ccc_renderer *renderer, int fold_lines, int show_thinking);
void ccc_renderer_destroy(struct ccc_renderer *renderer);

/* A new assistant message started (stream-json message_start). */
void ccc_renderer_begin_message(struct ccc_renderer *renderer);

/* Streamed deltas. `text` need not be NUL-terminated; `len` bounds it. */
void ccc_renderer_text_delta(struct ccc_renderer *renderer, const char *text, size_t len);
void ccc_renderer_thinking_delta(struct ccc_renderer *renderer, const char *text, size_t len);
void ccc_renderer_end_thinking(struct ccc_renderer *renderer);

/* The turn's result event arrived — flush any open streamed line. */
void ccc_renderer_finish_turn(struct ccc_renderer *renderer);

/*
 * Pinned zone control.
 *
 *   pin_setup   — register the line editor's buffer (borrowed pointers;
 *                 the buffer must outlive the renderer)
 *   pin_show    — pin the prompt row (idempotent; redraws)
 *   pin_hide    — remove the zone entirely (shutdown, shell handoff)
 *   pin_redraw  — the editor buffer/cursor changed: repaint the zone
 *   zone_suspend— erase the zone before writing history content
 *   zone_resume — redraw after a newline-terminated write (cursor must
 *                 be at the start of a fresh line)
 *   menu_set    — adopt prerendered completion rows (copied; redraws)
 *   menu_clear  — drop the completion rows (redraws)
 *
 * Activity glyph: `glyph_name` is a shader-glyph name from yfont's
 * table ("typing-dots", "orbit-dots", "hourglass", …); when the host is
 * not yetty (or the name is unknown) a static "✳" stands in.
 */
void ccc_renderer_pin_setup(struct ccc_renderer *renderer, const char *edit_buffer,
                            const size_t *edit_length, const size_t *edit_cursor);
void ccc_renderer_pin_show(struct ccc_renderer *renderer);
void ccc_renderer_pin_hide(struct ccc_renderer *renderer);
void ccc_renderer_pin_redraw(struct ccc_renderer *renderer);
void ccc_renderer_zone_suspend(struct ccc_renderer *renderer);
void ccc_renderer_zone_resume(struct ccc_renderer *renderer);
void ccc_renderer_activity_set(struct ccc_renderer *renderer, const char *glyph_name);
void ccc_renderer_activity_clear(struct ccc_renderer *renderer);
void ccc_renderer_menu_set(struct ccc_renderer *renderer, const char *const *rows, size_t count);
void ccc_renderer_menu_clear(struct ccc_renderer *renderer);

/* One tool invocation line: "⚙ name <one-line input summary>". */
void ccc_render_tool_call(struct ccc_renderer *renderer, const char *name, yyjson_val *input);

/* One-line summary of a tool input object (most informative field, or
 * compact JSON), truncated with an ellipsis. Used for the tool-call
 * line and the permission prompt. */
void ccc_summarize_tool_input(yyjson_val *input, char *out, size_t out_size);

/* One tool result: folded preview, or raw figure bytes when the yetty
 * MCP server handed the OSC envelope back via the parent-render
 * sentinel. `content` is the tool_result content field (string or
 * block list). Fails on allocation failure. */
struct yetty_ycore_void_result ccc_render_tool_result(struct ccc_renderer *renderer,
                                                      yyjson_val *content, int is_error,
                                                      const char *tool_name);

/* Best-effort one-liner for a hook lifecycle frame. */
void ccc_render_hook(struct ccc_renderer *renderer, yyjson_val *event);

/* Compact token count: 2391 -> "2.4k", 970 -> "970". `out` must hold
 * at least 16 bytes. */
void ccc_format_tokens(uint64_t count, char *out, size_t out_size);

#endif /* CCC_RENDER_H */
