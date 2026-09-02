/* GENERATED — do not edit. */
/* Object API for regular class(es) `complex_host` (implementation module: ygui2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI2_WIDGETS_COMPLEX_HOST_H
#define YETTY_YCLASSGEN_API_YGUI2_WIDGETS_COMPLEX_HOST_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui2_complex_host_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_complex_host;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_COMPLEX_HOST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_COMPLEX_HOST_PTR_RESULT
struct yetty_ygui2_complex_host_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_complex_host *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_complex_host_ptr_result yetty_ygui2_complex_host_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_complex_host_to(
    struct yetty_ygui2_complex_host *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_complex_host_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_complex_host_set_record(struct yetty_yclass_object *obj,
                                                                   const uint32_t *words,
                                                                   uint32_t word_count,
                                                                   uint32_t child_node_id);
/* Stream a runtime payload to the hosted complex: ONE addressed update on
 * the wire (its own tiny envelope, shipped immediately) — no repaint, no
 * reopen, the geometry stays frozen. */
struct yetty_ycore_void_result yetty_ygui2_complex_host_stream(struct yetty_yclass_object *obj,
                                                               const void *payload,
                                                               size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif
