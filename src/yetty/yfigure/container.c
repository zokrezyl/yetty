/*
 * yfigure_container — composite figure that holds child figures keyed
 * by parent-scoped id.
 *
 * Storage: each child is wrapped in a `struct child_entry` carrying its
 * id, the owned figure pointer, and a uthash handle. uthash doubles as
 * id → entry lookup AND insertion-ordered linked list (z-order: back-
 * to-front render, raise = move-to-end).
 *
 * ids are parent-scoped: distinct within one container, no global
 * registry required. `id == 0` is reserved (anonymous, no wire address);
 * add_child rejects it.
 *
 * process_input is the wire-routing layer: reads `{length, id}` record
 * headers from the SM and dispatches `length` bytes to the child found
 * by id (or to the container itself when id=0 = admin/self-target).
 */
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/wire.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ut/uthash.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

struct child_entry {
    /* uthash key. Parent-scoped, non-zero, unique within the group. */
    uint32_t id;
    /* Owned child figure. Destroyed when the entry is removed or when
     * the group itself is destroyed. */
    struct yetty_yfigure_figure *figure;
    /* Kind tag (YETTY_YFIGURE_KIND_*) captured at mint time. Used by
     * CREATE_CHILD on an existing id to decide between the fast path
     * (same kind → reset_content + process_bytes, reuse GPU state)
     * and the destroy + mint fallback (different kind). */
    uint32_t kind;
    UT_hash_handle hh;
};

struct [[clang::annotate("class@yfigure:container")]]
       [[clang::annotate("uses@yfigure:figure")]] yetty_yfigure_container {
    struct yetty_yfigure_figure base;
    /* uthash head — id → entry. Iterating via HASH_ITER / e->hh.next
     * walks children in INSERTION ORDER (= z-order: back-to-front). */
    struct child_entry *children;
    /* Borrowed — used for minting children from admin CREATE_CHILD
     * records. Sub-containers minted via that path inherit the same
     * pointers. */
    const struct yetty_context *context;
    struct yetty_yfigure_registry *registry;
    /* Producer emits child rects in pane-local; we add this offset on
     * CREATE_CHILD / SET_CHILD_RECT to land them in absolute target
     * pixel space. Set via yetty_yfigure_container_set_viewport_offset. */
    float viewport_offset_x;
    float viewport_offset_y;
};

/*===========================================================================
 * Internal helpers
 *=========================================================================*/

static void entry_destroy(struct child_entry *e, struct yetty_ycore_void_result *first_err,
                          int *have_err)
{
    struct yetty_ycore_void_result r = e->figure->ops->destroy(e->figure);
    if (YETTY_IS_ERR(r)) {
        if (!*have_err) {
            *first_err = r;
            *have_err = 1;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    free(e);
}

/*===========================================================================
 * Polymorphic dump — base wrapper + a tiny string-builder used by both
 * the container's own dump impl and the YAML fallback when a concrete
 * figure doesn't provide one.
 *=========================================================================*/

/* Grow-as-needed heap string. NULL `buf` starts a fresh allocation.
 * Returns the new buffer or NULL on OOM (in which case the old buffer
 * is freed). */
static char *dump_appendf(char *buf, size_t *len, size_t *cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* Snprintf needs a real buffer to measure; do it the safe way:
     * try once into the remaining slot, grow if it didn't fit. */
    if (!buf) {
        *len = 0;
        *cap = 64;
        buf = (char *)malloc(*cap);
        if (!buf) {
            va_end(ap);
            return NULL;
        }
        buf[0] = '\0';
    }
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (need < 0) {
        va_end(ap);
        return buf;
    }
    size_t want = *len + (size_t)need + 1u;
    if (want > *cap) {
        size_t ncap = *cap ? *cap : 64;
        while (ncap < want) {
            ncap *= 2;
        }
        char *grown = (char *)realloc(buf, ncap);
        if (!grown) {
            free(buf);
            va_end(ap);
            return NULL;
        }
        buf = grown;
        *cap = ncap;
    }
    int wrote = vsnprintf(buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    if (wrote < 0) {
        return buf;
    }
    *len += (size_t)wrote;
    return buf;
}

static void dump_indent_spaces(char *buf, size_t cap, int indent)
{
    int n = indent;
    if (n < 0) {
        n = 0;
    }
    if ((size_t)n + 1u > cap) {
        n = (int)cap - 1;
    }
    for (int i = 0; i < n; i++) {
        buf[i] = ' ';
    }
    buf[n] = '\0';
}

char *yetty_yfigure_dump(const struct yetty_yfigure_figure *self, int indent)
{
    if (!self) {
        char *out = (char *)malloc(8);
        if (out) {
            snprintf(out, 8, "null\n");
        }
        return out;
    }
    if (self->ops && self->ops->dump) {
        return self->ops->dump(self, indent);
    }
    /* Fallback: just rect + dirty. Concrete kinds that haven't migrated
     * to a real dump still produce something testable. */
    char pad[64];
    dump_indent_spaces(pad, sizeof(pad), indent);
    size_t len = 0, cap = 0;
    char *buf = NULL;
    buf = dump_appendf(buf, &len, &cap, "%skind: unknown\n", pad);
    if (!buf) {
        return NULL;
    }
    buf = dump_appendf(buf, &len, &cap, "%srect: [%.1f, %.1f, %.1f, %.1f]\n", pad, self->rect.min.x,
                       self->rect.min.y, self->rect.max.x, self->rect.max.y);
    if (!buf) {
        return NULL;
    }
    buf = dump_appendf(buf, &len, &cap, "%sdirty: %d\n", pad, self->dirty);
    return buf;
}

/*===========================================================================
 * Group ops
 *=========================================================================*/

static struct yetty_ycore_void_result group_destroy(struct yetty_yfigure_figure *self)
{
    struct yetty_yfigure_container *g = (struct yetty_yfigure_container *)self;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    struct child_entry *e, *tmp;
    HASH_ITER(hh, g->children, e, tmp)
    {
        HASH_DEL(g->children, e);
        entry_destroy(e, &first_err, &have_err);
    }
    free(g);
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "yfigure group_destroy: child destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result group_render(struct yetty_yfigure_figure *self,
                                                   struct yetty_ydraw_target *target)
{
    struct yetty_yfigure_container *g = (struct yetty_yfigure_container *)self;
    /* uthash insertion order = z-order; walk back-to-front. */
    struct child_entry *e, *tmp;
    HASH_ITER(hh, g->children, e, tmp)
    {
        struct yetty_yfigure_figure *c = e->figure;
        ydebug("yfigure group_render: rect=(%.1f,%.1f)-(%.1f,%.1f) child id=%u", self->rect.min.x,
               self->rect.min.y, self->rect.max.x, self->rect.max.y, e->id);
        struct yetty_ycore_void_result r = c->ops->render(c, target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yfigure group_render: child render failed");
        c->dirty = 0;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Wire-routing layer.
 *
 * Record header layout on the wire (and within any nested container's
 * payload): exactly 8 bytes per record.
 *
 *   u32 length     payload bytes that follow the header
 *   u32 id         parent-scoped figure id; 0 = admin/self
 *
 * Length-first so a generic linter or pipeline tool can walk the stream
 * without knowing ids or figure semantics — it just sums lengths and
 * skips. id=0 records are consumed by the container itself (admin
 * sub-cmds: create/delete child, etc.); id!=0 records resolve via
 * find_child_by_id and the payload is handed to child->process_input.
 *=========================================================================*/

struct container_record_header {
    uint32_t length;
    uint32_t id;
};
_Static_assert(sizeof(struct container_record_header) == 8,
               "container record header must be 8 bytes");

/* Read result: OK / END_OF_ENVELOPE (clean) / error. */
enum sm_read_status {
    SM_READ_OK = 0,
    SM_READ_EOE = 1,
};

/* Read exactly `n` bytes from the SM into `out`. Loops on partial reads
 * and yields the coroutine when no bytes are deliverable yet.
 *
 * Returns SM_READ_EOE via `*out_status` when read() yields 0 AND the
 * envelope has ended (terminator_seen) — caller must treat as a clean
 * end-of-envelope rather than retry. Partial reads followed by EOE
 * are errors. */
static struct yetty_ycore_void_result sm_read_exact(struct yetty_ywire_wire_statemachine *sm,
                                                    void *out, size_t n,
                                                    enum sm_read_status *out_status)
{
    if (out_status) {
        *out_status = SM_READ_OK;
    }
    uint8_t *p = (uint8_t *)out;
    size_t got = 0;
    while (got < n) {
        struct yetty_ycore_size_result rr =
            yetty_ywire_wire_statemachine_read(sm, p + got, n - got);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "container: sm read");
        if (rr.value == 0) {
            /* Read returned 0 — either EOE (all body bytes delivered AND
             * terminator seen) or "no bytes right now, more coming".
             * Distinguish by at_end(). */
            if (yetty_ywire_wire_statemachine_at_end(sm)) {
                if (got == 0 && out_status) {
                    *out_status = SM_READ_EOE;
                    return YETTY_OK_VOID();
                }
                return YETTY_ERR(yetty_ycore_void, "container: sm_read_exact short read at EOE");
            }
            yetty_yplatform_coro_yield();
            continue;
        }
        got += rr.value;
    }
    return YETTY_OK_VOID();
}

/* Skip `n` bytes off the SM (used when no child matches the id, so the
 * payload is unaddressable junk that must still be consumed to keep the
 * stream aligned). Stack scratch; loop reads. */
static struct yetty_ycore_void_result sm_skip(struct yetty_ywire_wire_statemachine *sm, size_t n)
{
    uint8_t scratch[4096];
    while (n > 0) {
        size_t take = n > sizeof(scratch) ? sizeof(scratch) : n;
        enum sm_read_status st = SM_READ_OK;
        struct yetty_ycore_void_result r = sm_read_exact(sm, scratch, take, &st);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "container: sm skip");
        if (st == SM_READ_EOE) {
            return YETTY_ERR(yetty_ycore_void,
                             "container: sm_skip hit EOE with bytes still expected");
        }
        n -= take;
    }
    return YETTY_OK_VOID();
}

/* Forward decl — container_process_bytes is reachable both directly
 * (op vtable) and from the consume_envelope path after buffering. */
static struct yetty_ycore_void_result container_process_bytes(struct yetty_yfigure_figure *self,
                                                              const uint8_t *bytes,
                                                              size_t bytes_len);

/* Admin sub-cmd dispatcher (byte-buffer source). Reads the first u32
 * as the admin op and dispatches with the remaining bytes as body. */
static struct yetty_ycore_void_result handle_admin_bytes(struct yetty_yfigure_container *g,
                                                         const uint8_t *bytes, size_t bytes_len)
{
    if (bytes_len < 4) {
        return YETTY_ERR(yetty_ycore_void, "container admin: payload too small for op");
    }
    uint32_t op;
    memcpy(&op, bytes, 4);
    const uint8_t *body = bytes + 4;
    size_t body_len = bytes_len - 4;

    switch (op) {
    case YETTY_YFIGURE_ADMIN_CLEAR_ALL: {
        if (body_len != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin CLEAR_ALL: unexpected trailing bytes");
        }
        struct child_entry *e, *tmp;
        struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
        int have_err = 0;
        HASH_ITER(hh, g->children, e, tmp)
        {
            HASH_DEL(g->children, e);
            entry_destroy(e, &first_err, &have_err);
        }
        g->base.dirty = 1;
        if (have_err) {
            return YETTY_ERR(yetty_ycore_void, "container admin CLEAR_ALL: child destroy",
                             first_err);
        }
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_CREATE_CHILD: {
        /* Layout: u32 child_id | u32 kind | f32 rect[4] | u32 init_bytes_size | init... */
        if (body_len < 4 + 4 + 16 + 4) {
            return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: header too small");
        }
        uint32_t child_id;
        uint32_t kind;
        float rect_floats[4];
        uint32_t init_bytes;
        memcpy(&child_id, body + 0, 4);
        memcpy(&kind, body + 4, 4);
        memcpy(rect_floats, body + 8, 16);
        memcpy(&init_bytes, body + 24, 4);
        if (init_bytes != body_len - 28) {
            return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: init_bytes mismatch");
        }
        if (!g->registry) {
            return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: no registry");
        }
        struct yetty_ycore_rectangle rect = {
            .min = {.x = rect_floats[0] + g->viewport_offset_x,
                    .y = rect_floats[1] + g->viewport_offset_y},
            .max = {.x = rect_floats[2] + g->viewport_offset_x,
                    .y = rect_floats[3] + g->viewport_offset_y},
        };
        /* Fast path: existing id of the same kind that supports
         * reset_content. Reuse the figure (and its cached binder /
         * pipeline / textures), just refresh its content. Without
         * this the receiver destroys + remints on every CREATE_CHILD,
         * which drops the binder cache and forces a full pipeline
         * rebuild on the next render — visible as ~100 ms hover lag. */
        struct child_entry *existing;
        HASH_FIND_INT(g->children, &child_id, existing);
        if (existing && existing->kind == kind && existing->figure->ops->reset_content) {
            struct yetty_ycore_void_result rc =
                existing->figure->ops->reset_content(existing->figure);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rc,
                                "container admin CREATE_CHILD: reset_content");
            existing->figure->rect = rect;
            if (init_bytes > 0) {
                if (!existing->figure->ops->process_bytes) {
                    return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: "
                                                       "no process_bytes for non-empty init");
                }
                struct yetty_ycore_void_result pr =
                    existing->figure->ops->process_bytes(existing->figure, body + 28, init_bytes);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, pr,
                                    "container admin CREATE_CHILD: child re-init");
            }
            existing->figure->dirty = 1;
            g->base.dirty = 1;
            return YETTY_OK_VOID();
        }

        struct yetty_yfigure_figure_ptr_result fr =
            yetty_yfigure_registry_mint(g->registry, kind, rect, g->context);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "container admin CREATE_CHILD: registry mint");
        struct yetty_yfigure_figure *child = fr.value;
        /* Existing id but different kind (or kind has no reset_content):
         * in-place swap the figure pointer. The hash entry stays put,
         * so z-order (uthash insertion order) is preserved, but the
         * old figure's GPU resources are released. This path is the
         * slow fallback — kind changes on the same id are rare. */
        if (existing) {
            struct yetty_ycore_void_result dr = existing->figure->ops->destroy(existing->figure);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            existing->figure = child;
            existing->kind = kind;
            child->dirty = 1;
        } else {
            struct yetty_ycore_void_result ar =
                yetty_yfigure_container_add_child(g, child, child_id);
            if (YETTY_IS_ERR(ar)) {
                struct yetty_ycore_void_result dr = child->ops->destroy(child);
                if (YETTY_IS_ERR(dr)) {
                    yetty_ycore_error_destroy(dr.error);
                }
                return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: add_child", ar);
            }
            struct child_entry *fresh;
            HASH_FIND_INT(g->children, &child_id, fresh);
            if (fresh) {
                fresh->kind = kind;
            }
        }
        if (init_bytes > 0) {
            if (!child->ops->process_bytes) {
                return YETTY_ERR(
                    yetty_ycore_void,
                    "container admin CREATE_CHILD: child has no process_bytes but init_bytes > 0");
            }
            struct yetty_ycore_void_result pr =
                child->ops->process_bytes(child, body + 28, init_bytes);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "container admin CREATE_CHILD: child init");
        }
        g->base.dirty = 1;
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_DELETE_CHILD: {
        if (body_len != 4) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin DELETE_CHILD: expected 4-byte payload");
        }
        uint32_t child_id;
        memcpy(&child_id, body, 4);
        struct yetty_ycore_void_result r = yetty_yfigure_container_remove_child_by_id(g, child_id);
        g->base.dirty = 1;
        return r;
    }

    case YETTY_YFIGURE_ADMIN_SET_CHILD_RECT: {
        if (body_len != 4 + 16) {
            return YETTY_ERR(yetty_ycore_void, "container admin SET_CHILD_RECT: bad payload size");
        }
        uint32_t child_id;
        float rect_floats[4];
        memcpy(&child_id, body + 0, 4);
        memcpy(rect_floats, body + 4, 16);
        struct yetty_yfigure_figure *child = yetty_yfigure_container_find_child_by_id(g, child_id);
        if (!child) {
            ydebug("container admin SET_CHILD_RECT: id=%u not bound", child_id);
            return YETTY_OK_VOID();
        }
        child->rect = (struct yetty_ycore_rectangle){
            .min = {.x = rect_floats[0] + g->viewport_offset_x,
                    .y = rect_floats[1] + g->viewport_offset_y},
            .max = {.x = rect_floats[2] + g->viewport_offset_x,
                    .y = rect_floats[3] + g->viewport_offset_y},
        };
        child->dirty = 1;
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_SET_RECT: {
        if (body_len != 16) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin SET_RECT: expected 16-byte payload");
        }
        float rect_floats[4];
        memcpy(rect_floats, body, 16);
        g->base.rect = (struct yetty_ycore_rectangle){
            .min = {.x = rect_floats[0], .y = rect_floats[1]},
            .max = {.x = rect_floats[2], .y = rect_floats[3]},
        };
        g->base.dirty = 1;
        return YETTY_OK_VOID();
    }

    default:
        ydebug("container admin: unknown op=%u, skipping %zu bytes", op, body_len);
        return YETTY_OK_VOID();
    }
}

/* Walk a byte buffer of `{length, id, payload}` records and dispatch
 * each. Called for nested container payloads (figure-tree recursion)
 * and for the direct byte-injection entry yui uses. */
static struct yetty_ycore_void_result container_process_bytes(struct yetty_yfigure_figure *self,
                                                              const uint8_t *bytes,
                                                              size_t bytes_len)
{
    struct yetty_yfigure_container *g = (struct yetty_yfigure_container *)self;
    size_t off = 0;
    while (off < bytes_len) {
        if (bytes_len - off < sizeof(struct yetty_yfigure_wire_record)) {
            return YETTY_ERR(yetty_ycore_void,
                             "container_process_bytes: trailing junk smaller than header");
        }
        struct yetty_yfigure_wire_record hdr;
        memcpy(&hdr, bytes + off, sizeof(hdr));
        off += sizeof(hdr);
        if (hdr.length > bytes_len - off) {
            return YETTY_ERR(yetty_ycore_void,
                             "container_process_bytes: record length overruns buffer");
        }
        const uint8_t *payload = bytes + off;
        if (hdr.id == 0) {
            struct yetty_ycore_void_result ar = handle_admin_bytes(g, payload, hdr.length);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "container_process_bytes: admin");
        } else {
            struct yetty_yfigure_figure *child =
                yetty_yfigure_container_find_child_by_id(g, hdr.id);
            if (!child) {
                ydebug("container_process_bytes: routed id=%u not bound, skipping %u", hdr.id,
                       hdr.length);
            } else if (!child->ops->process_bytes) {
                ydebug("container_process_bytes: id=%u no process_bytes, skipping %u", hdr.id,
                       hdr.length);
            } else {
                struct yetty_ycore_void_result cr =
                    child->ops->process_bytes(child, payload, hdr.length);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "container_process_bytes: child");
                child->dirty = 1;
            }
        }
        off += hdr.length;
    }
    return YETTY_OK_VOID();
}

/* process_input op — pumps the wire-statemachine via the existing
 * consume_envelope helper. The SM gives us a fresh envelope each time
 * its OSC code fires; consume_envelope walks `{length, id, body}`
 * records, routes id=0 to handle_admin_bytes and id!=0 to the matching
 * child. Children's input still flows through process_bytes for now —
 * that migrates to process_input figure-by-figure. */
static struct yetty_ycore_void_result container_process_input(
    struct yetty_yfigure_figure *self, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yfigure_container *g = (struct yetty_yfigure_container *)self;
    return yetty_yfigure_container_consume_envelope(g, sm);
}

static char *container_dump(const struct yetty_yfigure_figure *self, int indent)
{
    const struct yetty_yfigure_container *g = (const struct yetty_yfigure_container *)self;
    char pad[64];
    dump_indent_spaces(pad, sizeof(pad), indent);
    size_t len = 0, cap = 0;
    char *buf = NULL;
    buf = dump_appendf(buf, &len, &cap, "%skind: container\n", pad);
    if (!buf) {
        return NULL;
    }
    buf = dump_appendf(buf, &len, &cap, "%srect: [%.1f, %.1f, %.1f, %.1f]\n", pad, self->rect.min.x,
                       self->rect.min.y, self->rect.max.x, self->rect.max.y);
    if (!buf) {
        return NULL;
    }
    buf = dump_appendf(buf, &len, &cap, "%sdirty: %d\n", pad, self->dirty);
    if (!buf) {
        return NULL;
    }
    buf = dump_appendf(buf, &len, &cap, "%sviewport_offset: [%.1f, %.1f]\n", pad,
                       g->viewport_offset_x, g->viewport_offset_y);
    if (!buf) {
        return NULL;
    }
    /* Children section. uthash iter walks insertion order = z-order
     * (back-to-front), which matches the render order. Tests can assert
     * z-order from the line order in the dump. Empty when no children. */
    if (!g->children) {
        buf = dump_appendf(buf, &len, &cap, "%schildren: {}\n", pad);
        return buf;
    }
    buf = dump_appendf(buf, &len, &cap, "%schildren:\n", pad);
    if (!buf) {
        return NULL;
    }
    struct child_entry *e, *tmp;
    HASH_ITER(hh, g->children, e, tmp)
    {
        buf = dump_appendf(buf, &len, &cap, "%s  '%u':\n", pad, e->id);
        if (!buf) {
            return NULL;
        }
        char *child_dump = yetty_yfigure_dump(e->figure, indent + 4);
        if (!child_dump) {
            free(buf);
            return NULL;
        }
        buf = dump_appendf(buf, &len, &cap, "%s", child_dump);
        free(child_dump);
        if (!buf) {
            return NULL;
        }
    }
    return buf;
}

static const struct yetty_yfigure_figure_ops *group_ops(void)
{
    static const struct yetty_yfigure_figure_ops ops = {
        .destroy = group_destroy,
        .render = group_render,
        .process_input = container_process_input,
        .process_bytes = container_process_bytes,
        .dump = container_dump,
    };
    return &ops;
}

/*===========================================================================
 * Public API
 *=========================================================================*/

struct yetty_yfigure_container_ptr_result yetty_yfigure_container_create_local(
    struct yetty_ycore_rectangle rect, const struct yetty_context *context,
    struct yetty_yfigure_registry *registry)
{
    struct yetty_yfigure_container *g =
        (struct yetty_yfigure_container *)calloc(1, sizeof(struct yetty_yfigure_container));
    if (!g) {
        return YETTY_ERR(yetty_yfigure_container_ptr, "yfigure_container_create: oom");
    }
    g->base.ops = group_ops();
    g->base.rect = rect;
    g->base.dirty = 0;
    g->children = NULL;
    g->context = context;
    g->registry = registry;
    g->viewport_offset_x = 0.0f;
    g->viewport_offset_y = 0.0f;
    return YETTY_OK(yetty_yfigure_container_ptr, g);
}

void yetty_yfigure_container_set_viewport_offset(struct yetty_yfigure_container *container,
                                                 float offset_x, float offset_y)
{
    if (!container) {
        return;
    }
    container->viewport_offset_x = offset_x;
    container->viewport_offset_y = offset_y;
}

struct yetty_ycore_void_result yetty_yfigure_container_consume_envelope(
    struct yetty_yfigure_container *container, struct yetty_ywire_wire_statemachine *sm)
{
    if (!container || !sm) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_consume_envelope: NULL arg");
    }
    ydebug("consume_envelope: ENTRY container=%p at_end=%d", (void *)container,
           yetty_ywire_wire_statemachine_at_end(sm));
    /* Read records header-by-header from the SM, buffer each record's
     * payload, then hand it to process_bytes. Buffering is per-record
     * (not per-envelope) so very large payloads (e.g. an atlas) don't
     * inflate memory more than necessary. */
    /* Optional diagnostic: dump every {hdr, payload} that arrives in
     * this envelope so the decoded record stream can be inspected with
     * `tools/osc-analyzer -r <path>`. One frame per envelope, prefixed
     * with `===FRAME===\n`. */
    FILE *dump_fp = NULL;
    {
        const char *dump_path = getenv("YFIGURE_DUMP_RECORDS");
        if (dump_path && dump_path[0]) {
            dump_fp = fopen(dump_path, "ab");
            if (dump_fp) {
                fwrite("===FRAME===\n", 1, 12, dump_fp);
                fflush(dump_fp);
            }
        }
    }
    for (;;) {
        struct yetty_yfigure_wire_record hdr;
        enum sm_read_status hst = SM_READ_OK;
        struct yetty_ycore_void_result hr = sm_read_exact(sm, &hdr, sizeof(hdr), &hst);
        if (YETTY_IS_ERR(hr)) {
            if (dump_fp) {
                fclose(dump_fp);
            }
            return YETTY_ERR(yetty_ycore_void, "consume_envelope: read header", hr);
        }
        if (hst == SM_READ_EOE) {
            /* Clean end of envelope — all body bytes delivered. */
            break;
        }
        struct yetty_ycore_void_result dr = YETTY_OK_VOID();
        struct yetty_yfigure_figure *child =
            (hdr.id == 0) ? NULL : yetty_yfigure_container_find_child_by_id(container, hdr.id);

        /* Streaming dispatch: if the target child has its own
         * process_input coroutine, hand the SM straight to it — no
         * intermediate buffer, the child pumps the SM and reads its
         * `hdr.length` bytes via self-describing sub-records.
         *
         * Note: dump_fp can't see this record's payload because we
         * never buffer it. That's a known cost of the streaming path. */
        if (child && child->ops->process_input) {
            if (dump_fp) {
                fwrite(&hdr, 1, sizeof(hdr), dump_fp);
                fwrite("<<streamed>>", 1, 12, dump_fp);
            }
            dr = child->ops->process_input(child, sm);
            if (YETTY_IS_OK(dr)) {
                child->dirty = 1;
                container->base.dirty = 1;
            }
            if (YETTY_IS_ERR(dr)) {
                if (dump_fp) {
                    fclose(dump_fp);
                }
                return YETTY_ERR(yetty_ycore_void, "consume_envelope: child process_input", dr);
            }
            continue;
        }

        /* Legacy path — buffer the payload bytes then call process_bytes.
         * Used by admin records (id=0) and by child kinds that haven't
         * migrated to process_input yet. */
        uint8_t *payload = NULL;
        if (hdr.length > 0) {
            payload = (uint8_t *)malloc(hdr.length);
            if (!payload) {
                if (dump_fp) {
                    fclose(dump_fp);
                }
                return YETTY_ERR(yetty_ycore_void, "consume_envelope: payload oom");
            }
            enum sm_read_status pst = SM_READ_OK;
            struct yetty_ycore_void_result pr = sm_read_exact(sm, payload, hdr.length, &pst);
            if (YETTY_IS_ERR(pr)) {
                free(payload);
                if (dump_fp) {
                    fclose(dump_fp);
                }
                return YETTY_ERR(yetty_ycore_void, "consume_envelope: read payload", pr);
            }
            if (pst == SM_READ_EOE) {
                free(payload);
                if (dump_fp) {
                    fclose(dump_fp);
                }
                return YETTY_ERR(yetty_ycore_void, "consume_envelope: EOE mid-payload");
            }
        }
        if (dump_fp) {
            fwrite(&hdr, 1, sizeof(hdr), dump_fp);
            if (payload && hdr.length) {
                fwrite(payload, 1, hdr.length, dump_fp);
            }
        }
        if (hdr.id == 0) {
            dr = handle_admin_bytes(container, payload, hdr.length);
        } else if (!child) {
            ydebug("consume_envelope: id=%u not bound, skipping %u bytes", hdr.id, hdr.length);
        } else if (!child->ops->process_bytes) {
            ydebug("consume_envelope: id=%u no process_bytes / process_input", hdr.id);
        } else {
            dr = child->ops->process_bytes(child, payload, hdr.length);
            if (YETTY_IS_OK(dr)) {
                child->dirty = 1;
                container->base.dirty = 1;
            }
        }
        free(payload);
        if (YETTY_IS_ERR(dr)) {
            if (dump_fp) {
                fclose(dump_fp);
            }
            return YETTY_ERR(yetty_ycore_void, "consume_envelope: record dispatch", dr);
        }
    }
    if (dump_fp) {
        fclose(dump_fp);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_container_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yfigure_container *container = userdata;
    if (!container) {
        return YETTY_ERR(yetty_ycore_void, "container_process_input: NULL container");
    }
    for (;;) {
        struct yetty_ycore_void_result r = yetty_yfigure_container_consume_envelope(container, sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "container_process_input: consume_envelope");
        yetty_yplatform_coro_yield();
    }
}

struct yetty_ycore_void_result yetty_yfigure_container_process_records(
    struct yetty_yfigure_container *container, const uint8_t *bytes, size_t bytes_len)
{
    if (!container) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_process_records: NULL container");
    }
    if (bytes_len == 0) {
        return YETTY_OK_VOID();
    }
    if (!bytes) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_process_records: NULL bytes");
    }
    return container_process_bytes(yetty_yfigure_container_as_figure(container), bytes, bytes_len);
}

struct yetty_yfigure_figure *yetty_yfigure_container_as_figure(
    struct yetty_yfigure_container *group)
{
    return group ? &group->base : NULL;
}

struct yetty_ycore_void_result yetty_yfigure_container_add_child(
    struct yetty_yfigure_container *group, struct yetty_yfigure_figure *child, uint32_t id)
{
    if (!group || !child) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_add_child: NULL arg");
    }
    if (id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_add_child: id=0 is reserved");
    }
    /* Boundary check — verify the concrete figure was fully constructed
     * before it enters the group. After this point internal code may
     * assume child->ops + ops->destroy + ops->render are non-NULL. */
    if (!child->ops || !child->ops->destroy || !child->ops->render) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_add_child: child ops vtable incomplete");
    }
    struct child_entry *existing;
    HASH_FIND_INT(group->children, &id, existing);
    if (existing) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_add_child: id collision");
    }
    struct child_entry *e = (struct child_entry *)calloc(1, sizeof(*e));
    if (!e) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_add_child: oom");
    }
    e->id = id;
    e->figure = child;
    HASH_ADD_INT(group->children, id, e);
    child->dirty = 1;
    return YETTY_OK_VOID();
}

struct yetty_yfigure_figure *yetty_yfigure_container_find_child_by_id(
    const struct yetty_yfigure_container *group, uint32_t id)
{
    if (!group || id == 0) {
        return NULL;
    }
    struct child_entry *e;
    HASH_FIND_INT(group->children, &id, e);
    return e ? e->figure : NULL;
}

struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yfigure_container *group, uint32_t id)
{
    if (!group) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_remove_child_by_id: NULL group");
    }
    if (id == 0) {
        return YETTY_OK_VOID();
    }
    struct child_entry *e;
    HASH_FIND_INT(group->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID(); /* stale delete from wire — benign */
    }
    HASH_DEL(group->children, e);
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    entry_destroy(e, &first_err, &have_err);
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_remove_child_by_id: child destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

int yetty_yfigure_container_for_each(struct yetty_yfigure_container *group,
                                     yetty_yfigure_container_visitor_fn fn, void *user)
{
    if (!group || !fn) {
        return 0;
    }
    struct child_entry *e, *tmp;
    HASH_ITER(hh, group->children, e, tmp)
    {
        int rc = fn(e->id, e->figure, user);
        if (rc) {
            return rc;
        }
    }
    return 0;
}

struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yfigure_container *group, uint32_t id)
{
    if (!group) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_group_raise_child_by_id: NULL group");
    }
    if (id == 0) {
        return YETTY_OK_VOID();
    }
    struct child_entry *e;
    HASH_FIND_INT(group->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID();
    }
    /* uthash insertion order is the z-order. Raising to top = delete +
     * re-insert with the same id — the entry pointer stays valid, the
     * child isn't destroyed. */
    HASH_DEL(group->children, e);
    HASH_ADD_INT(group->children, id, e);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Hit-test
 *
 * Walks every child in insertion order (= back-to-front in z) and
 * returns the first one whose rect contains the point. Cursor coords
 * are absolute target pixels, the same space child rects live in.
 *=========================================================================*/

struct hit_visitor_state {
    float x;
    float y;
    struct yetty_yfigure_hit hit;
};

static int hit_visit(uint32_t id, struct yetty_yfigure_figure *child, void *user)
{
    struct hit_visitor_state *st = user;
    if (st->x < child->rect.min.x || st->x >= child->rect.max.x || st->y < child->rect.min.y ||
        st->y >= child->rect.max.y) {
        return 0;
    }
    /* uthash walks insertion order = back-to-front z-order. The front-most
     * figure must win, so we keep overwriting — the last match in the
     * walk is the top of the z-stack. Returning early on first match
     * would make decoration figures (e.g. shader-glyph at 0xFFFFFFFE,
     * inserted at terminal create) steal hits from interactive figures
     * (ygreeter / ygui chrome) inserted later. */
    st->hit.figure_id = id;
    st->hit.local_x = st->x - child->rect.min.x;
    st->hit.local_y = st->y - child->rect.min.y;
    return 0;
}

struct yetty_yfigure_hit yetty_yfigure_container_hit_test(struct yetty_yfigure_container *container,
                                                          float x, float y)
{
    struct hit_visitor_state st = {.x = x, .y = y, .hit = {0, 0, 0}};
    yetty_yfigure_container_for_each(container, hit_visit, &st);
    return st.hit;
}

/*===========================================================================
 * yclass slot overrides
 *
 * One wrapper impl per container public method whose signature can be
 * wire-marshalled. Skipped (signature incompatible):
 *   - consume_envelope, process_input       — wire-statemachine ptr
 *   - find_child_by_id, as_figure           — pointer return
 *   - hit_test                              — non-Result struct return
 *   - for_each                              — function pointer arg
 *   - set_viewport_offset                   — void return, not Result
 *   - create_local                          — replaced by codegen
 *                                             `yetty_yfigure_container_create(ctx)`
 *
 * Recovery cast: legacy-allocated container has the figure_ops pointer
 * at offset 0; a yclass-allocated one would have yclass_object at
 * offset 0 with the user data starting at sizeof(yclass_object).
 * No yclass-dispatched callsite exists yet, so the direct cast is
 * correct for every current caller. */

#include <yclass/class.h>
#include <yetty/yfigure/container.h>

/* yclass instance layout: yclass_object header at offset 0, user data
 * (the `struct yetty_yfigure_container` body) immediately after. Cast
 * via (obj + 1) advances past the header in pointer arithmetic. */
#define YCLASS_TO_CONTAINER(obj) \
    ((struct yetty_yfigure_container *)((struct yetty_yclass_object *)(obj) + 1))

[[clang::annotate("override@yfigure:container:add_child")]]
static struct yetty_ycore_void_result yetty_yfigure_container_add_child_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_yfigure_figure *child, uint32_t id)
{
    (void)ctx;
    return yetty_yfigure_container_add_child(YCLASS_TO_CONTAINER(obj), child, id);
}

[[clang::annotate("override@yfigure:container:remove_child_by_id")]]
static struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, uint32_t id)
{
    (void)ctx;
    return yetty_yfigure_container_remove_child_by_id(YCLASS_TO_CONTAINER(obj), id);
}

[[clang::annotate("override@yfigure:container:raise_child_by_id")]]
static struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, uint32_t id)
{
    (void)ctx;
    return yetty_yfigure_container_raise_child_by_id(YCLASS_TO_CONTAINER(obj), id);
}

[[clang::annotate("override@yfigure:container:process_records")]]
static struct yetty_ycore_void_result yetty_yfigure_container_process_records_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ycore_buffer bytes)
{
    (void)ctx;
    return yetty_yfigure_container_process_records(YCLASS_TO_CONTAINER(obj), bytes.data, bytes.size);
}

#include "container.gen.c"
