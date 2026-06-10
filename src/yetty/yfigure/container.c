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
#include <yetty/yclass/class.h>
/* Own generated header: pulls in this module's public stubs (called below) and
 * the hit struct. Skipped during codegen's parse pass (YCLASS_CODEGEN defined),
 * where the hit struct is instead supplied by the codegen block further down —
 * otherwise the two definitions would collide during parsing. */
#ifndef YCLASS_CODEGEN
#include <yetty/yfigure/container.h>
#endif
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

/* Hit-test result: the child whose rect contains the cursor, plus the cursor
 * coordinates inside that child's own pixel space (origin = child rect's
 * top-left). figure_id == 0 means "no hit". Iteration is back-to-front, so
 * for overlapping children the BACK-most match wins. Public API — authored in
 * a codegen block (header-destined only). The single real definition lives in
 * the generated container.h this .c includes at the top; codegen parses the
 * block but the real build does not compile it, so there is no double
 * definition. */
#ifdef YCLASS_CODEGEN
struct yetty_yfigure_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};
#endif

/* Visitor for the internal child walk (container_for_each): called once per
 * child in z-order (back-to-front) with its parent-scoped id, figure pointer
 * and the caller's cookie. Returning non-zero stops the walk and is returned.
 * Used only inside this TU (hit-testing), so it stays private — no `expose`. */
typedef int (*container_visitor_fn)(uint32_t id, struct yetty_yfigure_figure *child, void *user);

/* The container's own type, defined below — declared at file scope here so the
 * parameter types in the prototypes that follow refer to it (a struct tag first
 * mentioned inside a parameter list would otherwise get prototype scope and
 * clash with the real definition). */
struct yetty_yfigure_container;

/* Forward declarations of public API defined lower in this file, needed by the
 * admin/record handlers above their definitions. (container.c deliberately
 * does not include its own generated header — see the hit-struct note.) */
struct yetty_yfigure_figure *yetty_yfigure_container_as_figure(
    struct yetty_yfigure_container *container);
struct yetty_ycore_void_result yetty_yfigure_container_add_child(
    struct yetty_yfigure_container *container, struct yetty_yfigure_figure *child, uint32_t id);
struct yetty_yfigure_figure *yetty_yfigure_container_find_child_by_id(
    const struct yetty_yfigure_container *container, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yfigure_container *container, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yfigure_container *container, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_consume_envelope(
    struct yetty_yfigure_container *container, struct yetty_ywire_wire_statemachine *sm);

struct child_entry {
    /* uthash key. Parent-scoped, non-zero, unique within the container. */
    uint32_t id;
    /* Owned child figure. Destroyed when the entry is removed or when
     * the container itself is destroyed. */
    struct yetty_yfigure_figure *figure;
    /* Kind tag (YETTY_YFIGURE_KIND_*) captured at mint time. Used by
     * CREATE_CHILD on an existing id to decide between the fast path
     * (same kind → reset_content + process_bytes, reuse GPU state)
     * and the destroy + mint fallback (different kind). */
    uint32_t kind;
    /* Monotonic insertion sequence, assigned at add_child. The z-order
     * comparator breaks ties on (figure->z) with this, giving a total
     * order so the sort is deterministic regardless of uthash sort
     * stability and equal-z children keep insertion order. */
    uint64_t seq;
    /* When set, clear_all (the CLEAR_ALL admin op and the terminal's
     * full-screen-erase / reset hook) skips this child. Used for the host's
     * own structural figures — notably the terminal content grid — that are
     * NOT part of the producer-managed figure set a client means to drop.
     * Container destroy still frees it; only clear_all spares it. */
    int protected_from_clear;
    UT_hash_handle hh;
};

struct [[clang::annotate("class@yfigure:container")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_yfigure_container {
    struct yetty_yfigure_figure *base;
    /* uthash head — id → entry. The list is kept sorted by
     * (figure->z, seq) via container_ensure_sorted, so HASH_ITER walks
     * children back-to-front in true z-order (render order; hit-test
     * keeps the last match = front-most). */
    struct child_entry *children;
    /* Next insertion sequence handed to a child_entry. Monotonic. */
    uint64_t next_seq;
    /* Set whenever the child set or any child's z changes; cleared by
     * container_ensure_sorted after it re-sorts the list. Avoids
     * re-sorting on every render when nothing moved. */
    int z_order_dirty;
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

/* The container's own data slice (its fields sit after the figure
 * base slice in the shared yclass object). */
static struct yetty_yclass_void_ptr_result container_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_container_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "container_from_obj: class");
    return yetty_yclass_object_data(obj, class_r.value);
}

/*===========================================================================
 * Internal helpers
 *=========================================================================*/

/* render/destroy dispatch through the figure's yclass object. Every figure
 * kind is a yclass object (self_obj set at construction); the slots are
 * local@ (in-process) so ctx is NULL. */
static struct yetty_ycore_void_result figure_dispatch_render(struct yetty_yfigure_figure *fig,
                                                             struct yetty_ydraw_target *target)
{
    return yetty_yfigure_render(NULL, ((struct yetty_yclass_object *)(fig)-1), target);
}

static struct yetty_ycore_void_result figure_dispatch_destroy(struct yetty_yfigure_figure *fig)
{
    return yetty_yfigure_destroy(NULL, ((struct yetty_yclass_object *)(fig)-1));
}

/* True when fig's concrete class overrides the given figure-base slot
 * (rather than inheriting the base no-op/reject default). This is the
 * yclass replacement for the old "is this vtable op non-NULL" capability
 * check: a figure that doesn't implement, say, process_input inherits the
 * base default, so its dispatch impl equals the base class's impl. */
/* Returns 1 when fig's concrete class overrides the slot, 0 when it inherits
 * the base default. The lookups that back this can fail, so it returns an int
 * Result; callers propagate. */
static struct yetty_ycore_int_result figure_implements(struct yetty_yfigure_figure *fig,
                                                       yetty_yclass_method_id_t method_id)
{
    if (!fig || !((struct yetty_yclass_object *)(fig)-1)) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_method_slot_result slot_r =
        yetty_yclass_method_slot_get("yetty_yfigure", method_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, slot_r, "figure_implements: method_slot_get");
    struct yetty_yclass_ptr_result base_r = yetty_yfigure_figure_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, base_r, "figure_implements: figure_class_get");
    struct yetty_yclass_impl_t_result obj_impl =
        yetty_yclass_dispatch_lookup(((struct yetty_yclass_object *)(fig)-1)->klass, slot_r.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, obj_impl, "figure_implements: obj dispatch_lookup");
    struct yetty_yclass_impl_t_result base_impl =
        yetty_yclass_dispatch_lookup(base_r.value, slot_r.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, base_impl, "figure_implements: base dispatch_lookup");
    return YETTY_OK(yetty_ycore_int, obj_impl.value != base_impl.value ? 1 : 0);
}

/* Best-effort: destroy the entry's figure and always free the entry, even
 * if the figure destroy reported an error. Returns that destroy result so
 * the caller can fold it into its own first-error aggregation. */
static struct yetty_ycore_void_result entry_destroy(struct child_entry *e)
{
    struct yetty_ycore_void_result destroy_r = figure_dispatch_destroy(e->figure);
    free(e);
    return destroy_r;
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

[[clang::annotate("expose")]]
struct yetty_ycore_char_ptr_result yetty_yfigure_dump(const struct yetty_yfigure_figure *self,
                                                      int indent)
{
    if (!self) {
        char *out = (char *)malloc(8);
        if (!out) {
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump: out of memory");
        }
        snprintf(out, 8, "null\n");
        return YETTY_OK(yetty_ycore_char_ptr, out);
    }
    struct yetty_ycore_int_result implements_r = figure_implements(
        (struct yetty_yfigure_figure *)self, (yetty_yclass_method_id_t)yetty_yfigure_dump_state);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, implements_r,
                        "yetty_yfigure_dump: figure_implements");
    if (implements_r.value) {
        return yetty_yfigure_dump_state(
            NULL, (struct yetty_yclass_object *)((struct yetty_yclass_object *)(self)-1), indent);
    }
    /* Fallback: just rect + dirty. Concrete kinds that haven't migrated
     * to a real dump still produce something testable. */
    char pad[64];
    dump_indent_spaces(pad, sizeof(pad), indent);
    size_t len = 0, cap = 0;
    char *buf = NULL;
    buf = dump_appendf(buf, &len, &cap, "%skind: unknown\n", pad);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump: out of memory");
    }
    buf = dump_appendf(
        buf, &len, &cap, "%srect: [%.1f, %.1f, %.1f, %.1f]\n", pad,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.y,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.y);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yfigure_dump: out of memory");
    }
    buf =
        dump_appendf(buf, &len, &cap, "%sdirty: %d\n", pad,
                     yetty_yfigure_figure_dirty_get((struct yetty_yclass_object *)(self)-1).value);
    return YETTY_OK(yetty_ycore_char_ptr, buf);
}

/*===========================================================================
 * Group ops
 *=========================================================================*/

[[clang::annotate("override@yfigure:container:destroy")]]
static struct yetty_ycore_void_result container_destroy(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_destroy: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        HASH_DEL(container->children, e);
        struct yetty_ycore_void_result entry_r = entry_destroy(e);
        if (YETTY_IS_ERR(entry_r)) {
            if (!have_err) {
                first_err = entry_r;
                have_err = 1;
            } else {
                yetty_ycore_error_destroy(entry_r.error);
            }
        }
    }
    /* The destroy slot is dispatched with the yclass object header; the
     * container body is one slice inside it. Free the header so both it
     * and every slice (figure base + container) are reclaimed in one go,
     * honouring the allocation contract. */
    struct yetty_ycore_void_result fr = yetty_yclass_object_free(obj);
    if (YETTY_IS_ERR(fr)) {
        if (have_err) {
            yetty_ycore_error_destroy(fr.error);
        } else {
            first_err = fr;
            have_err = 1;
        }
    }
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "yfigure container_destroy: child destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

/* Total order on children: ascending figure->z, ties broken by
 * insertion sequence. A total order means the result is independent of
 * uthash's sort stability, and equal-z children keep insertion order
 * (so a single-z tree sorts to its original order — no behaviour change
 * until something sets a non-zero z). */
YETTY_EXTERNAL_CALLBACK
static int child_z_cmp(struct child_entry *a, struct child_entry *b)
{
    if (yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(a->figure) - 1).value !=
        yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(b->figure) - 1).value) {
        return yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(a->figure) - 1).value <
                       yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(b->figure) - 1)
                           .value
                   ? -1
                   : 1;
    }
    if (a->seq != b->seq) {
        return a->seq < b->seq ? -1 : 1;
    }
    return 0;
}

/* Re-sort the child list in place when a child was added/removed or a
 * z changed. Cheap no-op when nothing moved. After this, every HASH_ITER
 * walk (render, hit-test, dump, serialize) is in z-order. */
static void container_ensure_sorted(struct yetty_yfigure_container *container)
{
    if (!container->z_order_dirty) {
        return;
    }
    HASH_SRT(hh, container->children, child_z_cmp);
    container->z_order_dirty = 0;
}

[[clang::annotate("override@yfigure:container:render")]]
static struct yetty_ycore_void_result container_render(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_ydraw_target *target)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_render: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    struct yetty_yfigure_figure *self = container->base;
    container_ensure_sorted(container);
    /* List is z-sorted; HASH_ITER walks back-to-front. */
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        struct yetty_yfigure_figure *c = e->figure;
        if (yetty_yfigure_figure_hidden_get((struct yetty_yclass_object *)(c)-1).value) {
            continue;
        }
        ydebug("yfigure container_render: child id=%u z=%d rect=(%.0f,%.0f)-(%.0f,%.0f)", e->id,
               yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(c)-1).value,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.min.x,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.min.y,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.max.x,
               yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(c)-1).value.max.y);
        struct yetty_ycore_void_result r = figure_dispatch_render(c, target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yfigure container_render: child render failed");
        {
            struct yetty_ycore_void_result set_r =
                yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(c)-1, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
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

/* Remove and destroy every child figure, then mark the container dirty so the
 * empty result is repainted. Shared by the CLEAR_ALL admin op and the terminal's
 * full-screen-erase / reset path. Best-effort: keeps tearing down on a per-child
 * error, stashing the first to surface at the end. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yfigure_container_clear_all(
    struct yetty_yfigure_container *container)
{
    if (!container) {
        return YETTY_ERR(yetty_ycore_void, "container clear_all: NULL container");
    }
    struct child_entry *e, *tmp;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    HASH_ITER(hh, container->children, e, tmp)
    {
        /* Spare structural children a client's CLEAR_ALL must not drop (e.g.
         * the terminal's own content grid). They live and die with the
         * container itself, not with the producer-managed figure set. */
        if (e->protected_from_clear) {
            continue;
        }
        HASH_DEL(container->children, e);
        struct yetty_ycore_void_result entry_r = entry_destroy(e);
        if (YETTY_IS_ERR(entry_r)) {
            if (!have_err) {
                first_err = entry_r;
                have_err = 1;
            } else {
                yetty_ycore_error_destroy(entry_r.error);
            }
        }
    }
    {
        struct yetty_ycore_void_result set_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(container->base) - 1, 1);
        if (YETTY_IS_ERR(set_r)) {
            if (have_err) {
                yetty_ycore_error_destroy(first_err.error);
            }
            return YETTY_ERR(yetty_ycore_void, "container clear_all: figure attr set", set_r);
        }
    }
    if (have_err) {
        return YETTY_ERR(yetty_ycore_void, "container clear_all: child destroy", first_err);
    }
    return YETTY_OK_VOID();
}

/* Admin sub-cmd dispatcher (byte-buffer source). Reads the first u32
 * as the admin op and dispatches with the remaining bytes as body. */
static struct yetty_ycore_void_result handle_admin_bytes(struct yetty_yfigure_container *container,
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
        struct yetty_ycore_void_result clear_r = yetty_yfigure_container_clear_all(container);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_r, "container admin CLEAR_ALL");
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_CREATE_CHILD: {
        ydebug("container admin CREATE_CHILD: body_len=%zu", body_len);
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
        if (!container->registry) {
            return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: no registry");
        }
        struct yetty_ycore_rectangle rect = {
            .min = {.x = rect_floats[0] + container->viewport_offset_x,
                    .y = rect_floats[1] + container->viewport_offset_y},
            .max = {.x = rect_floats[2] + container->viewport_offset_x,
                    .y = rect_floats[3] + container->viewport_offset_y},
        };
        /* Fast path: existing id of the same kind that supports
         * reset_content. Reuse the figure (and its cached binder /
         * pipeline / textures), just refresh its content. Without
         * this the receiver destroys + remints on every CREATE_CHILD,
         * which drops the binder cache and forces a full pipeline
         * rebuild on the next render — visible as ~100 ms hover lag. */
        struct child_entry *existing;
        HASH_FIND_INT(container->children, &child_id, existing);
        int existing_can_reset = 0;
        if (existing && existing->kind == kind) {
            struct yetty_ycore_int_result reset_impl_r = figure_implements(
                existing->figure, (yetty_yclass_method_id_t)yetty_yfigure_reset_content);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, reset_impl_r,
                                "container admin CREATE_CHILD: reset_content support");
            existing_can_reset = reset_impl_r.value;
        }
        if (existing_can_reset) {
            struct yetty_ycore_void_result rc = yetty_yfigure_reset_content(
                NULL, ((struct yetty_yclass_object *)(existing->figure) - 1));
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rc,
                                "container admin CREATE_CHILD: reset_content");
            {
                struct yetty_ycore_void_result set_r = yetty_yfigure_figure_rect_set(
                    (struct yetty_yclass_object *)(existing->figure) - 1, rect);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
            if (init_bytes > 0) {
                struct yetty_ycore_int_result process_bytes_impl_r = figure_implements(
                    existing->figure, (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, process_bytes_impl_r,
                                    "container admin CREATE_CHILD: process_bytes support");
                if (!process_bytes_impl_r.value) {
                    return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: "
                                                       "no process_bytes for non-empty init");
                }
                struct yetty_ycore_void_result pr = yetty_yfigure_process_bytes(
                    NULL, ((struct yetty_yclass_object *)(existing->figure) - 1), body + 28,
                    init_bytes);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, pr,
                                    "container admin CREATE_CHILD: child re-init");
            }
            {
                struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                    (struct yetty_yclass_object *)(existing->figure) - 1, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
            {
                struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                    (struct yetty_yclass_object *)(container->base) - 1, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
            return YETTY_OK_VOID();
        }

        struct yetty_yfigure_figure_data_ptr_result fr =
            yetty_yfigure_registry_mint(container->registry, kind, rect, container->context);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "container admin CREATE_CHILD: registry mint");
        struct yetty_yfigure_figure *child = fr.value;
        /* Existing id but different kind (or kind has no reset_content):
         * in-place swap the figure pointer. The hash entry stays put,
         * so z-order (uthash insertion order) is preserved, but the
         * old figure's GPU resources are released. This path is the
         * slow fallback — kind changes on the same id are rare. */
        if (existing) {
            /* Carry the stacking order across the swap — a re-mint must
             * not silently drop the child's z back to 0. */
            {
                struct yetty_ycore_void_result set_r = yetty_yfigure_figure_z_set(
                    (struct yetty_yclass_object *)(child)-1,
                    yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(existing->figure) - 1)
                        .value);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
            struct yetty_ycore_void_result dr = figure_dispatch_destroy(existing->figure);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            existing->figure = child;
            existing->kind = kind;
            container->z_order_dirty = 1;
            {
                struct yetty_ycore_void_result set_r =
                    yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
        } else {
            struct yetty_ycore_void_result ar =
                yetty_yfigure_container_add_child(container, child, child_id);
            if (YETTY_IS_ERR(ar)) {
                struct yetty_ycore_void_result dr = figure_dispatch_destroy(child);
                if (YETTY_IS_ERR(dr)) {
                    yetty_ycore_error_destroy(dr.error);
                }
                return YETTY_ERR(yetty_ycore_void, "container admin CREATE_CHILD: add_child", ar);
            }
            struct child_entry *fresh;
            HASH_FIND_INT(container->children, &child_id, fresh);
            if (fresh) {
                fresh->kind = kind;
            }
        }
        if (init_bytes > 0) {
            struct yetty_ycore_int_result process_bytes_impl_r =
                figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, process_bytes_impl_r,
                                "container admin CREATE_CHILD: process_bytes support");
            if (!process_bytes_impl_r.value) {
                return YETTY_ERR(
                    yetty_ycore_void,
                    "container admin CREATE_CHILD: child has no process_bytes but init_bytes > 0");
            }
            struct yetty_ycore_void_result pr = yetty_yfigure_process_bytes(
                NULL, ((struct yetty_yclass_object *)(child)-1), body + 28, init_bytes);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "container admin CREATE_CHILD: child init");
        }
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                (struct yetty_yclass_object *)(container->base) - 1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_DELETE_CHILD: {
        if (body_len != 4) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin DELETE_CHILD: expected 4-byte payload");
        }
        uint32_t child_id;
        memcpy(&child_id, body, 4);
        struct yetty_ycore_void_result r =
            yetty_yfigure_container_remove_child_by_id(container, child_id);
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                (struct yetty_yclass_object *)(container->base) - 1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
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
        struct yetty_yfigure_figure *child =
            yetty_yfigure_container_find_child_by_id(container, child_id);
        if (!child) {
            ydebug("container admin SET_CHILD_RECT: id=%u not bound", child_id);
            return YETTY_OK_VOID();
        }
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_rect_set(
                (struct yetty_yclass_object *)(child)-1,
                (struct yetty_ycore_rectangle){
                    .min = {.x = rect_floats[0] + container->viewport_offset_x,
                            .y = rect_floats[1] + container->viewport_offset_y},
                    .max = {.x = rect_floats[2] + container->viewport_offset_x,
                            .y = rect_floats[3] + container->viewport_offset_y},
                });
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: child rect set");
        }
        {
            struct yetty_ycore_void_result set_r =
                yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_SET_RECT: {
        if (body_len != 16) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin SET_RECT: expected 16-byte payload");
        }
        float rect_floats[4];
        memcpy(rect_floats, body, 16);
        {
            struct yetty_ycore_void_result set_r =
                yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(container->base) - 1,
                                              (struct yetty_ycore_rectangle){
                                                  .min = {.x = rect_floats[0], .y = rect_floats[1]},
                                                  .max = {.x = rect_floats[2], .y = rect_floats[3]},
                                              });
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: base rect set");
        }
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                (struct yetty_yclass_object *)(container->base) - 1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_SET_CHILD_Z: {
        if (body_len != 4 + 4) {
            return YETTY_ERR(yetty_ycore_void, "container admin SET_CHILD_Z: bad payload size");
        }
        uint32_t child_id;
        int32_t z;
        memcpy(&child_id, body + 0, 4);
        memcpy(&z, body + 4, 4);
        struct yetty_yfigure_figure *child =
            yetty_yfigure_container_find_child_by_id(container, child_id);
        if (!child) {
            ydebug("container admin SET_CHILD_Z: id=%u not bound", child_id);
            return YETTY_OK_VOID();
        }
        if (yetty_yfigure_figure_z_get((struct yetty_yclass_object *)(child)-1).value != z) {
            {
                struct yetty_ycore_void_result set_r =
                    yetty_yfigure_figure_z_set((struct yetty_yclass_object *)(child)-1, z);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
            container->z_order_dirty = 1;
            {
                struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                    (struct yetty_yclass_object *)(container->base) - 1, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
        }
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_SET_CHILD_HIDDEN: {
        if (body_len != 4 + 4) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin SET_CHILD_HIDDEN: bad payload size");
        }
        uint32_t child_id;
        uint32_t hidden;
        memcpy(&child_id, body + 0, 4);
        memcpy(&hidden, body + 4, 4);
        struct yetty_yfigure_figure *child =
            yetty_yfigure_container_find_child_by_id(container, child_id);
        if (!child) {
            ydebug("container admin SET_CHILD_HIDDEN: id=%u not bound", child_id);
            return YETTY_OK_VOID();
        }
        int want = hidden ? 1 : 0;
        if (yetty_yfigure_figure_hidden_get((struct yetty_yclass_object *)(child)-1).value !=
            want) {
            {
                struct yetty_ycore_void_result set_r =
                    yetty_yfigure_figure_hidden_set((struct yetty_yclass_object *)(child)-1, want);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
            {
                struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                    (struct yetty_yclass_object *)(container->base) - 1, 1);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
            }
        }
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_SET_CHILD_SCROLL: {
        if (body_len != 4 + 8) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin SET_CHILD_SCROLL: bad payload size");
        }
        uint32_t child_id;
        float scroll[2];
        memcpy(&child_id, body + 0, 4);
        memcpy(scroll, body + 4, 8);
        struct yetty_yfigure_figure *child =
            yetty_yfigure_container_find_child_by_id(container, child_id);
        if (!child) {
            ydebug("container admin SET_CHILD_SCROLL: id=%u not bound", child_id);
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_int_result set_scroll_impl_r =
            figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_set_scroll);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_scroll_impl_r,
                            "container admin SET_CHILD_SCROLL: set_scroll support");
        if (!set_scroll_impl_r.value) {
            ydebug("container admin SET_CHILD_SCROLL: id=%u not scrollable", child_id);
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result sr = yetty_yfigure_set_scroll(
            NULL, ((struct yetty_yclass_object *)(child)-1), scroll[0], scroll[1]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "container admin SET_CHILD_SCROLL: set_scroll");
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                (struct yetty_yclass_object *)(container->base) - 1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
        return YETTY_OK_VOID();
    }

    case YETTY_YFIGURE_ADMIN_SET_CHILD_CONTENT_SIZE: {
        if (body_len != 4 + 8) {
            return YETTY_ERR(yetty_ycore_void,
                             "container admin SET_CHILD_CONTENT_SIZE: bad payload size");
        }
        uint32_t child_id;
        float content[2];
        memcpy(&child_id, body + 0, 4);
        memcpy(content, body + 4, 8);
        struct yetty_yfigure_figure *child =
            yetty_yfigure_container_find_child_by_id(container, child_id);
        if (!child) {
            ydebug("container admin SET_CHILD_CONTENT_SIZE: id=%u not bound", child_id);
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_int_result set_content_size_impl_r =
            figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_set_content_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_content_size_impl_r,
                            "container admin SET_CHILD_CONTENT_SIZE: set_content_size support");
        if (!set_content_size_impl_r.value) {
            ydebug("container admin SET_CHILD_CONTENT_SIZE: id=%u not scrollable", child_id);
            return YETTY_OK_VOID();
        }
        struct yetty_ycore_void_result sr = yetty_yfigure_set_content_size(
            NULL, ((struct yetty_yclass_object *)(child)-1), content[0], content[1]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sr,
                            "container admin SET_CHILD_CONTENT_SIZE: set_content_size");
        {
            struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                (struct yetty_yclass_object *)(container->base) - 1, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
        }
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
    struct yetty_yclass_void_ptr_result container_r =
        container_from_obj((struct yetty_yclass_object *)self - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_process_bytes: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    size_t off = 0;
    while (off < bytes_len) {
        if (bytes_len - off < sizeof(struct yetty_yfigure_header)) {
            return YETTY_ERR(yetty_ycore_void,
                             "container_process_bytes: trailing junk smaller than header");
        }
        struct yetty_yfigure_header hdr;
        memcpy(&hdr, bytes + off, sizeof(hdr));
        off += sizeof(hdr);
        if (hdr.length > bytes_len - off) {
            return YETTY_ERR(yetty_ycore_void,
                             "container_process_bytes: record length overruns buffer");
        }
        const uint8_t *payload = bytes + off;
        if (hdr.id == 0) {
            struct yetty_ycore_void_result ar = handle_admin_bytes(container, payload, hdr.length);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "container_process_bytes: admin");
        } else {
            struct yetty_yfigure_figure *child =
                yetty_yfigure_container_find_child_by_id(container, hdr.id);
            int child_handles_bytes = 0;
            if (child) {
                struct yetty_ycore_int_result process_bytes_impl_r =
                    figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, process_bytes_impl_r,
                                    "container_process_bytes: process_bytes support");
                child_handles_bytes = process_bytes_impl_r.value;
            }
            if (!child) {
                ydebug("container_process_bytes: routed id=%u not bound, skipping %u", hdr.id,
                       hdr.length);
            } else if (!child_handles_bytes) {
                ydebug("container_process_bytes: id=%u no process_bytes, skipping %u", hdr.id,
                       hdr.length);
            } else {
                struct yetty_ycore_void_result cr = yetty_yfigure_process_bytes(
                    NULL, ((struct yetty_yclass_object *)(child)-1), payload, hdr.length);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "container_process_bytes: child");
                {
                    struct yetty_ycore_void_result set_r =
                        yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
                }
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
    struct yetty_yclass_void_ptr_result container_r =
        container_from_obj((struct yetty_yclass_object *)self - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_process_input: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    return yetty_yfigure_container_consume_envelope(container, sm);
}

static struct yetty_ycore_char_ptr_result container_dump(const struct yetty_yfigure_figure *self,
                                                         int indent)
{
    struct yetty_yclass_void_ptr_result container_r =
        container_from_obj((struct yetty_yclass_object *)self - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, container_r, "container_dump: from_obj");
    const struct yetty_yfigure_container *container = container_r.value;
    char pad[64];
    dump_indent_spaces(pad, sizeof(pad), indent);
    size_t len = 0, cap = 0;
    char *buf = NULL;
    buf = dump_appendf(buf, &len, &cap, "%skind: container\n", pad);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    buf = dump_appendf(
        buf, &len, &cap, "%srect: [%.1f, %.1f, %.1f, %.1f]\n", pad,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.min.y,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.x,
        yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(self)-1).value.max.y);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    buf =
        dump_appendf(buf, &len, &cap, "%sdirty: %d\n", pad,
                     yetty_yfigure_figure_dirty_get((struct yetty_yclass_object *)(self)-1).value);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    buf = dump_appendf(buf, &len, &cap, "%sviewport_offset: [%.1f, %.1f]\n", pad,
                       container->viewport_offset_x, container->viewport_offset_y);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    /* Children section. The list is sorted by (z, insertion-seq) so the
     * dump lines come out back-to-front in true z-order, matching render
     * order — tests assert z-order from the line order. dump is
     * logically const, but re-sorting for deterministic output is a
     * benign internal reordering, so cast away const for the sort. */
    container_ensure_sorted((struct yetty_yfigure_container *)container);
    if (!container->children) {
        buf = dump_appendf(buf, &len, &cap, "%schildren: {}\n", pad);
        if (!buf) {
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
        }
        return YETTY_OK(yetty_ycore_char_ptr, buf);
    }
    buf = dump_appendf(buf, &len, &cap, "%schildren:\n", pad);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
    }
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        buf = dump_appendf(buf, &len, &cap, "%s  '%u':\n", pad, e->id);
        if (!buf) {
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
        }
        struct yetty_ycore_char_ptr_result child_dump_r = yetty_yfigure_dump(e->figure, indent + 4);
        if (YETTY_IS_ERR(child_dump_r)) {
            free(buf);
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: child dump", child_dump_r);
        }
        char *child_dump = child_dump_r.value;
        buf = dump_appendf(buf, &len, &cap, "%s", child_dump);
        free(child_dump);
        if (!buf) {
            return YETTY_ERR(yetty_ycore_char_ptr, "container_dump: out of memory");
        }
    }
    return YETTY_OK(yetty_ycore_char_ptr, buf);
}

/*===========================================================================
 * Public API
 *=========================================================================*/

/* Downcast from the yclass header to the container body. Callers that hold a
 * `yetty_yclass_object *` (e.g. from `yetty_yfigure_container_create`) use this
 * to reach the typed body for setter calls. It resolves the container's own
 * data slice via the class chain (yetty_yclass_object_data) — that is the
 * correct offset even when the figure base class occupies an earlier slice. A
 * raw `obj + 1` cast lands on the figure slice, not the container body, and
 * would read a bogus `base`. The Result from object_data has nowhere to
 * propagate through this exposed plain-pointer FFI signature, so a lookup miss
 * collapses to NULL at this boundary. */
[[clang::annotate("expose")]]
YETTY_EXTERNAL_CALLBACK struct yetty_yfigure_container *yetty_yfigure_container_from(
    struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    if (YETTY_IS_ERR(container_r)) {
        yetty_ycore_error_destroy(container_r.error);
        return NULL;
    }
    return container_r.value;
}

/* Setters for per-instance runtime state. These are owner-side
 * helpers — they take a body pointer (not a proxy), so they're only
 * meaningful on the side that hosts the actual container instance.
 * That side knows its `context` and `registry` from local C state;
 * neither pointer is meaningful across a wire. */
[[clang::annotate("expose")]]
void yetty_yfigure_container_set_registry(struct yetty_yfigure_container *container,
                                          struct yetty_yfigure_registry *registry)
{
    if (!container) {
        return;
    }
    container->registry = registry;
}

[[clang::annotate("expose")]]
void yetty_yfigure_container_set_context(struct yetty_yfigure_container *container,
                                         const struct yetty_context *context)
{
    if (!container) {
        return;
    }
    container->context = context;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yfigure_container_set_rect(
    struct yetty_yfigure_container *container, struct yetty_ycore_rectangle rect)
{
    if (!container) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_set_rect: NULL container");
    }
    struct yetty_ycore_void_result rect_r =
        yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(container->base) - 1, rect);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_r, "yfigure_container_set_rect: rect_set");
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
void yetty_yfigure_container_set_viewport_offset(struct yetty_yfigure_container *container,
                                                 float offset_x, float offset_y)
{
    if (!container) {
        return;
    }
    container->viewport_offset_x = offset_x;
    container->viewport_offset_y = offset_y;
}

[[clang::annotate("expose")]]
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
     * (not per-envelope) so very large payloads (e.container. an atlas) don't
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
        struct yetty_yfigure_header hdr;
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
        ydebug("consume_envelope: record id=%u length=%u", hdr.id, hdr.length);
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
        int child_handles_input = 0;
        if (child) {
            struct yetty_ycore_int_result process_input_impl_r =
                figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_process_input);
            if (YETTY_IS_ERR(process_input_impl_r)) {
                if (dump_fp) {
                    fclose(dump_fp);
                }
                return YETTY_ERR(yetty_ycore_void, "consume_envelope: process_input support",
                                 process_input_impl_r);
            }
            child_handles_input = process_input_impl_r.value;
        }
        if (child_handles_input) {
            if (dump_fp) {
                fwrite(&hdr, 1, sizeof(hdr), dump_fp);
                fwrite("<<streamed>>", 1, 12, dump_fp);
            }
            dr = yetty_yfigure_process_input(NULL, ((struct yetty_yclass_object *)(child)-1), sm);
            if (YETTY_IS_OK(dr)) {
                {
                    struct yetty_ycore_void_result set_r =
                        yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
                }
                {
                    struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                        (struct yetty_yclass_object *)(container->base) - 1, 1);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
                }
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
        int child_handles_bytes = 0;
        if (child) {
            struct yetty_ycore_int_result process_bytes_impl_r =
                figure_implements(child, (yetty_yclass_method_id_t)yetty_yfigure_process_bytes);
            if (YETTY_IS_ERR(process_bytes_impl_r)) {
                free(payload);
                if (dump_fp) {
                    fclose(dump_fp);
                }
                return YETTY_ERR(yetty_ycore_void, "consume_envelope: process_bytes support",
                                 process_bytes_impl_r);
            }
            child_handles_bytes = process_bytes_impl_r.value;
        }
        if (hdr.id == 0) {
            dr = handle_admin_bytes(container, payload, hdr.length);
        } else if (!child) {
            ydebug("consume_envelope: id=%u not bound, skipping %u bytes", hdr.id, hdr.length);
        } else if (!child_handles_bytes) {
            ydebug("consume_envelope: id=%u no process_bytes / process_input", hdr.id);
        } else {
            dr = yetty_yfigure_process_bytes(NULL, ((struct yetty_yclass_object *)(child)-1),
                                             payload, hdr.length);
            if (YETTY_IS_OK(dr)) {
                {
                    struct yetty_ycore_void_result set_r =
                        yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
                }
                {
                    struct yetty_ycore_void_result set_r = yetty_yfigure_figure_dirty_set(
                        (struct yetty_yclass_object *)(container->base) - 1, 1);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
                }
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

[[clang::annotate("expose")]]
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

[[clang::annotate("expose")]]
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

[[clang::annotate("expose")]]
struct yetty_yfigure_figure *yetty_yfigure_container_as_figure(
    struct yetty_yfigure_container *container)
{
    return container ? container->base : NULL;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yfigure_container_add_child(
    struct yetty_yfigure_container *container, struct yetty_yfigure_figure *child, uint32_t id)
{
    if (!container || !child) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: NULL arg");
    }
    if (id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: id=0 is reserved");
    }
    /* Boundary check — every figure is a yclass object carrying its class
     * header in self_obj; the container dispatches all of its ops (render,
     * destroy, process_input, process_bytes, reset_content, dump) through
     * yclass via that header. */
    if (!((struct yetty_yclass_object *)(child)-1)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yfigure_container_add_child: child has no yclass object");
    }
    struct child_entry *existing;
    HASH_FIND_INT(container->children, &id, existing);
    if (existing) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: id collision");
    }
    struct child_entry *e = (struct child_entry *)calloc(1, sizeof(*e));
    if (!e) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_add_child: oom");
    }
    e->id = id;
    e->figure = child;
    e->seq = container->next_seq++;
    HASH_ADD_INT(container->children, id, e);
    container->z_order_dirty = 1;
    {
        struct yetty_ycore_void_result set_r =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(child)-1, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, set_r, "container: figure attr set");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_yfigure_figure *yetty_yfigure_container_find_child_by_id(
    const struct yetty_yfigure_container *container, uint32_t id)
{
    if (!container || id == 0) {
        return NULL;
    }
    struct child_entry *e;
    HASH_FIND_INT(container->children, &id, e);
    return e ? e->figure : NULL;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yfigure_container *container, uint32_t id)
{
    if (!container) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_remove_child_by_id: NULL container");
    }
    if (id == 0) {
        return YETTY_OK_VOID();
    }
    struct child_entry *e;
    HASH_FIND_INT(container->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID(); /* stale delete from wire — benign */
    }
    HASH_DEL(container->children, e);
    struct yetty_ycore_void_result entry_r = entry_destroy(e);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, entry_r,
                        "yfigure_container_remove_child_by_id: child destroy failed");
    return YETTY_OK_VOID();
}

/* Mark an existing child as protected from clear_all (the CLEAR_ALL admin op
 * and the terminal's full-screen-erase / reset hook). The container's own
 * structural figures — the terminal content grid above all — use this so a
 * client emitting CLEAR_ALL to drop its figures cannot also wipe the host's
 * content. Idempotent; a stale id (already removed) is a benign no-op. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yfigure_container_protect_child(
    struct yetty_yfigure_container *container, uint32_t id)
{
    if (!container) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_protect_child: NULL container");
    }
    if (id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_protect_child: id=0 is reserved");
    }
    struct child_entry *e;
    HASH_FIND_INT(container->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID();
    }
    e->protected_from_clear = 1;
    return YETTY_OK_VOID();
}

static int yetty_yfigure_container_for_each(struct yetty_yfigure_container *container,
                                            container_visitor_fn fn, void *user)
{
    if (!container || !fn) {
        return 0;
    }
    container_ensure_sorted(container);
    struct child_entry *e, *tmp;
    HASH_ITER(hh, container->children, e, tmp)
    {
        int rc = fn(e->id, e->figure, user);
        if (rc) {
            return rc;
        }
    }
    return 0;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yfigure_container *container, uint32_t id)
{
    if (!container) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_container_raise_child_by_id: NULL container");
    }
    if (id == 0) {
        return YETTY_OK_VOID();
    }
    struct child_entry *e;
    HASH_FIND_INT(container->children, &id, e);
    if (!e) {
        return YETTY_OK_VOID();
    }
    /* uthash insertion order is the z-order. Raising to top = delete +
     * re-insert with the same id — the entry pointer stays valid, the
     * child isn't destroyed. */
    HASH_DEL(container->children, e);
    HASH_ADD_INT(container->children, id, e);
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
    /* Pane origin in target pixels (== container viewport offset). Used to
     * report pane-local coords for absolute-coords figures. */
    float viewport_offset_x;
    float viewport_offset_y;
    struct yetty_yfigure_hit hit;
};

YETTY_EXTERNAL_CALLBACK
static int hit_visit(uint32_t id, struct yetty_yfigure_figure *child, void *user)
{
    struct hit_visitor_state *st = user;
    if (yetty_yfigure_figure_hidden_get((struct yetty_yclass_object *)(child)-1).value) {
        return 0;
    }
    if (st->x <
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.min.x ||
        st->x >=
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.max.x ||
        st->y <
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.min.y ||
        st->y >=
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value.max.y) {
        return 0;
    }
    /* uthash walks insertion order = back-to-front z-order. The front-most
     * figure must win, so we keep overwriting — the last match in the
     * walk is the top of the z-stack. Returning early on first match
     * would make decoration figures (e.container. shader-glyph at 0xFFFFFFFE,
     * inserted at terminal create) steal hits from interactive figures
     * (ygreeter / ygui chrome) inserted later. */
    st->hit.figure_id = id;
    if (yetty_yfigure_figure_absolute_coords_get((struct yetty_yclass_object *)(child)-1).value) {
        /* Absolute-coords figure (ygui chrome / scrolling sub-figures): its
         * content is laid out and hit-tested in pane-root space and merely
         * scissor-clipped to the rect — no per-figure re-origin. Report the
         * cursor in that same pane-local space so an offset sub-figure (a
         * scrollarea below a tabbar) maps to the widget under the pointer.
         * A full-pane chrome figure has rect.min == viewport offset, so this
         * is identical to the local path for it. */
        st->hit.local_x = st->x - st->viewport_offset_x;
        st->hit.local_y = st->y - st->viewport_offset_y;
    } else {
        /* Local-coords producer figure (yplot/yimage/…): content drawn from
         * the figure's own origin; re-origin the cursor to its rect. */
        struct yetty_ycore_rectangle rect =
            yetty_yfigure_figure_rect_get((struct yetty_yclass_object *)(child)-1).value;
        st->hit.local_x = st->x - rect.min.x;
        st->hit.local_y = st->y - rect.min.y;
    }
    return 0;
}

[[clang::annotate("expose")]]
struct yetty_yfigure_hit yetty_yfigure_container_hit_test(struct yetty_yfigure_container *container,
                                                          float x, float y)
{
    struct hit_visitor_state st = {.x = x,
                                   .y = y,
                                   .viewport_offset_x = container->viewport_offset_x,
                                   .viewport_offset_y = container->viewport_offset_y,
                                   .hit = {0, 0, 0}};
    yetty_yfigure_container_for_each(container, hit_visit, &st);
    return st.hit;
}

/*===========================================================================
 * yclass slot overrides
 *
 * One wrapper impl per container public method whose signature is
 * wire-marshallable. Skipped (signature incompatible — these stay as
 * direct C calls on the body pointer):
 *   - consume_envelope, process_input       — wire-statemachine ptr
 *   - find_child_by_id, as_figure           — pointer return
 *   - hit_test                              — non-Result struct return
 *   - for_each                              — function pointer arg
 *   - set_viewport_offset / _rect / etc.    — owner-side setters
 *
 * Every container instance — local OR remote-proxy — has the yclass
 * `struct yetty_yclass_object` header at offset 0 followed by the
 * `struct yetty_yfigure_container` body. Allocation goes through
 * `yetty_yclass_object_alloc`; the body is set up by the constructor
 * slot below. */

/* yclass instance layout: yclass_object header at offset 0, user data
 * (the `struct yetty_yfigure_container` body) immediately after. Cast
 * via (obj + 1) advances past the header in pointer arithmetic. */
[[clang::annotate("override@yfigure:container:constructor")]]
static struct yetty_ycore_void_result yetty_yfigure_container_constructor_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    /* Set up class-intrinsic state — ops vtable. Per-instance state
     * (rect, context, registry, viewport_offset) is left zero-initialized
     * here and is wired up by the owner via the public setters below.
     * The yclass header sits in front of the body; YCLASS_TO_CONTAINER
     * advances past it. */
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_constructor: from_obj");
    struct yetty_yfigure_container *container = container_r.value;
    container->base = (struct yetty_yfigure_figure *)(obj + 1);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yfigure:container:add_child")]]
static struct yetty_ycore_void_result yetty_yfigure_container_add_child_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_yfigure_figure *child, uint32_t id)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_add_child: from_obj");
    return yetty_yfigure_container_add_child(container_r.value, child, id);
}

[[clang::annotate("override@yfigure:container:remove_child_by_id")]]
static struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, uint32_t id)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_remove_child_by_id: from_obj");
    return yetty_yfigure_container_remove_child_by_id(container_r.value, id);
}

[[clang::annotate("override@yfigure:container:raise_child_by_id")]]
static struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, uint32_t id)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_raise_child_by_id: from_obj");
    return yetty_yfigure_container_raise_child_by_id(container_r.value, id);
}

[[clang::annotate("override@yfigure:container:process_records")]]
static struct yetty_ycore_void_result yetty_yfigure_container_process_records_impl(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, struct yetty_ycore_buffer bytes)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result container_r = container_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, container_r, "container_process_records: from_obj");
    return yetty_yfigure_container_process_records(container_r.value, bytes.data, bytes.size);
}

[[clang::annotate("override@yfigure:container:process_input")]]
static struct yetty_ycore_void_result container_process_input_slot(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ywire_wire_statemachine *statemachine)
{
    (void)ctx;
    return container_process_input((struct yetty_yfigure_figure *)(obj + 1), statemachine);
}

[[clang::annotate("override@yfigure:container:process_bytes")]]
static struct yetty_ycore_void_result container_process_bytes_slot(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj,
                                                                   const uint8_t *bytes,
                                                                   size_t bytes_len)
{
    (void)ctx;
    return container_process_bytes((struct yetty_yfigure_figure *)(obj + 1), bytes, bytes_len);
}

[[clang::annotate("override@yfigure:container:dump_state")]]
static struct yetty_ycore_char_ptr_result container_dump_state_slot(struct yetty_yclass_ctx *ctx,
                                                                    struct yetty_yclass_object *obj,
                                                                    int indent)
{
    (void)ctx;
    return container_dump((struct yetty_yfigure_figure *)(obj + 1), indent);
}

#include "container.gen.c"
