#include <yetty/ywasm/client.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <yetty/ycore/result.h>
#include <yetty/yface/yface.h>
#include <yetty/ywasm/wire.h>

struct pending_req {
    uint32_t req_id; /* 0 = free slot */
    uint32_t method_id;
    yetty_ywasm_reply_cb cb;
    void *user;
};

struct yetty_ywasm_client {
    int in_fd;
    int out_fd;

    struct yetty_yface *in_face;

    int connected;  /* HELLO_ACK with status OK seen */
    int hello_sent; /* HELLO emitted */

    uint64_t next_handle;
    uint32_t next_req_id;
    uint32_t next_bulk_ref;

    struct pending_req *pending;
    size_t pending_cap;

    yetty_ywasm_event_cb event_cb;
    void *event_user;

    yetty_ywasm_input_key_cb input_key_cb;
    void *input_key_user;

    yetty_ywasm_input_resize_cb input_resize_cb;
    void *input_resize_user;

    uint8_t *rx_buf;
    size_t rx_buf_cap;
};

static struct pending_req *pending_find_free(struct yetty_ywasm_client *c)
{
    for (size_t i = 0; i < c->pending_cap; ++i) {
        if (c->pending[i].req_id == 0)
            return &c->pending[i];
    }
    size_t new_cap = c->pending_cap == 0 ? 16 : c->pending_cap * 2;
    struct pending_req *grown =
        (struct pending_req *)realloc(c->pending, new_cap * sizeof(struct pending_req));
    if (!grown)
        return NULL;
    memset(grown + c->pending_cap, 0, (new_cap - c->pending_cap) * sizeof(struct pending_req));
    c->pending = grown;
    struct pending_req *slot = &c->pending[c->pending_cap];
    c->pending_cap = new_cap;
    return slot;
}

static struct pending_req *pending_take(struct yetty_ywasm_client *c, uint32_t req_id)
{
    for (size_t i = 0; i < c->pending_cap; ++i) {
        if (c->pending[i].req_id == req_id)
            return &c->pending[i];
    }
    return NULL;
}

static void on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                   const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yetty_ywasm_client *c = (struct yetty_ywasm_client *)user;

    switch (osc_code) {
    case YETTY_YWASM_OSC_SC_HELLO_ACK: {
        if (payload_len < sizeof(struct yetty_ywasm_wire_hello_ack))
            return;
        const struct yetty_ywasm_wire_hello_ack *ack =
            (const struct yetty_ywasm_wire_hello_ack *)payload;
        if (ack->magic != YETTY_YWASM_MAGIC_HELLO_ACK)
            return;
        if (ack->status == YETTY_YWASM_HELLO_OK)
            c->connected = 1;
        return;
    }
    case YETTY_YWASM_OSC_SC_REPLY: {
        if (payload_len < sizeof(struct yetty_ywasm_wire_reply_hdr))
            return;
        const struct yetty_ywasm_wire_reply_hdr *hdr =
            (const struct yetty_ywasm_wire_reply_hdr *)payload;
        if (hdr->magic != YETTY_YWASM_MAGIC_REPLY)
            return;
        struct pending_req *slot = pending_take(c, hdr->req_id);
        if (!slot)
            return;
        yetty_ywasm_reply_cb cb = slot->cb;
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
    case YETTY_YWASM_OSC_SC_EVENT: {
        if (payload_len < sizeof(struct yetty_ywasm_wire_event_hdr))
            return;
        const struct yetty_ywasm_wire_event_hdr *hdr =
            (const struct yetty_ywasm_wire_event_hdr *)payload;
        if (hdr->magic != YETTY_YWASM_MAGIC_EVENT)
            return;
        if (c->event_cb) {
            const uint8_t *body = payload + sizeof(*hdr);
            size_t body_len = payload_len - sizeof(*hdr);
            c->event_cb(c->event_user, hdr->kind, hdr->device_handle, body, body_len);
        }
        return;
    }
    case YETTY_YWASM_OSC_SC_KEY: {
        if (payload_len < sizeof(struct yetty_ywasm_wire_input_key))
            return;
        const struct yetty_ywasm_wire_input_key *k =
            (const struct yetty_ywasm_wire_input_key *)payload;
        if (k->magic != YETTY_YWASM_MAGIC_INPUT_KEY)
            return;
        if (c->input_key_cb)
            c->input_key_cb(c->input_key_user, k->kind, k->key, k->mods, k->codepoint);
        return;
    }
    case YETTY_YWASM_OSC_SC_RESIZE: {
        if (payload_len < sizeof(struct yetty_ywasm_wire_input_resize))
            return;
        const struct yetty_ywasm_wire_input_resize *r =
            (const struct yetty_ywasm_wire_input_resize *)payload;
        if (r->magic != YETTY_YWASM_MAGIC_INPUT_RESIZE)
            return;
        if (c->input_resize_cb)
            c->input_resize_cb(c->input_resize_user, r->width, r->height);
        return;
    }
    case YETTY_YWASM_OSC_SC_BULK:
    case YETTY_YWASM_OSC_SC_ERROR:
        /* BULK reassembly and protocol-error handling land in the next
         * cut. Silent drop for now. */
        return;
    default:
        return;
    }
}

struct yetty_ywasm_client_ptr_result yetty_ywasm_client_create(int in_fd, int out_fd)
{
    struct yetty_ywasm_client *c = (struct yetty_ywasm_client *)calloc(1, sizeof(*c));
    if (!c)
        return YETTY_ERR(yetty_ywasm_client_ptr, "ywasm_client_create: oom");

    c->in_fd = in_fd;
    c->out_fd = out_fd;
    c->next_handle = 1;
    c->next_req_id = 1;
    c->next_bulk_ref = 1;

    /* Deliberately NOT setting O_NONBLOCK on in_fd: under a PTY the
     * child's stdin and stdout share one open file description, so
     * fcntl(STDIN, O_NONBLOCK) would force writes on stdout to return
     * EAGAIN and break yface_emit_to_fd. The pump uses poll() with a
     * zero timeout to peek for readability, then a blocking read of
     * just the available bytes. */

    c->rx_buf_cap = 65536;
    c->rx_buf = (uint8_t *)malloc(c->rx_buf_cap);
    if (!c->rx_buf) {
        free(c);
        return YETTY_ERR(yetty_ywasm_client_ptr, "ywasm_client_create: rx_buf oom");
    }

    struct yetty_yface_ptr_result fr = yetty_yface_create();
    if (YETTY_IS_ERR(fr)) {
        free(c->rx_buf);
        free(c);
        return YETTY_ERR(yetty_ywasm_client_ptr, "ywasm_client_create: yface_create", fr);
    }
    c->in_face = fr.value;
    yetty_yface_set_handlers(c->in_face, on_osc, NULL, c);

    return YETTY_OK(yetty_ywasm_client_ptr, c);
}

struct yetty_ycore_void_result yetty_ywasm_client_destroy(struct yetty_ywasm_client *c)
{
    if (!c)
        return YETTY_OK_VOID();
    if (c->in_face)
        yetty_yface_destroy(c->in_face);
    free(c->pending);
    free(c->rx_buf);
    free(c);
    return YETTY_OK_VOID();
}

void yetty_ywasm_client_set_event_cb(struct yetty_ywasm_client *c,
                                     yetty_ywasm_event_cb cb, void *user)
{
    c->event_cb = cb;
    c->event_user = user;
}

void yetty_ywasm_client_set_input_key_cb(struct yetty_ywasm_client *c,
                                         yetty_ywasm_input_key_cb cb, void *user)
{
    c->input_key_cb = cb;
    c->input_key_user = user;
}

void yetty_ywasm_client_set_input_resize_cb(struct yetty_ywasm_client *c,
                                            yetty_ywasm_input_resize_cb cb, void *user)
{
    c->input_resize_cb = cb;
    c->input_resize_user = user;
}

struct yetty_ycore_void_result yetty_ywasm_client_send_hello(struct yetty_ywasm_client *c)
{
    if (c->hello_sent)
        return YETTY_OK_VOID();

    struct yetty_ywasm_wire_hello hello = {
        .magic = YETTY_YWASM_MAGIC_HELLO,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(hello),
    };

    struct yetty_ycore_void_result emit = yetty_yface_emit_to_fd(
        c->out_fd, YETTY_YWASM_OSC_CS_HELLO, 0, NULL, 0, &hello, sizeof(hello));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit, "ywasm_client: emit HELLO");

    c->hello_sent = 1;
    return YETTY_OK_VOID();
}

int yetty_ywasm_client_connected(const struct yetty_ywasm_client *c)
{
    return c->connected;
}

uint64_t yetty_ywasm_client_alloc_handle(struct yetty_ywasm_client *c)
{
    return c->next_handle++;
}

static struct yetty_ycore_void_result emit_cmd(struct yetty_ywasm_client *c, uint32_t method_id,
                                               uint32_t req_id, const void *body, size_t body_len)
{
    size_t total = sizeof(struct yetty_ywasm_wire_cmd_hdr) + body_len;
    uint8_t *frame = (uint8_t *)malloc(total);
    if (!frame)
        return YETTY_ERR(yetty_ycore_void, "ywasm_client: cmd frame oom");

    struct yetty_ywasm_wire_cmd_hdr hdr = {
        .magic = YETTY_YWASM_MAGIC_CMD,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)total,
        .method_id = method_id,
        .req_id = req_id,
        .flags = 0,
    };
    memcpy(frame, &hdr, sizeof(hdr));
    if (body_len)
        memcpy(frame + sizeof(hdr), body, body_len);

    struct yetty_ycore_void_result emit = yetty_yface_emit_to_fd(
        c->out_fd, YETTY_YWASM_OSC_CS_CMD, 1, NULL, 0, frame, total);
    free(frame);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit, "ywasm_client: emit CMD");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywasm_client_send_cmd_sync(
    struct yetty_ywasm_client *c, uint32_t method_id,
    const void *body, size_t body_len)
{
    return emit_cmd(c, method_id, 0, body, body_len);
}

struct yetty_ycore_void_result yetty_ywasm_client_send_cmd_async(
    struct yetty_ywasm_client *c, uint32_t method_id,
    const void *body, size_t body_len,
    yetty_ywasm_reply_cb cb, void *user)
{
    struct pending_req *slot = pending_find_free(c);
    if (!slot)
        return YETTY_ERR(yetty_ycore_void, "ywasm_client: pending table oom");

    uint32_t req_id = c->next_req_id++;
    if (req_id == 0)
        req_id = c->next_req_id++;

    slot->req_id = req_id;
    slot->method_id = method_id;
    slot->cb = cb;
    slot->user = user;

    struct yetty_ycore_void_result emit_result = emit_cmd(c, method_id, req_id, body, body_len);
    if (YETTY_IS_ERR(emit_result)) {
        slot->req_id = 0;
        slot->cb = NULL;
        slot->user = NULL;
        return YETTY_ERR(yetty_ycore_void, "ywasm_client: emit_cmd async", emit_result);
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

static void blocking_cb(void *user, uint32_t status, uint32_t method_id,
                        const uint8_t *body, size_t body_len)
{
    (void)method_id;
    struct blocking_state *s = (struct blocking_state *)user;
    s->status = status;
    if (s->out && s->out_size > 0) {
        size_t n = body_len < s->out_size ? body_len : s->out_size;
        if (n)
            memcpy(s->out, body, n);
        s->got_len = n;
    }
    s->done = 1;
}

struct blocking_dyn_state {
    int done;
    uint32_t status;
    uint8_t *buf;   /* malloc'd, caller-owned */
    size_t len;
};

static void blocking_dyn_cb(void *user, uint32_t status, uint32_t method_id,
                            const uint8_t *body, size_t body_len)
{
    (void)method_id;
    struct blocking_dyn_state *s = (struct blocking_dyn_state *)user;
    s->status = status;
    if (body_len > 0) {
        s->buf = (uint8_t *)malloc(body_len);
        if (s->buf) {
            memcpy(s->buf, body, body_len);
            s->len = body_len;
        }
    }
    s->done = 1;
}

struct yetty_ycore_void_result yetty_ywasm_client_send_cmd_blocking_dyn(
    struct yetty_ywasm_client *c, uint32_t method_id,
    const void *body, size_t body_len,
    uint8_t **out_buf, size_t *out_len, uint32_t *out_status)
{
    if (out_buf) *out_buf = NULL;
    if (out_len) *out_len = 0;
    struct blocking_dyn_state st = {0};
    struct yetty_ycore_void_result sr = yetty_ywasm_client_send_cmd_async(
        c, method_id, body, body_len, blocking_dyn_cb, &st);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "send_cmd_blocking_dyn: send_cmd_async");
    int spins = 0;
    while (!st.done) {
        struct yetty_ycore_void_result pr = yetty_ywasm_client_pump(c);
        if (YETTY_IS_ERR(pr)) {
            free(st.buf);
            return YETTY_ERR(yetty_ycore_void, "send_cmd_blocking_dyn: pump", pr);
        }
        if (st.done)
            break;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000L};
        (void)nanosleep(&ts, NULL);
        if (++spins > 5000) {
            free(st.buf);
            return YETTY_ERR(yetty_ycore_void, "send_cmd_blocking_dyn: timeout");
        }
    }
    if (out_buf)    *out_buf    = st.buf;
    if (out_len)    *out_len    = st.len;
    if (out_status) *out_status = st.status;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywasm_client_send_cmd_blocking(
    struct yetty_ywasm_client *c, uint32_t method_id,
    const void *body, size_t body_len,
    void *out, size_t out_size, uint32_t *out_status)
{
    struct blocking_state st = {0};
    st.out = out;
    st.out_size = out_size;

    struct yetty_ycore_void_result sr = yetty_ywasm_client_send_cmd_async(
        c, method_id, body, body_len, blocking_cb, &st);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "send_cmd_blocking: send_cmd_async");

    /* Spin the pump until the reply lands. Hard cap at ~5 s of inactivity
     * so a wedged server doesn't hang the caller forever. */
    int spins = 0;
    while (!st.done) {
        struct yetty_ycore_void_result pr = yetty_ywasm_client_pump(c);
        if (YETTY_IS_ERR(pr))
            return YETTY_ERR(yetty_ycore_void, "send_cmd_blocking: pump", pr);
        if (st.done)
            break;
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000L};
        (void)nanosleep(&ts, NULL);
        if (++spins > 5000)
            return YETTY_ERR(yetty_ycore_void, "send_cmd_blocking: timeout");
    }
    if (out_status)
        *out_status = st.status;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywasm_client_send_bye(struct yetty_ywasm_client *c)
{
    struct yetty_ywasm_wire_bye bye = {
        .magic = YETTY_YWASM_MAGIC_BYE,
        .version = YETTY_YWASM_WIRE_VERSION,
        .total_size = (uint32_t)sizeof(bye),
    };
    struct yetty_ycore_void_result emit = yetty_yface_emit_to_fd(
        c->out_fd, YETTY_YWASM_OSC_CS_BYE, 0, NULL, 0, &bye, sizeof(bye));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit, "ywasm_client: emit BYE");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywasm_client_send_bulk(
    struct yetty_ywasm_client *c, uint32_t ref,
    const void *bytes, size_t len)
{
    if (ref == 0)
        return YETTY_ERR(yetty_ycore_void, "ywasm_client_send_bulk: ref=0");

    /* Chunk size aligned with the OSC envelope's 64KB lz4F block. */
    const size_t chunk_max = 64u * 1024u;
    const uint8_t *p = (const uint8_t *)bytes;
    size_t remaining = len;
    uint32_t seq = 0;

    do {
        size_t chunk = remaining > chunk_max ? chunk_max : remaining;
        int is_last = (remaining - chunk == 0);

        size_t frame_bytes = sizeof(struct yetty_ywasm_wire_bulk_hdr) + chunk;
        uint8_t *frame = (uint8_t *)malloc(frame_bytes);
        if (!frame)
            return YETTY_ERR(yetty_ycore_void, "ywasm_client_send_bulk: oom");

        struct yetty_ywasm_wire_bulk_hdr hdr = {
            .magic = YETTY_YWASM_MAGIC_BULK,
            .version = YETTY_YWASM_WIRE_VERSION,
            .total_size = (uint32_t)frame_bytes,
            .ref = ref,
            .seq = seq,
            .flags = is_last ? YETTY_YWASM_BULK_FLAG_LAST : 0u,
            .chunk_size = (uint32_t)chunk,
        };
        memcpy(frame, &hdr, sizeof(hdr));
        if (chunk)
            memcpy(frame + sizeof(hdr), p, chunk);

        struct yetty_ycore_void_result emit_r = yetty_yface_emit_to_fd(
            c->out_fd, YETTY_YWASM_OSC_CS_BULK, 1, NULL, 0, frame, frame_bytes);
        free(frame);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_r, "ywasm_client_send_bulk: emit chunk");

        p += chunk;
        remaining -= chunk;
        seq++;
    } while (remaining > 0);

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywasm_client_present_frame(
    struct yetty_ywasm_client *c, uint32_t width, uint32_t height,
    const void *pixels, size_t bytes)
{
    if (width == 0 || height == 0)
        return YETTY_ERR(yetty_ycore_void, "present_frame: zero dim");
    if (bytes != (size_t)width * (size_t)height * 4u)
        return YETTY_ERR(yetty_ycore_void, "present_frame: byte count != w*h*4");

    uint32_t ref = c->next_bulk_ref++;
    if (ref == 0)
        ref = c->next_bulk_ref++;

    struct yetty_ycore_void_result br = yetty_ywasm_client_send_bulk(c, ref, pixels, bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "present_frame: send_bulk");

    /* Inline the wire layout that the codegen mirrors so we don't pull
     * methods.gen.h into client.c (the protocol contract here is part
     * of the hand-written client surface — the method_id is the only
     * generated identifier we depend on, and we pin it locally). */
    struct present_body {
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t payload_ref;
    } body = {
        .width = width, .height = height, .format = 0u, .payload_ref = ref,
    };
    return yetty_ywasm_client_send_cmd_sync(c, /* method id */ 100u, &body, sizeof(body));
}

struct yetty_ycore_void_result yetty_ywasm_client_pump(struct yetty_ywasm_client *c)
{
    for (;;) {
        struct pollfd pfd = {.fd = c->in_fd, .events = POLLIN};
        int pr = poll(&pfd, 1, 0);
        if (pr == 0)
            break;
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            return YETTY_ERR(yetty_ycore_void, "ywasm_client: poll failed");
        }
        if (!(pfd.revents & (POLLIN | POLLHUP)))
            break;
        ssize_t n = read(c->in_fd, c->rx_buf, c->rx_buf_cap);
        if (n > 0) {
            struct yetty_ycore_void_result feed =
                yetty_yface_feed_bytes(c->in_face, (const char *)c->rx_buf, (size_t)n);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, feed, "ywasm_client: yface_feed_bytes");
            continue;
        }
        if (n == 0)
            break;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        if (errno == EINTR)
            continue;
        return YETTY_ERR(yetty_ycore_void, "ywasm_client: read failed");
    }
    return YETTY_OK_VOID();
}
