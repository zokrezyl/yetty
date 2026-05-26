/*
 * ygui-internal.h — private types shared across the ygui implementation.
 *
 * Held to the .c files (not installed). Exposes:
 *   - struct yetty_ygui_class (full definition; public API is opaque)
 *   - struct yetty_ygui_object (full definition; public API is opaque)
 *   - struct yetty_ygui_runtime (full definition; public API is opaque)
 *
 * Out-of-file callers go through the public API — including this
 * header from a widget or test .c is acceptable for unit tests that
 * need to poke at internals, but production widget code should stick
 * to public accessors / setters.
 */
#ifndef YETTY_YGUI_INTERNAL_H
#define YETTY_YGUI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/framework.h>
#include <yetty/ygui/event.h>
#include <yetty/ygui/object.h>
#include <yetty/ygui/widget.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Class — per-instance offset map + dispatch table.
 *=========================================================================*/

/* Map entry: a class pointer (own / parent-chain / mixin) and the byte
 * offset from the object header where its data slice lives. Linear
 * scan; classes have at most a handful of slots so this stays cache-
 * friendly compared to a hash table. */
struct yetty_ygui_data_slot {
    const struct yetty_ygui_class *cls;
    size_t offset;
};

struct yetty_ygui_class {
    const struct yetty_ygui_class_descriptor *desc;
    const struct yetty_ygui_class *parent;

    /* Heap-allocated, owned by class. */
    const struct yetty_ygui_class **mixins;
    size_t mixin_count;

    /* Per-class data slice offset map. Includes self, every class in the
     * parent chain (including parent's mixins, transitively), and every
     * mixin of this class. */
    struct yetty_ygui_data_slot *slots;
    size_t slot_count;

    /* Total bytes for one instance of this class (object header + every
     * reachable data slice). */
    size_t instance_size;

    /* Dispatch table: index = method_slot, value = implementation fn or
     * NULL. Sized to the framework's slot count at registration time. */
    yetty_ygui_impl_t *dispatch;
    size_t dispatch_count;
};

/*===========================================================================
 * Object — runtime widget instance.
 *=========================================================================*/

struct yetty_ygui_event_subscription {
    enum yetty_ygui_event_type type;
    yetty_ygui_event_cb cb;
    void *userdata;
    struct yetty_ygui_event_subscription *next;
};

struct yetty_ygui_object {
    const struct yetty_ygui_class *klass;
    struct yetty_ygui_object *parent;

    /* Sibling links inside parent->first_child list. */
    struct yetty_ygui_object *first_child;
    struct yetty_ygui_object *next_sibling;

    /* Wire id allocated by the engine at construction. 0 = unassigned. */
    uint32_t id;

    /* Dirty flag — content changed without geometry move. */
    int dirty;

    /* Hover state — set by the framework's pointer-tracking pass when
     * this widget is the deepest hit; cleared when the mouse leaves.
     * Widgets read it via yetty_ygui_object_is_hovered() to paint a
     * hover variant. */
    int hovered;

    /* Engine that owns this widget tree. Stored only on the root; child
     * widgets resolve via parent walk through yetty_ygui_object_engine. */
    struct yetty_ygui_runtime *engine;

    /* Event subscriptions — singly-linked list, freed at object destroy. */
    struct yetty_ygui_event_subscription *subscriptions;

    /* Per-class data slices follow this header in memory. yetty_ygui_data_get
     * resolves the requested class's offset and returns this + offset. */
};

/*===========================================================================
 * Engine.
 *=========================================================================*/

/* Byte-stream input decoder state. ASCII pass-through; ESC starts a
 * CSI escape sequence that ends on a final byte and decodes to one of
 * the YETTY_YGUI_KEY_* codes. */
enum yetty_ygui_csi_state {
    YETTY_YGUI_CSI_NORMAL = 0,
    YETTY_YGUI_CSI_ESC,
    YETTY_YGUI_CSI_BRACKET,
};

struct yetty_ygui_input_state {
    enum yetty_ygui_csi_state st;
    char params[16];
    int params_len;
};

struct yetty_ygui_runtime {
    /* Borrowed — caller owns the lifetime. */
    struct yetty_platform_pty *output_pty;

    uint32_t next_id;
    uint32_t *free_ids;
    size_t free_id_count;
    size_t free_id_cap;

    /* Pending deletes — ids whose receiver-side figures need to go
     * away on the next envelope. */
    uint32_t *pending_deletes;
    size_t pending_delete_count;
    size_t pending_delete_cap;

    /* Receiver-side ygrid id. Primitive widgets share this one ygrid;
     * figure widgets emit their own CREATE_CHILD records with their
     * own ids alongside it. */
    uint32_t ygrid_id;
    int ygrid_created;

    /* Set of figure ids that have already been minted on the receiver
     * via CREATE_CHILD. Each frame: figure widgets check this set; if
     * their id is present they emit SET_CHILD_RECT (cheap rect update);
     * otherwise they emit CREATE_CHILD and add themselves to the set.
     *
     * The set is a sorted dense array kept small — figure widgets are
     * rare (a handful per app). free_id drops the id back out. */
    uint32_t *minted_figures;
    size_t minted_figure_count;
    size_t minted_figure_cap;

    struct yetty_ygui_object *root;

    /* Viewport in pixels — root widget bounds for the next layout
     * pass. Defaults to 800x600 until set_viewport is called. */
    float viewport_w;
    float viewport_h;

    /* Engine-level dirty flag. Cleared by emit. */
    int dirty;

    /* Reusable per-emit buffers. Cleared at the start of each emit. */
    struct yetty_ycore_buffer container_records;
    struct yetty_ycore_buffer figure_bodies;
    /* Shared ydraw draw_list — primitive widgets append SDF / glyph
     * records here. Lazily created on first emit; reused across
     * frames. */
    struct yetty_ydraw_draw_list *ygrid_draw_list;

    /* Byte-stream input decoder state. */
    struct yetty_ygui_input_state input;

    /* App-level key callback. */
    yetty_ygui_key_cb key_cb;
    void *key_userdata;

    /* Deepest widget currently under the mouse, tracked by
     * feed_mouse_motion. Used to dispatch enter/leave + flip the
     * obj->hovered flag so widgets can paint a hover variant. */
    struct yetty_ygui_object *hovered_obj;
};

/*===========================================================================
 * Internal helpers.
 *=========================================================================*/

/* Build the framework's own ygrid record if not yet created.
 * Idempotent — flips ygrid_created on first call. */
struct yetty_ycore_void_result yetty_ygui_framework_ensure_chrome(struct yetty_ygui_runtime *engine,
                                                                  struct yetty_ygui_emit_ctx *ctx);

/* Walk the tree invoking emit_container on every widget (pre-order). */
struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_container(
    struct yetty_ygui_object *node, struct yetty_ygui_emit_ctx *ctx);

/* Walk the tree invoking emit_body on every widget (pre-order). */
struct yetty_ycore_void_result yetty_ygui_framework_walk_emit_body(struct yetty_ygui_object *node,
                                                                struct yetty_ygui_emit_ctx *ctx);

/* Flush the three streams into the output pty as one yface envelope.
 * The envelope body is a sequence of {length, id, payload} records:
 *   - container_records appear with id = ENGINE'S CONTAINER ID and the
 *     bytes are admin-record payloads
 *   - ygrid_body appears with id = ygrid_id wrapped in one record
 *   - figure_bodies are already record-framed inside the buffer
 *     (engine wraps each figure's body at append time using
 *     current_figure_id). */
struct yetty_ycore_void_result yetty_ygui_framework_flush(struct yetty_ygui_runtime *engine);

/* Append one {length, id, payload} record to `dst`. */
struct yetty_ycore_void_result yetty_ygui_wire_append_record(struct yetty_ycore_buffer *dst,
                                                             uint32_t id, const uint8_t *payload,
                                                             uint32_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_INTERNAL_H */
