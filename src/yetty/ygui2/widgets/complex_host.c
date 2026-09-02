/*
 * ygui2 complex_host — the T5 carrier: hosts one own-id COMPLEX node
 * (plot / video / image / shader) inside the widget's group. The creation
 * record ships once (CMD_NODE_ID + the complex envelope); data then
 * STREAMS via addressed updates (CMD_PATH + UPDATE) without ever
 * re-sending the record — the drawable contract's headline win.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>

#include "yetty/gen/impl/ygui2/widget.h"

/* Cross-class within-module: declared by hand (the generated framework
 * header exists only after codegen; the accessor pattern of the module). */
struct yetty_ycore_void_result yetty_ygui2_framework_stream_update(
    struct yetty_yclass_object *widget_obj, uint32_t child_node_id, const void *payload,
    size_t payload_size);

YETTY_YRESULT_DECLARE(yetty_ygui2_complex_host_ptr, struct yetty_ygui2_complex_host *);
struct yetty_yclass_ptr_result yetty_ygui2_complex_host_class_get(void);
struct yetty_ygui2_complex_host_ptr_result yetty_ygui2_complex_host_from(
    struct yetty_yclass_object *obj);

enum { YGUI2_COMPLEX_RECORD_MAX_WORDS = 65536 };

struct YETTY_ANNOTATE("class@ygui2:complex_host") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_complex_host {
    uint32_t *record_words; /* owned copy of the creation envelope */
    uint32_t record_word_count;
    uint32_t child_node_id; /* the complex's OWN id inside this group */
};

/* RETAINED hook — the creation record lives in the CONTAINMENT group, so
 * skin repaints / theme restyles / ancestor resizes never replace the
 * hosted runtime; only an intentional set_record does. */
YETTY_ANNOTATE("override@ygui2:widget:widget_paint_retained")
static struct yetty_ycore_void_result complex_host_paint(struct yetty_yclass_object *obj,
                                                         struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_complex_host_ptr_result data_res = yetty_ygui2_complex_host_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 complex_host paint: data");
    struct yetty_ygui2_complex_host *host = data_res.value;
    if (!host->record_words || host->record_word_count == 0) {
        return YETTY_OK_VOID();
    }
    if (host->child_node_id) {
        struct yetty_ycore_void_result id_res =
            yetty_ydraw_drawable_list_add_cmd_node_id(list, host->child_node_id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "ygui2 complex_host paint: node id");
    }
    struct yetty_ydraw_id_result record_res = yetty_ydraw_drawable_list_add_prim(
        list, host->record_words, (size_t)host->record_word_count * sizeof(uint32_t));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, record_res, "ygui2 complex_host paint: record");
    return YETTY_OK_VOID();
}

/* Install the creation envelope (copied) + the complex's own id. Marks the
 * skin dirty: the record ships (or re-ships, resetting the runtime) with
 * the next emit. */
/* The complex type space (>= COMPLEX_TYPE_BASE) COLLIDES with the command
 * constants (cmds.h documents the namespace overlap and requires exact
 * command matching before complex dispatch) — a record whose type word is
 * a command is a producer instruction, never a complex. */
static int complex_type_is_command(uint32_t type)
{
    switch (type) {
    case YETTY_YDRAW_CMD_DELETE:
    case YETTY_YDRAW_CMD_UPDATE:
    case YETTY_YDRAW_CMD_GROUP:
    case YETTY_YDRAW_CMD_GROUP_REF:
    case YETTY_YDRAW_CMD_PAINT_Z:
    case YETTY_YDRAW_CMD_PAINT_Z_END:
    case YETTY_YDRAW_CMD_NODE_ID:
    case YETTY_YDRAW_CMD_PATH:
    case YETTY_YDRAW_CMD_RESERVE:
        return 1;
    default:
        return 0;
    }
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_complex_host_set_record(struct yetty_yclass_object *obj,
                                                                   const uint32_t *words,
                                                                   uint32_t word_count,
                                                                   uint32_t child_node_id)
{
    struct yetty_ygui2_complex_host_ptr_result data_res = yetty_ygui2_complex_host_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 complex_host_set_record: data");
    struct yetty_ygui2_complex_host *host = data_res.value;
    if (!words || word_count < 2u || word_count > YGUI2_COMPLEX_RECORD_MAX_WORDS) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 complex_host_set_record: bad record");
    }
    /* Containment: EXACTLY ONE well-formed complex creation record —
     * [type][payload_size][payload], type in the complex space and not a
     * command constant, payload spanning the remaining words exactly (no
     * trailing records to splice into the group body). */
    if (!yetty_ydraw_is_complex(words[0]) || complex_type_is_command(words[0])) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 complex_host_set_record: type is not a complex");
    }
    if ((size_t)words[1] != (size_t)(word_count - 2u) * sizeof(uint32_t)) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 complex_host_set_record: payload size mismatch");
    }
    if (child_node_id == 0u) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui2 complex_host_set_record: child id 0 is not addressable");
    }
    uint32_t *copy = malloc((size_t)word_count * sizeof(uint32_t));
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 complex_host_set_record: alloc");
    }
    memcpy(copy, words, (size_t)word_count * sizeof(uint32_t));
    free(host->record_words);
    host->record_words = copy;
    host->record_word_count = word_count;
    host->child_node_id = child_node_id;
    /* Replacing the creation record intentionally replaces the runtime —
     * a structural reopen of this widget, not a skin repaint. */
    return yetty_ygui2_widget_mark_structure_dirty(obj);
}

/* Stream a runtime payload to the hosted complex: ONE addressed update on
 * the wire (its own tiny envelope, shipped immediately) — no repaint, no
 * reopen, the geometry stays frozen. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_complex_host_stream(struct yetty_yclass_object *obj,
                                                               const void *payload,
                                                               size_t payload_size)
{
    struct yetty_ygui2_complex_host_ptr_result data_res = yetty_ygui2_complex_host_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 complex_host_stream: data");
    struct yetty_ygui2_complex_host *host = data_res.value;
    if (!host->child_node_id) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 complex_host_stream: no child id");
    }
    return yetty_ygui2_framework_stream_update(obj, host->child_node_id, payload, payload_size);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_cleanup")
static struct yetty_ycore_void_result complex_host_cleanup(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_complex_host_ptr_result data_res = yetty_ygui2_complex_host_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 complex_host cleanup: data");
    free(data_res.value->record_words);
    data_res.value->record_words = NULL;
    data_res.value->record_word_count = 0;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/complex_host.c"
