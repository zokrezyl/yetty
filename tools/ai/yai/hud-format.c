/*
 * hud-format.c — parser/expander for the tmux-style HUD format string.
 * See hud-format.h for the grammar.
 */
#include "hud-format.h"

#include <stdlib.h>
#include <string.h>

/* Map a variable name (length-delimited) to its id. */
static int var_lookup(const char *name, size_t len, enum yai_hud_var *out)
{
    static const struct {
        const char *name;
        enum yai_hud_var var;
    } table[] = {
        {"state", YAI_HUD_VAR_STATE},
        {"engine", YAI_HUD_VAR_ENGINE},
        {"model", YAI_HUD_VAR_MODEL},
        {"title", YAI_HUD_VAR_TITLE},
        {"session_id", YAI_HUD_VAR_SESSION_ID},
        {"input", YAI_HUD_VAR_INPUT},
        {"output", YAI_HUD_VAR_OUTPUT},
        {"cache", YAI_HUD_VAR_CACHE},
        {"cost", YAI_HUD_VAR_COST},
        {"turns", YAI_HUD_VAR_TURNS},
        {"quota", YAI_HUD_VAR_QUOTA},
        {"quota_session_pct", YAI_HUD_VAR_QUOTA_SESSION_PCT},
        {"quota_session_resets", YAI_HUD_VAR_QUOTA_SESSION_RESETS},
        {"quota_week_pct", YAI_HUD_VAR_QUOTA_WEEK_PCT},
        {"quota_week_resets", YAI_HUD_VAR_QUOTA_WEEK_RESETS},
        {"est_tokens", YAI_HUD_VAR_EST_TOKENS},
        {"turn_input", YAI_HUD_VAR_TURN_INPUT},
        {"turn_output", YAI_HUD_VAR_TURN_OUTPUT},
        {"turn_cache", YAI_HUD_VAR_TURN_CACHE},
        {"turn_cost", YAI_HUD_VAR_TURN_COST},
        {"turn_time", YAI_HUD_VAR_TURN_TIME},
        {"turn_speed", YAI_HUD_VAR_TURN_SPEED},
        {"turn", YAI_HUD_VAR_TURN},
        {"session", YAI_HUD_VAR_SESSION},
        {"stats", YAI_HUD_VAR_STATS},
    };
    for (size_t index = 0; index < sizeof(table) / sizeof(table[0]); index++) {
        if (strlen(table[index].name) == len && memcmp(table[index].name, name, len) == 0) {
            *out = table[index].var;
            return 1;
        }
    }
    return 0;
}

/* Map a brand-role name (length-delimited) to its color id. */
static int color_role_lookup(const char *name, size_t len, enum yai_hud_format_color *out)
{
    static const struct {
        const char *name;
        enum yai_hud_format_color color;
    } table[] = {
        {"default", YAI_HUD_COLOR_DEFAULT},
        {"accent", YAI_HUD_COLOR_ACCENT},
        {"accent_bright", YAI_HUD_COLOR_ACCENT_BRIGHT},
        {"primary", YAI_HUD_COLOR_PRIMARY},
        {"secondary", YAI_HUD_COLOR_SECONDARY},
        {"muted", YAI_HUD_COLOR_MUTED},
    };
    for (size_t index = 0; index < sizeof(table) / sizeof(table[0]); index++) {
        if (strlen(table[index].name) == len && memcmp(table[index].name, name, len) == 0) {
            *out = table[index].color;
            return 1;
        }
    }
    return 0;
}

struct yetty_ycore_rgba yai_hud_format_span_rgba(const struct yai_hud_format_span *span,
                                                 struct yetty_ycore_rgba fallback)
{
    /* Brand palette (mirrors hud.c's role colors). */
    struct yetty_ycore_rgba accent_bright = {.r = 116, .g = 197, .b = 165, .a = 255};
    struct yetty_ycore_rgba accent = {.r = 107, .g = 168, .b = 146, .a = 255};
    struct yetty_ycore_rgba primary = {.r = 224, .g = 229, .b = 228, .a = 255};
    struct yetty_ycore_rgba secondary = {.r = 159, .g = 167, .b = 168, .a = 255};
    struct yetty_ycore_rgba muted = {.r = 85, .g = 97, .b = 98, .a = 255};
    switch (span->color) {
    case YAI_HUD_COLOR_ACCENT:
        return accent;
    case YAI_HUD_COLOR_ACCENT_BRIGHT:
        return accent_bright;
    case YAI_HUD_COLOR_PRIMARY:
        return primary;
    case YAI_HUD_COLOR_SECONDARY:
        return secondary;
    case YAI_HUD_COLOR_MUTED:
        return muted;
    case YAI_HUD_COLOR_CUSTOM:
        return span->custom_color;
    case YAI_HUD_COLOR_DEFAULT:
    default:
        return fallback;
    }
}

/*---------------------------------------------------------------------------
 * Parser. Single pass over the format, accumulating literal text and
 * flushing spans whenever the (cell, color, row) context changes.
 *---------------------------------------------------------------------------*/

struct parse_state {
    struct yai_hud_format *format;
    int row;
    enum yai_hud_format_cell cell;
    enum yai_hud_format_color color;
    struct yetty_ycore_rgba custom_color;
    int open_span; /* index+1 of the span tokens currently append to; 0 = none */
    char *literal; /* pending literal text under the current context */
    size_t literal_len;
    size_t literal_cap;
};

static int hex_nibble(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

/* Parse "#RRGGBB" (len includes the leading '#'); returns 1 on success. */
static int parse_hex_color(const char *text, size_t len, struct yetty_ycore_rgba *out)
{
    if (len != 7 || text[0] != '#') {
        return 0;
    }
    int channels[6];
    for (int index = 0; index < 6; index++) {
        channels[index] = hex_nibble(text[1 + index]);
        if (channels[index] < 0) {
            return 0;
        }
    }
    out->r = (uint8_t)(channels[0] << 4 | channels[1]);
    out->g = (uint8_t)(channels[2] << 4 | channels[3]);
    out->b = (uint8_t)(channels[4] << 4 | channels[5]);
    out->a = 255;
    return 1;
}

static struct yetty_ycore_void_result span_reserve(struct yai_hud_format *format)
{
    if (format->span_count < format->span_cap) {
        return YETTY_OK_VOID();
    }
    int new_cap = format->span_cap ? format->span_cap * 2 : 8;
    struct yai_hud_format_span *grown =
        realloc(format->spans, (size_t)new_cap * sizeof(struct yai_hud_format_span));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "hud_format: span realloc");
    }
    format->spans = grown;
    format->span_cap = new_cap;
    return YETTY_OK_VOID();
}

/* Ensure there is an open span matching the current context; create one
 * if needed. Sets *index to the open span's index. */
static struct yetty_ycore_void_result ensure_span(struct parse_state *state, int *index)
{
    if (state->open_span) {
        *index = state->open_span - 1;
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result reserve_res = span_reserve(state->format);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "ensure_span: reserve");
    struct yai_hud_format_span *span = &state->format->spans[state->format->span_count];
    memset(span, 0, sizeof(*span));
    span->row = state->row;
    span->cell = state->cell;
    span->color = state->color;
    span->custom_color = state->custom_color;
    *index = state->format->span_count;
    state->format->span_count++;
    state->open_span = *index + 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result span_add_token(struct yai_hud_format_span *span,
                                                     struct yai_hud_format_token token)
{
    if (span->token_count >= span->token_cap) {
        int new_cap = span->token_cap ? span->token_cap * 2 : 4;
        struct yai_hud_format_token *grown =
            realloc(span->tokens, (size_t)new_cap * sizeof(struct yai_hud_format_token));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "span_add_token: realloc");
        }
        span->tokens = grown;
        span->token_cap = new_cap;
    }
    span->tokens[span->token_count++] = token;
    return YETTY_OK_VOID();
}

/* Flush the pending literal text as a token of the current span. */
static struct yetty_ycore_void_result flush_literal(struct parse_state *state)
{
    if (state->literal_len == 0) {
        return YETTY_OK_VOID();
    }
    int index = 0;
    struct yetty_ycore_void_result span_res = ensure_span(state, &index);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, span_res, "flush_literal: ensure_span");
    char *copy = malloc(state->literal_len + 1);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "flush_literal: malloc");
    }
    memcpy(copy, state->literal, state->literal_len);
    copy[state->literal_len] = '\0';
    struct yai_hud_format_token token = {.is_var = 0, .literal = copy};
    struct yetty_ycore_void_result add_res = span_add_token(&state->format->spans[index], token);
    if (YETTY_IS_ERR(add_res)) {
        free(copy);
        return YETTY_ERR(yetty_ycore_void, "flush_literal: add_token", add_res);
    }
    state->literal_len = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result literal_push(struct parse_state *state, char character)
{
    if (state->literal_len + 1 > state->literal_cap) {
        size_t new_cap = state->literal_cap ? state->literal_cap * 2 : 64;
        char *grown = realloc(state->literal, new_cap);
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "literal_push: realloc");
        }
        state->literal = grown;
        state->literal_cap = new_cap;
    }
    state->literal[state->literal_len++] = character;
    return YETTY_OK_VOID();
}

/* A directive or newline changed the context: commit the pending literal,
 * then force the next token to open a fresh span. */
static struct yetty_ycore_void_result break_span(struct parse_state *state)
{
    struct yetty_ycore_void_result flush_res = flush_literal(state);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "break_span: flush");
    state->open_span = 0;
    return YETTY_OK_VOID();
}

/* Append a variable token to the current span. */
static struct yetty_ycore_void_result append_var(struct parse_state *state, enum yai_hud_var var)
{
    struct yetty_ycore_void_result flush_res = flush_literal(state);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "append_var: flush");
    int index = 0;
    struct yetty_ycore_void_result span_res = ensure_span(state, &index);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, span_res, "append_var: ensure_span");
    struct yai_hud_format_token token = {.is_var = 1, .var = var};
    struct yetty_ycore_void_result add_res = span_add_token(&state->format->spans[index], token);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "append_var: add_token");
    return YETTY_OK_VOID();
}

/* Apply one comma-separated key in a #[...] directive. Unknown keys are
 * ignored (forward-compatible, like tmux). */
static void apply_directive_part(struct parse_state *state, const char *part, size_t len)
{
    while (len > 0 && (part[0] == ' ' || part[0] == '\t')) {
        part++;
        len--;
    }
    while (len > 0 && (part[len - 1] == ' ' || part[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) {
        return;
    }
    if (len == 7 && memcmp(part, "default", 7) == 0) {
        state->color = YAI_HUD_COLOR_DEFAULT;
        return;
    }
    if (len > 3 && memcmp(part, "fg=", 3) == 0) {
        const char *value = part + 3;
        size_t value_len = len - 3;
        if (value[0] == '#') {
            struct yetty_ycore_rgba rgba = {0};
            if (parse_hex_color(value, value_len, &rgba)) {
                state->color = YAI_HUD_COLOR_CUSTOM;
                state->custom_color = rgba;
            }
            return;
        }
        enum yai_hud_format_color role = YAI_HUD_COLOR_DEFAULT;
        if (color_role_lookup(value, value_len, &role)) {
            state->color = role;
        }
        return;
    }
    if (len > 6 && memcmp(part, "align=", 6) == 0) {
        const char *value = part + 6;
        size_t value_len = len - 6;
        if (value_len == 4 && memcmp(value, "left", 4) == 0) {
            state->cell = YAI_HUD_CELL_LEFT;
        } else if (value_len == 6 && memcmp(value, "center", 6) == 0) {
            state->cell = YAI_HUD_CELL_CENTER;
        } else if (value_len == 5 && memcmp(value, "right", 5) == 0) {
            state->cell = YAI_HUD_CELL_RIGHT;
        }
        return;
    }
}

/* Parse a #[...] directive starting at fmt[*pos] == '['. Advances *pos past
 * the closing ']'. Returns 1 if a directive was consumed, 0 if there was no
 * closing ']' (caller treats the '#' as a literal). */
static struct yetty_ycore_int_result handle_directive(struct parse_state *state, const char *fmt,
                                                      size_t *pos)
{
    size_t scan = *pos + 2; /* past '#' and '[' */
    size_t start = scan;
    while (fmt[scan] && fmt[scan] != ']') {
        scan++;
    }
    if (fmt[scan] != ']') {
        return YETTY_OK(yetty_ycore_int, 0); /* unmatched — literal '#' */
    }
    struct yetty_ycore_void_result break_res = break_span(state);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, break_res, "handle_directive: break");
    /* Split [start, scan) on commas. */
    size_t part_start = start;
    for (size_t cursor = start; cursor <= scan; cursor++) {
        if (cursor == scan || fmt[cursor] == ',') {
            apply_directive_part(state, fmt + part_start, cursor - part_start);
            part_start = cursor + 1;
        }
    }
    *pos = scan + 1; /* past ']' */
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Parse a #{var} starting at fmt[*pos] == '{'. Advances *pos past '}'.
 * Returns 1 if consumed, 0 if unmatched (caller treats '#' as literal). */
static struct yetty_ycore_int_result handle_variable(struct parse_state *state, const char *fmt,
                                                     size_t *pos)
{
    size_t start = *pos + 2; /* past '#' and '{' */
    size_t scan = start;
    while (fmt[scan] && fmt[scan] != '}') {
        scan++;
    }
    if (fmt[scan] != '}') {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    enum yai_hud_var var = YAI_HUD_VAR_STATE;
    if (var_lookup(fmt + start, scan - start, &var)) {
        struct yetty_ycore_void_result var_res = append_var(state, var);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, var_res, "handle_variable: append");
    }
    /* Unknown variable expands to nothing — still consume the token. */
    *pos = scan + 1;
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_void_result newline(struct parse_state *state)
{
    struct yetty_ycore_void_result break_res = break_span(state);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, break_res, "newline: break");
    state->row++;
    state->cell = YAI_HUD_CELL_LEFT;
    state->color = YAI_HUD_COLOR_DEFAULT;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yai_hud_format_parse(struct yai_hud_format *out, const char *fmt)
{
    memset(out, 0, sizeof(*out));
    struct parse_state state = {0};
    state.format = out;
    state.color = YAI_HUD_COLOR_DEFAULT;

    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    const char *text = fmt ? fmt : "";
    size_t pos = 0;
    while (text[pos]) {
        char character = text[pos];
        if (character == '#') {
            char next = text[pos + 1];
            if (next == '#') {
                result = literal_push(&state, '#');
                if (YETTY_IS_ERR(result)) {
                    break;
                }
                pos += 2;
                continue;
            }
            if (next == '[') {
                struct yetty_ycore_int_result directive_res = handle_directive(&state, text, &pos);
                if (YETTY_IS_ERR(directive_res)) {
                    result = YETTY_ERR(yetty_ycore_void, "parse: directive", directive_res);
                    break;
                }
                if (directive_res.value) {
                    continue;
                }
                /* Unmatched: emit the '#' literally and re-scan from '['. */
                result = literal_push(&state, '#');
                if (YETTY_IS_ERR(result)) {
                    break;
                }
                pos += 1;
                continue;
            }
            if (next == '{') {
                struct yetty_ycore_int_result variable_res = handle_variable(&state, text, &pos);
                if (YETTY_IS_ERR(variable_res)) {
                    result = YETTY_ERR(yetty_ycore_void, "parse: variable", variable_res);
                    break;
                }
                if (variable_res.value) {
                    continue;
                }
                result = literal_push(&state, '#');
                if (YETTY_IS_ERR(result)) {
                    break;
                }
                pos += 1;
                continue;
            }
            /* '#' before any other char is literal. */
            result = literal_push(&state, '#');
            if (YETTY_IS_ERR(result)) {
                break;
            }
            pos += 1;
            continue;
        }
        if (character == '\\' && text[pos + 1] == 'n') {
            result = newline(&state);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            pos += 2;
            continue;
        }
        if (character == '\n') {
            result = newline(&state);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            pos += 1;
            continue;
        }
        result = literal_push(&state, character);
        if (YETTY_IS_ERR(result)) {
            break;
        }
        pos += 1;
    }
    if (YETTY_IS_OK(result)) {
        result = flush_literal(&state);
    }
    free(state.literal);
    if (YETTY_IS_ERR(result)) {
        yai_hud_format_free(out);
        return YETTY_ERR(yetty_ycore_void, "yai_hud_format_parse", result);
    }
    /* row_count = widest row index + 1 (at least 1). */
    int max_row = 0;
    for (int index = 0; index < out->span_count; index++) {
        if (out->spans[index].row > max_row) {
            max_row = out->spans[index].row;
        }
    }
    out->row_count = max_row + 1;
    return YETTY_OK_VOID();
}

void yai_hud_format_free(struct yai_hud_format *format)
{
    if (!format) {
        return;
    }
    for (int index = 0; index < format->span_count; index++) {
        struct yai_hud_format_span *span = &format->spans[index];
        for (int token = 0; token < span->token_count; token++) {
            if (!span->tokens[token].is_var) {
                free(span->tokens[token].literal);
            }
        }
        free(span->tokens);
    }
    free(format->spans);
    memset(format, 0, sizeof(*format));
}

void yai_hud_format_expand_span(const struct yai_hud_format_span *span,
                                const struct yai_hud_var_values *values, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    size_t used = 0;
    out[0] = '\0';
    for (int index = 0; index < span->token_count && used + 1 < out_size; index++) {
        const struct yai_hud_format_token *token = &span->tokens[index];
        const char *piece = token->is_var ? values->value[token->var] : token->literal;
        if (!piece) {
            continue;
        }
        size_t piece_len = strlen(piece);
        size_t room = out_size - 1 - used;
        if (piece_len > room) {
            piece_len = room;
        }
        memcpy(out + used, piece, piece_len);
        used += piece_len;
        out[used] = '\0';
    }
}
