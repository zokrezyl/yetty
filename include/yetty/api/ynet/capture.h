/* GENERATED — do not edit. */
/* Object API for regular class(es) `capture` (implementation module: ynet).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YNET_CAPTURE_H
#define YETTY_YCLASSGEN_API_YNET_CAPTURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ynet_capture;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YNET_CAPTURE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YNET_CAPTURE_PTR_RESULT
struct yetty_ynet_capture_ptr_result {
    int ok;
    union {
        struct yetty_ynet_capture *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ynet_capture_ptr_result yetty_ynet_capture_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ynet_capture_to(struct yetty_ynet_capture *data);

/* Load a pcap / pcapng file. Reads every packet, dissects it for the summary
 * columns + 5-tuple, copies its bytes into the arena, and folds it into the
 * flow table. Uses libpcap offline reading — no root, no live interface. */
struct yetty_ycore_void_result yetty_ynet_load_file(struct yetty_yclass_object *obj,
                                                    const char *path);
struct yetty_ycore_uint32_result yetty_ynet_packet_count(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_ynet_packet_time(struct yetty_yclass_object *obj,
                                                       uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_packet_length(struct yetty_yclass_object *obj,
                                                          uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_protocol(struct yetty_yclass_object *obj,
                                                                    uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_source(struct yetty_yclass_object *obj,
                                                                  uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_destination(
    struct yetty_yclass_object *obj, uint32_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynet_packet_info(struct yetty_yclass_object *obj,
                                                                uint32_t index);
struct yetty_ycore_const_uint8_ptr_result yetty_ynet_packet_bytes(struct yetty_yclass_object *obj,
                                                                  uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_packet_caplen(struct yetty_yclass_object *obj,
                                                          uint32_t index);
struct yetty_ycore_uint32_result yetty_ynet_flow_count(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynet_flow_summary(struct yetty_yclass_object *obj,
                                                                 uint32_t index);
/* Render the capture's conversations as a topology figure. `width`/`height` are
 * the figure's pixel size (0 → defaults). Returns a ydraw drawable list the
 * caller emits and then destroys. */
struct yetty_ydraw_drawable_list_result yetty_ynet_render(struct yetty_yclass_object *obj,
                                                          uint32_t width, uint32_t height);
struct yetty_ycore_void_result yetty_ynet_destroy(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_ynet_capture_create(struct yetty_yclass_ctx *ctx);

/* Serialize a rendered drawable list as a YDRAW_BIN DCS envelope on `fd` — the
 * scrolling-layer figure path (mirrors yflame's emit_osc). */
struct yetty_ycore_void_result yetty_ynet_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                   int fd);

#ifdef __cplusplus
}
#endif

#endif
