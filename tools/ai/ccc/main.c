/*
 * ccc — a minimal, hackable `claude` CLI loop rendering into yetty.
 *
 * C sibling of the retired Python cc_loop: drives the headless `claude`
 * CLI over a bidirectional stream-json pipe, owns its own scrollback,
 * streams responses live, and keeps the non-scrolling status (state,
 * tokens, cost) in a floating ygui HUD panel (hud.c) while the
 * conversation scrolls like any CLI.
 *
 * Two rendering paths reach yetty (both rely on running *inside* a
 * yetty pane, so stdout is the yetty PTY):
 *
 *   1. Agent-drawn figures — the sub-`claude` gets the `yetty` MCP
 *      server; under YETTY_MCP_VIA_PARENT its tools hand the OSC
 *      envelope back through a sentinel in the tool result and ccc
 *      (the single PTY writer) emits it itself.
 *
 *   2. The ygui HUD — a compositor figure floating top-right that does
 *      NOT scroll away.
 *
 * Scrollback: every stream-json event is mirrored to
 * tmp/transcript-<session>.jsonl, replayable/searchable beyond what
 * the terminal keeps.
 *
 * Usage:
 *     ./ccc                      # fresh session
 *     ./ccc --resume <uuid>      # resume a prior session id
 *     CCC_SHOW_THINKING=1 ./ccc  # show dim thinking text
 *     CCC_FOLD_LINES=20 ./ccc    # tool-output preview cap (default 8)
 *     CCC_NO_HUD=1 ./ccc         # stats as plain lines, no ygui panel
 *
 * Type a message at the prompt. Ctrl-D or /quit to exit; Ctrl-C
 * interrupts the turn in flight.
 */

#include "hud.h"
#include "render.h"

#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <uv.h>
#include <yyjson.h>

#define CCC_DEFAULT_FOLD_LINES 8
#define CCC_TOOL_NAME_MAP_SIZE 128
#define CCC_QUEUE_MAX 16
#define CCC_KILL_TIMEOUT_MS 5000

#define CCC_PROMPT "\n" CCC_MINT "you ▸ " CCC_RESET

struct ccc_tool_name_entry {
    char id[80];
    char name[80];
};

struct ccc_session_usage {
    uint64_t input;
    uint64_t output;
    uint64_t cache_read;
    uint64_t cache_creation;
    double cost;
    int turns;
};

struct ccc_app {
    uv_loop_t loop;
    uv_process_t child_process;
    uv_pipe_t child_stdin_pipe;
    uv_pipe_t child_stdout_pipe;
    uv_poll_t stdin_poll;
    uv_signal_t sigint_handle;
    uv_signal_t sigwinch_handle;
    uv_timer_t kill_timer;

    struct ccc_hud *hud;
    struct ccc_renderer renderer;
    struct ccc_session_usage usage;

    FILE *transcript_file;
    char transcript_path[256];
    char session_id[40];

    int child_alive;
    int child_stdin_open;
    int waiting;       /* a turn is in flight */
    int shutting_down; /* /quit or EOF — wind the child down */
    int exit_code;

    /* child stdout line assembly (lines can be MBs — figures) */
    char *child_out_buf;
    size_t child_out_len;
    size_t child_out_cap;

    /* our stdin line assembly (canonical mode; read may still split) */
    char stdin_buf[8192];
    size_t stdin_len;

    /* messages typed while a turn was in flight */
    char *queue[CCC_QUEUE_MAX];
    int queue_len;

    struct ccc_tool_name_entry tool_names[CCC_TOOL_NAME_MAP_SIZE];
    int tool_name_next;

    int interrupt_request_counter;
};

/*---------------------------------------------------------------------------
 * Small helpers
 *---------------------------------------------------------------------------*/

static int env_flag(const char *name)
{
    const char *value = getenv(name);
    return value && strcmp(value, "") != 0 && strcmp(value, "0") != 0 && strcmp(value, "no") != 0;
}

static int env_int(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (!value || !value[0]) {
        return fallback;
    }
    return (int)strtol(value, NULL, 10);
}

/* RFC 4122 v4 UUID from /dev/urandom; falls back to pid/time salt. */
static void generate_session_id(char *out, size_t out_size)
{
    unsigned char bytes[16];
    int filled = 0;
    int urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd >= 0) {
        filled = read(urandom_fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes);
        close(urandom_fd);
    }
    if (!filled) {
        uint64_t salt = (uint64_t)getpid() * 2654435761u ^ (uint64_t)uv_hrtime();
        for (size_t index = 0; index < sizeof(bytes); index++) {
            salt = salt * 6364136223846793005ULL + 1442695040888963407ULL;
            bytes[index] = (unsigned char)(salt >> 33);
        }
    }
    bytes[6] = (unsigned char)((bytes[6] & 0x0F) | 0x40);
    bytes[8] = (unsigned char)((bytes[8] & 0x3F) | 0x80);
    snprintf(out, out_size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

static void show_prompt(struct ccc_app *app)
{
    if (app->shutting_down) {
        return;
    }
    fputs(CCC_PROMPT, stdout);
    fflush(stdout);
}

/*---------------------------------------------------------------------------
 * Child stdin writes
 *---------------------------------------------------------------------------*/

struct ccc_child_write {
    uv_write_t request;
    char *data;
};

static void on_child_write_done(uv_write_t *request, int status)
{
    struct ccc_child_write *write_request = (struct ccc_child_write *)request;
    (void)status;
    free(write_request->data);
    free(write_request);
}

/* Take ownership of `line` (heap, newline-terminated) and ship it. */
static void write_to_child(struct ccc_app *app, char *line, size_t len)
{
    if (!app->child_alive || !app->child_stdin_open) {
        free(line);
        return;
    }
    struct ccc_child_write *write_request = calloc(1, sizeof(*write_request));
    if (!write_request) {
        free(line);
        return;
    }
    write_request->data = line;
    uv_buf_t buffer = uv_buf_init(line, (unsigned)len);
    if (uv_write(&write_request->request, (uv_stream_t *)&app->child_stdin_pipe, &buffer, 1,
                 on_child_write_done) != 0) {
        free(write_request->data);
        free(write_request);
    }
}

static void send_user_message(struct ccc_app *app, const char *text)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "user");
    yyjson_mut_val *message = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, message, "role", "user");
    yyjson_mut_obj_add_str(doc, message, "content", text);
    yyjson_mut_obj_add_val(doc, envelope, "message", message);

    size_t json_len = 0;
    char *json_text = yyjson_mut_write(doc, 0, &json_len);
    yyjson_mut_doc_free(doc);
    if (!json_text) {
        return;
    }
    /* Repack with a trailing newline (stream-json is line-delimited). */
    char *line = malloc(json_len + 2);
    if (!line) {
        free(json_text);
        return;
    }
    memcpy(line, json_text, json_len);
    line[json_len] = '\n';
    line[json_len + 1] = '\0';
    free(json_text);

    app->waiting = 1;
    ccc_hud_set_state(app->hud, "… thinking");
    ccc_hud_flush(app->hud);
    write_to_child(app, line, json_len + 1);
}

static void send_interrupt_request(struct ccc_app *app)
{
    char request_id[32];
    snprintf(request_id, sizeof(request_id), "ccc-%d", ++app->interrupt_request_counter);

    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *envelope = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, envelope);
    yyjson_mut_obj_add_str(doc, envelope, "type", "control_request");
    yyjson_mut_obj_add_str(doc, envelope, "request_id", request_id);
    yyjson_mut_val *request = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, request, "subtype", "interrupt");
    yyjson_mut_obj_add_val(doc, envelope, "request", request);

    size_t json_len = 0;
    char *json_text = yyjson_mut_write(doc, 0, &json_len);
    yyjson_mut_doc_free(doc);
    if (!json_text) {
        return;
    }
    char *line = malloc(json_len + 2);
    if (!line) {
        free(json_text);
        return;
    }
    memcpy(line, json_text, json_len);
    line[json_len] = '\n';
    line[json_len + 1] = '\0';
    free(json_text);
    write_to_child(app, line, json_len + 1);
}

/*---------------------------------------------------------------------------
 * Tool-name map (tool_use_id -> name, to label results)
 *---------------------------------------------------------------------------*/

static void remember_tool_name(struct ccc_app *app, const char *tool_use_id, const char *name)
{
    struct ccc_tool_name_entry *entry =
        &app->tool_names[app->tool_name_next % CCC_TOOL_NAME_MAP_SIZE];
    app->tool_name_next++;
    snprintf(entry->id, sizeof(entry->id), "%s", tool_use_id);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
}

static const char *lookup_tool_name(struct ccc_app *app, const char *tool_use_id)
{
    if (!tool_use_id) {
        return NULL;
    }
    for (int index = 0; index < CCC_TOOL_NAME_MAP_SIZE; index++) {
        if (strcmp(app->tool_names[index].id, tool_use_id) == 0) {
            return app->tool_names[index].name;
        }
    }
    return NULL;
}

/*---------------------------------------------------------------------------
 * Usage accounting
 *---------------------------------------------------------------------------*/

static uint64_t usage_field(yyjson_val *usage, const char *key)
{
    yyjson_val *value = usage ? yyjson_obj_get(usage, key) : NULL;
    return value ? (uint64_t)yyjson_get_uint(value) : 0;
}

static void render_turn_usage(struct ccc_app *app, yyjson_val *result_event)
{
    yyjson_val *usage = yyjson_obj_get(result_event, "usage");
    yyjson_val *cost_value = yyjson_obj_get(result_event, "total_cost_usd");
    yyjson_val *duration_value = yyjson_obj_get(result_event, "duration_ms");

    uint64_t turn_input = usage_field(usage, "input_tokens");
    uint64_t turn_output = usage_field(usage, "output_tokens");
    uint64_t cache_read = usage_field(usage, "cache_read_input_tokens");
    uint64_t cache_creation = usage_field(usage, "cache_creation_input_tokens");
    double cost = cost_value ? yyjson_get_num(cost_value) : 0.0;
    double seconds = duration_value ? yyjson_get_num(duration_value) / 1000.0 : 0.0;

    app->usage.input += turn_input;
    app->usage.output += turn_output;
    app->usage.cache_read += cache_read;
    app->usage.cache_creation += cache_creation;
    app->usage.cost += cost;
    app->usage.turns++;

    char input_text[16];
    char output_text[16];
    char cached_text[16];
    char session_output_text[16];
    ccc_format_tokens(turn_input, input_text, sizeof(input_text));
    ccc_format_tokens(turn_output, output_text, sizeof(output_text));
    ccc_format_tokens(cache_read, cached_text, sizeof(cached_text));
    ccc_format_tokens(app->usage.output, session_output_text, sizeof(session_output_text));

    char timing_text[48] = "";
    if (seconds > 0.0) {
        snprintf(timing_text, sizeof(timing_text), " · %.1fs · %.0f tok/s", seconds,
                 (double)turn_output / seconds);
    }
    char turn_line[192];
    snprintf(turn_line, sizeof(turn_line), "↑%s in · %s cached · ↓%s out%s · $%.4f", input_text,
             cached_text, output_text, timing_text, cost);
    char session_line[128];
    snprintf(session_line, sizeof(session_line), "session: ↓%s out · $%.4f · %d turn(s)",
             session_output_text, app->usage.cost, app->usage.turns);

    if (app->hud) {
        /* Tokens go to the HUD; the terminal scrollback stays clean. */
        ccc_hud_set_turn(app->hud, turn_line);
        ccc_hud_set_session(app->hud, session_line);
        ccc_hud_set_state(app->hud, "idle");
        ccc_hud_flush(app->hud);
        return;
    }
    printf(CCC_MUTED "  %s" CCC_RESET "\n" CCC_DIM "  %s" CCC_RESET "\n", turn_line, session_line);
    fflush(stdout);
}

/*---------------------------------------------------------------------------
 * Stream-json event handling (port of cc_loop's reader_loop)
 *---------------------------------------------------------------------------*/

static void begin_shutdown(struct ccc_app *app);

static void handle_stream_event(struct ccc_app *app, yyjson_val *event)
{
    yyjson_val *inner = yyjson_obj_get(event, "event");
    const char *inner_type = yyjson_get_str(yyjson_obj_get(inner, "type"));
    if (!inner_type) {
        return;
    }
    if (strcmp(inner_type, "message_start") == 0) {
        ccc_renderer_begin_message(&app->renderer);
        return;
    }
    if (strcmp(inner_type, "content_block_delta") == 0) {
        yyjson_val *delta = yyjson_obj_get(inner, "delta");
        const char *delta_type = yyjson_get_str(yyjson_obj_get(delta, "type"));
        if (!delta_type) {
            return;
        }
        if (strcmp(delta_type, "text_delta") == 0) {
            yyjson_val *text = yyjson_obj_get(delta, "text");
            if (yyjson_is_str(text)) {
                ccc_renderer_end_thinking(&app->renderer);
                ccc_renderer_text_delta(&app->renderer, yyjson_get_str(text), yyjson_get_len(text));
            }
        } else if (strcmp(delta_type, "thinking_delta") == 0) {
            yyjson_val *thinking = yyjson_obj_get(delta, "thinking");
            if (yyjson_is_str(thinking)) {
                ccc_renderer_thinking_delta(&app->renderer, yyjson_get_str(thinking),
                                            yyjson_get_len(thinking));
            }
        }
        return;
    }
    if (strcmp(inner_type, "content_block_stop") == 0) {
        ccc_renderer_end_thinking(&app->renderer);
    }
}

static void handle_assistant_event(struct ccc_app *app, yyjson_val *event)
{
    ccc_renderer_end_thinking(&app->renderer);
    yyjson_val *message = yyjson_obj_get(event, "message");
    yyjson_val *content = message ? yyjson_obj_get(message, "content") : NULL;
    if (!yyjson_is_arr(content)) {
        return;
    }
    yyjson_val *block;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(content, &iter);
    while ((block = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *block_type = yyjson_get_str(yyjson_obj_get(block, "type"));
        if (!block_type || strcmp(block_type, "tool_use") != 0) {
            continue;
        }
        const char *name = yyjson_get_str(yyjson_obj_get(block, "name"));
        const char *tool_use_id = yyjson_get_str(yyjson_obj_get(block, "id"));
        if (!name) {
            name = "?";
        }
        if (tool_use_id) {
            remember_tool_name(app, tool_use_id, name);
        }
        ccc_render_tool_call(name, yyjson_obj_get(block, "input"));
        char state_line[96];
        snprintf(state_line, sizeof(state_line), "⚙ %s", name);
        ccc_hud_set_state(app->hud, state_line);
        ccc_hud_flush(app->hud);
    }
}

static void handle_user_event(struct ccc_app *app, yyjson_val *event)
{
    yyjson_val *message = yyjson_obj_get(event, "message");
    yyjson_val *content = message ? yyjson_obj_get(message, "content") : NULL;
    if (!yyjson_is_arr(content)) {
        return;
    }
    yyjson_val *block;
    yyjson_arr_iter iter;
    yyjson_arr_iter_init(content, &iter);
    while ((block = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *block_type = yyjson_get_str(yyjson_obj_get(block, "type"));
        if (!block_type || strcmp(block_type, "tool_result") != 0) {
            continue;
        }
        const char *tool_use_id = yyjson_get_str(yyjson_obj_get(block, "tool_use_id"));
        yyjson_val *is_error_value = yyjson_obj_get(block, "is_error");
        int is_error = is_error_value && yyjson_get_bool(is_error_value);
        ccc_render_tool_result(&app->renderer, yyjson_obj_get(block, "content"), is_error,
                               lookup_tool_name(app, tool_use_id));
    }
}

static void handle_result_event(struct ccc_app *app, yyjson_val *event)
{
    ccc_renderer_finish_turn(&app->renderer);
    render_turn_usage(app, event);
    app->waiting = 0;

    if (app->queue_len > 0) {
        /* Pump the next queued message instead of prompting. */
        char *queued = app->queue[0];
        memmove(&app->queue[0], &app->queue[1],
                sizeof(app->queue[0]) * (size_t)(app->queue_len - 1));
        app->queue_len--;
        printf("\n" CCC_DIM "(sending queued message)" CCC_RESET "\n");
        fflush(stdout);
        send_user_message(app, queued);
        free(queued);
        return;
    }
    show_prompt(app);
}

static void handle_event(struct ccc_app *app, yyjson_val *event)
{
    const char *kind = yyjson_get_str(yyjson_obj_get(event, "type"));

    if (kind && strcmp(kind, "system") == 0) {
        const char *subtype = yyjson_get_str(yyjson_obj_get(event, "subtype"));
        if (subtype && strcmp(subtype, "init") == 0) {
            const char *session_id = yyjson_get_str(yyjson_obj_get(event, "session_id"));
            if (session_id) {
                snprintf(app->session_id, sizeof(app->session_id), "%s", session_id);
            }
        }
        return;
    }
    if (kind && strcmp(kind, "stream_event") == 0) {
        handle_stream_event(app, event);
        return;
    }
    if (kind && strcmp(kind, "assistant") == 0) {
        handle_assistant_event(app, event);
        return;
    }
    if (kind && strcmp(kind, "user") == 0) {
        handle_user_event(app, event);
        return;
    }
    if ((kind && strstr(kind, "hook")) || yyjson_obj_get(event, "hook_event_name")) {
        ccc_render_hook(event);
        return;
    }
    if (kind && strcmp(kind, "result") == 0) {
        handle_result_event(app, event);
    }
}

/*---------------------------------------------------------------------------
 * Child stdout pump
 *---------------------------------------------------------------------------*/

static void handle_child_line(struct ccc_app *app, const char *line, size_t len)
{
    if (len == 0) {
        return;
    }
    if (app->transcript_file) {
        fwrite(line, 1, len, app->transcript_file);
        fputc('\n', app->transcript_file);
        fflush(app->transcript_file);
    }
    yyjson_doc *doc = yyjson_read(line, len, 0);
    if (!doc) {
        return; /* not JSON — ignore, parity with the Python loop */
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        handle_event(app, root);
    }
    yyjson_doc_free(doc);
}

static void on_child_stdout_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer)
{
    (void)handle;
    buffer->base = malloc(suggested_size);
    buffer->len = buffer->base ? suggested_size : 0;
}

static void child_exited_unexpectedly(struct ccc_app *app)
{
    if (app->shutting_down) {
        return;
    }
    printf("\n" CCC_RED "✗ claude exited unexpectedly — see stderr log under tmp/" CCC_RESET "\n");
    fflush(stdout);
    app->exit_code = 1;
    begin_shutdown(app);
}

static void on_child_stdout_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer)
{
    struct ccc_app *app = stream->data;
    if (nread < 0) {
        free(buffer->base);
        /* Pipe closed: the turn (if any) will never finish — unlike the
         * Python loop, never hang waiting for it. */
        uv_read_stop(stream);
        if (app->child_alive) {
            child_exited_unexpectedly(app);
        }
        return;
    }
    if (nread == 0) {
        free(buffer->base);
        return;
    }
    if (app->child_out_len + (size_t)nread + 1 > app->child_out_cap) {
        size_t new_cap = app->child_out_cap ? app->child_out_cap : 64 * 1024;
        while (new_cap < app->child_out_len + (size_t)nread + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(app->child_out_buf, new_cap);
        if (!grown) {
            free(buffer->base);
            return;
        }
        app->child_out_buf = grown;
        app->child_out_cap = new_cap;
    }
    memcpy(app->child_out_buf + app->child_out_len, buffer->base, (size_t)nread);
    app->child_out_len += (size_t)nread;
    free(buffer->base);

    /* Extract complete lines; keep any partial tail. */
    size_t line_start = 0;
    for (size_t index = 0; index < app->child_out_len; index++) {
        if (app->child_out_buf[index] != '\n') {
            continue;
        }
        app->child_out_buf[index] = '\0';
        handle_child_line(app, app->child_out_buf + line_start, index - line_start);
        line_start = index + 1;
    }
    if (line_start > 0) {
        memmove(app->child_out_buf, app->child_out_buf + line_start,
                app->child_out_len - line_start);
        app->child_out_len -= line_start;
    }
}

/*---------------------------------------------------------------------------
 * Our stdin (the operator typing)
 *---------------------------------------------------------------------------*/

static void handle_input_line(struct ccc_app *app, const char *line, size_t len)
{
    /* Trim surrounding whitespace. */
    while (len > 0 && (line[0] == ' ' || line[0] == '\t')) {
        line++;
        len--;
    }
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
        len--;
    }
    if (len == 0) {
        if (!app->waiting) {
            show_prompt(app);
        }
        return;
    }
    if ((len == 5 && strncmp(line, "/quit", 5) == 0) ||
        (len == 5 && strncmp(line, "/exit", 5) == 0)) {
        begin_shutdown(app);
        return;
    }
    char *copy = strndup(line, len);
    if (!copy) {
        return;
    }
    if (app->waiting) {
        if (app->queue_len < CCC_QUEUE_MAX) {
            app->queue[app->queue_len++] = copy;
            printf(CCC_DIM "(queued — sends when this turn finishes)" CCC_RESET "\n");
            fflush(stdout);
        } else {
            printf(CCC_RED "queue full — message dropped" CCC_RESET "\n");
            fflush(stdout);
            free(copy);
        }
        return;
    }
    send_user_message(app, copy);
    free(copy);
}

static void on_stdin_readable(uv_poll_t *poll_handle, int status, int events)
{
    struct ccc_app *app = poll_handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char chunk[4096];
    ssize_t nread = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (nread < 0) {
        return;
    }
    if (nread == 0) {
        /* EOF (Ctrl-D on an empty line, or closed pipe). */
        begin_shutdown(app);
        return;
    }
    for (ssize_t index = 0; index < nread; index++) {
        char ch = chunk[index];
        if (ch == '\n') {
            app->stdin_buf[app->stdin_len] = '\0';
            handle_input_line(app, app->stdin_buf, app->stdin_len);
            app->stdin_len = 0;
            continue;
        }
        if (app->stdin_len + 1 < sizeof(app->stdin_buf)) {
            app->stdin_buf[app->stdin_len++] = ch;
        }
    }
}

/*---------------------------------------------------------------------------
 * Signals
 *---------------------------------------------------------------------------*/

static void on_sigint(uv_signal_t *signal_handle, int signum)
{
    struct ccc_app *app = signal_handle->data;
    (void)signum;
    if (app->waiting && app->child_alive) {
        printf("\n" CCC_MUTED "(interrupt requested)" CCC_RESET "\n");
        fflush(stdout);
        send_interrupt_request(app);
        return;
    }
    begin_shutdown(app);
}

static void on_sigwinch(uv_signal_t *signal_handle, int signum)
{
    struct ccc_app *app = signal_handle->data;
    (void)signum;
    ccc_hud_viewport_changed(app->hud);
}

/*---------------------------------------------------------------------------
 * Child lifecycle / shutdown
 *---------------------------------------------------------------------------*/

static void on_handle_closed(uv_handle_t *handle)
{
    (void)handle;
}

static void on_kill_timer(uv_timer_t *timer)
{
    struct ccc_app *app = timer->data;
    if (app->child_alive) {
        uv_process_kill(&app->child_process, SIGKILL);
    }
}

static void begin_shutdown(struct ccc_app *app)
{
    if (app->shutting_down) {
        return;
    }
    app->shutting_down = 1;
    uv_poll_stop(&app->stdin_poll);
    uv_signal_stop(&app->sigint_handle);
    uv_signal_stop(&app->sigwinch_handle);
    if (app->child_stdin_open) {
        app->child_stdin_open = 0;
        uv_close((uv_handle_t *)&app->child_stdin_pipe, on_handle_closed);
    }
    if (app->child_alive) {
        /* Closing stdin asks claude to exit; SIGKILL backstop after 5 s. */
        uv_timer_start(&app->kill_timer, on_kill_timer, CCC_KILL_TIMEOUT_MS, 0);
    } else {
        uv_stop(&app->loop);
    }
}

static void on_child_exit(uv_process_t *process, int64_t exit_status, int term_signal)
{
    struct ccc_app *app = process->data;
    (void)term_signal;
    app->child_alive = 0;
    if (!app->shutting_down) {
        if (exit_status != 0) {
            printf("\n" CCC_RED "✗ claude exited with status %lld — see %s and tmp/" CCC_RESET "\n",
                   (long long)exit_status, app->transcript_path);
            app->exit_code = 1;
        }
        begin_shutdown(app);
    }
    uv_timer_stop(&app->kill_timer);
    uv_close((uv_handle_t *)process, on_handle_closed);
    uv_stop(&app->loop);
}

/*---------------------------------------------------------------------------
 * Spawn
 *---------------------------------------------------------------------------*/

static int spawn_claude(struct ccc_app *app, const char *resume_session_id, int stderr_fd)
{
    const char *permission_mode = getenv("CCC_PERMISSION_MODE");
    if (!permission_mode || !permission_mode[0]) {
        permission_mode = "auto";
    }
    const char *allowed_tools = getenv("CCC_ALLOWED_TOOLS");
    if (!allowed_tools || !allowed_tools[0]) {
        /* mcp__yetty allows the whole yetty MCP server (all draw_* tools). */
        allowed_tools = "Read,Bash(git *),mcp__yetty";
    }
    const char *model = getenv("CCC_MODEL");

    const char *args[24];
    int arg_count = 0;
    args[arg_count++] = "claude";
    args[arg_count++] = "--print";
    args[arg_count++] = "--input-format";
    args[arg_count++] = "stream-json";
    args[arg_count++] = "--output-format";
    args[arg_count++] = "stream-json";
    args[arg_count++] = "--include-partial-messages";
    args[arg_count++] = "--include-hook-events";
    args[arg_count++] = "--verbose";
    args[arg_count++] = "--permission-mode";
    args[arg_count++] = permission_mode;
    args[arg_count++] = "--allowedTools";
    args[arg_count++] = allowed_tools;
    if (resume_session_id) {
        args[arg_count++] = "--resume";
        args[arg_count++] = resume_session_id;
    } else {
        args[arg_count++] = "--session-id";
        args[arg_count++] = app->session_id;
    }
    if (model && model[0]) {
        args[arg_count++] = "--model";
        args[arg_count++] = model;
    }
    args[arg_count] = NULL;

    uv_pipe_init(&app->loop, &app->child_stdin_pipe, 0);
    uv_pipe_init(&app->loop, &app->child_stdout_pipe, 0);
    app->child_stdin_pipe.data = app;
    app->child_stdout_pipe.data = app;

    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
    stdio[0].data.stream = (uv_stream_t *)&app->child_stdin_pipe;
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&app->child_stdout_pipe;
    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = stderr_fd;

    uv_process_options_t options = {0};
    options.exit_cb = on_child_exit;
    options.file = "claude";
    options.args = (char **)args;
    options.stdio_count = 3;
    options.stdio = stdio;
    /* env = NULL inherits ours — including YETTY_MCP_VIA_PARENT below. */

    app->child_process.data = app;
    int spawn_status = uv_spawn(&app->loop, &app->child_process, &options);
    if (spawn_status != 0) {
        fprintf(stderr, "ccc: cannot spawn `claude`: %s\n", uv_strerror(spawn_status));
        return -1;
    }
    app->child_alive = 1;
    app->child_stdin_open = 1;
    uv_read_start((uv_stream_t *)&app->child_stdout_pipe, on_child_stdout_alloc,
                  on_child_stdout_read);
    return 0;
}

/*---------------------------------------------------------------------------
 * main
 *---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    ytrace_init();

    const char *resume_session_id = NULL;
    if (argc >= 3 && strcmp(argv[1], "--resume") == 0) {
        resume_session_id = argv[2];
    }

    struct ccc_app *app = calloc(1, sizeof(*app));
    if (!app) {
        return 1;
    }
    if (resume_session_id) {
        snprintf(app->session_id, sizeof(app->session_id), "%s", resume_session_id);
    } else {
        generate_session_id(app->session_id, sizeof(app->session_id));
    }
    ccc_renderer_init(&app->renderer, env_int("CCC_FOLD_LINES", CCC_DEFAULT_FOLD_LINES),
                      env_flag("CCC_SHOW_THINKING"));

    mkdir("tmp", 0777);
    snprintf(app->transcript_path, sizeof(app->transcript_path), "tmp/transcript-%s.jsonl",
             app->session_id);
    app->transcript_file = fopen(app->transcript_path, "a");

    char stderr_log_path[256];
    snprintf(stderr_log_path, sizeof(stderr_log_path), "tmp/claude-stderr-%s.log", app->session_id);
    int stderr_fd = open(stderr_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stderr_fd < 0) {
        stderr_fd = STDERR_FILENO;
    }

    /* The loop owns the terminal: the yetty MCP server must hand figures
     * back via the sentinel instead of racing us to the PTY. Set before
     * spawn; the child inherits our environment. */
    setenv("YETTY_MCP_VIA_PARENT", "1", 1);

    /* HUD before the first emit-producing output (it draws immediately). */
    app->hud = ccc_hud_create();

    uv_loop_init(&app->loop);
    uv_timer_init(&app->loop, &app->kill_timer);
    app->kill_timer.data = app;

    if (spawn_claude(app, resume_session_id, stderr_fd) != 0) {
        ccc_hud_destroy(app->hud);
        free(app);
        return 1;
    }

    uv_poll_init(&app->loop, &app->stdin_poll, STDIN_FILENO);
    app->stdin_poll.data = app;
    uv_poll_start(&app->stdin_poll, UV_READABLE, on_stdin_readable);

    uv_signal_init(&app->loop, &app->sigint_handle);
    app->sigint_handle.data = app;
    uv_signal_start(&app->sigint_handle, on_sigint, SIGINT);
    uv_signal_init(&app->loop, &app->sigwinch_handle);
    app->sigwinch_handle.data = app;
    uv_signal_start(&app->sigwinch_handle, on_sigwinch, SIGWINCH);

    printf(CCC_MINT CCC_BOLD
           "ccc" CCC_RESET " " CCC_MUTED "session %s" CCC_RESET "\n" CCC_DIM
           "transcript=%s  hud=%s" CCC_RESET "\n" CCC_DIM
           "Type a message. /quit or Ctrl-D to exit; Ctrl-C interrupts a turn." CCC_RESET "\n",
           app->session_id, app->transcript_path, app->hud ? "on" : "off");
    show_prompt(app);

    uv_run(&app->loop, UV_RUN_DEFAULT);

    /* Drain closing handles. */
    uv_close((uv_handle_t *)&app->stdin_poll, on_handle_closed);
    uv_close((uv_handle_t *)&app->sigint_handle, on_handle_closed);
    uv_close((uv_handle_t *)&app->sigwinch_handle, on_handle_closed);
    uv_close((uv_handle_t *)&app->kill_timer, on_handle_closed);
    if (!uv_is_closing((uv_handle_t *)&app->child_stdout_pipe)) {
        uv_close((uv_handle_t *)&app->child_stdout_pipe, on_handle_closed);
    }
    uv_run(&app->loop, UV_RUN_NOWAIT);
    uv_loop_close(&app->loop);

    ccc_hud_destroy(app->hud);
    printf("\n" CCC_DIM "transcript saved to %s" CCC_RESET "\n", app->transcript_path);

    int exit_code = app->exit_code;
    if (app->transcript_file) {
        fclose(app->transcript_file);
    }
    if (stderr_fd != STDERR_FILENO) {
        close(stderr_fd);
    }
    for (int queue_index = 0; queue_index < app->queue_len; queue_index++) {
        free(app->queue[queue_index]);
    }
    free(app->child_out_buf);
    free(app);
    return exit_code;
}
