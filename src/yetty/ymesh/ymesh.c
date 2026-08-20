/*
 * ymesh.c — public API: load a mesh (GLB / PLY / STL / OBJ — content
 * sniffed by ymesh-load.c), serialize to the ymesh wire format, attach to
 * a fresh ydraw buffer, emit via DCS.
 *
 * See include/yetty/ymesh/ymesh.h for the wire format. Hand-written (not
 * schema-generated) because ymesh's GPU pipeline uses a real vertex layout
 * and depth attachment, neither of which the schema generator expresses.
 */

#include <yetty/ymesh/ymesh.h>
#include <yetty/ymesh/ymesh-load.h>

#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wire-format header word counts, before the variable-size attribute streams.
 *   2  : type_id, payload_size
 *   4  : bounds_x/y/w/h
 *   6  : bbox_min[3], bbox_max[3]
 *   6  : azimuth, elevation, dist_factor, pan_x, pan_y, mode
 *   3  : vertex_count, index_count, index_size
 */
#define YMESH_HEADER_WORDS (2 + 4 + 6 + 6 + 3)

static size_t ymesh_serialized_size(const struct yetty_ymesh_glb_data *mesh)
{
    size_t pos_words = mesh->vertex_count * 3;
    size_t nrm_words = mesh->vertex_count * 3;
    size_t idx_words = mesh->index_count; /* index_size always 4 in MVP */
    return (YMESH_HEADER_WORDS + pos_words + nrm_words + idx_words) * sizeof(uint32_t);
}

static struct yetty_ycore_size_result ymesh_serialize_prim(const struct yetty_ymesh_uniforms *u,
                                                           const struct yetty_ymesh_glb_data *mesh,
                                                           uint8_t *out, size_t out_capacity)
{
    size_t required = ymesh_serialized_size(mesh);
    if (out_capacity < required) {
        return YETTY_ERR(yetty_ycore_size, "ymesh: out buffer too small");
    }

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_YMESH_TYPE_ID;
    *p++ = (uint32_t)(required - 2 * sizeof(uint32_t));

    /* bounds */
    memcpy(p, &u->bounds_x, sizeof(float));
    p++;
    memcpy(p, &u->bounds_y, sizeof(float));
    p++;
    memcpy(p, &u->bounds_w, sizeof(float));
    p++;
    memcpy(p, &u->bounds_h, sizeof(float));
    p++;

    /* bbox */
    memcpy(p, u->bbox_min, 3 * sizeof(float));
    p += 3;
    memcpy(p, u->bbox_max, 3 * sizeof(float));
    p += 3;

    /* camera + mode */
    memcpy(p, &u->azimuth, sizeof(float));
    p++;
    memcpy(p, &u->elevation, sizeof(float));
    p++;
    memcpy(p, &u->dist_factor, sizeof(float));
    p++;
    memcpy(p, &u->pan_x, sizeof(float));
    p++;
    memcpy(p, &u->pan_y, sizeof(float));
    p++;
    *p++ = u->mode;

    /* counts */
    *p++ = (uint32_t)mesh->vertex_count;
    *p++ = (uint32_t)mesh->index_count;
    *p++ = mesh->index_size;

    /* attribute streams */
    memcpy(p, mesh->positions, mesh->vertex_count * 3 * sizeof(float));
    p += mesh->vertex_count * 3;
    memcpy(p, mesh->normals, mesh->vertex_count * 3 * sizeof(float));
    p += mesh->vertex_count * 3;
    memcpy(p, mesh->indices, mesh->index_count * sizeof(uint32_t));
    p += mesh->index_count;

    return YETTY_OK(yetty_ycore_size, required);
}

struct yetty_ydraw_drawable_list_result yetty_ymesh_render(
    const uint8_t *glb_bytes, size_t len, const struct yetty_ymesh_render_config *config)
{
    if (!glb_bytes || len == 0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh: glb_bytes is NULL/empty");
    }

    struct yetty_ymesh_glb_data_result mr = yetty_ymesh_load(glb_bytes, len);
    if (YETTY_IS_ERR(mr)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh: mesh parse failed", mr);
    }
    struct yetty_ymesh_glb_data mesh = mr.value;

    struct yetty_ymesh_uniforms u = {0};
    u.bounds_x = config ? config->bounds_x : 0.0f;
    u.bounds_y = config ? config->bounds_y : 0.0f;
    u.bounds_w = (config && config->bounds_w > 0.0f) ? config->bounds_w : 400.0f;
    u.bounds_h = (config && config->bounds_h > 0.0f) ? config->bounds_h : 400.0f;
    memcpy(u.bbox_min, mesh.bbox_min, 3 * sizeof(float));
    memcpy(u.bbox_max, mesh.bbox_max, 3 * sizeof(float));

    /* Camera + mode: pass through whatever the caller set, leaving zero/
     * default sentinels for the gen.c camera-builder to interpret. */
    if (config) {
        u.azimuth = config->azimuth;
        u.elevation = config->elevation;
        u.dist_factor = config->dist_factor;
        u.pan_x = config->pan_x;
        u.pan_y = config->pan_y;
        u.mode = config->mode;
    }

    size_t required = ymesh_serialized_size(&mesh);
    uint8_t *drawable_buf = malloc(required);
    if (!drawable_buf) {
        yetty_ymesh_glb_destroy(&mesh);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh: prim alloc failed");
    }

    struct yetty_ycore_size_result ser = ymesh_serialize_prim(&u, &mesh, drawable_buf, required);
    yetty_ymesh_glb_destroy(&mesh);
    if (YETTY_IS_ERR(ser)) {
        free(drawable_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh: serialize failed", ser);
    }

    struct yetty_ydraw_drawable_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_x + u.bounds_w,
        .scene_max_y = u.bounds_y + u.bounds_h,
    };
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        free(drawable_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh: ydraw buffer create failed", br);
    }

    struct yetty_ydraw_id_result idr =
        yetty_ydraw_drawable_list_add_prim(br.value, drawable_buf, required);
    free(drawable_buf);
    if (YETTY_IS_ERR(idr)) {
        yetty_ydraw_drawable_list_destroy(br.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh: ydraw add_prim failed", idr);
    }

    return YETTY_OK(yetty_ydraw_drawable_list, br.value);
}

struct yetty_ydraw_drawable_list_result yetty_ymesh_render_path(
    const char *path, const struct yetty_ymesh_render_config *config)
{
    if (!path) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh_render_path: path is NULL");
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh_render_path: fopen failed");
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh_render_path: fseek failed");
    }
    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh_render_path: empty or unreadable");
    }
    rewind(f);
    uint8_t *bytes = malloc((size_t)size);
    if (!bytes) {
        fclose(f);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh_render_path: malloc failed");
    }
    size_t nread = fread(bytes, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(bytes);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymesh_render_path: short read");
    }

    struct yetty_ydraw_drawable_list_result r = yetty_ymesh_render(bytes, (size_t)size, config);
    free(bytes);
    return r;
}

struct yetty_ycore_size_result yetty_ymesh_dcs_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "ymesh_dcs_bin_emit: NULL buffer or out");
    }

    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)buffer, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "ymesh_dcs_bin_emit: empty serialize");
    }

    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "ymesh_dcs_bin_emit: yface_emit failed", r);
    }

    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
