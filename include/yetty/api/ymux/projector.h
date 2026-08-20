/* GENERATED — do not edit. */
/* Object API for regular class(es) `projector` (implementation module: ymux).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YMUX_PROJECTOR_H
#define YETTY_YCLASSGEN_API_YMUX_PROJECTOR_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_buffer;

/* The projector — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_projector_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_projector;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_PROJECTOR_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_PROJECTOR_PTR_RESULT
struct yetty_ymux_projector_ptr_result {
    int ok;
    union {
        struct yetty_ymux_projector *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_projector_ptr_result yetty_ymux_projector_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_projector_to(struct yetty_ymux_projector *data);

struct yetty_yclass_object_ptr_result yetty_ymux_projector_create(struct yetty_yclass_ctx *ctx);

struct yetty_yclass_object_ptr_result yetty_ymux_projector_make(
    struct yetty_yclass_object *pane, struct yetty_yclass_object *attachment);
/* Consume RAW terminal-response bytes from this attachment's renderer
 * (review #16/#17): a STREAMING state machine — arbitrary fragmentation,
 * arbitrary length (growable body, bounded at 64 KB per sequence). Raw
 * bytes are never re-encoded; this is the response CONSUMER. */
struct yetty_ycore_void_result yetty_ymux_projector_consume_tty_response(
    struct yetty_yclass_object *obj, const uint8_t *bytes, uint32_t byte_count);
/* The decoded XTGETTCAP capability table: entry `index` copied out as
 * "name=value"; returns the entry count. */
struct yetty_ycore_uint32_result yetty_ymux_projector_response_cap(struct yetty_yclass_object *obj,
                                                                   uint32_t cap_index,
                                                                   char *out_text,
                                                                   uint32_t out_capacity);
/* Response-state observers: (da_count << 40) | (cpr_count << 16) | row<<8 | col
 * would be cramped — expose the counts and last CPR separately. */
struct yetty_ycore_uint64_result yetty_ymux_projector_response_state(
    struct yetty_yclass_object *obj);
/* Record the attachment's terminfo capability profile (YMUX_TERM_CAP_*); the
 * VT colour path reads it to choose truecolor passthrough vs an RGB->256
 * downgrade. Set once from the attach handshake. */
struct yetty_ycore_void_result yetty_ymux_projector_set_capabilities(
    struct yetty_yclass_object *obj, uint32_t capabilities);
/* tmux's terminfo/features STATE MODEL entry (review #17 item 8): resolve
 * the client's TERM name + features string through the tty-features
 * pipeline (family base -> TERM-implied defaults -> explicit additions)
 * instead of a pre-chewed capability bitmask. The bitmask path above stays
 * as the legacy/override input; this is what an attach carrying TERM
 * strings uses. The ATTACH_PREAMBLE toggle stays with the mask path (a
 * client-mode choice, not a terminfo property). */
struct yetty_ycore_void_result yetty_ymux_projector_set_terminal(struct yetty_yclass_object *obj,
                                                                 const char *term_name,
                                                                 const char *features);
/* The negotiated capability mask (post terminal-strings resolution when the
 * attach named its terminal). */
struct yetty_ycore_uint32_result yetty_ymux_projector_capabilities(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_projector_dispose(struct yetty_yclass_object *obj);
/* Force the next projection to be a FULL (client resync request, transport
 * loss, geometry change). The rich half resends too. */
struct yetty_ycore_void_result yetty_ymux_projector_invalidate(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymux_projector_project_rich(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_buffer *out);
struct yetty_ycore_void_result yetty_ymux_projector_project_vt(struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer *out);

#ifdef __cplusplus
}
#endif

#endif
