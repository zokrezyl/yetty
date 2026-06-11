/*
 * render.c — scrollback rendering for ccc.
 */
#include "render.h"

#include <yetty/yfont/shader-glyph.h>

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Sentinels the yetty MCP server emits under YETTY_MCP_VIA_PARENT: ccc is
 * the single PTY writer, so it writes the envelope itself. The FILE form
 * carries a temp-file path (envelopes can be 100+ KB — music scores embed
 * font glyphs — far past the agent's tool-result token cap, so the bytes
 * must not travel through the tool result). The inline-base64 form is the
 * legacy variant, still accepted from older servers. */
#define CCC_FIGURE_FILE_SENTINEL_OPEN "<<CCLOOP_FIGURE_FILE "
#define CCC_FIGURE_FILE_SENTINEL_CLOSE " CCLOOP_FIGURE_FILE>>"
#define CCC_FIGURE_SENTINEL_OPEN "<<CCLOOP_FIGURE "
#define CCC_FIGURE_SENTINEL_CLOSE " CCLOOP_FIGURE>>"

#define CCC_SUMMARY_MAX 100

#define CCC_STREAM_NONE 0
#define CCC_STREAM_TEXT 1
#define CCC_STREAM_THINKING 2

/* "you ▸ " — 6 display columns; the glyph prefix adds 2 more. */
#define CCC_PROMPT_COLUMNS 6

void ccc_renderer_init(struct ccc_renderer *renderer, int fold_lines, int show_thinking)
{
    memset(renderer, 0, sizeof(*renderer));
    renderer->fold_lines = fold_lines;
    renderer->show_thinking = show_thinking;
    /* The pinned zone uses in-place erase; only a tty can take it. The
     * animated glyph additionally needs yetty's text layer behind the
     * tty — any other terminal would show PUA-B tofu. */
    renderer->pin_enabled = isatty(STDOUT_FILENO);
    const char *term_program = getenv("TERM_PROGRAM");
    renderer->pin_shader_glyphs = term_program && strcmp(term_program, "yetty") == 0;
}

void ccc_renderer_destroy(struct ccc_renderer *renderer)
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

/* Codepoints in [bytes, bytes+len). */
static size_t count_chars(const char *bytes, size_t len)
{
    size_t chars = 0;
    for (size_t index = 0; index < len; index++) {
        if (((unsigned char)bytes[index] & 0xC0) != 0x80) {
            chars++;
        }
    }
    return chars;
}

/* Byte offset of the codepoint with index `target_chars`. */
static size_t char_index_to_byte(const char *bytes, size_t len, size_t target_chars)
{
    size_t chars = 0;
    for (size_t index = 0; index < len; index++) {
        if (((unsigned char)bytes[index] & 0xC0) != 0x80) {
            if (chars == target_chars) {
                return index;
            }
            chars++;
        }
    }
    return len;
}

/* The current in-progress streamed line (the remainder in stream_buf),
 * shown as a single clipped ticker row. */
static void zone_draw_ticker(const struct ccc_renderer *renderer, int columns)
{
    size_t tail_len = 0;
    int clipped = 0;
    const char *tail =
        clip_tail(renderer->stream_buf, renderer->stream_len, columns - 2, &tail_len, &clipped);
    const char *style = (renderer->stream_kind == CCC_STREAM_THINKING) ? CCC_DIM : "";
    printf("%s%s%.*s" CCC_RESET, style, clipped ? "…" : "", (int)tail_len, tail);
}

/* Editor window: the slice of the buffer shown on the prompt row, slid
 * so the cursor always falls inside it. All units are codepoints; byte
 * bounds come out for printing. */
static void editor_window(const struct ccc_renderer *renderer, int max_columns,
                          size_t *out_start_byte, size_t *out_window_bytes, int *out_clipped_left,
                          size_t *out_cursor_window_chars)
{
    const char *buffer = renderer->edit_buffer;
    size_t length = renderer->edit_length ? *renderer->edit_length : 0;
    size_t cursor = renderer->edit_cursor ? *renderer->edit_cursor : length;
    if (cursor > length) {
        cursor = length;
    }
    size_t total_chars = count_chars(buffer, length);
    size_t cursor_chars = count_chars(buffer, cursor);
    size_t window_chars = (size_t)(max_columns > 0 ? max_columns : 1);

    size_t window_start_chars = 0;
    if (total_chars > window_chars) {
        if (cursor_chars + 1 > window_chars) {
            window_start_chars = cursor_chars + 1 - window_chars;
        }
        if (window_start_chars > total_chars - window_chars) {
            window_start_chars = total_chars - window_chars;
        }
    }
    size_t start_byte = char_index_to_byte(buffer, length, window_start_chars);
    size_t end_byte = char_index_to_byte(buffer, length, window_start_chars + window_chars);
    *out_start_byte = start_byte;
    *out_window_bytes = end_byte - start_byte;
    *out_clipped_left = window_start_chars > 0;
    *out_cursor_window_chars = cursor_chars - window_start_chars;
}

/* 1-based terminal column where the editor cursor parks. */
static size_t prompt_cursor_column(const struct ccc_renderer *renderer, int columns)
{
    int prefix_columns = CCC_PROMPT_COLUMNS + (renderer->activity_glyph[0] ? 2 : 0);
    size_t start_byte = 0;
    size_t window_bytes = 0;
    int clipped_left = 0;
    size_t cursor_window_chars = 0;
    editor_window(renderer, columns - prefix_columns - 2, &start_byte, &window_bytes, &clipped_left,
                  &cursor_window_chars);
    return 1 + (size_t)prefix_columns + (clipped_left ? 1 : 0) + cursor_window_chars;
}

static void zone_draw_prompt(const struct ccc_renderer *renderer, int columns)
{
    int prefix_columns = CCC_PROMPT_COLUMNS + (renderer->activity_glyph[0] ? 2 : 0);
    if (renderer->activity_glyph[0]) {
        printf(CCC_MINT "%s" CCC_RESET " ", renderer->activity_glyph);
    }
    fputs(CCC_MINT "you ▸ " CCC_RESET, stdout);
    if (renderer->edit_buffer && renderer->edit_length && *renderer->edit_length > 0) {
        size_t start_byte = 0;
        size_t window_bytes = 0;
        int clipped_left = 0;
        size_t cursor_window_chars = 0;
        editor_window(renderer, columns - prefix_columns - 2, &start_byte, &window_bytes,
                      &clipped_left, &cursor_window_chars);
        printf("%s%.*s", clipped_left ? "…" : "", (int)window_bytes,
               renderer->edit_buffer + start_byte);
    }
}

static void zone_draw(struct ccc_renderer *renderer)
{
    if (!renderer->pin_enabled || !renderer->pin_active || renderer->zone_visible) {
        return;
    }
    int columns = terminal_columns();
    renderer->zone_rows_above = 0;
    if (renderer->stream_kind != CCC_STREAM_NONE && renderer->stream_len > 0) {
        zone_draw_ticker(renderer, columns);
        fputc('\n', stdout);
        renderer->zone_rows_above = 1;
    }
    zone_draw_prompt(renderer, columns);
    /* Menu rows walk down with newlines (scrolling at the screen
     * bottom), then the cursor comes back up — scroll-proof. */
    renderer->zone_rows_below = 0;
    for (size_t row = 0; row < renderer->menu_row_count; row++) {
        printf("\n\033[2K%s", renderer->menu_rows[row]);
        renderer->zone_rows_below++;
    }
    if (renderer->zone_rows_below > 0) {
        printf("\033[%dA", renderer->zone_rows_below);
    }
    printf("\033[%zuG", prompt_cursor_column(renderer, columns));
    fflush(stdout);
    renderer->zone_visible = 1;
}

void ccc_renderer_zone_suspend(struct ccc_renderer *renderer)
{
    if (!renderer->zone_visible) {
        return;
    }
    /* Cursor sits on the prompt row. Erase it, the menu rows below,
     * then the ticker above; end at the top zone row, column 0. */
    fputs("\r\033[2K", stdout);
    for (int row = 0; row < renderer->zone_rows_below; row++) {
        fputs("\033[1B\033[2K", stdout);
    }
    if (renderer->zone_rows_below > 0) {
        printf("\033[%dA", renderer->zone_rows_below);
    }
    for (int row = 0; row < renderer->zone_rows_above; row++) {
        fputs("\033[1A\033[2K", stdout);
    }
    fflush(stdout);
    renderer->zone_visible = 0;
    renderer->zone_rows_above = 0;
    renderer->zone_rows_below = 0;
}

void ccc_renderer_zone_resume(struct ccc_renderer *renderer)
{
    zone_draw(renderer);
}

void ccc_renderer_pin_setup(struct ccc_renderer *renderer, const char *edit_buffer,
                            const size_t *edit_length, const size_t *edit_cursor)
{
    renderer->edit_buffer = edit_buffer;
    renderer->edit_length = edit_length;
    renderer->edit_cursor = edit_cursor;
}

void ccc_renderer_pin_show(struct ccc_renderer *renderer)
{
    if (!renderer->pin_enabled) {
        return;
    }
    ccc_renderer_zone_suspend(renderer);
    renderer->pin_active = 1;
    zone_draw(renderer);
}

void ccc_renderer_pin_hide(struct ccc_renderer *renderer)
{
    ccc_renderer_zone_suspend(renderer);
    renderer->pin_active = 0;
    renderer->activity_glyph[0] = '\0';
    renderer->menu_row_count = 0;
}

void ccc_renderer_pin_redraw(struct ccc_renderer *renderer)
{
    ccc_renderer_zone_suspend(renderer);
    zone_draw(renderer);
}

void ccc_renderer_activity_set(struct ccc_renderer *renderer, const char *glyph_name)
{
    if (!renderer->pin_enabled) {
        return;
    }
    char glyph_bytes[8] = "";
    size_t glyph_len = 0;
    if (renderer->pin_shader_glyphs && glyph_name) {
        struct uint32_result codepoint_res = yetty_yfont_shader_glyph_codepoint(glyph_name);
        if (YETTY_IS_ERR(codepoint_res)) {
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
    ccc_renderer_pin_redraw(renderer);
}

void ccc_renderer_activity_clear(struct ccc_renderer *renderer)
{
    renderer->activity_glyph[0] = '\0';
    ccc_renderer_pin_redraw(renderer);
}

void ccc_renderer_menu_set(struct ccc_renderer *renderer, const char *const *rows, size_t count)
{
    if (count > CCC_RENDERER_MENU_ROWS) {
        count = CCC_RENDERER_MENU_ROWS;
    }
    for (size_t row = 0; row < count; row++) {
        snprintf(renderer->menu_rows[row], sizeof(renderer->menu_rows[row]), "%s", rows[row]);
    }
    renderer->menu_row_count = count;
    ccc_renderer_pin_redraw(renderer);
}

void ccc_renderer_menu_clear(struct ccc_renderer *renderer)
{
    if (renderer->menu_row_count == 0) {
        return;
    }
    renderer->menu_row_count = 0;
    ccc_renderer_pin_redraw(renderer);
}

/*---------------------------------------------------------------------------
 * Streaming — deltas accumulate per line; complete lines go to history,
 * the in-progress remainder rides the zone's ticker row.
 *---------------------------------------------------------------------------*/

static int stream_reserve(struct ccc_renderer *renderer, size_t wanted)
{
    if (wanted <= renderer->stream_cap) {
        return 1;
    }
    size_t new_cap = renderer->stream_cap ? renderer->stream_cap : 4096;
    while (new_cap < wanted) {
        new_cap *= 2;
    }
    char *grown = realloc(renderer->stream_buf, new_cap);
    if (!grown) {
        return 0;
    }
    renderer->stream_buf = grown;
    renderer->stream_cap = new_cap;
    return 1;
}

/* Print one completed streamed line into history. The zone must be
 * suspended. The first line of a message carries its label. */
static void stream_print_line(struct ccc_renderer *renderer, const char *line, size_t len)
{
    if (!renderer->stream_labeled) {
        if (renderer->stream_kind == CCC_STREAM_THINKING) {
            fputs("\n" CCC_MUTED "thinking" CCC_RESET " ", stdout);
        } else {
            fputs("\n" CCC_MINT CCC_BOLD "claude" CCC_RESET " ", stdout);
        }
        renderer->stream_labeled = 1;
    }
    if (renderer->stream_kind == CCC_STREAM_THINKING) {
        fputs(CCC_DIM, stdout);
    }
    fwrite(line, 1, len, stdout);
    fputs(CCC_RESET "\n", stdout);
}

/* Flush completed lines out of stream_buf; keep the partial tail. */
static void stream_flush_lines(struct ccc_renderer *renderer)
{
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

/* End of a streamed run: flush the unterminated remainder. */
static void stream_finish(struct ccc_renderer *renderer)
{
    if (renderer->stream_kind == CCC_STREAM_NONE) {
        return;
    }
    ccc_renderer_zone_suspend(renderer);
    if (renderer->stream_len > 0) {
        stream_print_line(renderer, renderer->stream_buf, renderer->stream_len);
        renderer->stream_len = 0;
    }
    renderer->stream_kind = CCC_STREAM_NONE;
    fflush(stdout);
    ccc_renderer_zone_resume(renderer);
}

static void stream_delta(struct ccc_renderer *renderer, int kind, const char *text, size_t len)
{
    if (renderer->stream_kind != CCC_STREAM_NONE && renderer->stream_kind != kind) {
        stream_finish(renderer);
    }
    ccc_renderer_zone_suspend(renderer);
    renderer->stream_kind = kind;
    if (!stream_reserve(renderer, renderer->stream_len + len)) {
        /* Out of memory for the line buffer: degrade to direct write. */
        fwrite(text, 1, len, stdout);
        fflush(stdout);
        return;
    }
    memcpy(renderer->stream_buf + renderer->stream_len, text, len);
    renderer->stream_len += len;
    stream_flush_lines(renderer);
    fflush(stdout);
    ccc_renderer_zone_resume(renderer);
}

void ccc_renderer_begin_message(struct ccc_renderer *renderer)
{
    /* A leftover unterminated line from the previous message must not
     * merge into this one. */
    stream_finish(renderer);
    renderer->stream_labeled = 0;
}

void ccc_renderer_text_delta(struct ccc_renderer *renderer, const char *text, size_t len)
{
    stream_delta(renderer, CCC_STREAM_TEXT, text, len);
}

void ccc_renderer_thinking_delta(struct ccc_renderer *renderer, const char *text, size_t len)
{
    if (!renderer->show_thinking) {
        return;
    }
    stream_delta(renderer, CCC_STREAM_THINKING, text, len);
}

void ccc_renderer_end_thinking(struct ccc_renderer *renderer)
{
    if (renderer->stream_kind == CCC_STREAM_THINKING) {
        stream_finish(renderer);
        renderer->stream_labeled = 0;
    }
}

void ccc_renderer_finish_turn(struct ccc_renderer *renderer)
{
    stream_finish(renderer);
}

/* Pick the most informative one-line summary for a tool call. */
void ccc_summarize_tool_input(yyjson_val *input, char *out, size_t out_size)
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

void ccc_render_tool_call(struct ccc_renderer *renderer, const char *name, yyjson_val *input)
{
    ccc_renderer_zone_suspend(renderer);
    char summary[CCC_SUMMARY_MAX + 1];
    ccc_summarize_tool_input(input, summary, sizeof(summary));
    printf("\n" CCC_MUTED "⚙ " CCC_BOLD "%s" CCC_RESET " " CCC_DIM "%s" CCC_RESET "\n", name,
           summary);
    fflush(stdout);
    ccc_renderer_zone_resume(renderer);
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

/* Write all `len` bytes to `fd`, polling through EAGAIN. The uv_poll on
 * STDIN_FILENO put the tty in non-blocking mode, and fd 0/1 share the
 * tty's open file description — so stdout is NON-BLOCKING here. A plain
 * fwrite of an envelope larger than the kernel PTY buffer (~64 KB; music
 * scores with embedded fonts are ~100 KB) hits EAGAIN mid-stream and
 * silently truncates the DCS, which the terminal then discards whole. */
static void write_all_to_terminal(int fd, const unsigned char *bytes, size_t len)
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
        break; /* real write error — nothing sane to do mid-envelope */
    }
}

/* Write `envelope_len` figure bytes to the terminal, preceded by the
 * tool checkmark line. Flushes around the raw write so the envelope
 * can't be cut in half by buffered text (single-writer discipline). */
static void write_figure_envelope(const unsigned char *envelope, size_t envelope_len,
                                  const char *tool_name)
{
    printf("  " CCC_MINT "✓" CCC_RESET " " CCC_MUTED "%s" CCC_RESET "\n",
           tool_name ? tool_name : "figure");
    fflush(stdout);
    write_all_to_terminal(STDOUT_FILENO, envelope, envelope_len);
}

/* FILE sentinel: the payload between the markers is a path to a temp file
 * holding the raw envelope bytes. Read, write to the terminal, unlink.
 * Returns 1 when a figure was emitted. */
static int try_render_figure_file(const char *raw, const char *tool_name)
{
    const char *open_marker = strstr(raw, CCC_FIGURE_FILE_SENTINEL_OPEN);
    if (!open_marker) {
        return 0;
    }
    const char *payload = open_marker + strlen(CCC_FIGURE_FILE_SENTINEL_OPEN);
    const char *close_marker = strstr(payload, CCC_FIGURE_FILE_SENTINEL_CLOSE);
    if (!close_marker) {
        return 0;
    }
    size_t path_len = (size_t)(close_marker - payload);
    char path[1024];
    if (path_len == 0 || path_len >= sizeof(path)) {
        return 0;
    }
    memcpy(path, payload, path_len);
    path[path_len] = '\0';

    FILE *spool = fopen(path, "rb");
    if (!spool) {
        return 0;
    }
    unsigned char *envelope = NULL;
    size_t envelope_len = 0;
    if (fseek(spool, 0, SEEK_END) == 0) {
        long file_size = ftell(spool);
        if (file_size > 0 && fseek(spool, 0, SEEK_SET) == 0) {
            envelope = malloc((size_t)file_size);
            if (envelope) {
                envelope_len = fread(envelope, 1, (size_t)file_size, spool);
            }
        }
    }
    fclose(spool);
    unlink(path);
    if (!envelope || envelope_len == 0) {
        free(envelope);
        return 0;
    }
    write_figure_envelope(envelope, envelope_len, tool_name);
    free(envelope);
    return 1;
}

/* If `raw` carries a parent-render figure sentinel (FILE or legacy
 * inline-base64), write the OSC envelope bytes straight to the terminal.
 * Returns 1 when a figure was emitted. */
static int try_render_figure(const char *raw, const char *tool_name)
{
    if (try_render_figure_file(raw, tool_name)) {
        return 1;
    }
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
    write_figure_envelope(decoded, decoded_len, tool_name);
    free(decoded);
    return 1;
}

struct yetty_ycore_void_result ccc_render_tool_result(struct ccc_renderer *renderer,
                                                      yyjson_val *content, int is_error,
                                                      const char *tool_name)
{
    char *raw = tool_result_text(content);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "ccc_render_tool_result: tool_result_text alloc");
    }
    ccc_renderer_zone_suspend(renderer);
    if (!is_error && try_render_figure(raw, tool_name)) {
        free(raw);
        ccc_renderer_zone_resume(renderer);
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
    ccc_renderer_zone_resume(renderer);
    return YETTY_OK_VOID();
}

void ccc_render_hook(struct ccc_renderer *renderer, yyjson_val *event)
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
    ccc_renderer_zone_suspend(renderer);
    printf(CCC_MUTED "⤷ hook: %s" CCC_RESET "\n", name ? name : "hook");
    fflush(stdout);
    ccc_renderer_zone_resume(renderer);
}

void ccc_format_tokens(uint64_t count, char *out, size_t out_size)
{
    if (count >= 1000) {
        snprintf(out, out_size, "%.1fk", (double)count / 1000.0);
    } else {
        snprintf(out, out_size, "%llu", (unsigned long long)count);
    }
}
