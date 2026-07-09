/*
 * ymux headless server — the GPU-less, windowless terminal server that
 * owns the pane models (PTY + wire statemachine + yvterm:grid). Bring-up
 * for docs/ymux.md phase 1b: pump a real event loop, run real shells, and
 * expose a msgpack-RPC control channel with debug verbs (dump-text, list,
 * shutdown). No WebGPU, no renderer, no window.
 */
#include <yetty/ymux/server.h>
#include <yetty/ymux/pane.h>

#include <msgpack.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <signal.h>
#endif

#include <yetty/ycore/memtag.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yconfig/config.h>
#include <yetty/yctl/rpc-message.h>
#include <yetty/yctl/rpc-server.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvterm/grid.h>

enum {
    /* Phase-1b bring-up cap. Phase 4 (layout) replaces the fixed array with
     * dynamic panes; a fixed array keeps slot pointers stable so per-pane
     * child-exit callbacks can carry a slot backref. */
    YMUX_SERVER_MAX_PANES = 16,
    YMUX_SERVER_DEFAULT_COLS = 80,
    YMUX_SERVER_DEFAULT_ROWS = 24,
    YMUX_SERVER_DEFAULT_PORT = 9998,
};

/* Retained per-pane PTY-output log. Model-shipping replays this into the
 * client's own emulator. Capped; the oldest bytes are dropped when full (the
 * live screen stays reconstructable from an app repaint after resize). */
#define YMUX_SERVER_OUTPUT_MAX (8u * 1024u * 1024u)

/* Max output bytes returned per ymux-poll. Kept well under the yctl response
 * frame cap (RESPONSE_BUFFER_SIZE, 4096) with room for the msgpack envelope;
 * the client polls repeatedly to drain a burst. */
#define YMUX_SERVER_POLL_CHUNK 3072u

struct ymux_server_pane_slot {
    struct yetty_ymux_server *server; /* backref (borrowed) */
    struct yetty_ymux_pane *pane;     /* owned */
    uint32_t index;
    int alive; /* 0 once the inferior has exited */
    /* Raw PTY-output log. The absolute byte offset of out[0] is out_base; the
     * live end is out_base + out_len. Clients poll by absolute offset. */
    uint8_t *out;
    size_t out_len;
    size_t out_cap;
    uint64_t out_base;
};

struct yetty_ymux_server {
    struct yetty_yconfig_config *config;                 /* borrowed */
    struct yetty_yevent_event_loop *event_loop;          /* owned */
    struct yetty_yplatform_pty_factory *pty_factory;     /* owned */
    struct yetty_ycore_memtag_registry *memtag_registry; /* owned, optional */
    struct yetty_yctl_server *control;                   /* owned */
    struct yetty_context context;                        /* POD; runtime = NULL */
    struct ymux_server_pane_slot panes[YMUX_SERVER_MAX_PANES];
    size_t pane_count;
    char *session_name; /* owned */
    /* dump-text / list response scratch — repacked each call. The pointer
     * handed back to the dispatcher is copied into the send buffer
     * synchronously before the next message runs (single-threaded loop),
     * so one shared buffer is safe. Mirrors yctl's memtag_response. */
    msgpack_sbuffer response;
};

/* --------------------------------------------------------------------- */
/* Pane hooks                                                            */
/* --------------------------------------------------------------------- */

/* Fires after each PTY feed. In the legacy monolith this drives
 * request_render; the headless server has no renderer. Phase 2a will mark
 * the pane dirty here so the delta stream knows to ship. */
static struct yetty_ycore_void_result server_pane_notify(void *userdata)
{
    (void)userdata;
    return YETTY_OK_VOID();
}

/* Append raw PTY output to the slot's retained log, trimming the oldest bytes
 * when the cap is reached (out_base advances to keep offsets absolute). */
static void slot_append_output(struct ymux_server_pane_slot *slot, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }
    /* One chunk larger than the whole cap: keep only its tail. */
    if (len >= YMUX_SERVER_OUTPUT_MAX) {
        size_t skip = len - YMUX_SERVER_OUTPUT_MAX;
        slot->out_base += (uint64_t)slot->out_len + skip;
        slot->out_len = 0;
        data += skip;
        len = YMUX_SERVER_OUTPUT_MAX;
    }
    /* Drop from the front so buffered + new fits the cap. */
    if (slot->out_len + len > YMUX_SERVER_OUTPUT_MAX) {
        size_t need_drop = (slot->out_len + len) - YMUX_SERVER_OUTPUT_MAX;
        memmove(slot->out, slot->out + need_drop, slot->out_len - need_drop);
        slot->out_len -= need_drop;
        slot->out_base += need_drop;
    }
    size_t needed = slot->out_len + len;
    if (needed > slot->out_cap) {
        size_t newcap = slot->out_cap ? slot->out_cap : 65536;
        while (newcap < needed) {
            newcap *= 2;
        }
        uint8_t *grown = realloc(slot->out, newcap);
        if (!grown) {
            ywarn("ymux server: pane %u output grow failed, dropping %zu bytes", slot->index, len);
            return;
        }
        slot->out = grown;
        slot->out_cap = newcap;
    }
    memcpy(slot->out + slot->out_len, data, len);
    slot->out_len += len;
}

/* Raw PTY-output tap (yetty_ymux_pane_output_tap_fn). Ships the exact byte
 * stream to the model-shipping log. */
static void server_pane_output_tap(const char *data, size_t len, void *userdata)
{
    struct ymux_server_pane_slot *slot = userdata;
    slot_append_output(slot, (const uint8_t *)data, len);
}

/* The pane's inferior exited. Keep the pane object (its grid still holds
 * the final screen for dump-text); mark the slot dead. The server never
 * exits on a child exit — only on an explicit shutdown verb. */
static struct yetty_ycore_void_result server_pane_child_exit(void *userdata)
{
    struct ymux_server_pane_slot *slot = userdata;
    if (slot) {
        slot->alive = 0;
        ydebug("ymux server: pane %u inferior exited", slot->index);
    }
    return YETTY_OK_VOID();
}

/* --------------------------------------------------------------------- */
/* Pane lifecycle                                                        */
/* --------------------------------------------------------------------- */

/* Spawn one pane running a real shell, sized cols x rows. Wires the
 * notify + child-exit hooks and gives the PTY an initial winsize. */
static struct yetty_ycore_void_result server_spawn_pane(struct yetty_ymux_server *server,
                                                        uint32_t cols, uint32_t rows)
{
    if (server->pane_count >= YMUX_SERVER_MAX_PANES) {
        return YETTY_ERR(yetty_ycore_void, "ymux server: pane limit reached");
    }

    struct ymux_server_pane_slot *slot = &server->panes[server->pane_count];
    slot->server = server;
    slot->index = (uint32_t)server->pane_count;
    slot->alive = 1;

    struct yetty_ycore_grid_size grid_size = {.rows = rows, .cols = cols};
    struct yetty_ymux_pane_ptr_result pane_res =
        yetty_ymux_pane_create(grid_size, &server->context);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pane_res, "ymux server: pane create failed");
    slot->pane = pane_res.value;

    yetty_ymux_pane_set_notify(slot->pane, server_pane_notify, slot);
    yetty_ymux_pane_set_child_exit(slot->pane, server_pane_child_exit, slot);
    yetty_ymux_pane_set_output_tap(slot->pane, server_pane_output_tap, slot);

    /* Headless: no font, so no true cell pixel size. Give the PTY a
     * conventional 8x16 cell so ws_xpixel/ypixel are non-zero; apps that
     * matter read ws_row/ws_col. */
    struct yetty_ycore_pixel_size cell_size = {.width = 8.0f, .height = 16.0f};
    struct yetty_ycore_void_result resize_res =
        yetty_ymux_pane_resize_pty(slot->pane, grid_size, cell_size);
    if (YETTY_IS_ERR(resize_res)) {
        (void)yetty_ymux_pane_destroy(slot->pane);
        slot->pane = NULL;
        return YETTY_ERR(yetty_ycore_void, "ymux server: initial pane resize failed", resize_res);
    }

    server->pane_count++;
    yinfo("ymux server: pane %u ready (%ux%u)", slot->index, cols, rows);
    return YETTY_OK_VOID();
}

/* --------------------------------------------------------------------- */
/* Grid text readback (dump-text)                                        */
/* --------------------------------------------------------------------- */

/* UTF-8-encode one codepoint into buf (>=4 bytes). Returns bytes written. */
static size_t utf8_encode(uint32_t codepoint, char *buf)
{
    if (codepoint < 0x80) {
        buf[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    buf[0] = (char)(0xF0 | (codepoint >> 18));
    buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

/* Render the visible grid to a freshly-malloc'd, NUL-terminated string:
 * each row's cells UTF-8-encoded, trailing blanks trimmed, rows joined by
 * '\n'. Caller frees *out_text. */
static struct yetty_ycore_void_result build_pane_text(struct yetty_yclass_object *grid,
                                                      char **out_text, size_t *out_len)
{
    *out_text = NULL;
    *out_len = 0;

    uint32_t cols = 0, rows = 0;
    struct yetty_ycore_void_result dims_res = yetty_yvterm_grid_dims(grid, &cols, &rows, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dims_res, "ymux server: grid dims failed");

    /* Worst case: every cell a 4-byte codepoint, plus one newline per row,
     * plus the terminating NUL. */
    size_t capacity = (size_t)rows * ((size_t)cols * 4 + 1) + 1;
    char *text = malloc(capacity);
    if (!text) {
        return YETTY_ERR(yetty_ycore_void, "ymux server: dump-text alloc failed");
    }

    size_t len = 0;
    for (uint32_t row = 0; row < rows; row++) {
        struct yetty_yvterm_text_cell_const_ptr_result cells_res =
            yetty_yvterm_grid_line_cells(grid, row);
        size_t line_start = len;
        size_t line_end = len; /* position after last non-blank */
        if (YETTY_IS_OK(cells_res) && cells_res.value) {
            const struct yetty_yvterm_text_cell *cells = cells_res.value;
            for (uint32_t col = 0; col < cols; col++) {
                uint32_t codepoint = cells[col].codepoint;
                if (codepoint == 0) {
                    codepoint = ' ';
                }
                len += utf8_encode(codepoint, text + len);
                if (codepoint != ' ') {
                    line_end = len;
                }
            }
        }
        /* Trim trailing blanks on the row. */
        len = (line_end > line_start) ? line_end : line_start;
        text[len++] = '\n';
    }
    text[len] = '\0';

    *out_text = text;
    *out_len = len;
    return YETTY_OK_VOID();
}

/* --------------------------------------------------------------------- */
/* Control verbs                                                         */
/* --------------------------------------------------------------------- */

/* Read an int field `key` from a msgpack map params blob, or return def. */
static int params_get_int(const struct yetty_yctl_message *msg, const char *key, int def)
{
    if (!msg->params || msg->params_len == 0) {
        return def;
    }
    int result = def;
    msgpack_unpacked unpacked;
    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) ==
        MSGPACK_UNPACK_SUCCESS) {
        const msgpack_object *params = &unpacked.data;
        if (params->type == MSGPACK_OBJECT_MAP) {
            size_t key_len = strlen(key);
            for (uint32_t i = 0; i < params->via.map.size; i++) {
                const msgpack_object_kv *kv = &params->via.map.ptr[i];
                if (kv->key.type == MSGPACK_OBJECT_STR && kv->key.via.str.size == key_len &&
                    memcmp(kv->key.via.str.ptr, key, key_len) == 0) {
                    if (kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                        result = (int)kv->val.via.u64;
                    } else if (kv->val.type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
                        result = (int)kv->val.via.i64;
                    }
                }
            }
        }
    }
    msgpack_unpacked_destroy(&unpacked);
    return result;
}

/* Pack a NUL-terminated string into the shared response scratch and hand
 * back a borrowed pointer for the dispatcher to copy. */
static struct yetty_rpc_handler_result respond_str(struct yetty_ymux_server *server,
                                                   const char *text, size_t len)
{
    msgpack_sbuffer_clear(&server->response);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &server->response, msgpack_sbuffer_write);
    msgpack_pack_str(&packer, len);
    msgpack_pack_str_body(&packer, text, len);
    return YETTY_YCTL_HANDLER_OK_DATA((const uint8_t *)server->response.data,
                                      server->response.size);
}

/* ymux-dump-text: return pane[N]'s visible grid as UTF-8 text. */
static struct yetty_rpc_handler_result handle_ymux_dump_text(const struct yetty_yctl_message *msg,
                                                             void *userdata)
{
    struct yetty_ymux_server *server = userdata;
    int pane_index = params_get_int(msg, "pane", 0);
    if (pane_index < 0 || (size_t)pane_index >= server->pane_count) {
        return YETTY_YCTL_HANDLER_ERR("ymux: no such pane");
    }
    struct yetty_yclass_object *grid = yetty_ymux_pane_grid(server->panes[pane_index].pane);
    if (!grid) {
        return YETTY_YCTL_HANDLER_ERR("ymux: pane has no grid");
    }

    char *text = NULL;
    size_t len = 0;
    struct yetty_ycore_void_result text_res = build_pane_text(grid, &text, &len);
    if (YETTY_IS_ERR(text_res)) {
        yetty_ycore_error_destroy(text_res.error);
        return YETTY_YCTL_HANDLER_ERR("ymux: dump-text failed");
    }
    struct yetty_rpc_handler_result result = respond_str(server, text, len);
    free(text);
    return result;
}

/* ymux-list: one line per pane — "index cols rows alive". */
static struct yetty_rpc_handler_result handle_ymux_list(const struct yetty_yctl_message *msg,
                                                        void *userdata)
{
    (void)msg;
    struct yetty_ymux_server *server = userdata;

    /* Small, bounded: MAX_PANES lines of a short fixed format. */
    char buf[YMUX_SERVER_MAX_PANES * 64 + 64];
    size_t len = 0;
    len += (size_t)snprintf(buf + len, sizeof(buf) - len, "session %s panes %zu\n",
                            server->session_name ? server->session_name : "default",
                            server->pane_count);
    for (size_t i = 0; i < server->pane_count && len < sizeof(buf); i++) {
        struct ymux_server_pane_slot *slot = &server->panes[i];
        len += (size_t)snprintf(buf + len, sizeof(buf) - len, "%zu %u %u %s\n", i,
                                yetty_ymux_pane_cols(slot->pane), yetty_ymux_pane_rows(slot->pane),
                                slot->alive ? "alive" : "dead");
    }
    return respond_str(server, buf, len);
}

/* Read a u64 field `key` from a msgpack map params blob, or return def. */
static uint64_t params_get_u64(const struct yetty_yctl_message *msg, const char *key, uint64_t def)
{
    if (!msg->params || msg->params_len == 0) {
        return def;
    }
    uint64_t result = def;
    msgpack_unpacked unpacked;
    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) ==
        MSGPACK_UNPACK_SUCCESS) {
        const msgpack_object *params = &unpacked.data;
        if (params->type == MSGPACK_OBJECT_MAP) {
            size_t key_len = strlen(key);
            for (uint32_t i = 0; i < params->via.map.size; i++) {
                const msgpack_object_kv *kv = &params->via.map.ptr[i];
                if (kv->key.type == MSGPACK_OBJECT_STR && kv->key.via.str.size == key_len &&
                    memcmp(kv->key.via.str.ptr, key, key_len) == 0 &&
                    kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                    result = kv->val.via.u64;
                }
            }
        }
    }
    msgpack_unpacked_destroy(&unpacked);
    return result;
}

/* Resolve the "pane" param (default 0) to a live slot, or NULL. */
static struct ymux_server_pane_slot *resolve_pane(struct yetty_ymux_server *server,
                                                  const struct yetty_yctl_message *msg)
{
    int pane_index = params_get_int(msg, "pane", 0);
    if (pane_index < 0 || (size_t)pane_index >= server->pane_count) {
        return NULL;
    }
    return &server->panes[pane_index];
}

/* Resize a pane's grid model AND its PTY winsize (fires the child SIGWINCH). */
static struct yetty_ycore_void_result server_resize_pane(struct ymux_server_pane_slot *slot,
                                                         uint32_t cols, uint32_t rows)
{
    struct yetty_yclass_object *grid = yetty_ymux_pane_grid(slot->pane);
    struct yetty_ycore_void_result gr = yetty_yvterm_grid_resize(grid, cols, rows);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "ymux server: grid resize failed");
    struct yetty_ycore_grid_size grid_size = {.rows = rows, .cols = cols};
    struct yetty_ycore_pixel_size cell_size = {.width = 8.0f, .height = 16.0f};
    struct yetty_ycore_void_result pr =
        yetty_ymux_pane_resize_pty(slot->pane, grid_size, cell_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ymux server: pty resize failed");
    return YETTY_OK_VOID();
}

/* ymux-attach {pane, cols, rows}: resize the pane to the client geometry and
 * return {cols, rows, offset}. The client replays the output log from `offset`
 * into its own emulator to reconstruct the screen, then polls for more. */
static struct yetty_rpc_handler_result handle_ymux_attach(const struct yetty_yctl_message *msg,
                                                          void *userdata)
{
    struct yetty_ymux_server *server = userdata;
    struct ymux_server_pane_slot *slot = resolve_pane(server, msg);
    if (!slot) {
        return YETTY_YCTL_HANDLER_ERR("ymux: no such pane");
    }
    int cols = params_get_int(msg, "cols", (int)yetty_ymux_pane_cols(slot->pane));
    int rows = params_get_int(msg, "rows", (int)yetty_ymux_pane_rows(slot->pane));
    if (cols > 0 && rows > 0) {
        struct yetty_ycore_void_result rr =
            server_resize_pane(slot, (uint32_t)cols, (uint32_t)rows);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
            return YETTY_YCTL_HANDLER_ERR("ymux: attach resize failed");
        }
    }

    /* Replay from the start of the retained log so a fresh attach reconstructs
     * the exact screen (out_base is 0 until the cap forces a trim). */
    msgpack_sbuffer_clear(&server->response);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &server->response, msgpack_sbuffer_write);
    msgpack_pack_map(&packer, 3);
    msgpack_pack_str(&packer, 4);
    msgpack_pack_str_body(&packer, "cols", 4);
    msgpack_pack_uint32(&packer, yetty_ymux_pane_cols(slot->pane));
    msgpack_pack_str(&packer, 4);
    msgpack_pack_str_body(&packer, "rows", 4);
    msgpack_pack_uint32(&packer, yetty_ymux_pane_rows(slot->pane));
    msgpack_pack_str(&packer, 6);
    msgpack_pack_str_body(&packer, "offset", 6);
    msgpack_pack_uint64(&packer, slot->out_base);
    return YETTY_YCTL_HANDLER_OK_DATA((const uint8_t *)server->response.data,
                                      server->response.size);
}

/* ymux-poll {pane, since}: return {offset, data} — the retained output bytes at
 * absolute offset [since, end). Empty data when the client is up to date. */
static struct yetty_rpc_handler_result handle_ymux_poll(const struct yetty_yctl_message *msg,
                                                        void *userdata)
{
    struct yetty_ymux_server *server = userdata;
    struct ymux_server_pane_slot *slot = resolve_pane(server, msg);
    if (!slot) {
        return YETTY_YCTL_HANDLER_ERR("ymux: no such pane");
    }
    uint64_t since = params_get_u64(msg, "since", slot->out_base);
    uint64_t end = slot->out_base + slot->out_len;
    size_t start;
    if (since <= slot->out_base) {
        start = 0;
    } else if (since >= end) {
        start = slot->out_len;
    } else {
        start = (size_t)(since - slot->out_base);
    }
    const uint8_t *chunk = slot->out ? slot->out + start : NULL;
    size_t chunk_len = slot->out_len - start;
    /* The yctl response frame is bounded (RESPONSE_BUFFER_SIZE). Cap the chunk
     * well under it and leave the client to poll again — the returned `offset`
     * only advances by what we actually shipped, so it re-polls from there. */
    if (chunk_len > YMUX_SERVER_POLL_CHUNK) {
        chunk_len = YMUX_SERVER_POLL_CHUNK;
    }
    uint64_t chunk_end = (since <= slot->out_base ? slot->out_base : since) + chunk_len;

    msgpack_sbuffer_clear(&server->response);
    msgpack_packer packer;
    msgpack_packer_init(&packer, &server->response, msgpack_sbuffer_write);
    msgpack_pack_map(&packer, 2);
    msgpack_pack_str(&packer, 6);
    msgpack_pack_str_body(&packer, "offset", 6);
    msgpack_pack_uint64(&packer, chunk_end);
    msgpack_pack_str(&packer, 4);
    msgpack_pack_str_body(&packer, "data", 4);
    msgpack_pack_bin(&packer, chunk_len);
    if (chunk_len > 0) {
        msgpack_pack_bin_body(&packer, chunk, chunk_len);
    }
    return YETTY_YCTL_HANDLER_OK_DATA((const uint8_t *)server->response.data,
                                      server->response.size);
}

/* ymux-input {pane, data}: write the client's keystrokes to the pane's PTY. */
static struct yetty_rpc_handler_result handle_ymux_input(const struct yetty_yctl_message *msg,
                                                         void *userdata)
{
    struct yetty_ymux_server *server = userdata;
    struct ymux_server_pane_slot *slot = resolve_pane(server, msg);
    if (!slot) {
        return YETTY_YCTL_HANDLER_ERR("ymux: no such pane");
    }
    if (!msg->params || msg->params_len == 0) {
        return YETTY_YCTL_HANDLER_ERR("ymux: input missing params");
    }

    struct yetty_rpc_handler_result result = YETTY_YCTL_HANDLER_ERR("ymux: input missing data");
    msgpack_unpacked unpacked;
    msgpack_unpacked_init(&unpacked);
    if (msgpack_unpack_next(&unpacked, (const char *)msg->params, msg->params_len, NULL) ==
        MSGPACK_UNPACK_SUCCESS) {
        const msgpack_object *params = &unpacked.data;
        if (params->type == MSGPACK_OBJECT_MAP) {
            for (uint32_t i = 0; i < params->via.map.size; i++) {
                const msgpack_object_kv *kv = &params->via.map.ptr[i];
                if (kv->key.type == MSGPACK_OBJECT_STR && kv->key.via.str.size == 4 &&
                    memcmp(kv->key.via.str.ptr, "data", 4) == 0) {
                    const char *bytes = NULL;
                    size_t len = 0;
                    if (kv->val.type == MSGPACK_OBJECT_BIN) {
                        bytes = kv->val.via.bin.ptr;
                        len = kv->val.via.bin.size;
                    } else if (kv->val.type == MSGPACK_OBJECT_STR) {
                        bytes = kv->val.via.str.ptr;
                        len = kv->val.via.str.size;
                    }
                    if (bytes) {
                        struct yetty_ycore_size_result wr =
                            yetty_ymux_pane_write(slot->pane, bytes, len);
                        if (YETTY_IS_ERR(wr)) {
                            yetty_ycore_error_destroy(wr.error);
                            result = YETTY_YCTL_HANDLER_ERR("ymux: input write failed");
                        } else {
                            result = YETTY_YCTL_HANDLER_OK_BOOL(1);
                        }
                    }
                }
            }
        }
    }
    msgpack_unpacked_destroy(&unpacked);
    return result;
}

/* ymux-resize {pane, cols, rows}: client window resize. */
static struct yetty_rpc_handler_result handle_ymux_resize(const struct yetty_yctl_message *msg,
                                                          void *userdata)
{
    struct yetty_ymux_server *server = userdata;
    struct ymux_server_pane_slot *slot = resolve_pane(server, msg);
    if (!slot) {
        return YETTY_YCTL_HANDLER_ERR("ymux: no such pane");
    }
    int cols = params_get_int(msg, "cols", 0);
    int rows = params_get_int(msg, "rows", 0);
    if (cols <= 0 || rows <= 0) {
        return YETTY_YCTL_HANDLER_ERR("ymux: resize needs positive cols/rows");
    }
    struct yetty_ycore_void_result rr = server_resize_pane(slot, (uint32_t)cols, (uint32_t)rows);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return YETTY_YCTL_HANDLER_ERR("ymux: resize failed");
    }
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

/* ymux-detach {pane}: pull-model no-op — the client simply stops polling. Kept
 * for protocol symmetry and future push transports. */
static struct yetty_rpc_handler_result handle_ymux_detach(const struct yetty_yctl_message *msg,
                                                          void *userdata)
{
    (void)msg;
    (void)userdata;
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

/* ymux-shutdown: stop the loop so the server process exits cleanly. */
static struct yetty_rpc_handler_result handle_ymux_shutdown(const struct yetty_yctl_message *msg,
                                                            void *userdata)
{
    (void)msg;
    struct yetty_ymux_server *server = userdata;
    yinfo("ymux server: shutdown requested");
    struct yetty_ycore_void_result stop_res = server->event_loop->ops->stop(server->event_loop);
    if (YETTY_IS_ERR(stop_res)) {
        yetty_ycore_error_destroy(stop_res.error);
        return YETTY_YCTL_HANDLER_ERR("ymux: loop stop failed");
    }
    return YETTY_YCTL_HANDLER_OK_BOOL(1);
}

static struct yetty_ycore_void_result server_register_verbs(struct yetty_ymux_server *server)
{
    struct yetty_ycore_void_result res;
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-dump-text", handle_ymux_dump_text, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register dump-text");
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-list", handle_ymux_list, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register list");
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-attach", handle_ymux_attach, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register attach");
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-poll", handle_ymux_poll, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register poll");
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-input", handle_ymux_input, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register input");
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-resize", handle_ymux_resize, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register resize");
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-detach", handle_ymux_detach, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register detach");
    res = yetty_yctl_server_register_handler(server->control, YETTY_YCTL_CHANNEL_EVENT_LOOP,
                                             "ymux-shutdown", handle_ymux_shutdown, server);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "ymux server: register shutdown");
    return YETTY_OK_VOID();
}

/* --------------------------------------------------------------------- */
/* Teardown                                                              */
/* --------------------------------------------------------------------- */

static void server_teardown(struct yetty_ymux_server *server)
{
    for (size_t i = 0; i < server->pane_count; i++) {
        if (server->panes[i].pane) {
            struct yetty_ycore_void_result dr = yetty_ymux_pane_destroy(server->panes[i].pane);
            if (YETTY_IS_ERR(dr)) {
                ywarn("ymux server: pane %zu destroy: %s", i, dr.error.msg);
                yetty_ycore_error_destroy(dr.error);
            }
            server->panes[i].pane = NULL;
        }
        free(server->panes[i].out);
        server->panes[i].out = NULL;
    }
    if (server->control) {
        struct yetty_ycore_void_result cr = yetty_yctl_server_destroy(server->control);
        if (YETTY_IS_ERR(cr)) {
            ywarn("ymux server: control destroy: %s", cr.error.msg);
            yetty_ycore_error_destroy(cr.error);
        }
        server->control = NULL;
    }
    if (server->pty_factory) {
        server->pty_factory->ops->destroy(server->pty_factory);
        server->pty_factory = NULL;
    }
    if (server->memtag_registry) {
        yetty_ycore_memtag_registry_destroy(server->memtag_registry);
        server->memtag_registry = NULL;
    }
    if (server->event_loop) {
        struct yetty_ycore_void_result er = server->event_loop->ops->destroy(server->event_loop);
        if (YETTY_IS_ERR(er)) {
            ywarn("ymux server: event loop destroy: %s", er.error.msg);
            yetty_ycore_error_destroy(er.error);
        }
        server->event_loop = NULL;
    }
    msgpack_sbuffer_destroy(&server->response);
    free(server->session_name);
}

/* --------------------------------------------------------------------- */
/* Entry point                                                           */
/* --------------------------------------------------------------------- */

struct yetty_ycore_void_result yetty_ymux_server_run(struct yetty_yconfig_config *config)
{
    if (!config) {
        return YETTY_ERR(yetty_ycore_void, "ymux server: config is NULL");
    }

#if !defined(_WIN32)
    /* A hangup on the controlling terminal (the launching client detaching)
     * must not kill the server — that is the whole point of detach. Ignore
     * SIGHUP so the loop keeps pumping the surviving shells. */
    signal(SIGHUP, SIG_IGN);
#endif

    const char *session = config->ops->get_string(config, YETTY_YCONFIG_KEY_YMUX_SERVER, "default");
    const char *port_str = config->ops->get_string(config, YETTY_YCONFIG_KEY_YMUX_PORT, NULL);
    int port = port_str ? atoi(port_str) : YMUX_SERVER_DEFAULT_PORT;
    const char *bind_addr =
        config->ops->get_string(config, YETTY_YCONFIG_KEY_YMUX_BIND, "127.0.0.1");

    struct yetty_ymux_server server = {0};
    server.config = config;
    server.session_name = session ? strdup(session) : NULL;
    msgpack_sbuffer_init(&server.response);

    yinfo("ymux server: starting session '%s' on %s:%d",
          server.session_name ? server.session_name : "default", bind_addr, port);

    /* Event loop — no platform input pipe (headless: no window thread). */
    struct yetty_ycore_event_loop_result loop_res = yetty_ycore_event_loop_create(NULL);
    if (YETTY_IS_ERR(loop_res)) {
        server_teardown(&server);
        return YETTY_ERR(yetty_ycore_void, "ymux server: event loop create failed", loop_res);
    }
    server.event_loop = loop_res.value;

    /* PTY factory (honours -e / shell config, GPU-free). */
    struct yetty_yplatform_pty_factory_ptr_result factory_res =
        yetty_yplatform_pty_factory_create(config, NULL);
    if (YETTY_IS_ERR(factory_res)) {
        server_teardown(&server);
        return YETTY_ERR(yetty_ycore_void, "ymux server: pty factory create failed", factory_res);
    }
    server.pty_factory = factory_res.value;

    /* Per-owner allocation accounting — best-effort (the `memtags` verb
     * dumps it; the server runs fine without it). */
    struct yetty_ycore_memtag_registry_ptr_result memtag_res = yetty_ycore_memtag_registry_create();
    if (YETTY_IS_OK(memtag_res)) {
        server.memtag_registry = memtag_res.value;
    } else {
        ywarn("ymux server: memtag registry create failed: %s", memtag_res.error.msg);
        yetty_ycore_error_destroy(memtag_res.error);
    }

    /* Context handed to panes: no GPU runtime, real pty factory + loop. */
    server.context.runtime = NULL;
    server.context.pty_factory = server.pty_factory;
    server.context.event_loop = server.event_loop;

    /* Control channel (msgpack-RPC over TCP). */
    struct yetty_rpc_server_ptr_result control_res = yetty_yctl_server_create(server.event_loop);
    if (YETTY_IS_ERR(control_res)) {
        server_teardown(&server);
        return YETTY_ERR(yetty_ycore_void, "ymux server: control create failed", control_res);
    }
    server.control = control_res.value;
    yetty_yctl_server_set_memtag_registry(server.control, server.memtag_registry);

    struct yetty_ycore_void_result verbs_res = server_register_verbs(&server);
    if (YETTY_IS_ERR(verbs_res)) {
        server_teardown(&server);
        return YETTY_ERR(yetty_ycore_void, "ymux server: verb registration failed", verbs_res);
    }

    struct yetty_ycore_void_result start_res =
        yetty_yctl_server_start(server.control, bind_addr, port);
    if (YETTY_IS_ERR(start_res)) {
        server_teardown(&server);
        return YETTY_ERR(yetty_ycore_void, "ymux server: control start failed", start_res);
    }
    yinfo("ymux server: control listening on %s:%d", bind_addr, port);

    /* Bring up the initial pane (the bootstrap session). Phase 2a moves
     * pane creation to an attach verb; for 1b one shell proves the model
     * pumps headless. */
    struct yetty_ycore_void_result pane_res =
        server_spawn_pane(&server, YMUX_SERVER_DEFAULT_COLS, YMUX_SERVER_DEFAULT_ROWS);
    if (YETTY_IS_ERR(pane_res)) {
        server_teardown(&server);
        return YETTY_ERR(yetty_ycore_void, "ymux server: initial pane spawn failed", pane_res);
    }

    /* Pump the loop — blocks until ymux-shutdown calls stop(). The control
     * TCP server + the pane's PTY pipe keep the loop alive. */
    yinfo("ymux server: running");
    struct yetty_ycore_void_result run_res = server.event_loop->ops->start(server.event_loop);
    if (YETTY_IS_ERR(run_res)) {
        server_teardown(&server);
        return YETTY_ERR(yetty_ycore_void, "ymux server: event loop run failed", run_res);
    }

    yinfo("ymux server: stopped, tearing down");
    server_teardown(&server);
    return YETTY_OK_VOID();
}
