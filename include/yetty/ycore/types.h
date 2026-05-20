#ifndef YETTY_YCORE_TYPES_H
#define YETTY_YCORE_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Get pointer to containing struct from member pointer */
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

typedef uint64_t yetty_ycore_object_id;

#define YETTY_YCORE_OBJECT_ID_NONE 0

/* Mint a fresh, never-before-used object id. Chrome pre-allocates the ids
 * of every workspace/pane/split it intends to create and embeds them in
 * the corresponding event payloads (WORKSPACE_CREATE / PANE_CREATE /
 * PANE_SPLIT). The workspace-side handler creates the tile with the
 * exact id chrome minted, so both sides agree on the identifier without
 * a discovery round-trip. */
yetty_ycore_object_id yetty_ycore_next_object_id(void);

/*=============================================================================
 * Color
 *===========================================================================*/

struct yetty_ycore_rgba {
    uint8_t r, g, b, a;
};

/*=============================================================================
 * Buffer hierarchy
 *===========================================================================*/

#define YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH 32

struct yetty_ycore_buffer {
    uint8_t *data;
    size_t capacity;
    size_t size;
};

YETTY_YRESULT_DECLARE(yetty_ycore_buffer, struct yetty_ycore_buffer);

/* Buffer operations - doubling growth like kernel/C++ vector */
struct yetty_ycore_buffer_result yetty_ycore_buffer_create(size_t initial_capacity);
void yetty_ycore_buffer_destroy(struct yetty_ycore_buffer *buf);
void yetty_ycore_buffer_clear(struct yetty_ycore_buffer *buf);
struct yetty_ycore_void_result yetty_ycore_buffer_append(struct yetty_ycore_buffer *buf,
                                                         const struct yetty_ycore_buffer *src);

/* Append raw bytes to a buffer, growing capacity (doubling) if needed.
 * Same growth semantics as yetty_ycore_buffer_append; this is the
 * (void*, size_t) sibling for callers that don't have a yetty_ycore_buffer
 * wrapper around their bytes. Used heavily by yetty_yface's streaming
 * pipeline (LZ4 chunks, base64 chars). */
struct yetty_ycore_void_result yetty_ycore_buffer_write(struct yetty_ycore_buffer *buf,
                                                        const void *src, size_t len);

struct yetty_ycore_named_buffer {
    struct yetty_ycore_buffer buf;
    char name[YETTY_YCORE_NAMED_BUFFER_MAX_NAME_LENGTH];
};

/*=============================================================================
 * Font blob - named buffer containing TTF data
 *===========================================================================*/

struct yetty_ycore_blob {
    struct yetty_ycore_named_buffer named_buf;
    int32_t font_id;
};

/*=============================================================================
 * Image data - named buffer containing RGBA8 pixels
 *===========================================================================*/

struct yetty_ycore_data {
    struct yetty_ycore_named_buffer named_buf;
    float x, y, w, h;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t layer;
    uint32_t atlas_x, atlas_y;
};

/*=============================================================================
 * Text span - named buffer containing UTF-8 text
 *===========================================================================*/

struct yetty_ycore_span {
    struct yetty_ycore_named_buffer named_buf;
    float x, y;
    float font_size;
    float rotation;
    struct yetty_ycore_rgba color;
    uint32_t layer;
    int32_t font_id;
};

struct yetty_ycore_grid_size {
    uint32_t rows;
    uint32_t cols;
};

struct yetty_ycore_pixel_size {
    float width;
    float height;
};

struct yetty_ycore_pixel_coord {
    float x;
    float y;
};

struct yetty_ycore_grid_cursor_pos {
    uint32_t rows;
    uint32_t cols;
};

struct yetty_ycore_rectangle {
    struct yetty_ycore_pixel_coord min;
    struct yetty_ycore_pixel_coord max;
};

YETTY_YRESULT_DECLARE(uint32, uint32_t);
YETTY_YRESULT_DECLARE(float, float);
YETTY_YRESULT_DECLARE(pixel_size, struct yetty_ycore_pixel_size);
YETTY_YRESULT_DECLARE(pixel_coord, struct yetty_ycore_pixel_coord);
YETTY_YRESULT_DECLARE(rectangle, struct yetty_ycore_rectangle);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_TYPES_H */
