/*
 * hud-format.h — a tmux-style format string for yai's status HUD.
 *
 * The HUD content is no longer hard-coded: the user writes a single
 * format string (config key `hud_format`) and the values currently shown
 * in the HUD become variables. This module is the backend-neutral parser
 * shared by both render backends — the ygui widget HUD (hud.c) and the
 * text status bar (render.c, driven from main.c).
 *
 * Grammar (deliberately close to tmux):
 *   #{var}                      substitute a variable's current value
 *   #[fg=ROLE] / #[fg=#RRGGBB]  color the following span; ROLE is a brand
 *                               role (accent, accent_bright, primary,
 *                               secondary, muted)
 *   #[default]                  reset the color to the backend default
 *   #[align=left|center|right]  open a new alignment cell in the row
 *   \n (or a real newline)      start a new row
 *   ##                          a literal '#'
 *
 * A parsed format is a flat list of SPANS, each tagged with its row,
 * alignment cell, and color, and carrying a token list (literals +
 * variable ids). Expansion concatenates a span's tokens against a set of
 * resolved variable values. The 3-cell-per-row model reproduces the
 * legacy HUD grid exactly and maps onto the text bar's left/center/right
 * segments.
 *
 * Functions that can fail return a Result; expansion cannot fail (it
 * truncates silently) so it returns void.
 */
#ifndef YAI_HUD_FORMAT_H
#define YAI_HUD_FORMAT_H

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

/* The variables a format string may reference. To add one: extend this
 * enum, the name table in hud-format.c, and yai_hud_collect_values (main.c). */
enum yai_hud_var {
    YAI_HUD_VAR_STATE = 0, /* activity: "idle", "… thinking", "✗ turn failed" */
    YAI_HUD_VAR_ENGINE,    /* claude / codex / gemini */
    YAI_HUD_VAR_MODEL,     /* active model (or "default") */
    YAI_HUD_VAR_TITLE,     /* user-set session title (/title); empty until set */
    YAI_HUD_VAR_SESSION_ID,
    YAI_HUD_VAR_INPUT, /* session totals, human-formatted (12.3k) */
    YAI_HUD_VAR_OUTPUT,
    YAI_HUD_VAR_CACHE,      /* session cache_read */
    YAI_HUD_VAR_COST,       /* session cost, "%.4f" (no leading $) */
    YAI_HUD_VAR_TURNS,      /* session turn count */
    YAI_HUD_VAR_QUOTA,      /* account quota summary (usage proxy) */
    /* Decomposed quota — the 5h ("session") and 7d ("week") windows, each as a
     * percentage and a locale-formatted reset time. */
    YAI_HUD_VAR_QUOTA_SESSION_PCT,    /* "5" */
    YAI_HUD_VAR_QUOTA_SESSION_RESETS, /* "4:40pm" (local time, locale) */
    YAI_HUD_VAR_QUOTA_WEEK_PCT,       /* "12" */
    YAI_HUD_VAR_QUOTA_WEEK_RESETS,    /* "Jun 21, 10:40am" (local time, locale) */
    YAI_HUD_VAR_EST_TOKENS,           /* in-flight token estimate */
    YAI_HUD_VAR_TURN_INPUT, /* last turn, human-formatted */
    YAI_HUD_VAR_TURN_OUTPUT,
    YAI_HUD_VAR_TURN_CACHE,
    YAI_HUD_VAR_TURN_COST,  /* last turn cost, "%.4f" */
    YAI_HUD_VAR_TURN_TIME,  /* last turn seconds, "%.1f" */
    YAI_HUD_VAR_TURN_SPEED, /* last turn tokens/sec, "%.0f" */
    /* Convenience composites matching the legacy strings, so the default
     * format stays readable. */
    YAI_HUD_VAR_TURN,    /* "↑.. in · .. cached · ↓.. out · ..s · .. tok/s" */
    YAI_HUD_VAR_SESSION, /* "session: ↓.. out · $.. · N turn(s)" */
    YAI_HUD_VAR_STATS,   /* "Σ ↑.. ↓.." */
    YAI_HUD_VAR_COUNT,
};

/* Longest single value; a composite (turn line) is the widest, and the
 * UTF-8 arrows cost three bytes each. */
#define YAI_HUD_VALUE_MAX 192

/* Resolved variable strings. main.c fills this from app state each refresh
 * (yai_hud_collect_values); the parser/expander only reads it. */
struct yai_hud_var_values {
    char value[YAI_HUD_VAR_COUNT][YAI_HUD_VALUE_MAX];
};

enum yai_hud_format_cell {
    YAI_HUD_CELL_LEFT = 0,
    YAI_HUD_CELL_CENTER,
    YAI_HUD_CELL_RIGHT,
    YAI_HUD_CELL_COUNT,
};

enum yai_hud_format_color {
    YAI_HUD_COLOR_DEFAULT = 0, /* backend default (ygui: primary; text: FG reset) */
    YAI_HUD_COLOR_ACCENT,
    YAI_HUD_COLOR_ACCENT_BRIGHT,
    YAI_HUD_COLOR_PRIMARY,
    YAI_HUD_COLOR_SECONDARY,
    YAI_HUD_COLOR_MUTED,
    YAI_HUD_COLOR_CUSTOM, /* explicit #RRGGBB held in span->custom_color */
};

struct yai_hud_format_token {
    int is_var;           /* 1 = variable, 0 = literal */
    enum yai_hud_var var; /* valid when is_var */
    char *literal;        /* heap-owned when !is_var */
};

struct yai_hud_format_span {
    int row;
    enum yai_hud_format_cell cell;
    enum yai_hud_format_color color;
    struct yetty_ycore_rgba custom_color; /* valid when color == CUSTOM */
    struct yai_hud_format_token *tokens;
    int token_count;
    int token_cap;
};

struct yai_hud_format {
    struct yai_hud_format_span *spans;
    int span_count;
    int span_cap;
    int row_count; /* >= 1 */
};

/* Parse `fmt` into `out` (zeroed first). On error nothing is leaked and
 * `out` is left empty. A NULL/empty fmt parses to a single empty row. */
struct yetty_ycore_void_result yai_hud_format_parse(struct yai_hud_format *out, const char *fmt);

/* Release everything a successful parse allocated and zero the struct.
 * Safe on an all-zero struct. */
void yai_hud_format_free(struct yai_hud_format *format);

/* Concatenate one span's tokens into `out`, substituting variables from
 * `values`. Truncates silently to out_size. */
void yai_hud_format_expand_span(const struct yai_hud_format_span *span,
                                const struct yai_hud_var_values *values, char *out,
                                size_t out_size);

/* The rgba a span's color resolves to. DEFAULT returns `fallback`. */
struct yetty_ycore_rgba yai_hud_format_span_rgba(const struct yai_hud_format_span *span,
                                                 struct yetty_ycore_rgba fallback);

#endif /* YAI_HUD_FORMAT_H */
