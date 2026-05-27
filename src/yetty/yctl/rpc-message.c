/* RPC message parsing and serialization */

#include <yetty/yctl/rpc-message.h>

#include <msgpack.h>
#include <stdlib.h>
#include <string.h>

/*
 * msgpack-RPC wire format:
 *   Request:      [0, msgid, channel, method, params]
 *   Response:     [1, msgid, error, result]
 *   Notification: [2, channel, method, params]
 */

struct yetty_rpc_message_result yetty_yctl_message_parse(const uint8_t *data, size_t len,
                                                         size_t *out_consumed)
{
    msgpack_unpacked unpacked;
    msgpack_unpack_return ret;
    msgpack_object *array;
    struct yetty_yctl_message msg = {0};
    size_t array_size;
    int type;
    size_t off = 0;

    if (out_consumed) {
        *out_consumed = 0;
    }

    msgpack_unpacked_init(&unpacked);
    ret = msgpack_unpack_next(&unpacked, (const char *)data, len, &off);

    /* Partial message — caller should wait for more bytes. Return OK
     * with consumed == 0 so the caller's parse loop terminates without
     * dropping the connection. */
    if (ret == MSGPACK_UNPACK_CONTINUE) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_OK(yetty_rpc_message, msg);
    }

    if (ret != MSGPACK_UNPACK_SUCCESS) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_ERR(yetty_rpc_message, "failed to unpack msgpack");
    }

    if (unpacked.data.type != MSGPACK_OBJECT_ARRAY) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_ERR(yetty_rpc_message, "expected array");
    }

    array = &unpacked.data;
    array_size = array->via.array.size;

    if (array_size < 1) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_ERR(yetty_rpc_message, "empty array");
    }

    /* First element is message type */
    if (array->via.array.ptr[0].type != MSGPACK_OBJECT_POSITIVE_INTEGER) {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_ERR(yetty_rpc_message, "invalid message type");
    }

    type = (int)array->via.array.ptr[0].via.u64;

    if (type == YETTY_YCTL_MSG_REQUEST) {
        /* Request: [0, msgid, channel, method, params] */
        if (array_size < 5) {
            msgpack_unpacked_destroy(&unpacked);
            return YETTY_ERR(yetty_rpc_message, "request needs 5 elements");
        }

        msg.type = YETTY_YCTL_MSG_REQUEST;
        msg.msgid = (uint32_t)array->via.array.ptr[1].via.u64;
        msg.channel = (uint32_t)array->via.array.ptr[2].via.u64;

        if (array->via.array.ptr[3].type != MSGPACK_OBJECT_STR) {
            msgpack_unpacked_destroy(&unpacked);
            return YETTY_ERR(yetty_rpc_message, "method must be string");
        }
        msg.method = array->via.array.ptr[3].via.str.ptr;
        msg.method_len = array->via.array.ptr[3].via.str.size;

        /* Pack params object back to msgpack bytes */
        if (array_size >= 5) {
            msgpack_sbuffer sbuf;
            msgpack_packer pk;
            msgpack_sbuffer_init(&sbuf);
            msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
            msgpack_pack_object(&pk, array->via.array.ptr[4]);
            /* Caller must free msg.params */
            msg.params = (uint8_t *)sbuf.data;
            msg.params_len = sbuf.size;
        }

    } else if (type == YETTY_YCTL_MSG_RESPONSE) {
        /* Response: [1, msgid, error, result] */
        if (array_size < 4) {
            msgpack_unpacked_destroy(&unpacked);
            return YETTY_ERR(yetty_rpc_message, "response needs 4 elements");
        }

        msg.type = YETTY_YCTL_MSG_RESPONSE;
        msg.msgid = (uint32_t)array->via.array.ptr[1].via.u64;
        /* error and result parsing handled by caller */

    } else if (type == YETTY_YCTL_MSG_NOTIFICATION) {
        /* Notification: [2, channel, method, params] */
        if (array_size < 4) {
            msgpack_unpacked_destroy(&unpacked);
            return YETTY_ERR(yetty_rpc_message, "notification needs 4 elements");
        }

        msg.type = YETTY_YCTL_MSG_NOTIFICATION;
        msg.msgid = 0;
        msg.channel = (uint32_t)array->via.array.ptr[1].via.u64;

        if (array->via.array.ptr[2].type != MSGPACK_OBJECT_STR) {
            msgpack_unpacked_destroy(&unpacked);
            return YETTY_ERR(yetty_rpc_message, "method must be string");
        }
        msg.method = array->via.array.ptr[2].via.str.ptr;
        msg.method_len = array->via.array.ptr[2].via.str.size;

        /* Pack params object back to msgpack bytes */
        if (array_size >= 4) {
            msgpack_sbuffer sbuf;
            msgpack_packer pk;
            msgpack_sbuffer_init(&sbuf);
            msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
            msgpack_pack_object(&pk, array->via.array.ptr[3]);
            /* Caller must free msg.params */
            msg.params = (uint8_t *)sbuf.data;
            msg.params_len = sbuf.size;
        }

    } else {
        msgpack_unpacked_destroy(&unpacked);
        return YETTY_ERR(yetty_rpc_message, "unknown message type");
    }

    if (out_consumed) {
        *out_consumed = off;
    }
    msgpack_unpacked_destroy(&unpacked);
    return YETTY_OK(yetty_rpc_message, msg);
}

void yetty_yctl_write_buffer_init(struct yetty_yctl_write_buffer *buf, uint8_t *storage,
                                  size_t capacity)
{
    buf->data = storage;
    buf->len = 0;
    buf->capacity = capacity;
}

void yetty_yctl_write_buffer_reset(struct yetty_yctl_write_buffer *buf)
{
    buf->len = 0;
}

/* Helper to write msgpack to buffer */
static int buffer_write(void *data, const char *buf, size_t len)
{
    struct yetty_yctl_write_buffer *wb = data;
    if (wb->len + len > wb->capacity) {
        return -1;
    }
    memcpy(wb->data + wb->len, buf, len);
    wb->len += len;
    return 0;
}

struct yetty_ycore_void_result yetty_yctl_write_response_ok(struct yetty_yctl_write_buffer *buf,
                                                            uint32_t msgid, const uint8_t *result,
                                                            size_t result_len)
{
    msgpack_packer pk;

    yetty_yctl_write_buffer_reset(buf);
    msgpack_packer_init(&pk, buf, buffer_write);

    /* [1, msgid, nil, result] */
    msgpack_pack_array(&pk, 4);
    msgpack_pack_int(&pk, YETTY_YCTL_MSG_RESPONSE);
    msgpack_pack_uint32(&pk, msgid);
    msgpack_pack_nil(&pk); /* no error */

    if (result && result_len > 0) {
        /* Write raw msgpack result */
        if (buf->len + result_len > buf->capacity) {
            return YETTY_ERR(yetty_ycore_void, "buffer overflow");
        }
        memcpy(buf->data + buf->len, result, result_len);
        buf->len += result_len;
    } else {
        msgpack_pack_nil(&pk);
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yctl_write_response_error(struct yetty_yctl_write_buffer *buf,
                                                               uint32_t msgid,
                                                               const char *error_msg)
{
    msgpack_packer pk;
    size_t error_len;

    yetty_yctl_write_buffer_reset(buf);
    msgpack_packer_init(&pk, buf, buffer_write);

    error_len = error_msg ? strlen(error_msg) : 0;

    /* [1, msgid, error, nil] */
    msgpack_pack_array(&pk, 4);
    msgpack_pack_int(&pk, YETTY_YCTL_MSG_RESPONSE);
    msgpack_pack_uint32(&pk, msgid);
    msgpack_pack_str(&pk, error_len);
    if (error_len > 0) {
        msgpack_pack_str_body(&pk, error_msg, error_len);
    }
    msgpack_pack_nil(&pk); /* no result */

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yctl_write_response_bool(struct yetty_yctl_write_buffer *buf,
                                                              uint32_t msgid, int value)
{
    msgpack_packer pk;

    yetty_yctl_write_buffer_reset(buf);
    msgpack_packer_init(&pk, buf, buffer_write);

    /* [1, msgid, nil, bool] */
    msgpack_pack_array(&pk, 4);
    msgpack_pack_int(&pk, YETTY_YCTL_MSG_RESPONSE);
    msgpack_pack_uint32(&pk, msgid);
    msgpack_pack_nil(&pk); /* no error */

    if (value) {
        msgpack_pack_true(&pk);
    } else {
        msgpack_pack_false(&pk);
    }

    return YETTY_OK_VOID();
}
