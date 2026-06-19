/*
 * result.c — chain helpers for the Result error type.
 *
 * The top-of-chain error lives by value inside a Result; the cause chain is
 * heap-allocated. yetty_ycore_error_chain copies an inner error onto the heap
 * so a wrapping YETTY_ERR can take ownership of its chain.
 * yetty_ycore_error_destroy walks and frees the chain.
 */

#include <yetty/ycore/result.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Upper bound on the number of frames a serialized chain may carry. Bounds the
 * wire size and the deserialize allocation count against a corrupt or hostile
 * frame_count field; chains deeper than this are silently truncated on the way
 * out (a debugging-context loss, never a correctness one). */
#define YETTY_YCORE_ERROR_WIRE_MAX_FRAMES 64

struct yetty_ycore_error *yetty_ycore_error_chain(struct yetty_ycore_error prev)
{
    struct yetty_ycore_error *p = malloc(sizeof(*p));
    if (!p) {
        /* OOM during error wrapping: drop the inner chain so we don't
         * leak it. The outer error still surfaces; debug context is lost. */
        yetty_ycore_error_destroy(prev);
        return NULL;
    }
    *p = prev;
    return p;
}

/* Walks the cause chain and frees every node with a single free() each. Note
 * the per-node free also reclaims the msg/file/func strings of any node minted
 * by yetty_ycore_error_deserialize: that helper packs each node's strings into
 * the same allocation as the node, so no separate string free is required (and
 * literal-lifetime strings from YETTY_ERR are, as ever, never freed). */
void yetty_ycore_error_destroy(struct yetty_ycore_error err)
{
    struct yetty_ycore_error *p = err.cause;
    while (p) {
        struct yetty_ycore_error *next = p->cause;
        free(p);
        p = next;
    }
}

struct yetty_ycore_void_result yetty_ycore_void_chain(struct yetty_ycore_void_result chain,
                                                      struct yetty_ycore_void_result step)
{
    if (YETTY_IS_OK(step)) {
        return chain;
    }
    if (YETTY_IS_OK(chain)) {
        return step;
    }
    /* Both failed. The latest step's top message is always a static literal;
     * keep it as the new head and link the accumulated chain beneath it. Free
     * step's own deeper cause first so it is not leaked. */
    const char *head_message = step.error.msg;
    yetty_ycore_error_destroy(step.error);
    return YETTY_ERR(yetty_ycore_void, head_message, chain);
}

void yetty_ycore_error_print(FILE *out, const char *headline, struct yetty_ycore_error err)
{
    if (!out) {
        fprintf(stderr, "yetty_ycore_error_print: out not set, setting it to stderr");
        out = stderr;
    }
    if (headline) {
        fprintf(out, "%s: %s\n", headline, err.msg ? err.msg : "<no message>");
    } else {
        fprintf(out, "%s\n", err.msg ? err.msg : "<no message>");
    }
    fprintf(out, "    at %s:%d (%s)\n", err.file ? err.file : "<unknown>", err.line,
            err.func ? err.func : "<unknown>");
    for (const struct yetty_ycore_error *c = err.cause; c; c = c->cause) {
        fprintf(out, "  caused by: %s\n", c->msg ? c->msg : "<no message>");
        fprintf(out, "    at %s:%d (%s)\n", c->file ? c->file : "<unknown>", c->line,
                c->func ? c->func : "<unknown>");
    }
}

size_t yetty_ycore_error_snprint(char *buf, size_t bufsize, struct yetty_ycore_error err)
{
    if (!buf || bufsize == 0) {
        return 0;
    }
    int n =
        snprintf(buf, bufsize, "%s\n    at %s:%d (%s)", err.msg ? err.msg : "<no message>",
                 err.file ? err.file : "<unknown>", err.line, err.func ? err.func : "<unknown>");
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    size_t off = (size_t)n < bufsize ? (size_t)n : bufsize - 1;
    for (const struct yetty_ycore_error *c = err.cause; c; c = c->cause) {
        if (off >= bufsize - 1) {
            break;
        }
        int m = snprintf(buf + off, bufsize - off, "\n  caused by: %s\n    at %s:%d (%s)",
                         c->msg ? c->msg : "<no message>", c->file ? c->file : "<unknown>", c->line,
                         c->func ? c->func : "<unknown>");
        if (m < 0) {
            break;
        }
        off += (size_t)m < bufsize - off ? (size_t)m : bufsize - 1 - off;
    }
    return off;
}

/* Append a length-prefixed string (u32 length + raw bytes, no NUL) at *offset.
 * Returns 0 without writing if it would overrun `bufsize`. */
static int yetty_ycore_error_wire_write_str(uint8_t *buf, size_t bufsize, size_t *offset,
                                            const char *str)
{
    size_t str_len = str ? strlen(str) : 0;
    if (str_len > UINT32_MAX) {
        return 0;
    }
    if (*offset + sizeof(uint32_t) + str_len > bufsize) {
        return 0;
    }
    uint32_t len_field = (uint32_t)str_len;
    memcpy(buf + *offset, &len_field, sizeof(len_field));
    *offset += sizeof(len_field);
    if (str_len) {
        memcpy(buf + *offset, str, str_len);
        *offset += str_len;
    }
    return 1;
}

/* Read a length-prefixed string slice (a borrow into `buf`, not a copy) at
 * *offset. Returns 0 if the length field or the bytes would overrun `len`. */
static int yetty_ycore_error_wire_read_str(const uint8_t *buf, size_t len, size_t *offset,
                                           const char **out_str, size_t *out_len)
{
    if (*offset + sizeof(uint32_t) > len) {
        return 0;
    }
    uint32_t str_len = 0;
    memcpy(&str_len, buf + *offset, sizeof(str_len));
    *offset += sizeof(str_len);
    if (str_len > len - *offset) {
        return 0;
    }
    *out_str = (const char *)(buf + *offset);
    *out_len = str_len;
    *offset += str_len;
    return 1;
}

size_t yetty_ycore_error_serialize(struct yetty_ycore_error err, uint8_t *buf, size_t bufsize)
{
    if (!buf || bufsize < sizeof(uint32_t)) {
        return 0;
    }

    /* Count frames (top + causes), capped so a pathological chain can't
     * exceed the wire bound. */
    uint32_t frame_count = 1;
    for (const struct yetty_ycore_error *cause = err.cause;
         cause && frame_count < YETTY_YCORE_ERROR_WIRE_MAX_FRAMES; cause = cause->cause) {
        frame_count++;
    }

    size_t offset = 0;
    memcpy(buf + offset, &frame_count, sizeof(frame_count));
    offset += sizeof(frame_count);

    const struct yetty_ycore_error *frame = &err;
    for (uint32_t index = 0; index < frame_count; index++) {
        if (!yetty_ycore_error_wire_write_str(buf, bufsize, &offset, frame->msg) ||
            !yetty_ycore_error_wire_write_str(buf, bufsize, &offset, frame->file) ||
            !yetty_ycore_error_wire_write_str(buf, bufsize, &offset, frame->func)) {
            return 0;
        }
        if (offset + sizeof(int32_t) > bufsize) {
            return 0;
        }
        int32_t line = (int32_t)frame->line;
        memcpy(buf + offset, &line, sizeof(line));
        offset += sizeof(line);
        frame = frame->cause;
    }
    return offset;
}

struct yetty_ycore_error *yetty_ycore_error_deserialize(const uint8_t *buf, size_t len)
{
    if (!buf || len < sizeof(uint32_t)) {
        return NULL;
    }

    size_t offset = 0;
    uint32_t frame_count = 0;
    memcpy(&frame_count, buf + offset, sizeof(frame_count));
    offset += sizeof(frame_count);
    if (frame_count == 0 || frame_count > YETTY_YCORE_ERROR_WIRE_MAX_FRAMES) {
        return NULL;
    }

    struct yetty_ycore_error *head = NULL;
    struct yetty_ycore_error *tail = NULL;
    for (uint32_t index = 0; index < frame_count; index++) {
        const char *msg = NULL, *file = NULL, *func = NULL;
        size_t msg_len = 0, file_len = 0, func_len = 0;
        if (!yetty_ycore_error_wire_read_str(buf, len, &offset, &msg, &msg_len) ||
            !yetty_ycore_error_wire_read_str(buf, len, &offset, &file, &file_len) ||
            !yetty_ycore_error_wire_read_str(buf, len, &offset, &func, &func_len)) {
            goto fail;
        }
        if (offset + sizeof(int32_t) > len) {
            goto fail;
        }
        int32_t line = 0;
        memcpy(&line, buf + offset, sizeof(line));
        offset += sizeof(line);

        /* One allocation holds the node and its three NUL-terminated strings,
         * so a later yetty_ycore_error_destroy frees both with one free(). */
        size_t node_size =
            sizeof(struct yetty_ycore_error) + msg_len + 1 + file_len + 1 + func_len + 1;
        struct yetty_ycore_error *node = malloc(node_size);
        if (!node) {
            goto fail;
        }
        char *msg_dst = (char *)(node + 1);
        char *file_dst = msg_dst + msg_len + 1;
        char *func_dst = file_dst + file_len + 1;
        memcpy(msg_dst, msg, msg_len);
        msg_dst[msg_len] = '\0';
        memcpy(file_dst, file, file_len);
        file_dst[file_len] = '\0';
        memcpy(func_dst, func, func_len);
        func_dst[func_len] = '\0';
        node->msg = msg_dst;
        node->file = file_dst;
        node->func = func_dst;
        node->line = (int)line;
        node->cause = NULL;

        if (!head) {
            head = node;
        } else {
            tail->cause = node;
        }
        tail = node;
    }
    return head;

fail:
    while (head) {
        struct yetty_ycore_error *next = head->cause;
        free(head);
        head = next;
    }
    return NULL;
}
