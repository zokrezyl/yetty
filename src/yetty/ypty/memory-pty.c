/* memory-pty.c — in-memory PTY pair with two fixed-cap ring buffers.
 *
 * Two endpoints share one owner holding two rings; endpoint a's read
 * drains b_to_a (which endpoint b's write fills) and endpoint b's read
 * drains a_to_b. The owner is refcounted (= 2 at create); each endpoint's
 * destroy decrements; the last one frees the rings and the owner.
 *
 * Used to bridge in-process components through the platform_pty abstraction
 * (yui ↔ render thread today, same thread for now). No fd, no compression,
 * no locking — single-thread use only until a thread-safe ring variant lands.
 */
#include <yetty/yplatform/pty.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

#define YETTY_MEMORY_PTY_DEFAULT_BUF_SIZE (16u * 1024u * 1024u)

struct memory_ring {
    uint8_t *data;
    size_t   cap;       /* power of 2 */
    size_t   mask;      /* cap - 1 */
    size_t   read_pos;  /* monotonic */
    size_t   write_pos; /* monotonic */
};

struct memory_pty_pair {
    struct memory_ring a_to_b;
    struct memory_ring b_to_a;
    int                refs;
};

struct memory_pty_endpoint {
    struct yetty_platform_pty base;
    struct memory_pty_pair   *pair;
    /* Side A reads from b_to_a and writes to a_to_b.
     * Side B reads from a_to_b and writes to b_to_a. */
    struct memory_ring *read_ring;
    struct memory_ring *write_ring;
    /* Peer endpoint — its wake_fn is fired by THIS endpoint's write so the
     * peer's reader-side event loop drains. */
    struct memory_pty_endpoint        *peer;
    yetty_yplatform_memory_pty_wake_fn wake_fn;
    void                              *wake_userdata;
};

/*===========================================================================
 * Ring helpers (same shape as wire-statemachine's ring: monotonic positions,
 * mask on access, power-of-2 cap).
 *===========================================================================*/

static int ring_init(struct memory_ring *r, size_t cap_request)
{
    size_t cap = 1;
    while (cap < cap_request) {
        cap <<= 1;
    }
    r->data = malloc(cap);
    if (!r->data) {
        return 0;
    }
    r->cap = cap;
    r->mask = cap - 1;
    r->read_pos = 0;
    r->write_pos = 0;
    return 1;
}

static void ring_destroy(struct memory_ring *r)
{
    free(r->data);
    r->data = NULL;
}

static size_t ring_avail(const struct memory_ring *r)
{
    return r->write_pos - r->read_pos;
}

static size_t ring_free_space(const struct memory_ring *r)
{
    return r->cap - ring_avail(r);
}

static size_t ring_read(struct memory_ring *r, void *dst, size_t n)
{
    size_t avail = ring_avail(r);
    if (n > avail) {
        n = avail;
    }
    if (n == 0) {
        return 0;
    }
    size_t off = r->read_pos & r->mask;
    size_t first = r->cap - off;
    if (first >= n) {
        memcpy(dst, r->data + off, n);
    } else {
        memcpy(dst, r->data + off, first);
        memcpy((uint8_t *)dst + first, r->data, n - first);
    }
    r->read_pos += n;
    return n;
}

static size_t ring_write(struct memory_ring *r, const void *src, size_t n)
{
    size_t free_n = ring_free_space(r);
    if (n > free_n) {
        n = free_n;
    }
    if (n == 0) {
        return 0;
    }
    size_t off = r->write_pos & r->mask;
    size_t first = r->cap - off;
    if (first >= n) {
        memcpy(r->data + off, src, n);
    } else {
        memcpy(r->data + off, src, first);
        memcpy(r->data, (const uint8_t *)src + first, n - first);
    }
    r->write_pos += n;
    return n;
}

/*===========================================================================
 * pty_ops
 *===========================================================================*/

static struct yetty_ycore_void_result memory_pty_destroy(struct yetty_platform_pty *self)
{
    struct memory_pty_endpoint *ep = container_of(self, struct memory_pty_endpoint, base);
    if (ep->peer) {
        ep->peer->peer = NULL; /* peer no longer wakes us */
    }
    if (!ep->pair) {
        free(ep);
        return YETTY_OK_VOID();
    }
    if (--ep->pair->refs == 0) {
        ring_destroy(&ep->pair->a_to_b);
        ring_destroy(&ep->pair->b_to_a);
        free(ep->pair);
    }
    free(ep);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result memory_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len)
{
    struct memory_pty_endpoint *ep = container_of(self, struct memory_pty_endpoint, base);
    if (!buf || max_len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    return YETTY_OK(yetty_ycore_size, ring_read(ep->read_ring, buf, max_len));
}

static struct yetty_ycore_size_result memory_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct memory_pty_endpoint *ep = container_of(self, struct memory_pty_endpoint, base);
    if (!data || len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    size_t w = ring_write(ep->write_ring, data, len);
    if (w < len) {
        /* Local protocol carries whole frames; a short write would split
         * an envelope mid-stream. Surface as an error so the caller can
         * either grow the buffer at create time or drain before retrying. */
        return YETTY_ERR(yetty_ycore_size, "memory_pty_write: ring full (short write)");
    }
    /* Wake the peer's reader-side event loop. Memory-pty has no fd so
     * libuv has nothing to wait on — the wake is the only signal that
     * bytes have arrived. */
    if (ep->peer && ep->peer->wake_fn) {
        ep->peer->wake_fn(ep->peer->wake_userdata);
    }
    return YETTY_OK(yetty_ycore_size, w);
}

static struct yetty_ycore_void_result memory_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows, uint32_t pixel_w, uint32_t pixel_h)
{
    (void)self;
    (void)cols;
    (void)rows;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result memory_pty_stop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *memory_pty_pipe_source(struct yetty_platform_pty *self)
{
    (void)self;
    /* No fd. libuv async hookup is not applicable to a memory pty; the
     * caller drives osc_statemachine_process directly from its frame loop. */
    return NULL;
}

static const struct yetty_platform_pty_ops memory_pty_ops = {
    .destroy = memory_pty_destroy,
    .read = memory_pty_read,
    .write = memory_pty_write,
    .resize = memory_pty_resize,
    .stop = memory_pty_stop,
    .pipe_source = memory_pty_pipe_source,
};

/*===========================================================================
 * Public create
 *===========================================================================*/

struct yetty_yplatform_memory_pty_pair_result yetty_yplatform_memory_pty_pair_create(size_t buf_size)
{
    if (buf_size == 0) {
        buf_size = YETTY_MEMORY_PTY_DEFAULT_BUF_SIZE;
    }

    struct memory_pty_pair *pair = calloc(1, sizeof(*pair));
    if (!pair) {
        return YETTY_ERR(yetty_yplatform_memory_pty_pair, "memory_pty: pair alloc failed");
    }
    if (!ring_init(&pair->a_to_b, buf_size)) {
        free(pair);
        return YETTY_ERR(yetty_yplatform_memory_pty_pair, "memory_pty: a_to_b ring init failed");
    }
    if (!ring_init(&pair->b_to_a, buf_size)) {
        ring_destroy(&pair->a_to_b);
        free(pair);
        return YETTY_ERR(yetty_yplatform_memory_pty_pair, "memory_pty: b_to_a ring init failed");
    }
    pair->refs = 2;

    struct memory_pty_endpoint *ep_a = calloc(1, sizeof(*ep_a));
    struct memory_pty_endpoint *ep_b = calloc(1, sizeof(*ep_b));
    if (!ep_a || !ep_b) {
        free(ep_a);
        free(ep_b);
        ring_destroy(&pair->a_to_b);
        ring_destroy(&pair->b_to_a);
        free(pair);
        return YETTY_ERR(yetty_yplatform_memory_pty_pair, "memory_pty: endpoint alloc failed");
    }

    ep_a->base.ops = &memory_pty_ops;
    ep_a->pair = pair;
    ep_a->read_ring = &pair->b_to_a;
    ep_a->write_ring = &pair->a_to_b;
    ep_a->peer = ep_b;

    ep_b->base.ops = &memory_pty_ops;
    ep_b->pair = pair;
    ep_b->read_ring = &pair->a_to_b;
    ep_b->write_ring = &pair->b_to_a;
    ep_b->peer = ep_a;

    struct yetty_yplatform_memory_pty_pair out = {.a = &ep_a->base, .b = &ep_b->base};
    return YETTY_OK(yetty_yplatform_memory_pty_pair, out);
}

void yetty_yplatform_memory_pty_set_wake(struct yetty_platform_pty *endpoint,
                                         yetty_yplatform_memory_pty_wake_fn wake,
                                         void *userdata)
{
    if (!endpoint || endpoint->ops != &memory_pty_ops) {
        return;
    }
    struct memory_pty_endpoint *ep = container_of(endpoint, struct memory_pty_endpoint, base);
    ep->wake_fn = wake;
    ep->wake_userdata = userdata;
}
