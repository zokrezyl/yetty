#ifndef YETTY_YRPC_RPC_MESSAGE_H
#define YETTY_YRPC_RPC_MESSAGE_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * msgpack-RPC wire format (compatible with yetty-poc):
 *   Request:      [0, msgid, channel, method, params]
 *   Response:     [1, msgid, error, result]
 *   Notification: [2, channel, method, params]
 */

/* Message types */
enum yetty_yrpc_message_type {
    YETTY_YRPC_MSG_REQUEST = 0,
    YETTY_YRPC_MSG_RESPONSE = 1,
    YETTY_YRPC_MSG_NOTIFICATION = 2,
};

/* Channel IDs (extensible) */
enum yetty_yrpc_channel {
    YETTY_YRPC_CHANNEL_EVENT_LOOP = 0,
    YETTY_YRPC_CHANNEL_STREAM = 1,
};

/* Parsed RPC message */
struct yetty_yrpc_message {
    enum yetty_yrpc_message_type type;
    uint32_t msgid;        /* 0 for notifications */
    uint32_t channel;      /* target channel */
    const char *method;    /* method name (borrowed, valid during handling) */
    size_t method_len;     /* method name length */
    const uint8_t *params; /* msgpack params (borrowed) */
    size_t params_len;     /* params length */
};

/* Result types */
YETTY_YRESULT_DECLARE(yetty_rpc_message, struct yetty_yrpc_message);

/*
 * Parse a msgpack-RPC message from raw bytes.
 *
 * Streaming-safe: TCP can deliver multiple framed messages in one read
 * (especially when the loop is stalled by GPU work like recording), or
 * truncate one across two reads. The caller must loop and track buffer
 * progress via *out_consumed:
 *
 *   - YETTY_OK + *out_consumed > 0: one full message parsed; advance
 *       the input cursor by *out_consumed and parse again from there.
 *   - YETTY_OK + *out_consumed == 0: buffer holds only a partial message;
 *       the returned struct is unpopulated. Wait for more bytes.
 *   - YETTY_ERR: malformed bytes; close the connection.
 *
 * The returned message borrows method/method_len pointers into the
 * input buffer. msg.params is heap-allocated (re-packed) and must be
 * freed by the caller.
 */
struct yetty_rpc_message_result
yetty_yrpc_message_parse(const uint8_t *data, size_t len, size_t *out_consumed);

/*
 * Write buffer for serializing responses.
 */
struct yetty_yrpc_write_buffer {
    uint8_t *data;
    size_t len;
    size_t capacity;
};

/* Initialize write buffer (caller owns memory) */
void yetty_yrpc_write_buffer_init(struct yetty_yrpc_write_buffer *buf, uint8_t *storage,
                                 size_t capacity);

/* Reset buffer for reuse */
void yetty_yrpc_write_buffer_reset(struct yetty_yrpc_write_buffer *buf);

/* Serialize success response: [1, msgid, nil, result] */
struct yetty_ycore_void_result yetty_yrpc_write_response_ok(struct yetty_yrpc_write_buffer *buf,
                                                           uint32_t msgid, const uint8_t *result,
                                                           size_t result_len);

/* Serialize error response: [1, msgid, error_msg, nil] */
struct yetty_ycore_void_result yetty_yrpc_write_response_error(struct yetty_yrpc_write_buffer *buf,
                                                              uint32_t msgid,
                                                              const char *error_msg);

/* Serialize bool result (common case) */
struct yetty_ycore_void_result yetty_yrpc_write_response_bool(struct yetty_yrpc_write_buffer *buf,
                                                             uint32_t msgid, int value);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRPC_RPC_MESSAGE_H */
