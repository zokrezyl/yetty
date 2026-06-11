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

#include <yyjson.h>

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
    int in_text;       /* an assistant text run is open */
    int in_thinking;   /* a thinking run is open */
};

void ccc_renderer_init(struct ccc_renderer *renderer, int fold_lines, int show_thinking);

/* A new assistant message started (stream-json message_start). */
void ccc_renderer_begin_message(struct ccc_renderer *renderer);

/* Streamed deltas. `text` need not be NUL-terminated; `len` bounds it. */
void ccc_renderer_text_delta(struct ccc_renderer *renderer, const char *text, size_t len);
void ccc_renderer_thinking_delta(struct ccc_renderer *renderer, const char *text, size_t len);
void ccc_renderer_end_thinking(struct ccc_renderer *renderer);

/* The turn's result event arrived — close any open text run. */
void ccc_renderer_finish_turn(struct ccc_renderer *renderer);

/* One tool invocation line: "⚙ name <one-line input summary>". */
void ccc_render_tool_call(const char *name, yyjson_val *input);

/* One tool result: folded preview, or raw figure bytes when the yetty
 * MCP server handed the OSC envelope back via the parent-render
 * sentinel. `content` is the tool_result content field (string or
 * block list). */
void ccc_render_tool_result(const struct ccc_renderer *renderer, yyjson_val *content, int is_error,
                            const char *tool_name);

/* Best-effort one-liner for a hook lifecycle frame. */
void ccc_render_hook(yyjson_val *event);

/* Compact token count: 2391 -> "2.4k", 970 -> "970". `out` must hold
 * at least 16 bytes. */
void ccc_format_tokens(uint64_t count, char *out, size_t out_size);

#endif /* CCC_RENDER_H */
