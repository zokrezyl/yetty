/*
 * render.c — scrollback rendering for ccc.
 */
#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Sentinel the yetty MCP server emits under YETTY_MCP_VIA_PARENT: ccc is
 * the single PTY writer, so it decodes and writes the envelope itself. */
#define CCC_FIGURE_SENTINEL_OPEN "<<CCLOOP_FIGURE "
#define CCC_FIGURE_SENTINEL_CLOSE " CCLOOP_FIGURE>>"

#define CCC_SUMMARY_MAX 100

void ccc_renderer_init(struct ccc_renderer *renderer, int fold_lines, int show_thinking)
{
    memset(renderer, 0, sizeof(*renderer));
    renderer->fold_lines = fold_lines;
    renderer->show_thinking = show_thinking;
}

void ccc_renderer_begin_message(struct ccc_renderer *renderer)
{
    renderer->in_text = 0;
    renderer->in_thinking = 0;
}

void ccc_renderer_text_delta(struct ccc_renderer *renderer, const char *text, size_t len)
{
    if (!renderer->in_text) {
        fputs("\n" CCC_MINT CCC_BOLD "claude" CCC_RESET " ", stdout);
        renderer->in_text = 1;
    }
    fwrite(text, 1, len, stdout);
    fflush(stdout);
}

void ccc_renderer_thinking_delta(struct ccc_renderer *renderer, const char *text, size_t len)
{
    if (!renderer->show_thinking) {
        return;
    }
    if (!renderer->in_thinking) {
        fputs("\n" CCC_MUTED "thinking " CCC_RESET CCC_DIM, stdout);
        renderer->in_thinking = 1;
    }
    fwrite(text, 1, len, stdout);
    fflush(stdout);
}

void ccc_renderer_end_thinking(struct ccc_renderer *renderer)
{
    if (renderer->in_thinking) {
        fputs(CCC_RESET "\n", stdout);
        fflush(stdout);
        renderer->in_thinking = 0;
    }
}

void ccc_renderer_finish_turn(struct ccc_renderer *renderer)
{
    if (renderer->in_text) {
        fputc('\n', stdout);
        fflush(stdout);
        renderer->in_text = 0;
    }
}

/* Pick the most informative one-line summary for a tool call. */
static void summarize_tool_input(yyjson_val *input, char *out, size_t out_size)
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

void ccc_render_tool_call(const char *name, yyjson_val *input)
{
    char summary[CCC_SUMMARY_MAX + 1];
    summarize_tool_input(input, summary, sizeof(summary));
    printf("\n" CCC_MUTED "⚙ " CCC_BOLD "%s" CCC_RESET " " CCC_DIM "%s" CCC_RESET "\n", name,
           summary);
    fflush(stdout);
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
        if (text && yyjson_is_str(text)) {
            total += strlen(yyjson_get_str(text)) + 1;
        } else {
            total += 64; /* placeholder for non-text blocks */
        }
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

/* If `raw` carries the parent-render figure sentinel, decode and write
 * the OSC envelope bytes straight to the terminal. Returns 1 when a
 * figure was emitted. */
static int try_render_figure(const char *raw, const char *tool_name)
{
    const char *open_marker = strstr(raw, CCC_FIGURE_SENTINEL_OPEN);
    if (!open_marker) {
        return 0;
    }
    const char *payload = open_marker + strlen(CCC_FIGURE_SENTINEL_OPEN);
    const char *close_marker = strstr(payload, CCC_FIGURE_SENTINEL_CLOSE);
    if (!close_marker) {
        return 0;
    }
    size_t decoded_len = 0;
    unsigned char *decoded = base64_decode(payload, (size_t)(close_marker - payload), &decoded_len);
    if (!decoded) {
        return 0;
    }
    printf("  " CCC_MINT "✓" CCC_RESET " " CCC_MUTED "%s" CCC_RESET "\n",
           tool_name ? tool_name : "figure");
    /* Flush pending text first so the envelope bytes can't be cut in
     * half by buffered output (single-writer discipline). */
    fflush(stdout);
    fwrite(decoded, 1, decoded_len, stdout);
    fflush(stdout);
    free(decoded);
    return 1;
}

struct yetty_ycore_void_result ccc_render_tool_result(const struct ccc_renderer *renderer,
                                                      yyjson_val *content, int is_error,
                                                      const char *tool_name)
{
    char *raw = tool_result_text(content);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "ccc_render_tool_result: tool_result_text alloc");
    }
    if (!is_error && try_render_figure(raw, tool_name)) {
        free(raw);
        return YETTY_OK_VOID();
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

    printf("  %s" CCC_RESET, is_error ? CCC_RED "✗" : CCC_MINT "✓");
    if (tool_name) {
        printf(" " CCC_MUTED "%s" CCC_RESET, tool_name);
    }
    fputc('\n', stdout);

    /* First fold_lines lines, each indented two spaces; note the rest. */
    const char *style = is_error ? CCC_RED : CCC_DIM;
    int shown = 0;
    size_t hidden = 0;
    const char *line = body;
    while (*line) {
        const char *line_end = strchr(line, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);
        if (shown < renderer->fold_lines) {
            printf("%s  %.*s" CCC_RESET "\n", style, (int)line_len, line);
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
        printf(CCC_DIM "    … %zu more line(s) (full in transcript)" CCC_RESET "\n", hidden);
    }
    fflush(stdout);
    free(raw);
    return YETTY_OK_VOID();
}

void ccc_render_hook(yyjson_val *event)
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
    printf(CCC_MUTED "⤷ hook: %s" CCC_RESET "\n", name ? name : "hook");
    fflush(stdout);
}

void ccc_format_tokens(uint64_t count, char *out, size_t out_size)
{
    if (count >= 1000) {
        snprintf(out, out_size, "%.1fk", (double)count / 1000.0);
    } else {
        snprintf(out, out_size, "%llu", (unsigned long long)count);
    }
}
