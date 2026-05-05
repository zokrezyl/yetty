#include <yetty/ymesh/ymesh-glb.h>

#include <stdlib.h>
#include <string.h>

#include <cgltf.h>

static cgltf_attribute *find_attribute(cgltf_primitive *prim, cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim->attributes_count; i++) {
        if (prim->attributes[i].type == type)
            return &prim->attributes[i];
    }
    return NULL;
}

static int read_vec3_accessor(const cgltf_accessor *acc, float *out, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (!cgltf_accessor_read_float(acc, i, &out[i * 3], 3))
            return 0;
    }
    return 1;
}

struct yetty_ymesh_glb_data_result yetty_ymesh_glb_parse(
    const uint8_t *bytes, size_t len)
{
    if (!bytes || len == 0)
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: empty input");

    cgltf_options options = {0};
    cgltf_data *gltf = NULL;
    cgltf_result pres = cgltf_parse(&options, bytes, len, &gltf);
    if (pres != cgltf_result_success)
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: cgltf_parse failed");

    cgltf_result lres = cgltf_load_buffers(&options, gltf, NULL);
    if (lres != cgltf_result_success) {
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: cgltf_load_buffers failed");
    }

    if (gltf->meshes_count == 0 || gltf->meshes[0].primitives_count == 0) {
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: no mesh/primitive in file");
    }

    cgltf_primitive *prim = &gltf->meshes[0].primitives[0];
    cgltf_attribute *pos_attr = find_attribute(prim, cgltf_attribute_type_position);
    cgltf_attribute *nrm_attr = find_attribute(prim, cgltf_attribute_type_normal);
    if (!pos_attr || !nrm_attr) {
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data,
                         "ymesh_glb: primitive missing POSITION or NORMAL");
    }
    if (!prim->indices) {
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: primitive has no index buffer");
    }

    cgltf_accessor *pos_acc = pos_attr->data;
    cgltf_accessor *nrm_acc = nrm_attr->data;
    cgltf_accessor *idx_acc = prim->indices;

    if (pos_acc->count != nrm_acc->count) {
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data,
                         "ymesh_glb: POSITION/NORMAL vertex counts differ");
    }

    size_t vcount = pos_acc->count;
    size_t icount = idx_acc->count;

    struct yetty_ymesh_glb_data out = {0};
    out.vertex_count = vcount;
    out.index_count = icount;
    out.positions = malloc(vcount * 3 * sizeof(float));
    out.normals = malloc(vcount * 3 * sizeof(float));
    if (!out.positions || !out.normals) {
        free(out.positions);
        free(out.normals);
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: vertex alloc failed");
    }

    if (!read_vec3_accessor(pos_acc, out.positions, vcount) ||
        !read_vec3_accessor(nrm_acc, out.normals, vcount)) {
        free(out.positions);
        free(out.normals);
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: accessor read failed");
    }

    /* Pick uint32 always — keep the wire format simple. cgltf gives us the
     * right value regardless of source component type. */
    out.index_size = 4;
    out.indices = malloc(icount * sizeof(uint32_t));
    if (!out.indices) {
        free(out.positions);
        free(out.normals);
        cgltf_free(gltf);
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_glb: index alloc failed");
    }
    uint32_t *idx_out = (uint32_t *)out.indices;
    for (size_t i = 0; i < icount; i++) {
        idx_out[i] = (uint32_t)cgltf_accessor_read_index(idx_acc, i);
    }

    /* Bounding box: prefer accessor-stored min/max (most .glb files have
     * these for POSITION); fall back to scanning vertices. */
    if (pos_acc->has_min && pos_acc->has_max) {
        for (int i = 0; i < 3; i++) {
            out.bbox_min[i] = pos_acc->min[i];
            out.bbox_max[i] = pos_acc->max[i];
        }
    } else {
        out.bbox_min[0] = out.bbox_max[0] = out.positions[0];
        out.bbox_min[1] = out.bbox_max[1] = out.positions[1];
        out.bbox_min[2] = out.bbox_max[2] = out.positions[2];
        for (size_t i = 1; i < vcount; i++) {
            for (int j = 0; j < 3; j++) {
                float v = out.positions[i * 3 + j];
                if (v < out.bbox_min[j]) out.bbox_min[j] = v;
                if (v > out.bbox_max[j]) out.bbox_max[j] = v;
            }
        }
    }

    cgltf_free(gltf);
    return YETTY_OK(yetty_ymesh_glb_data, out);
}

void yetty_ymesh_glb_destroy(struct yetty_ymesh_glb_data *data)
{
    if (!data)
        return;
    free(data->positions);
    free(data->normals);
    free(data->indices);
    data->positions = NULL;
    data->normals = NULL;
    data->indices = NULL;
    data->vertex_count = 0;
    data->index_count = 0;
}
