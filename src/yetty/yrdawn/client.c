/*
 * client.c — yrdawn client (the wasm/POSIX side of the bridge).
 *
 * Architecture:
 *   - `client` owns the PTY connection. It allocates handles, holds
 *     the in-yface decoder, and carries a "current canvas" marker that
 *     stamps every outbound CMD record with the right figure_id.
 *   - `canvas` is one remote yfigure. Per-canvas callbacks
 *     (reply / event / key / resize) and the pending-req table live
 *     here — multiple canvases on the same client each track their own
 *     in-flight requests.
 *
 * Outbound wire shape: every CMD/BULK/BYE goes out as a record inside
 * a YETTY_OSC_YCOMPOSITOR_BIN envelope of shape `{u32 length, u32 id,
 * u32 SUB_OP, payload...}`. canvas_create emits a CREATE_CHILD admin
 * record (id=0) whose init_payload is the SUB_HELLO body, so the
 * server-side figure is bound to its session in the same envelope.
 */
#include <yetty/yrdawn/client.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/yface/yface.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yplatform/io.h>
#include <yetty/yplatform/time.h>
#include <yetty/yrdawn/wire.h>

/* Local copy — pulling <yetty/yterminal/osc-codes.h> would drag in
 * terminal-only constants for a single value. */
#define YETTY_YRDAWN_CLIENT_OSC_COMPOSITOR_BIN 630000

struct pending_req {
    uint32_t req_id; /* 0 = free slot */
    uint32_t method_id;
    yetty_yrdawn_reply_cb cb;
    void *user;
};

struct yetty_yrdawn_canvas {
    struct yetty_yrdawn_client *client;
    uint32_t figure_id;
    int connected;

    yetty_yrdawn_event_cb event_cb;
    void *event_user;
    yetty_yrdawn_input_key_cb input_key_cb;
    void *input_key_user;
    yetty_yrdawn_input_resize_cb input_resize_cb;
    void *input_resize_user;

    struct pending_req *pending;
    size_t pending_cap;
};

struct yetty_yrdawn_client {
    int in_fd;
    int out_fd;
    uint32_t session_id;

    struct yetty_yface *in_face;

    uint64_t next_handle;
    uint32_t next_req_id;
    uint32_t next_bulk_ref;

    /* Canvas list + current-canvas marker. Linear scan — N is small. */
    struct yetty_yrdawn_canvas **canvases;
    size_t canvas_count;
    size_t canvas_cap;
    struct yetty_yrdawn_canvas *current_canvas;

    uint8_t *rx_buf;
    size_t rx_buf_cap;
};

/*===========================================================================
 * Helpers
 *=========================================================================*/

static struct yetty_yrdawn_canvas *client_find_canvas(struct yetty_yrdawn_client *c,
                                                      uint32_t figure_id)
{
    for (size_t i = 0; i < c->canvas_count; ++i) {
        if (c->canvases[i] && c->canvases[i]->figure_id == figure_id) {
            return c->canvases[i];
        }
    }
    return NULL;
}

static struct pending_req *canvas_pending_find_free(struct yetty_yrdawn_canvas *cv)
{
    for (size_t i = 0; i < cv->pending_cap; ++i) {
        if (cv->pending[i].req_id == 0) {
            return &cv->pending[i];
        }
    }
    size_t new_cap = cv->pending_cap == 0 ? 16 : cv->pending_cap * 2;
    struct pending_req *grown = realloc(cv->pending, new_cap * sizeof(*grown));
    if (!grown) {
        return NULL;
    }
    memset(grown + cv->pending_cap, 0, (new_cap - cv->pending_cap) * sizeof(*grown));
    cv->pending = grown;
    struct pending_req *slot = &cv->pending[cv->pending_cap];
    cv->pending_cap = new_cap;
    return slot;
}

static struct pending_req *canvas_pending_take(struct yetty_yrdawn_canvas *cv, uint32_t req_id)
{
    for (size_t i = 0; i < cv->pending_cap; ++i) {
        if (cv->pending[i].req_id == req_id) {
            return &cv->pending[i];
        }
    }
    return NULL;
}

/*===========================================================================
 * Inbound dispatch
 *=========================================================================*/

static void on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yetty_yrdawn_client *c = (struct yetty_yrdawn_client *)user;

    switch (osc_code) {
    case YETTY_YRDAWN_OSC_SC_HELLO_ACK: {
        if (payload_len < sizeof(struct yetty_yrdawn_wire_hello_ack)) {
            return;
        }
        const struct yetty_yrdawn_wire_hello_ack *ack =
            (const struct yetty_yrdawn_wire_hello_ack *)payload;
        if (ack->magic != YETTY_YRDAWN_MAGIC_HELLO_ACK) {
            return;
        }
        struct yetty_yrdawn_canvas *cv = client_find_canvas(c, ack->figure_id);
        if (cv && ack->status == YETTY_YRDAWN_HELLO_OK) {
            cv->connected = 1;
        }
        return;
    }
    case YETTY_YRDAWN_OSC_SC_REPLY: {
        if (payload_len < sizeof(struct yetty_yrdawn_wire_reply_hdr)) {
            return;
        }
        const struct yetty_yrdawn_wire_reply_hdr *hdr =
            (const struct yetty_yrdawn_wire_reply_hdr *)payload;
        if (hdr->magic != YETTY_YRDAWN_MAGIC_REPLY) {
            return;
        }
        struct yetty_yrdawn_canvas *cv = client_find_canvas(c, hdr->figure_id);
        if (!cv) {
            return;
        }
        struct pending_req *slot = canvas_pending_take(cv, hdr->req_id);
        if (!slot) {
            return;
        }
        yetty_yrdawn_reply_cb cb = slot->cb;
        void *cb_user = slot->user;
        uint32_t method_id = hdr->method_id;
        slot->req_id = 0;
        slot->cb = NULL;
        slot->user = NULL;
        if (cb) {
            const uint8_t *body = payload + sizeof(*hdr);
            size_t body_len = payload_len - sizeof(*hdr);
            cb(cb_user, hdr->status, method_id, body, body_len);
        }
        return;
    }
    case YETTY_YRDAWN_OSC_SC_EVENT: {
        if (payload_len < sizeof(struct yetty_yrdawn_wire_event_hdr)) {
            return;
        }
        const struct yetty_yrdawn_wire_event_hdr *hdr =
            (const struct yetty_yrdawn_wire_event_hdr *)payload;
        if (hdr->magic != YETTY_YRDAWN_MAGIC_EVENT) {
            return;
        }
        struct yetty_yrdawn_canvas *cv = client_find_canvas(c, hdr->figure_id);
        if (cv && cv->event_cb) {
            const uint8_t *body = payload + sizeof(*hdr);
            size_t body_len = payload_len - sizeof(*hdr);
            cv->event_cb(cv->event_user, hdr->kind, hdr->device_handle, body, body_len);
        }
        return;
    }
    case YETTY_YRDAWN_OSC_SC_KEY: {
        if (payload_len < sizeof(struct yetty_yrdawn_wire_input_key)) {
            return;
        }
        const struct yetty_yrdawn_wire_input_key *k =
            (const struct yetty_yrdawn_wire_input_key *)payload;
        if (k->magic != YETTY_YRDAWN_MAGIC_INPUT_KEY) {
            return;
        }
        struct yetty_yrdawn_canvas *cv = client_find_canvas(c, k->figure_id);
        if (cv && cv->input_key_cb) {
            cv->input_key_cb(cv->input_key_user, k->kind, k->key, k->mods, k->codepoint);
        }
        return;
    }
    case YETTY_YRDAWN_OSC_SC_RESIZE: {
        if (payload_len < sizeof(struct yetty_yrdawn_wire_input_resize)) {
            return;
        }
        const struct yetty_yrdawn_wire_input_resize *r =
            (const struct yetty_yrdawn_wire_input_resize *)payload;
        if (r->magic != YETTY_YRDAWN_MAGIC_INPUT_RESIZE) {
            return;
        }
        struct yetty_yrdawn_canvas *cv = client_find_canvas(c, r->figure_id);
        if (cv && cv->input_resize_cb) {
            cv->input_resize_cb(cv->input_resize_user, r->width, r->height);
        }
        return;
    }
    case YETTY_YRDAWN_OSC_SC_BULK:
    case YETTY_YRDAWN_OSC_SC_ERROR:
        /* BULK reassembly and protocol-error handling land in the next
         * cut. Silent drop for now. */
        return;
    default:
        return;
    }
}

/*===========================================================================
 * Client lifecycle
 *=========================================================================*/

struct yetty_yrdawn_client_ptr_result yetty_yrdawn_client_create(int in_fd, int out_fd,
                                                                 uint32_t session_id)
{
    if (session_id == 0) {
        return YETTY_ERR(yetty_yrdawn_client_ptr, "yrdawn_client_create: session_id == 0");
    }
    struct yetty_yrdawn_client *c = calloc(1, sizeof(*c));
    if (!c) {
        return YETTY_ERR(yetty_yrdawn_client_ptr, "yrdawn_client_create: oom");
    }

    c->in_fd = in_fd;
    c->out_fd = out_fd;
    c->session_id = session_id;
    c->next_handle = 1;
    c->next_req_id = 1;
    c->next_bulk_ref = 1;

    c->rx_buf_cap = 65536;
    c->rx_buf = malloc(c->rx_buf_cap);
    if (!c->rx_buf) {
        free(c);
        return YETTY_ERR(yetty_yrdawn_client_ptr, "yrdawn_client_create: rx_buf oom");
    }

    struct yetty_yface_ptr_result fr = yetty_yface_create();
    if (YETTY_IS_ERR(fr)) {
        free(c->rx_buf);
        free(c);
        return YETTY_ERR(yetty_yrdawn_client_ptr, "yrdawn_client_create: yface_create", fr);
    }
    c->in_face = fr.value;
    yetty_yface_set_handlers(c->in_face, on_osc, NULL, c);

    return YETTY_OK(yetty_yrdawn_client_ptr, c);
}

struct yetty_ycore_void_result yetty_yrdawn_client_destroy(struct yetty_yrdawn_client *c)
{
    if (!c) {
        return YETTY_OK_VOID();
    }
    /* Destroy any canvases still attached. Each one drops its
     * DELETE_CHILD on the wire before freeing. */
    while (c->canvas_count > 0) {
        struct yetty_yrdawn_canvas *cv = c->canvases[c->canvas_count - 1];
        struct yetty_ycore_void_result r = yetty_yrdawn_canvas_destroy(cv);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }
    free(c->canvases);
    if (c->in_face) {
        yetty_yface_destroy(c->in_face);
    }
    free(c->rx_buf);
    free(c);
    return YETTY_OK_VOID();
}

uint32_t yetty_yrdawn_client_session_id(const struct yetty_yrdawn_client *c)
{
    return c ? c->session_id : 0;
}

uint64_t yetty_yrdawn_client_alloc_handle(struct yetty_yrdawn_client *c)
{
    return c->next_handle++;
}

/*===========================================================================
 * Outbound: container envelope writer
 *
 * Every outbound record gets wrapped in `{u32 record_length, u32 id}
 * + payload` and ridden on YETTY_OSC_YCOMPOSITOR_BIN. payload always
 * starts with a u32 SUB_OP (figure-targeted) or u32 admin_op (id==0
 * container admin).
 *=========================================================================*/

static struct yetty_ycore_void_result emit_envelope(struct yetty_yrdawn_client *c, int compressed,
                                                    const uint8_t *body, size_t body_len)
{
    return yetty_yface_emit_to_fd(c->out_fd, YETTY_YRDAWN_CLIENT_OSC_COMPOSITOR_BIN, compressed,
                                  NULL, 0, body, body_len);
}

/* Emit one figure-tree record (id != 0) carrying SUB_OP + body. */
static struct yetty_ycore_void_result emit_record(struct yetty_yrdawn_client *c, uint32_t figure_id,
                                                  int compressed, uint32_t sub_op, const void *body,
                                                  size_t body_len)
{
    size_t inner_len = 4u + body_len;  /* sub_op + body */
    size_t frame_len = 8u + inner_len; /* record hdr + inner */
    uint8_t *frame = malloc(frame_len);
    if (!frame) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client: emit_record oom");
    }
    uint32_t length = (uint32_t)inner_len;
    memcpy(frame + 0, &length, 4);
    memcpy(frame + 4, &figure_id, 4);
    memcpy(frame + 8, &sub_op, 4);
    if (body_len) {
        memcpy(frame + 12, body, body_len);
    }
    struct yetty_ycore_void_result r = emit_envelope(c, compressed, frame, frame_len);
    free(frame);
    return r;
}

/* Emit a CREATE_CHILD admin record (id == 0) carrying SUB_HELLO. */
static struct yetty_ycore_void_result emit_create_child(struct yetty_yrdawn_client *c,
                                                        uint32_t figure_id, float min_x,
                                                        float min_y, float max_x, float max_y,
                                                        uint32_t session_id)
{
    /* Admin payload: u32 admin_op | u32 child_id | u32 kind | f32 rect[4] |
     *                u32 init_bytes | init... */
    struct yetty_yrdawn_wire_hello hello = {
        .magic = YETTY_YRDAWN_MAGIC_HELLO,
        .version = YETTY_YRDAWN_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(hello),
        .session_id = session_id,
        .figure_id = figure_id,
    };
    uint32_t sub_op = YETTY_YRDAWN_FIGURE_SUB_HELLO;

    size_t init_bytes = sizeof(sub_op) + sizeof(hello);
    size_t admin_len = 4u + 4u + 4u + 16u + 4u + init_bytes;
    size_t frame_len = 8u + admin_len;
    uint8_t *frame = malloc(frame_len);
    if (!frame) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client: emit_create_child oom");
    }
    uint8_t *p = frame;

    uint32_t length = (uint32_t)admin_len;
    uint32_t id_zero = 0;
    memcpy(p, &length, 4);
    p += 4;
    memcpy(p, &id_zero, 4);
    p += 4;

    uint32_t admin_op = YETTY_YFIGURE_ADMIN_CREATE_CHILD;
    memcpy(p, &admin_op, 4);
    p += 4;
    memcpy(p, &figure_id, 4);
    p += 4;
    uint32_t kind = YETTY_YFIGURE_KIND_YRDAWN;
    memcpy(p, &kind, 4);
    p += 4;
    float rect_floats[4] = {min_x, min_y, max_x, max_y};
    memcpy(p, rect_floats, 16);
    p += 16;
    uint32_t init_len = (uint32_t)init_bytes;
    memcpy(p, &init_len, 4);
    p += 4;
    memcpy(p, &sub_op, 4);
    p += 4;
    memcpy(p, &hello, sizeof(hello));
    p += sizeof(hello);
    (void)p;

    /* Admin records contain version/sizes; raw b64 is fine. */
    struct yetty_ycore_void_result r = emit_envelope(c, 0, frame, frame_len);
    free(frame);
    return r;
}

/* Emit a DELETE_CHILD admin record (id == 0). */
static struct yetty_ycore_void_result emit_delete_child(struct yetty_yrdawn_client *c,
                                                        uint32_t figure_id)
{
    /* Admin payload: u32 admin_op | u32 child_id */
    size_t admin_len = 4u + 4u;
    size_t frame_len = 8u + admin_len;
    uint8_t frame[16];
    uint32_t length = (uint32_t)admin_len;
    uint32_t id_zero = 0;
    uint32_t admin_op = YETTY_YFIGURE_ADMIN_DELETE_CHILD;
    memcpy(frame + 0, &length, 4);
    memcpy(frame + 4, &id_zero, 4);
    memcpy(frame + 8, &admin_op, 4);
    memcpy(frame + 12, &figure_id, 4);
    return emit_envelope(c, 0, frame, frame_len);
}

/*===========================================================================
 * Canvas lifecycle
 *=========================================================================*/

struct yetty_yrdawn_canvas_ptr_result yetty_yrdawn_canvas_create(struct yetty_yrdawn_client *c,
                                                                 uint32_t figure_id, float x,
                                                                 float y, float w, float h)
{
    if (!c) {
        return YETTY_ERR(yetty_yrdawn_canvas_ptr, "yrdawn_canvas_create: NULL client");
    }
    if (figure_id == 0) {
        return YETTY_ERR(yetty_yrdawn_canvas_ptr, "yrdawn_canvas_create: figure_id == 0");
    }
    if (client_find_canvas(c, figure_id)) {
        return YETTY_ERR(yetty_yrdawn_canvas_ptr, "yrdawn_canvas_create: duplicate figure_id");
    }
    struct yetty_yrdawn_canvas *cv = calloc(1, sizeof(*cv));
    if (!cv) {
        return YETTY_ERR(yetty_yrdawn_canvas_ptr, "yrdawn_canvas_create: oom");
    }
    cv->client = c;
    cv->figure_id = figure_id;

    if (c->canvas_count == c->canvas_cap) {
        size_t cap = c->canvas_cap ? c->canvas_cap * 2u : 4u;
        struct yetty_yrdawn_canvas **grown = realloc(c->canvases, cap * sizeof(*grown));
        if (!grown) {
            free(cv);
            return YETTY_ERR(yetty_yrdawn_canvas_ptr, "yrdawn_canvas_create: canvases oom");
        }
        c->canvases = grown;
        c->canvas_cap = cap;
    }
    c->canvases[c->canvas_count++] = cv;
    c->current_canvas = cv;

    struct yetty_ycore_void_result er =
        emit_create_child(c, figure_id, x, y, x + w, y + h, c->session_id);
    if (YETTY_IS_ERR(er)) {
        c->canvas_count--;
        c->canvases[c->canvas_count] = NULL;
        if (c->current_canvas == cv) {
            c->current_canvas = NULL;
        }
        free(cv->pending);
        free(cv);
        return YETTY_ERR(yetty_yrdawn_canvas_ptr, "yrdawn_canvas_create: emit_create_child", er);
    }
    return YETTY_OK(yetty_yrdawn_canvas_ptr, cv);
}

struct yetty_ycore_void_result yetty_yrdawn_canvas_destroy(struct yetty_yrdawn_canvas *cv)
{
    if (!cv) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrdawn_client *c = cv->client;
    /* Drop the canvas server-side. Best-effort — if the PTY is already
     * gone the emit will fail and we still need to free local state. */
    struct yetty_ycore_void_result dr = emit_delete_child(c, cv->figure_id);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    for (size_t i = 0; i < c->canvas_count; ++i) {
        if (c->canvases[i] == cv) {
            c->canvases[i] = c->canvases[--c->canvas_count];
            break;
        }
    }
    if (c->current_canvas == cv) {
        c->current_canvas = c->canvas_count ? c->canvases[0] : NULL;
    }
    free(cv->pending);
    free(cv);
    return YETTY_OK_VOID();
}

struct yetty_yrdawn_canvas *yetty_yrdawn_canvas_make_current(struct yetty_yrdawn_canvas *cv)
{
    if (!cv || !cv->client) {
        return NULL;
    }
    struct yetty_yrdawn_canvas *prev = cv->client->current_canvas;
    cv->client->current_canvas = cv;
    return prev;
}

uint32_t yetty_yrdawn_canvas_figure_id(const struct yetty_yrdawn_canvas *cv)
{
    return cv ? cv->figure_id : 0;
}
struct yetty_yrdawn_client *yetty_yrdawn_canvas_client(struct yetty_yrdawn_canvas *cv)
{
    return cv ? cv->client : NULL;
}
int yetty_yrdawn_canvas_connected(const struct yetty_yrdawn_canvas *cv)
{
    return cv ? cv->connected : 0;
}

void yetty_yrdawn_canvas_set_event_cb(struct yetty_yrdawn_canvas *cv, yetty_yrdawn_event_cb cb,
                                      void *user)
{
    if (!cv) {
        return;
    }
    cv->event_cb = cb;
    cv->event_user = user;
}
void yetty_yrdawn_canvas_set_input_key_cb(struct yetty_yrdawn_canvas *cv,
                                          yetty_yrdawn_input_key_cb cb, void *user)
{
    if (!cv) {
        return;
    }
    cv->input_key_cb = cb;
    cv->input_key_user = user;
}
void yetty_yrdawn_canvas_set_input_resize_cb(struct yetty_yrdawn_canvas *cv,
                                             yetty_yrdawn_input_resize_cb cb, void *user)
{
    if (!cv) {
        return;
    }
    cv->input_resize_cb = cb;
    cv->input_resize_user = user;
}

/*===========================================================================
 * CMD emit helpers (used by the codegen wgpu* client stubs)
 *=========================================================================*/

static struct yetty_ycore_void_result emit_cmd(struct yetty_yrdawn_client *c, uint32_t method_id,
                                               uint32_t req_id, const void *body, size_t body_len)
{
    if (!c->current_canvas) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client: no current canvas");
    }
    size_t total = sizeof(struct yetty_yrdawn_wire_cmd_hdr) + body_len;
    uint8_t *frame = malloc(total);
    if (!frame) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client: cmd frame oom");
    }
    struct yetty_yrdawn_wire_cmd_hdr hdr = {
        .magic = YETTY_YRDAWN_MAGIC_CMD,
        .version = YETTY_YRDAWN_WIRE_VERSION,
        .total_size = (uint32_t)total,
        .method_id = method_id,
        .req_id = req_id,
        .flags = 0,
    };
    memcpy(frame, &hdr, sizeof(hdr));
    if (body_len) {
        memcpy(frame + sizeof(hdr), body, body_len);
    }
    struct yetty_ycore_void_result r =
        emit_record(c, c->current_canvas->figure_id, /*compressed=*/1, YETTY_YRDAWN_FIGURE_SUB_CMD,
                    frame, total);
    free(frame);
    return r;
}

struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_sync(struct yetty_yrdawn_client *c,
                                                                 uint32_t method_id,
                                                                 const void *body, size_t body_len)
{
    return emit_cmd(c, method_id, 0, body, body_len);
}

struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_async(struct yetty_yrdawn_client *c,
                                                                  uint32_t method_id,
                                                                  const void *body, size_t body_len,
                                                                  yetty_yrdawn_reply_cb cb,
                                                                  void *user)
{
    if (!c->current_canvas) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client send_cmd_async: no current canvas");
    }
    struct yetty_yrdawn_canvas *cv = c->current_canvas;
    struct pending_req *slot = canvas_pending_find_free(cv);
    if (!slot) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client: pending table oom");
    }
    uint32_t req_id = c->next_req_id++;
    if (req_id == 0) {
        req_id = c->next_req_id++;
    }
    slot->req_id = req_id;
    slot->method_id = method_id;
    slot->cb = cb;
    slot->user = user;

    struct yetty_ycore_void_result r = emit_cmd(c, method_id, req_id, body, body_len);
    if (YETTY_IS_ERR(r)) {
        slot->req_id = 0;
        slot->cb = NULL;
        slot->user = NULL;
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client: emit_cmd async", r);
    }
    return YETTY_OK_VOID();
}

struct blocking_state {
    int done;
    uint32_t status;
    void *out;
    size_t out_size;
    size_t got_len;
};

static void blocking_cb(void *user, uint32_t status, uint32_t method_id, const uint8_t *body,
                        size_t body_len)
{
    (void)method_id;
    struct blocking_state *s = user;
    s->status = status;
    if (s->out && s->out_size > 0) {
        size_t n = body_len < s->out_size ? body_len : s->out_size;
        if (n) {
            memcpy(s->out, body, n);
        }
        s->got_len = n;
    }
    s->done = 1;
}

struct blocking_dyn_state {
    int done;
    uint32_t status;
    uint8_t *buf;
    size_t len;
};

static void blocking_dyn_cb(void *user, uint32_t status, uint32_t method_id, const uint8_t *body,
                            size_t body_len)
{
    (void)method_id;
    struct blocking_dyn_state *s = user;
    s->status = status;
    if (body_len > 0) {
        s->buf = malloc(body_len);
        if (s->buf) {
            memcpy(s->buf, body, body_len);
            s->len = body_len;
        }
    }
    s->done = 1;
}

struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_blocking(
    struct yetty_yrdawn_client *c, uint32_t method_id, const void *body, size_t body_len, void *out,
    size_t out_size, uint32_t *out_status)
{
    struct blocking_state st = {0};
    st.out = out;
    st.out_size = out_size;
    struct yetty_ycore_void_result sr =
        yetty_yrdawn_client_send_cmd_async(c, method_id, body, body_len, blocking_cb, &st);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "send_cmd_blocking: send_cmd_async");
    int spins = 0;
    while (!st.done) {
        struct yetty_ycore_void_result pr = yetty_yrdawn_client_pump(c);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "send_cmd_blocking: pump");
        if (st.done) {
            break;
        }
        yetty_yplatform_ytime_sleep_ms(1);
        if (++spins > 5000) {
            return YETTY_ERR(yetty_ycore_void, "send_cmd_blocking: timeout");
        }
    }
    if (out_status) {
        *out_status = st.status;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yrdawn_client_send_cmd_blocking_dyn(
    struct yetty_yrdawn_client *c, uint32_t method_id, const void *body, size_t body_len,
    uint8_t **out_buf, size_t *out_len, uint32_t *out_status)
{
    if (out_buf) {
        *out_buf = NULL;
    }
    if (out_len) {
        *out_len = 0;
    }
    struct blocking_dyn_state st = {0};
    struct yetty_ycore_void_result sr =
        yetty_yrdawn_client_send_cmd_async(c, method_id, body, body_len, blocking_dyn_cb, &st);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "send_cmd_blocking_dyn: send_cmd_async");
    int spins = 0;
    while (!st.done) {
        struct yetty_ycore_void_result pr = yetty_yrdawn_client_pump(c);
        if (YETTY_IS_ERR(pr)) {
            free(st.buf);
            return YETTY_ERR(yetty_ycore_void, "send_cmd_blocking_dyn: pump", pr);
        }
        if (st.done) {
            break;
        }
        yetty_yplatform_ytime_sleep_ms(1);
        if (++spins > 5000) {
            free(st.buf);
            return YETTY_ERR(yetty_ycore_void, "send_cmd_blocking_dyn: timeout");
        }
    }
    if (out_buf) {
        *out_buf = st.buf;
    }
    if (out_len) {
        *out_len = st.len;
    }
    if (out_status) {
        *out_status = st.status;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * BULK emit — chunks under a non-zero ref, current canvas
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yrdawn_client_send_bulk(struct yetty_yrdawn_client *c,
                                                             uint32_t ref, const void *bytes,
                                                             size_t len)
{
    if (ref == 0) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client_send_bulk: ref=0");
    }
    if (!c->current_canvas) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_client_send_bulk: no current canvas");
    }

    const size_t chunk_max = 64u * 1024u;
    const uint8_t *p = bytes;
    size_t remaining = len;
    uint32_t seq = 0;

    do {
        size_t chunk = remaining > chunk_max ? chunk_max : remaining;
        int is_last = (remaining - chunk == 0);

        size_t frame_bytes = sizeof(struct yetty_yrdawn_wire_bulk_hdr) + chunk;
        uint8_t *frame = malloc(frame_bytes);
        if (!frame) {
            return YETTY_ERR(yetty_ycore_void, "yrdawn_client_send_bulk: oom");
        }
        struct yetty_yrdawn_wire_bulk_hdr hdr = {
            .magic = YETTY_YRDAWN_MAGIC_BULK,
            .version = YETTY_YRDAWN_WIRE_VERSION,
            .total_size = (uint32_t)frame_bytes,
            .ref = ref,
            .seq = seq,
            .flags = is_last ? YETTY_YRDAWN_BULK_FLAG_LAST : 0u,
            .chunk_size = (uint32_t)chunk,
            .figure_id = c->current_canvas->figure_id,
        };
        memcpy(frame, &hdr, sizeof(hdr));
        if (chunk) {
            memcpy(frame + sizeof(hdr), p, chunk);
        }
        struct yetty_ycore_void_result emit_r =
            emit_record(c, c->current_canvas->figure_id, /*compressed=*/1,
                        YETTY_YRDAWN_FIGURE_SUB_BULK, frame, frame_bytes);
        free(frame);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_r, "yrdawn_client_send_bulk: emit chunk");

        p += chunk;
        remaining -= chunk;
        seq++;
    } while (remaining > 0);

    return YETTY_OK_VOID();
}

/*===========================================================================
 * Present helper — bulk ref + present_frame CMD
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yrdawn_canvas_present_frame(struct yetty_yrdawn_canvas *canvas,
                                                                 uint32_t width, uint32_t height,
                                                                 const void *pixels, size_t bytes)
{
    if (!canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas_present_frame: NULL canvas");
    }
    if (width == 0 || height == 0) {
        return YETTY_ERR(yetty_ycore_void, "canvas_present_frame: zero dim");
    }
    if (bytes != (size_t)width * (size_t)height * 4u) {
        return YETTY_ERR(yetty_ycore_void, "canvas_present_frame: bytes != w*h*4");
    }
    struct yetty_yrdawn_client *c = canvas->client;

    /* Honor the addressed canvas regardless of which is "current". */
    struct yetty_yrdawn_canvas *prev = c->current_canvas;
    c->current_canvas = canvas;

    uint32_t ref = c->next_bulk_ref++;
    if (ref == 0) {
        ref = c->next_bulk_ref++;
    }
    struct yetty_ycore_void_result br = yetty_yrdawn_client_send_bulk(c, ref, pixels, bytes);
    if (YETTY_IS_ERR(br)) {
        c->current_canvas = prev;
        return YETTY_ERR(yetty_ycore_void, "canvas_present_frame: send_bulk", br);
    }

    /* Inline the wire layout that the codegen mirrors so we don't pull
     * methods.gen.h into client.c. */
    struct present_body {
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t payload_ref;
    } body = {.width = width, .height = height, .format = 0u, .payload_ref = ref};
    struct yetty_ycore_void_result cr =
        yetty_yrdawn_client_send_cmd_sync(c, /* method id */ 100u, &body, sizeof(body));
    c->current_canvas = prev;
    return cr;
}

/*===========================================================================
 * Pump
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yrdawn_client_pump(struct yetty_yrdawn_client *c)
{
    for (;;) {
        struct yetty_yplatform_io_size_result r =
            yetty_yplatform_io_read_nonblocking(c->in_fd, c->rx_buf, c->rx_buf_cap);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yrdawn_client: io_read_nonblocking");
        if (r.value == 0) {
            break;
        }
        struct yetty_ycore_void_result feed =
            yetty_yface_feed_bytes(c->in_face, (const char *)c->rx_buf, r.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, feed, "yrdawn_client: yface_feed_bytes");
    }
    return YETTY_OK_VOID();
}
