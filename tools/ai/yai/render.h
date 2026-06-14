/*
 * render.h — scrollback rendering for yai.
 *
 * Everything here writes styled text to stdout. The terminal scrollback
 * stays the primary surface (it scrolls like any CLI); the non-scrolling
 * status lives in the HUD (hud.h).
 *
 * Every function that can fail — anything that writes to stdout,
 * allocates, or both — returns a Result and chains its causes. Pure
 * state setters (pointer registration, flag flips) stay void.
 */
#ifndef YAI_RENDER_H
#define YAI_RENDER_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yyjson.h>

/* Completion-menu rows the pinned zone can host below the prompt. */
#define YAI_RENDERER_MENU_ROWS 5
#define YAI_RENDERER_MENU_ROW_BYTES 192

/* ANSI styling — yai owns its pane, so it styles its own output. */
#define YAI_DIM "\033[2m"
#define YAI_BOLD "\033[1m"
#define YAI_MINT "\033[38;2;107;168;146m" /* brand accent */
#define YAI_MUTED "\033[38;2;133;141;143m"
#define YAI_RED "\033[38;2;220;100;100m"
#define YAI_RESET "\033[0m"

/* Per-message streaming state + render configuration. */
struct yai_renderer {
    int fold_lines;    /* tool-output preview cap */
    int show_thinking; /* stream dim thinking text */
    /* Render completed assistant answers as a yetty markdown figure
     * (via ycat) instead of plain text. Opt-in via --markdown, and only
     * on the yetty host (the figure envelope can't display elsewhere);
     * falls back to plain text if ycat is unavailable or fails. */
    int render_markdown;
    /* Use the animated shader glyph for the activity indicator. OFF by
     * default: an animated glyph keeps yetty's renderer running every
     * frame (≈100% of a CPU core for the whole turn). Opt in with
     * --animate; otherwise a static glyph is used and yetty idles when
     * nothing changes. */
    int animate_glyph;
    /* Text status bar at the bottom of the pinned zone, for non-yetty
     * terminals (where the ygui HUD window can't render). Active when
     * stdout is a tty but the host is not yetty. hud_state = left
     * (activity), hud_stats = right (model + token/cost summary). */
    int text_hud;
    char hud_state[160];
    char hud_stats[192];

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
    int pin_enabled;          /* stdout is a tty — the zone may be drawn */
    int pin_shader_glyphs;    /* host is yetty: PUA-B glyphs animate */
    int pin_active;           /* prompt pinned (off during shutdown/shell) */
    int zone_visible;         /* the zone is on screen right now */
    int zone_rows_above;      /* ticker rows drawn above the prompt row */
    int zone_rows_below;      /* menu rows drawn below the prompt row */
    char activity_glyph[8];   /* UTF-8 glyph; "" while idle */
    const char *edit_buffer;  /* the line editor's buffer (borrowed) */
    const size_t *edit_length;
    const size_t *edit_cursor;   /* byte offset of the editor cursor */
    const char *edit_status_ptr; /* borrowed mode indicator ("[N]"/""); may be NULL */
    char menu_rows[YAI_RENDERER_MENU_ROWS][YAI_RENDERER_MENU_ROW_BYTES];
    size_t menu_row_count;

    /* Streamed-line assembly: deltas accumulate here; complete lines
     * are flushed into history, the remainder feeds the ticker row. */
    char *stream_buf;
    size_t stream_len;
    size_t stream_cap;
    int stream_kind;    /* 0 none, 1 assistant text, 2 thinking */
    int stream_labeled; /* "<engine>"/"thinking" label already printed */
    /* Engine display name for the streamed-message label ("claude",
     * "codex", "gemini"); borrowed, set once at init. */
    const char *engine_label;
};

void yai_renderer_init(struct yai_renderer *renderer, int fold_lines, int show_thinking,
                       const char *engine_label);
void yai_renderer_destroy(struct yai_renderer *renderer);

/* A new assistant message started (stream-json message_start). */
struct yetty_ycore_void_result yai_renderer_begin_message(struct yai_renderer *renderer);

/* Streamed deltas. `text` need not be NUL-terminated; `len` bounds it. */
struct yetty_ycore_void_result yai_renderer_text_delta(struct yai_renderer *renderer,
                                                       const char *text, size_t len);
struct yetty_ycore_void_result yai_renderer_thinking_delta(struct yai_renderer *renderer,
                                                           const char *text, size_t len);
struct yetty_ycore_void_result yai_renderer_end_thinking(struct yai_renderer *renderer);

/* The turn's result event arrived — flush any open streamed line. */
struct yetty_ycore_void_result yai_renderer_finish_turn(struct yai_renderer *renderer);

/*
 * Pinned zone control.
 *
 *   pin_setup   — register the line editor's buffer (borrowed pointers;
 *                 the buffer must outlive the renderer). Cannot fail.
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
void yai_renderer_pin_setup(struct yai_renderer *renderer, const char *edit_buffer,
                            const size_t *edit_length, const size_t *edit_cursor);
struct yetty_ycore_void_result yai_renderer_pin_show(struct yai_renderer *renderer);
struct yetty_ycore_void_result yai_renderer_pin_hide(struct yai_renderer *renderer);
struct yetty_ycore_void_result yai_renderer_pin_redraw(struct yai_renderer *renderer);
struct yetty_ycore_void_result yai_renderer_zone_suspend(struct yai_renderer *renderer);
struct yetty_ycore_void_result yai_renderer_zone_resume(struct yai_renderer *renderer);
struct yetty_ycore_void_result yai_renderer_activity_set(struct yai_renderer *renderer,
                                                         const char *glyph_name);
struct yetty_ycore_void_result yai_renderer_activity_clear(struct yai_renderer *renderer);
struct yetty_ycore_void_result yai_renderer_menu_set(struct yai_renderer *renderer,
                                                     const char *const *rows, size_t count);
struct yetty_ycore_void_result yai_renderer_menu_clear(struct yai_renderer *renderer);

/* Text-HUD status bar (non-yetty terminals). Set the left (activity) and
 * right (model/usage) segments; redraws the zone when the text HUD is
 * active, otherwise just records the text. */
struct yetty_ycore_void_result yai_renderer_hud_state(struct yai_renderer *renderer,
                                                      const char *text);
struct yetty_ycore_void_result yai_renderer_hud_stats(struct yai_renderer *renderer,
                                                      const char *text);

/* Reserve / release the bottom terminal row for the text-HUD bar via a
 * DECSTBM scroll region (conversation text scrolls above it). Call
 * reserve at startup and after a resize / shell / alt-screen, release at
 * shutdown and before handing the terminal to a shell or the alt screen.
 * No-ops unless the text HUD is active. */
struct yetty_ycore_void_result yai_renderer_text_hud_reserve(struct yai_renderer *renderer);
struct yetty_ycore_void_result yai_renderer_text_hud_release(struct yai_renderer *renderer);

/* One tool invocation line: "⚙ name <one-line input summary>". */
struct yetty_ycore_void_result yai_render_tool_call(struct yai_renderer *renderer, const char *name,
                                                    yyjson_val *input);

/* One-line summary of a tool input object (most informative field, or
 * compact JSON), truncated with an ellipsis. Used for the tool-call
 * line and the permission prompt. Always produces a valid (possibly
 * empty) string in `out` — cannot fail. */
void yai_summarize_tool_input(yyjson_val *input, char *out, size_t out_size);

/* One tool result: folded preview, or raw figure bytes when the yetty
 * MCP server handed the OSC envelope back via the parent-render
 * sentinel. `content` is the tool_result content field (string or
 * block list). */
struct yetty_ycore_void_result yai_render_tool_result(struct yai_renderer *renderer,
                                                      yyjson_val *content, int is_error,
                                                      const char *tool_name);

/* Best-effort one-liner for a hook lifecycle frame. */
struct yetty_ycore_void_result yai_render_hook(struct yai_renderer *renderer, yyjson_val *event);

/* Compact token count: 2391 -> "2.4k", 970 -> "970". `out` must hold
 * at least 16 bytes. Cannot fail. */
void yai_format_tokens(uint64_t count, char *out, size_t out_size);

/* Flush stdio's stdout to the tty, waiting out EAGAIN (stdout is
 * non-blocking — it shares the tty file description with the polled
 * stdin). EVERY printf-style write outside render.c must be followed
 * by this and its Result chained; a bare fflush(stdout) silently drops
 * buffered bytes on a busy PTY. */
struct yetty_ycore_void_result yai_render_flush_stdout(void);

#endif /* YAI_RENDER_H */
