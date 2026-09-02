/*
 * complex2-internal.h — module-private helpers shared by the ycomplex2
 * drawable classes. Static-inline only (no file-scope data).
 */
#ifndef YETTY_YCOMPLEX2_INTERNAL_H
#define YETTY_YCOMPLEX2_INTERNAL_H

#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/drawable-list.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    YCOMPLEX2_PATH_LIMIT = 512,
};

/* Read a whole file. Caller frees *out_bytes. */
static inline struct yetty_ycore_size_result ycomplex2_read_file(const char *path,
                                                                 uint8_t **out_bytes)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_size, "ycomplex2: cannot open file");
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_size, "ycomplex2: seek failed");
    }
    long file_size = ftell(file);
    if (file_size <= 0) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_size, "ycomplex2: empty file");
    }
    rewind(file);
    uint8_t *bytes = malloc((size_t)file_size);
    if (!bytes) {
        fclose(file);
        return YETTY_ERR(yetty_ycore_size, "ycomplex2: alloc failed");
    }
    size_t read_len = fread(bytes, 1, (size_t)file_size, file);
    fclose(file);
    if (read_len != (size_t)file_size) {
        free(bytes);
        return YETTY_ERR(yetty_ycore_size, "ycomplex2: short read");
    }
    *out_bytes = bytes;
    return YETTY_OK(yetty_ycore_size, read_len);
}

/* Lift a rendered single-prim list's records into `dest`: serialize the
 * temp list ([magic|bounds|byte_count][prim bytes]…) and append exactly
 * byte_count prim bytes. Used by the kinds whose client emitters only
 * offer render-to-fresh-list. */
static inline struct yetty_ycore_void_result ycomplex2_append_rendered(
    struct yetty_ydraw_drawable_list *dest, struct yetty_ydraw_drawable_list *rendered)
{
    const uint8_t *raw = NULL;
    size_t raw_size = yetty_ydraw_drawable_list_serialize(rendered, &raw);
    if (raw_size <= YETTY_YDRAW_SERIAL_HEADER_BYTES || !raw) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2: rendered list serialized empty");
    }
    uint32_t byte_count = 0;
    memcpy(&byte_count, raw + YETTY_YDRAW_SERIAL_HEADER_BYTES - 4, 4);
    if (byte_count == 0 || byte_count > raw_size - YETTY_YDRAW_SERIAL_HEADER_BYTES) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2: bad rendered prim byte count");
    }
    struct yetty_ydraw_id_result add_r =
        yetty_ydraw_drawable_list_add_prim(dest, raw + YETTY_YDRAW_SERIAL_HEADER_BYTES, byte_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_r, "ycomplex2: append rendered prim");
    return YETTY_OK_VOID();
}

/* Open / close a paint-z scope around a complex's emitted record(s), so a
 * complex (which carries no z in its wire record) stacks at `layer` like
 * any shape. layer 0 is the default plane and emits nothing. */
static inline struct yetty_ycore_void_result ycomplex2_paint_z_open(
    struct yetty_ydraw_drawable_list *list, int32_t layer)
{
    if (layer == 0) {
        return YETTY_OK_VOID();
    }
    return yetty_ydraw_drawable_list_add_cmd_paint_z(list, layer);
}

static inline struct yetty_ycore_void_result ycomplex2_paint_z_close(
    struct yetty_ydraw_drawable_list *list, int32_t layer)
{
    if (layer == 0) {
        return YETTY_OK_VOID();
    }
    return yetty_ydraw_drawable_list_add_cmd_paint_z_end(list);
}

/* Emit CMD_NODE_ID(id) ahead of a complex's record run when id != 0: the
 * receiver binds the complex ITSELF as the addressable node at (enclosing
 * path . id) — its own id, no wrapper group (drawable-use-cases.md §2).
 * id 0 = anonymous, emits nothing. */
static inline struct yetty_ycore_void_result ycomplex2_node_id(
    struct yetty_ydraw_drawable_list *list, uint32_t id)
{
    if (id == 0) {
        return YETTY_OK_VOID();
    }
    return yetty_ydraw_drawable_list_add_cmd_node_id(list, id);
}

#endif /* YETTY_YCOMPLEX2_INTERNAL_H */
