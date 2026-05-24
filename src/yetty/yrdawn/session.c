/*
 * session.c — per-client shared state for yrdawn.
 *
 * One session per client process. Owns the handle table (every
 * wgpuCreate* result lands here), the BULK reassembly slots (texture/
 * buffer uploads land here), and the borrowed shared Dawn handles
 * pulled from yframework's GPU context.
 *
 * Sessions are created lazily by the yrdawn factory when it sees the
 * first SUB_HELLO bearing a previously-unseen session_id. Figures
 * minted for that session attach via session_attach_figure; the last
 * detach unbinds the session and the factory args' session table
 * removes the entry.
 */
#include <yetty/yrdawn/session.h>

#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yrdawn/wire.h>
#include <yetty/ytrace/ytrace.h>

struct handle_entry {
    uint64_t handle;
    void *ptr;
};

struct bulk_entry {
    uint32_t ref;      /* 0 = free slot */
    uint32_t next_seq; /* expected next chunk; UINT32_MAX = ready */
    struct yetty_ycore_buffer buf;
};

struct yetty_yrdawn_session {
    uint32_t session_id;

    /* Borrowed Dawn handles — yframework owns lifetime; the session
     * outlives no longer than every figure bound to it, which outlives
     * no longer than the host (yframework). */
    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    /* Per-session shared handle table. Linear scan — matches the
     * original yrdawn-layer's table; sized for a handful to a few
     * hundred handles per active session. */
    struct handle_entry *handles;
    size_t handle_count;
    size_t handle_cap;

    /* BULK reassembly slots. */
    struct bulk_entry *bulks;
    size_t bulk_count;
    size_t bulk_cap;

    /* Attached figures — used as a refcount and for shutdown ordering. */
    struct yetty_yrdawn_figure **figures;
    size_t figure_count;
    size_t figure_cap;
};

struct yetty_yrdawn_session_ptr_result yetty_yrdawn_session_create(
    uint32_t session_id, const struct yetty_context *context)
{
    if (!context) {
        return YETTY_ERR(yetty_yrdawn_session_ptr, "yrdawn_session_create: NULL context");
    }
    if (session_id == 0) {
        return YETTY_ERR(yetty_yrdawn_session_ptr, "yrdawn_session_create: session_id == 0");
    }

    struct yetty_yrdawn_session *s = calloc(1, sizeof(*s));
    if (!s) {
        return YETTY_ERR(yetty_yrdawn_session_ptr, "yrdawn_session_create: oom");
    }
    s->session_id = session_id;
    s->instance = context->runtime->gpu.app_gpu_context.instance;
    s->adapter = context->runtime->gpu.adapter;
    s->device = context->runtime->gpu.device;
    s->queue = context->runtime->gpu.queue;
    ydebug("yrdawn_session: created id=%u", session_id);
    return YETTY_OK(yetty_yrdawn_session_ptr, s);
}

struct yetty_ycore_void_result yetty_yrdawn_session_destroy(struct yetty_yrdawn_session *s)
{
    if (!s) {
        return YETTY_OK_VOID();
    }
    ydebug("yrdawn_session: destroying id=%u (handles=%zu, bulks=%zu, figures=%zu)", s->session_id,
           s->handle_count, s->bulk_count, s->figure_count);
    for (size_t i = 0; i < s->bulk_count; ++i) {
        yetty_ycore_buffer_destroy(&s->bulks[i].buf);
    }
    free(s->bulks);
    free(s->handles);
    free(s->figures);
    free(s);
    return YETTY_OK_VOID();
}

uint32_t yetty_yrdawn_session_id(const struct yetty_yrdawn_session *s)
{
    return s ? s->session_id : 0;
}

void yetty_yrdawn_session_attach_figure(struct yetty_yrdawn_session *s,
                                        struct yetty_yrdawn_figure *f)
{
    if (!s || !f) {
        return;
    }
    if (s->figure_count == s->figure_cap) {
        size_t cap = s->figure_cap ? s->figure_cap * 2u : 4u;
        struct yetty_yrdawn_figure **grown = realloc(s->figures, cap * sizeof(*grown));
        if (!grown) {
            ywarn("yrdawn_session_attach_figure: oom (id=%u)", s->session_id);
            return;
        }
        s->figures = grown;
        s->figure_cap = cap;
    }
    s->figures[s->figure_count++] = f;
}

void yetty_yrdawn_session_detach_figure(struct yetty_yrdawn_session *s,
                                        struct yetty_yrdawn_figure *f)
{
    if (!s || !f) {
        return;
    }
    for (size_t i = 0; i < s->figure_count; ++i) {
        if (s->figures[i] == f) {
            s->figures[i] = s->figures[--s->figure_count];
            return;
        }
    }
}

size_t yetty_yrdawn_session_figure_count(const struct yetty_yrdawn_session *s)
{
    return s ? s->figure_count : 0;
}

static struct handle_entry *handle_find(struct yetty_yrdawn_session *s, uint64_t handle)
{
    for (size_t i = 0; i < s->handle_count; ++i) {
        if (s->handles[i].handle == handle) {
            return &s->handles[i];
        }
    }
    return NULL;
}

void *yetty_yrdawn_session_handle_get(struct yetty_yrdawn_session *s, uint64_t handle)
{
    if (!s || handle == YETTY_YRDAWN_HANDLE_NULL) {
        return NULL;
    }
    struct handle_entry *e = handle_find(s, handle);
    return e ? e->ptr : NULL;
}

struct yetty_ycore_void_result yetty_yrdawn_session_handle_set(struct yetty_yrdawn_session *s,
                                                               uint64_t handle, void *ptr)
{
    if (!s || handle == YETTY_YRDAWN_HANDLE_NULL) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_session_handle_set: bad ctx/handle");
    }
    struct handle_entry *e = handle_find(s, handle);
    if (e) {
        e->ptr = ptr;
        return YETTY_OK_VOID();
    }
    if (s->handle_count == s->handle_cap) {
        size_t cap = s->handle_cap ? s->handle_cap * 2u : 16u;
        struct handle_entry *grown = realloc(s->handles, cap * sizeof(*grown));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "yrdawn_session_handle_set: oom");
        }
        s->handles = grown;
        s->handle_cap = cap;
    }
    s->handles[s->handle_count++] = (struct handle_entry){.handle = handle, .ptr = ptr};
    return YETTY_OK_VOID();
}

void yetty_yrdawn_session_handle_release(struct yetty_yrdawn_session *s, uint64_t handle)
{
    if (!s) {
        return;
    }
    for (size_t i = 0; i < s->handle_count; ++i) {
        if (s->handles[i].handle == handle) {
            s->handles[i] = s->handles[--s->handle_count];
            return;
        }
    }
}

WGPUInstance yetty_yrdawn_session_shared_instance(struct yetty_yrdawn_session *s)
{
    return s ? s->instance : NULL;
}
WGPUAdapter yetty_yrdawn_session_shared_adapter(struct yetty_yrdawn_session *s)
{
    return s ? s->adapter : NULL;
}
WGPUDevice yetty_yrdawn_session_shared_device(struct yetty_yrdawn_session *s)
{
    return s ? s->device : NULL;
}
WGPUQueue yetty_yrdawn_session_shared_queue(struct yetty_yrdawn_session *s)
{
    return s ? s->queue : NULL;
}

static struct bulk_entry *bulk_find_or_alloc(struct yetty_yrdawn_session *s, uint32_t ref)
{
    for (size_t i = 0; i < s->bulk_count; ++i) {
        if (s->bulks[i].ref == ref) {
            return &s->bulks[i];
        }
    }
    for (size_t i = 0; i < s->bulk_count; ++i) {
        if (s->bulks[i].ref == 0) {
            s->bulks[i].ref = ref;
            s->bulks[i].next_seq = 0;
            yetty_ycore_buffer_clear(&s->bulks[i].buf);
            return &s->bulks[i];
        }
    }
    if (s->bulk_count == s->bulk_cap) {
        size_t cap = s->bulk_cap ? s->bulk_cap * 2u : 4u;
        struct bulk_entry *grown = realloc(s->bulks, cap * sizeof(*grown));
        if (!grown) {
            return NULL;
        }
        memset(grown + s->bulk_count, 0, (cap - s->bulk_count) * sizeof(*grown));
        s->bulks = grown;
        s->bulk_cap = cap;
    }
    struct bulk_entry *e = &s->bulks[s->bulk_count++];
    e->ref = ref;
    e->next_seq = 0;
    e->buf = (struct yetty_ycore_buffer){0};
    return e;
}

static void bulk_release(struct yetty_yrdawn_session *s, uint32_t ref)
{
    for (size_t i = 0; i < s->bulk_count; ++i) {
        if (s->bulks[i].ref == ref) {
            yetty_ycore_buffer_destroy(&s->bulks[i].buf);
            s->bulks[i] = (struct bulk_entry){0};
            return;
        }
    }
}

struct yetty_ycore_void_result yetty_yrdawn_session_bulk_append(struct yetty_yrdawn_session *s,
                                                                uint32_t ref, uint32_t seq,
                                                                const uint8_t *chunk,
                                                                size_t chunk_size, int last)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_session_bulk_append: NULL session");
    }
    if (ref == 0) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_session_bulk_append: ref=0");
    }
    struct bulk_entry *e = bulk_find_or_alloc(s, ref);
    if (!e) {
        return YETTY_ERR(yetty_ycore_void, "yrdawn_session_bulk_append: table oom");
    }
    if (seq != e->next_seq) {
        bulk_release(s, ref);
        return YETTY_ERR(yetty_ycore_void, "yrdawn_session_bulk_append: out-of-order seq");
    }
    if (chunk_size > 0) {
        struct yetty_ycore_void_result w = yetty_ycore_buffer_write(&e->buf, chunk, chunk_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, w, "yrdawn_session_bulk_append: accum");
    }
    e->next_seq++;
    if (last) {
        e->next_seq = UINT32_MAX; /* mark ready for bulk_take */
    }
    return YETTY_OK_VOID();
}

int yetty_yrdawn_session_bulk_take(struct yetty_yrdawn_session *s, uint32_t ref,
                                   struct yetty_ycore_buffer *out)
{
    if (!s || !out) {
        return 0;
    }
    for (size_t i = 0; i < s->bulk_count; ++i) {
        if (s->bulks[i].ref == ref && s->bulks[i].next_seq == UINT32_MAX) {
            *out = s->bulks[i].buf;
            s->bulks[i] = (struct bulk_entry){0};
            return 1;
        }
    }
    return 0;
}
