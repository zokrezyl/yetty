#include <yetty/ycore/types.h>

/* Forward declaration so buffer_append below can call buffer_write before
 * it's defined in source order. */
struct yetty_ycore_void_result yetty_ycore_buffer_write(struct yetty_ycore_buffer *buf,
                                                        const void *src, size_t len);

#include <stdlib.h>
#include <string.h>

struct yetty_ycore_buffer_result yetty_ycore_buffer_create(size_t initial_capacity)
{
    struct yetty_ycore_buffer buf = {0};
    if (initial_capacity > 0) {
        buf.data = malloc(initial_capacity);
        if (!buf.data) {
            return YETTY_ERR(yetty_ycore_buffer, "malloc failed");
        }
        buf.capacity = initial_capacity;
    }
    return YETTY_OK(yetty_ycore_buffer, buf);
}

void yetty_ycore_buffer_destroy(struct yetty_ycore_buffer *buf)
{
    if (!buf) {
        return;
    }
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

void yetty_ycore_buffer_clear(struct yetty_ycore_buffer *buf)
{
    if (buf) {
        buf->size = 0;
    }
}

struct yetty_ycore_void_result yetty_ycore_buffer_append(struct yetty_ycore_buffer *buf,
                                                         const struct yetty_ycore_buffer *src)
{
    if (!src) {
        return YETTY_OK_VOID();
    }
    return yetty_ycore_buffer_write(buf, src->data, src->size);
}

struct yetty_ycore_void_result yetty_ycore_buffer_write(struct yetty_ycore_buffer *buf,
                                                        const void *src, size_t len)
{
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "buf is NULL");
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    if (!src) {
        return YETTY_ERR(yetty_ycore_void, "src is NULL");
    }

    size_t needed = buf->size + len;
    if (needed > buf->capacity) {
        size_t new_cap = buf->capacity ? buf->capacity : 64;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        uint8_t *new_data = realloc(buf->data, new_cap);
        if (!new_data) {
            return YETTY_ERR(yetty_ycore_void, "realloc failed");
        }
        buf->data = new_data;
        buf->capacity = new_cap;
    }

    memcpy(buf->data + buf->size, src, len);
    buf->size += len;
    return YETTY_OK_VOID();
}
