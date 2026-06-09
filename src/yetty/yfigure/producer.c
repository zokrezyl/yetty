/*
 * producer.c — generic figure-tree wire producer (see producer.h).
 *
 * Plain C, no yclass annotations: a thin transport object that turns
 * emit_* calls into the wire record stream defined in wire.h and ships
 * it down a pty inside the shared YCOMPOSITOR_BIN yface envelope — the
 * exact format the hosting container's process_records consumes.
 *
 * The little-endian record writers below intentionally mirror the ones
 * in src/yetty/ygui/framework.c. That copy predates this module and is
 * still used by the ygui widget emit path; folding it onto this producer
 * is a follow-up dedup, tracked separately.
 */
#include <yetty/yfigure/producer.h>

#include <yetty/yface/yface.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdlib.h>
#include <string.h>

struct yetty_yfigure_producer {
    struct yetty_platform_pty *output_pty; /* borrowed */
    struct yetty_ycore_buffer records;     /* accumulated, flushed as one envelope */
};

/*===========================================================================
 * Little-endian wire writers.
 *=========================================================================*/

static struct yetty_ycore_void_result write_uint32_le(struct yetty_ycore_buffer *destination,
                                                      uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)(value & 0xff), (uint8_t)((value >> 8) & 0xff),
                        (uint8_t)((value >> 16) & 0xff), (uint8_t)((value >> 24) & 0xff)};
    return yetty_ycore_buffer_write(destination, bytes, sizeof(bytes));
}

static struct yetty_ycore_void_result write_float32_le(struct yetty_ycore_buffer *destination,
                                                       float value)
{
    /* Both host and wire are little-endian; this is a reinterpret + copy. */
    uint32_t raw_bits;
    memcpy(&raw_bits, &value, sizeof(raw_bits));
    return write_uint32_le(destination, raw_bits);
}

/* Append a top-level record: u32 length | u32 id | payload[length]. */
static struct yetty_ycore_void_result append_record(struct yetty_ycore_buffer *destination,
                                                    uint32_t id, const uint8_t *payload,
                                                    uint32_t payload_len)
{
    struct yetty_ycore_void_result write_result = write_uint32_le(destination, payload_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "producer append_record: length");
    write_result = write_uint32_le(destination, id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "producer append_record: id");
    if (payload_len > 0 && payload) {
        write_result = yetty_ycore_buffer_write(destination, payload, payload_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "producer append_record: payload");
    }
    return YETTY_OK_VOID();
}

/* Append an admin record (id=0): payload = u32 admin_op | body. */
static struct yetty_ycore_void_result append_admin_record(struct yetty_ycore_buffer *destination,
                                                          uint32_t admin_op, const uint8_t *body,
                                                          uint32_t body_len)
{
    uint32_t payload_len = 4 + body_len;
    struct yetty_ycore_void_result write_result = write_uint32_le(destination, payload_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "producer append_admin: length");
    write_result = write_uint32_le(destination, 0u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "producer append_admin: id=0");
    write_result = write_uint32_le(destination, admin_op);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "producer append_admin: admin_op");
    if (body_len > 0 && body) {
        write_result = yetty_ycore_buffer_write(destination, body, body_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "producer append_admin: body");
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Lifecycle.
 *=========================================================================*/

struct yetty_yfigure_producer_ptr_result yetty_yfigure_producer_create(
    struct yetty_platform_pty *output_pty)
{
    if (!output_pty) {
        return YETTY_ERR(yetty_yfigure_producer_ptr,
                         "yfigure_producer_create: output_pty required");
    }
    struct yetty_yfigure_producer *producer = calloc(1, sizeof(*producer));
    if (!producer) {
        return YETTY_ERR(yetty_yfigure_producer_ptr, "yfigure_producer_create: alloc");
    }
    producer->output_pty = output_pty;
    return YETTY_OK(yetty_yfigure_producer_ptr, producer);
}

struct yetty_ycore_void_result yetty_yfigure_producer_destroy(
    struct yetty_yfigure_producer *producer)
{
    if (!producer) {
        return YETTY_OK_VOID();
    }
    yetty_ycore_buffer_destroy(&producer->records);
    free(producer);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Emit — accumulate records until flush().
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yfigure_producer_create_child(
    struct yetty_yfigure_producer *producer, uint32_t child_id, uint32_t kind,
    struct yetty_ycore_rectangle rect, const uint8_t *init_bytes, uint32_t init_len)
{
    if (!producer) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_create_child: NULL producer");
    }
    /* Body: u32 child_id | u32 kind | f32x4 rect | u32 init_len | bytes init. */
    struct yetty_ycore_buffer body = {0};
    struct yetty_ycore_void_result write_result = write_uint32_le(&body, child_id);
    if (YETTY_IS_OK(write_result)) {
        write_result = write_uint32_le(&body, kind);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.min.x);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.min.y);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.max.x);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.max.y);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_uint32_le(&body, init_len);
    }
    if (YETTY_IS_OK(write_result) && init_len > 0 && init_bytes) {
        write_result = yetty_ycore_buffer_write(&body, init_bytes, init_len);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = append_admin_record(&producer->records, YETTY_YFIGURE_ADMIN_CREATE_CHILD,
                                            body.data, (uint32_t)body.size);
    }
    yetty_ycore_buffer_destroy(&body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "yfigure_producer_create_child");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_producer_figure_body(
    struct yetty_yfigure_producer *producer, uint32_t child_id, const uint8_t *bytes, uint32_t len)
{
    if (!producer) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_figure_body: NULL producer");
    }
    if (child_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_figure_body: child_id is 0");
    }
    struct yetty_ycore_void_result append_result =
        append_record(&producer->records, child_id, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, append_result, "yfigure_producer_figure_body");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_producer_set_child_z(
    struct yetty_yfigure_producer *producer, uint32_t child_id, int32_t z)
{
    if (!producer) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_set_child_z: NULL producer");
    }
    struct yetty_ycore_buffer body = {0};
    struct yetty_ycore_void_result write_result = write_uint32_le(&body, child_id);
    if (YETTY_IS_OK(write_result)) {
        write_result = write_uint32_le(&body, (uint32_t)z);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = append_admin_record(&producer->records, YETTY_YFIGURE_ADMIN_SET_CHILD_Z,
                                            body.data, (uint32_t)body.size);
    }
    yetty_ycore_buffer_destroy(&body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "yfigure_producer_set_child_z");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_producer_set_child_rect(
    struct yetty_yfigure_producer *producer, uint32_t child_id, struct yetty_ycore_rectangle rect)
{
    if (!producer) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_set_child_rect: NULL producer");
    }
    struct yetty_ycore_buffer body = {0};
    struct yetty_ycore_void_result write_result = write_uint32_le(&body, child_id);
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.min.x);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.min.y);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.max.x);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = write_float32_le(&body, rect.max.y);
    }
    if (YETTY_IS_OK(write_result)) {
        write_result = append_admin_record(&producer->records, YETTY_YFIGURE_ADMIN_SET_CHILD_RECT,
                                            body.data, (uint32_t)body.size);
    }
    yetty_ycore_buffer_destroy(&body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "yfigure_producer_set_child_rect");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yfigure_producer_delete_child(
    struct yetty_yfigure_producer *producer, uint32_t child_id)
{
    if (!producer) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_delete_child: NULL producer");
    }
    struct yetty_ycore_buffer body = {0};
    struct yetty_ycore_void_result write_result = write_uint32_le(&body, child_id);
    if (YETTY_IS_OK(write_result)) {
        write_result = append_admin_record(&producer->records, YETTY_YFIGURE_ADMIN_DELETE_CHILD,
                                            body.data, (uint32_t)body.size);
    }
    yetty_ycore_buffer_destroy(&body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_result, "yfigure_producer_delete_child");
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Flush — yface-wrap accumulated records + write to pty, then clear.
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yfigure_producer_flush(struct yetty_yfigure_producer *producer)
{
    if (!producer) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_flush: NULL producer");
    }
    if (producer->records.size == 0) {
        return YETTY_OK_VOID();
    }
    if (!producer->output_pty || !producer->output_pty->ops || !producer->output_pty->ops->write) {
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_flush: output pty has no write op");
    }

    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = producer->records.size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer wire_out = {0};
    struct yetty_ycore_void_result emit_result =
        yetty_yface_emit(YETTY_DCS_YCOMPOSITOR_BIN, /*compressed=*/1, &meta, sizeof(meta),
                         producer->records.data, producer->records.size, &wire_out);
    if (YETTY_IS_ERR(emit_result)) {
        yetty_ycore_buffer_destroy(&wire_out);
        return YETTY_ERR(yetty_ycore_void, "yfigure_producer_flush: yface_emit", emit_result);
    }

    if (wire_out.size > 0) {
        struct yetty_ycore_size_result pty_write_result = producer->output_pty->ops->write(
            producer->output_pty, (const char *)wire_out.data, wire_out.size);
        if (YETTY_IS_ERR(pty_write_result)) {
            yetty_ycore_buffer_destroy(&wire_out);
            return YETTY_ERR(yetty_ycore_void, "yfigure_producer_flush: pty write",
                             pty_write_result);
        }
        if (pty_write_result.value != wire_out.size) {
            yetty_ycore_buffer_destroy(&wire_out);
            return YETTY_ERR(yetty_ycore_void, "yfigure_producer_flush: short write");
        }
    }
    yetty_ycore_buffer_destroy(&wire_out);
    yetty_ycore_buffer_clear(&producer->records);
    return YETTY_OK_VOID();
}
