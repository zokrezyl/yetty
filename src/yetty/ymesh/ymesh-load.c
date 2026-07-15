/*
 * ymesh-load.c — multi-format mesh loading: PLY / STL / OBJ parsers plus
 * dispatch to the existing GLB loader. All parsers emit the same
 * struct yetty_ymesh_glb_data triangle soup (positions + normals + u32
 * indices + bbox); see ymesh-load.h for the format notes.
 */

#include <yetty/ymesh/ymesh-load.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum { YMESH_POINT_CLOUD_MAX_POINTS = 150000 };

/*=============================================================================
 * Shared helpers
 *===========================================================================*/

static void compute_bbox(struct yetty_ymesh_glb_data *mesh)
{
    for (int axis = 0; axis < 3; axis++) {
        mesh->bbox_min[axis] = mesh->positions[axis];
        mesh->bbox_max[axis] = mesh->positions[axis];
    }
    for (size_t i = 1; i < mesh->vertex_count; i++) {
        for (int axis = 0; axis < 3; axis++) {
            float value = mesh->positions[i * 3 + axis];
            if (value < mesh->bbox_min[axis]) {
                mesh->bbox_min[axis] = value;
            }
            if (value > mesh->bbox_max[axis]) {
                mesh->bbox_max[axis] = value;
            }
        }
    }
}

/* Smooth vertex normals: accumulate (area-weighted) face normals, then
 * normalize. Used when the source format carries no normals. */
static struct yetty_ycore_void_result compute_smooth_normals(struct yetty_ymesh_glb_data *mesh)
{
    mesh->normals = calloc(mesh->vertex_count * 3, sizeof(float));
    if (!mesh->normals) {
        return YETTY_ERR(yetty_ycore_void, "normal alloc failed");
    }
    const uint32_t *indices = (const uint32_t *)mesh->indices;
    for (size_t tri = 0; tri + 2 < mesh->index_count; tri += 3) {
        uint32_t corner_a = indices[tri];
        uint32_t corner_b = indices[tri + 1];
        uint32_t corner_c = indices[tri + 2];
        if (corner_a >= mesh->vertex_count || corner_b >= mesh->vertex_count ||
            corner_c >= mesh->vertex_count) {
            continue;
        }
        const float *pa = &mesh->positions[corner_a * 3];
        const float *pb = &mesh->positions[corner_b * 3];
        const float *pc = &mesh->positions[corner_c * 3];
        float edge_ab[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
        float edge_ac[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
        float face_normal[3] = {
            edge_ab[1] * edge_ac[2] - edge_ab[2] * edge_ac[1],
            edge_ab[2] * edge_ac[0] - edge_ab[0] * edge_ac[2],
            edge_ab[0] * edge_ac[1] - edge_ab[1] * edge_ac[0],
        };
        for (int axis = 0; axis < 3; axis++) {
            mesh->normals[corner_a * 3 + axis] += face_normal[axis];
            mesh->normals[corner_b * 3 + axis] += face_normal[axis];
            mesh->normals[corner_c * 3 + axis] += face_normal[axis];
        }
    }
    for (size_t i = 0; i < mesh->vertex_count; i++) {
        float *normal = &mesh->normals[i * 3];
        float length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 1.0e-12f) {
            normal[0] /= length;
            normal[1] /= length;
            normal[2] /= length;
        } else {
            normal[1] = 1.0f;
        }
    }
    return YETTY_OK_VOID();
}

/* Turn a bare point set into renderable geometry: one small octahedron per
 * point (6 vertices, 8 faces), sized from the cloud's bounding box. Keeps
 * the wire and the shader unchanged — a point cloud IS a mesh. */
static struct yetty_ymesh_glb_data_result points_to_markers(const float *points, size_t point_count)
{
    if (point_count == 0) {
        return YETTY_ERR(yetty_ymesh_glb_data, "point cloud is empty");
    }
    if (point_count > YMESH_POINT_CLOUD_MAX_POINTS) {
        point_count = YMESH_POINT_CLOUD_MAX_POINTS;
    }

    float bbox_min[3] = {points[0], points[1], points[2]};
    float bbox_max[3] = {points[0], points[1], points[2]};
    for (size_t i = 1; i < point_count; i++) {
        for (int axis = 0; axis < 3; axis++) {
            float value = points[i * 3 + axis];
            if (value < bbox_min[axis]) {
                bbox_min[axis] = value;
            }
            if (value > bbox_max[axis]) {
                bbox_max[axis] = value;
            }
        }
    }
    float diagonal = sqrtf((bbox_max[0] - bbox_min[0]) * (bbox_max[0] - bbox_min[0]) +
                           (bbox_max[1] - bbox_min[1]) * (bbox_max[1] - bbox_min[1]) +
                           (bbox_max[2] - bbox_min[2]) * (bbox_max[2] - bbox_min[2]));
    float radius = diagonal > 0.0f ? diagonal * 0.003f : 0.01f;

    /* Octahedron template: 6 vertices along the axes, 8 triangles. Normals
     * equal the (normalized) vertex offsets — good enough for markers. */
    static const float offsets[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };
    static const uint32_t faces[8][3] = {
        {0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5},
    };

    struct yetty_ymesh_glb_data out = {0};
    out.vertex_count = point_count * 6;
    out.index_count = point_count * 24;
    out.index_size = 4;
    out.positions = malloc(out.vertex_count * 3 * sizeof(float));
    out.normals = malloc(out.vertex_count * 3 * sizeof(float));
    out.indices = malloc(out.index_count * sizeof(uint32_t));
    if (!out.positions || !out.normals || !out.indices) {
        free(out.positions);
        free(out.normals);
        free(out.indices);
        return YETTY_ERR(yetty_ymesh_glb_data, "point marker alloc failed");
    }
    uint32_t *index_out = (uint32_t *)out.indices;
    for (size_t point = 0; point < point_count; point++) {
        const float *center = &points[point * 3];
        for (int corner = 0; corner < 6; corner++) {
            size_t vertex = point * 6 + (size_t)corner;
            for (int axis = 0; axis < 3; axis++) {
                out.positions[vertex * 3 + axis] = center[axis] + offsets[corner][axis] * radius;
                out.normals[vertex * 3 + axis] = offsets[corner][axis];
            }
        }
        for (int face = 0; face < 8; face++) {
            for (int corner = 0; corner < 3; corner++) {
                index_out[point * 24 + (size_t)face * 3 + (size_t)corner] =
                    (uint32_t)(point * 6) + faces[face][corner];
            }
        }
    }
    memcpy(out.bbox_min, bbox_min, sizeof bbox_min);
    memcpy(out.bbox_max, bbox_max, sizeof bbox_max);
    return YETTY_OK(yetty_ymesh_glb_data, out);
}

/*=============================================================================
 * PLY (ascii + binary_little_endian)
 *===========================================================================*/

struct ply_property {
    char name[24];
    int type_size;       /* bytes for scalar; 0 = list */
    bool is_float;       /* float/double scalar */
    int list_count_size; /* list: bytes of the count field */
    int list_item_size;  /* list: bytes per item */
};

struct ply_element {
    char name[24];
    size_t count;
    struct ply_property properties[16];
    size_t property_count;
};

static int ply_type_size(const char *type_name, bool *out_is_float)
{
    *out_is_float = false;
    if (strcmp(type_name, "char") == 0 || strcmp(type_name, "uchar") == 0 ||
        strcmp(type_name, "int8") == 0 || strcmp(type_name, "uint8") == 0) {
        return 1;
    }
    if (strcmp(type_name, "short") == 0 || strcmp(type_name, "ushort") == 0 ||
        strcmp(type_name, "int16") == 0 || strcmp(type_name, "uint16") == 0) {
        return 2;
    }
    if (strcmp(type_name, "int") == 0 || strcmp(type_name, "uint") == 0 ||
        strcmp(type_name, "int32") == 0 || strcmp(type_name, "uint32") == 0) {
        return 4;
    }
    if (strcmp(type_name, "float") == 0 || strcmp(type_name, "float32") == 0) {
        *out_is_float = true;
        return 4;
    }
    if (strcmp(type_name, "double") == 0 || strcmp(type_name, "float64") == 0) {
        *out_is_float = true;
        return 8;
    }
    return 0;
}

static double ply_read_scalar(const uint8_t *bytes, int size, bool is_float)
{
    if (is_float && size == 4) {
        float value;
        memcpy(&value, bytes, 4);
        return value;
    }
    if (is_float && size == 8) {
        double value;
        memcpy(&value, bytes, 8);
        return value;
    }
    /* Integers: little-endian, treat as unsigned (indices/colors) except
     * that sign never matters for the fields we consume. */
    uint32_t value = 0;
    for (int i = 0; i < size && i < 4; i++) {
        value |= (uint32_t)bytes[i] << (8 * i);
    }
    return value;
}

static struct yetty_ymesh_glb_data_result ply_parse(const uint8_t *bytes, size_t len)
{
    /* ---- header ---- */
    const char *cursor = (const char *)bytes;
    const char *end = cursor + len;
    bool binary = false;
    struct ply_element elements[4];
    size_t element_count = 0;

    /* Line-by-line header scan up to end_header. */
    const char *line = cursor;
    bool header_done = false;
    while (line < end && !header_done) {
        const char *line_end = memchr(line, '\n', (size_t)(end - line));
        if (!line_end) {
            break;
        }
        char text[160];
        size_t line_len = (size_t)(line_end - line);
        if (line_len >= sizeof(text)) {
            line_len = sizeof(text) - 1;
        }
        memcpy(text, line, line_len);
        text[line_len] = '\0';
        if (line_len > 0 && text[line_len - 1] == '\r') {
            text[line_len - 1] = '\0';
        }

        if (strncmp(text, "format ", 7) == 0) {
            if (strstr(text, "binary_little_endian")) {
                binary = true;
            } else if (strstr(text, "binary_big_endian")) {
                return YETTY_ERR(yetty_ymesh_glb_data, "ply: big-endian not supported");
            }
        } else if (strncmp(text, "element ", 8) == 0) {
            if (element_count >= 4) {
                return YETTY_ERR(yetty_ymesh_glb_data, "ply: too many elements");
            }
            struct ply_element *element = &elements[element_count++];
            memset(element, 0, sizeof(*element));
            unsigned long parsed_count = 0;
            char name[24] = {0};
            if (sscanf(text + 8, "%23s %lu", name, &parsed_count) != 2) {
                return YETTY_ERR(yetty_ymesh_glb_data, "ply: malformed element line");
            }
            memcpy(element->name, name, sizeof(name));
            element->count = parsed_count;
        } else if (strncmp(text, "property ", 9) == 0 && element_count > 0) {
            struct ply_element *element = &elements[element_count - 1];
            if (element->property_count >= 16) {
                return YETTY_ERR(yetty_ymesh_glb_data, "ply: too many properties");
            }
            struct ply_property *property = &element->properties[element->property_count++];
            memset(property, 0, sizeof(*property));
            char type_a[24] = {0}, type_b[24] = {0}, name[24] = {0};
            if (strncmp(text + 9, "list ", 5) == 0) {
                if (sscanf(text + 14, "%23s %23s %23s", type_a, type_b, name) != 3) {
                    return YETTY_ERR(yetty_ymesh_glb_data, "ply: malformed list property");
                }
                bool ignored;
                property->type_size = 0;
                property->list_count_size = ply_type_size(type_a, &ignored);
                property->list_item_size = ply_type_size(type_b, &ignored);
                if (!property->list_count_size || !property->list_item_size) {
                    return YETTY_ERR(yetty_ymesh_glb_data, "ply: unsupported list types");
                }
            } else {
                if (sscanf(text + 9, "%23s %23s", type_a, name) != 2) {
                    return YETTY_ERR(yetty_ymesh_glb_data, "ply: malformed property");
                }
                property->type_size = ply_type_size(type_a, &property->is_float);
                if (!property->type_size) {
                    return YETTY_ERR(yetty_ymesh_glb_data, "ply: unsupported property type");
                }
            }
            memcpy(property->name, name, sizeof(name));
        } else if (strcmp(text, "end_header") == 0) {
            header_done = true;
        }
        line = line_end + 1;
    }
    if (!header_done) {
        return YETTY_ERR(yetty_ymesh_glb_data, "ply: missing end_header");
    }

    struct ply_element *vertex_element = NULL;
    struct ply_element *face_element = NULL;
    for (size_t i = 0; i < element_count; i++) {
        if (strcmp(elements[i].name, "vertex") == 0) {
            vertex_element = &elements[i];
        } else if (strcmp(elements[i].name, "face") == 0) {
            face_element = &elements[i];
        }
    }
    if (!vertex_element || vertex_element->count == 0) {
        return YETTY_ERR(yetty_ymesh_glb_data, "ply: no vertex element");
    }

    /* Property indexes we care about. */
    int x_index = -1, y_index = -1, z_index = -1;
    int nx_index = -1, ny_index = -1, nz_index = -1;
    for (size_t i = 0; i < vertex_element->property_count; i++) {
        const char *name = vertex_element->properties[i].name;
        if (strcmp(name, "x") == 0) {
            x_index = (int)i;
        } else if (strcmp(name, "y") == 0) {
            y_index = (int)i;
        } else if (strcmp(name, "z") == 0) {
            z_index = (int)i;
        } else if (strcmp(name, "nx") == 0) {
            nx_index = (int)i;
        } else if (strcmp(name, "ny") == 0) {
            ny_index = (int)i;
        } else if (strcmp(name, "nz") == 0) {
            nz_index = (int)i;
        }
    }
    if (x_index < 0 || y_index < 0 || z_index < 0) {
        return YETTY_ERR(yetty_ymesh_glb_data, "ply: vertex element lacks x/y/z");
    }
    bool have_normals = nx_index >= 0 && ny_index >= 0 && nz_index >= 0;

    size_t vertex_count = vertex_element->count;
    float *positions = malloc(vertex_count * 3 * sizeof(float));
    float *normals = have_normals ? malloc(vertex_count * 3 * sizeof(float)) : NULL;
    if (!positions || (have_normals && !normals)) {
        free(positions);
        free(normals);
        return YETTY_ERR(yetty_ymesh_glb_data, "ply: vertex alloc failed");
    }

    /* Growable index array (faces triangulated by fan). */
    uint32_t *indices = NULL;
    size_t index_count = 0;
    size_t index_capacity = 0;

    const uint8_t *body = (const uint8_t *)line;
    const uint8_t *body_end = bytes + len;

#define PLY_FAIL(msg)                                                                              \
    do {                                                                                           \
        free(positions);                                                                           \
        free(normals);                                                                             \
        free(indices);                                                                             \
        return YETTY_ERR(yetty_ymesh_glb_data, msg);                                               \
    } while (0)

    for (size_t element_index = 0; element_index < element_count; element_index++) {
        struct ply_element *element = &elements[element_index];
        bool is_vertex = element == vertex_element;
        bool is_face = element == face_element;

        for (size_t row = 0; row < element->count; row++) {
            double values[16] = {0};
            uint32_t face_corners[64];
            size_t face_corner_count = 0;

            if (binary) {
                for (size_t prop = 0; prop < element->property_count; prop++) {
                    const struct ply_property *property = &element->properties[prop];
                    if (property->type_size > 0) {
                        if (body + property->type_size > body_end) {
                            PLY_FAIL("ply: truncated body");
                        }
                        values[prop] =
                            ply_read_scalar(body, property->type_size, property->is_float);
                        body += property->type_size;
                    } else {
                        if (body + property->list_count_size > body_end) {
                            PLY_FAIL("ply: truncated list count");
                        }
                        size_t item_count =
                            (size_t)ply_read_scalar(body, property->list_count_size, false);
                        body += property->list_count_size;
                        if (body + item_count * (size_t)property->list_item_size > body_end) {
                            PLY_FAIL("ply: truncated list body");
                        }
                        for (size_t item = 0; item < item_count; item++) {
                            uint32_t corner =
                                (uint32_t)ply_read_scalar(body, property->list_item_size, false);
                            body += property->list_item_size;
                            if (is_face && face_corner_count < 64) {
                                face_corners[face_corner_count++] = corner;
                            }
                        }
                    }
                }
            } else {
                /* ascii: one row per line. */
                while (body < body_end && (*body == '\n' || *body == '\r')) {
                    body++;
                }
                const uint8_t *row_end = memchr(body, '\n', (size_t)(body_end - body));
                if (!row_end) {
                    row_end = body_end;
                }
                char text[512];
                size_t text_len = (size_t)(row_end - body);
                if (text_len >= sizeof(text)) {
                    text_len = sizeof(text) - 1;
                }
                memcpy(text, body, text_len);
                text[text_len] = '\0';
                body = row_end;

                char *field_cursor = text;
                for (size_t prop = 0; prop < element->property_count; prop++) {
                    const struct ply_property *property = &element->properties[prop];
                    char *next = NULL;
                    if (property->type_size > 0) {
                        values[prop] = strtod(field_cursor, &next);
                        if (next == field_cursor) {
                            PLY_FAIL("ply: short ascii row");
                        }
                        field_cursor = next;
                    } else {
                        size_t item_count = (size_t)strtoul(field_cursor, &next, 10);
                        if (next == field_cursor) {
                            PLY_FAIL("ply: bad ascii list count");
                        }
                        field_cursor = next;
                        for (size_t item = 0; item < item_count; item++) {
                            uint32_t corner = (uint32_t)strtoul(field_cursor, &next, 10);
                            if (next == field_cursor) {
                                PLY_FAIL("ply: short ascii list");
                            }
                            field_cursor = next;
                            if (is_face && face_corner_count < 64) {
                                face_corners[face_corner_count++] = corner;
                            }
                        }
                    }
                }
            }

            if (is_vertex) {
                positions[row * 3 + 0] = (float)values[x_index];
                positions[row * 3 + 1] = (float)values[y_index];
                positions[row * 3 + 2] = (float)values[z_index];
                if (have_normals) {
                    normals[row * 3 + 0] = (float)values[nx_index];
                    normals[row * 3 + 1] = (float)values[ny_index];
                    normals[row * 3 + 2] = (float)values[nz_index];
                }
            } else if (is_face && face_corner_count >= 3) {
                size_t triangles = face_corner_count - 2;
                if (index_count + triangles * 3 > index_capacity) {
                    size_t grown_capacity = index_capacity ? index_capacity * 2 : 4096;
                    while (grown_capacity < index_count + triangles * 3) {
                        grown_capacity *= 2;
                    }
                    uint32_t *grown = realloc(indices, grown_capacity * sizeof(uint32_t));
                    if (!grown) {
                        PLY_FAIL("ply: index alloc failed");
                    }
                    indices = grown;
                    index_capacity = grown_capacity;
                }
                for (size_t tri = 0; tri < triangles; tri++) {
                    indices[index_count++] = face_corners[0];
                    indices[index_count++] = face_corners[tri + 1];
                    indices[index_count++] = face_corners[tri + 2];
                }
            }
        }
    }
#undef PLY_FAIL

    if (index_count == 0) {
        /* Vertex-only PLY: a point cloud. */
        struct yetty_ymesh_glb_data_result markers = points_to_markers(positions, vertex_count);
        free(positions);
        free(normals);
        free(indices);
        return markers;
    }

    struct yetty_ymesh_glb_data out = {0};
    out.positions = positions;
    out.normals = normals;
    out.indices = indices;
    out.vertex_count = vertex_count;
    out.index_count = index_count;
    out.index_size = 4;
    compute_bbox(&out);
    if (!have_normals) {
        struct yetty_ycore_void_result normals_res = compute_smooth_normals(&out);
        if (YETTY_IS_ERR(normals_res)) {
            yetty_ymesh_glb_destroy(&out);
            return YETTY_ERR(yetty_ymesh_glb_data, "ply: normals", normals_res);
        }
    }
    return YETTY_OK(yetty_ymesh_glb_data, out);
}

/*=============================================================================
 * STL (binary + ascii)
 *===========================================================================*/

static struct yetty_ymesh_glb_data_result stl_finish(float *positions, float *normals,
                                                     size_t vertex_count)
{
    if (vertex_count == 0) {
        free(positions);
        free(normals);
        return YETTY_ERR(yetty_ymesh_glb_data, "stl: no facets");
    }
    struct yetty_ymesh_glb_data out = {0};
    out.positions = positions;
    out.normals = normals;
    out.vertex_count = vertex_count;
    out.index_count = vertex_count;
    out.index_size = 4;
    out.indices = malloc(vertex_count * sizeof(uint32_t));
    if (!out.indices) {
        free(positions);
        free(normals);
        return YETTY_ERR(yetty_ymesh_glb_data, "stl: index alloc failed");
    }
    uint32_t *index_out = (uint32_t *)out.indices;
    for (size_t i = 0; i < vertex_count; i++) {
        index_out[i] = (uint32_t)i;
    }
    compute_bbox(&out);
    return YETTY_OK(yetty_ymesh_glb_data, out);
}

static struct yetty_ymesh_glb_data_result stl_parse_binary(const uint8_t *bytes, size_t len)
{
    if (len < 84) {
        return YETTY_ERR(yetty_ymesh_glb_data, "stl: truncated binary header");
    }
    uint32_t triangle_count;
    memcpy(&triangle_count, bytes + 80, sizeof(triangle_count));
    if (84 + (size_t)triangle_count * 50 > len) {
        return YETTY_ERR(yetty_ymesh_glb_data, "stl: truncated binary body");
    }
    size_t vertex_count = (size_t)triangle_count * 3;
    float *positions = malloc(vertex_count * 3 * sizeof(float));
    float *normals = malloc(vertex_count * 3 * sizeof(float));
    if (!positions || !normals) {
        free(positions);
        free(normals);
        return YETTY_ERR(yetty_ymesh_glb_data, "stl: alloc failed");
    }
    for (size_t tri = 0; tri < triangle_count; tri++) {
        const uint8_t *record = bytes + 84 + tri * 50;
        float facet_normal[3];
        memcpy(facet_normal, record, 12);
        for (int corner = 0; corner < 3; corner++) {
            size_t vertex = tri * 3 + (size_t)corner;
            memcpy(&positions[vertex * 3], record + 12 + corner * 12, 12);
            memcpy(&normals[vertex * 3], facet_normal, 12);
        }
    }
    return stl_finish(positions, normals, vertex_count);
}

static struct yetty_ymesh_glb_data_result stl_parse_ascii(const char *text, size_t len)
{
    float *positions = NULL;
    float *normals = NULL;
    size_t vertex_count = 0;
    size_t vertex_capacity = 0;
    float facet_normal[3] = {0, 1, 0};

    const char *cursor = text;
    const char *end = text + len;
    while (cursor < end) {
        while (cursor < end &&
               (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r')) {
            cursor++;
        }
        if (cursor >= end) {
            break;
        }
        if (strncmp(cursor, "facet normal", 12) == 0) {
            char *next = NULL;
            facet_normal[0] = strtof(cursor + 12, &next);
            facet_normal[1] = strtof(next, &next);
            facet_normal[2] = strtof(next, &next);
        } else if (strncmp(cursor, "vertex", 6) == 0) {
            if (vertex_count == vertex_capacity) {
                size_t grown_capacity = vertex_capacity ? vertex_capacity * 2 : 3072;
                float *grown_positions = realloc(positions, grown_capacity * 3 * sizeof(float));
                float *grown_normals = realloc(normals, grown_capacity * 3 * sizeof(float));
                if (!grown_positions || !grown_normals) {
                    free(grown_positions ? grown_positions : positions);
                    free(grown_normals ? grown_normals : normals);
                    return YETTY_ERR(yetty_ymesh_glb_data, "stl: alloc failed");
                }
                positions = grown_positions;
                normals = grown_normals;
                vertex_capacity = grown_capacity;
            }
            char *next = NULL;
            positions[vertex_count * 3 + 0] = strtof(cursor + 6, &next);
            positions[vertex_count * 3 + 1] = strtof(next, &next);
            positions[vertex_count * 3 + 2] = strtof(next, &next);
            memcpy(&normals[vertex_count * 3], facet_normal, sizeof facet_normal);
            vertex_count++;
        }
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!line_end) {
            break;
        }
        cursor = line_end + 1;
    }
    return stl_finish(positions, normals, vertex_count);
}

/*=============================================================================
 * OBJ
 *===========================================================================*/

struct obj_float_array {
    float *values;
    size_t count; /* in vec3 */
    size_t capacity;
};

static struct yetty_ycore_void_result obj_push_vec3(struct obj_float_array *array, float x, float y,
                                                    float z)
{
    if (array->count == array->capacity) {
        size_t grown_capacity = array->capacity ? array->capacity * 2 : 4096;
        float *grown = realloc(array->values, grown_capacity * 3 * sizeof(float));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "obj: alloc failed");
        }
        array->values = grown;
        array->capacity = grown_capacity;
    }
    array->values[array->count * 3 + 0] = x;
    array->values[array->count * 3 + 1] = y;
    array->values[array->count * 3 + 2] = z;
    array->count++;
    return YETTY_OK_VOID();
}

/* Parse one face corner "v", "v/vt", "v//vn" or "v/vt/vn". Returns 0-based
 * position/normal indices (-1 = absent). */
static const char *obj_parse_corner(const char *cursor, long position_count, long normal_count,
                                    long *out_position, long *out_normal)
{
    char *next = NULL;
    long position_index = strtol(cursor, &next, 10);
    if (next == cursor) {
        return NULL;
    }
    long normal_index = 0;
    bool have_normal = false;
    cursor = next;
    if (*cursor == '/') {
        cursor++;
        if (*cursor != '/') {
            strtol(cursor, &next, 10); /* texture index — unused */
            cursor = next;
        }
        if (*cursor == '/') {
            cursor++;
            normal_index = strtol(cursor, &next, 10);
            have_normal = next != cursor;
            cursor = next;
        }
    }
    *out_position = position_index > 0 ? position_index - 1 : position_count + position_index;
    *out_normal =
        have_normal ? (normal_index > 0 ? normal_index - 1 : normal_count + normal_index) : -1;
    return cursor;
}

static struct yetty_ymesh_glb_data_result obj_parse(const char *text, size_t len)
{
    struct obj_float_array source_positions = {0};
    struct obj_float_array source_normals = {0};

    /* Output vertices are emitted per unique corner occurrence (no dedup —
     * simple and fine at demo scale). */
    struct obj_float_array out_positions = {0};
    struct obj_float_array out_normals = {0};
    uint32_t *indices = NULL;
    size_t index_count = 0;
    size_t index_capacity = 0;
    bool any_missing_normal = false;

#define OBJ_FAIL(msg)                                                                              \
    do {                                                                                           \
        free(source_positions.values);                                                             \
        free(source_normals.values);                                                               \
        free(out_positions.values);                                                                \
        free(out_normals.values);                                                                  \
        free(indices);                                                                             \
        return YETTY_ERR(yetty_ymesh_glb_data, msg);                                               \
    } while (0)

    const char *cursor = text;
    const char *end = text + len;
    while (cursor < end) {
        const char *line_end = memchr(cursor, '\n', (size_t)(end - cursor));
        if (!line_end) {
            line_end = end;
        }
        if (cursor[0] == 'v' && cursor[1] == ' ') {
            char *next = NULL;
            float x = strtof(cursor + 2, &next);
            float y = strtof(next, &next);
            float z = strtof(next, &next);
            if (YETTY_IS_ERR(obj_push_vec3(&source_positions, x, y, z))) {
                OBJ_FAIL("obj: position alloc failed");
            }
        } else if (cursor[0] == 'v' && cursor[1] == 'n' && cursor[2] == ' ') {
            char *next = NULL;
            float x = strtof(cursor + 3, &next);
            float y = strtof(next, &next);
            float z = strtof(next, &next);
            if (YETTY_IS_ERR(obj_push_vec3(&source_normals, x, y, z))) {
                OBJ_FAIL("obj: normal alloc failed");
            }
        } else if (cursor[0] == 'f' && cursor[1] == ' ') {
            long corner_positions[64];
            long corner_normals[64];
            size_t corner_count = 0;
            const char *field = cursor + 2;
            while (field && field < line_end && corner_count < 64) {
                while (field < line_end && (*field == ' ' || *field == '\t')) {
                    field++;
                }
                if (field >= line_end || *field == '\r') {
                    break;
                }
                field = obj_parse_corner(
                    field, (long)source_positions.count, (long)source_normals.count,
                    &corner_positions[corner_count], &corner_normals[corner_count]);
                if (!field) {
                    break;
                }
                corner_count++;
            }
            for (size_t tri = 0; tri + 2 < corner_count; tri++) {
                size_t fan[3] = {0, tri + 1, tri + 2};
                for (int corner = 0; corner < 3; corner++) {
                    long position_index = corner_positions[fan[corner]];
                    long normal_index = corner_normals[fan[corner]];
                    if (position_index < 0 || (size_t)position_index >= source_positions.count) {
                        OBJ_FAIL("obj: face position index out of range");
                    }
                    const float *position = &source_positions.values[position_index * 3];
                    if (YETTY_IS_ERR(
                            obj_push_vec3(&out_positions, position[0], position[1], position[2]))) {
                        OBJ_FAIL("obj: vertex alloc failed");
                    }
                    if (normal_index >= 0 && (size_t)normal_index < source_normals.count) {
                        const float *normal = &source_normals.values[normal_index * 3];
                        if (YETTY_IS_ERR(
                                obj_push_vec3(&out_normals, normal[0], normal[1], normal[2]))) {
                            OBJ_FAIL("obj: normal alloc failed");
                        }
                    } else {
                        any_missing_normal = true;
                        if (YETTY_IS_ERR(obj_push_vec3(&out_normals, 0, 0, 0))) {
                            OBJ_FAIL("obj: normal alloc failed");
                        }
                    }
                    if (index_count == index_capacity) {
                        size_t grown_capacity = index_capacity ? index_capacity * 2 : 4096;
                        uint32_t *grown = realloc(indices, grown_capacity * sizeof(uint32_t));
                        if (!grown) {
                            OBJ_FAIL("obj: index alloc failed");
                        }
                        indices = grown;
                        index_capacity = grown_capacity;
                    }
                    indices[index_count] = (uint32_t)index_count;
                    index_count++;
                }
            }
        }
        cursor = line_end + 1;
    }
#undef OBJ_FAIL

    free(source_positions.values);
    free(source_normals.values);

    if (index_count == 0) {
        free(out_positions.values);
        free(out_normals.values);
        free(indices);
        return YETTY_ERR(yetty_ymesh_glb_data, "obj: no faces");
    }

    struct yetty_ymesh_glb_data out = {0};
    out.positions = out_positions.values;
    out.normals = out_normals.values;
    out.indices = indices;
    out.vertex_count = out_positions.count;
    out.index_count = index_count;
    out.index_size = 4;
    compute_bbox(&out);
    if (any_missing_normal) {
        free(out.normals);
        out.normals = NULL;
        struct yetty_ycore_void_result normals_res = compute_smooth_normals(&out);
        if (YETTY_IS_ERR(normals_res)) {
            yetty_ymesh_glb_destroy(&out);
            return YETTY_ERR(yetty_ymesh_glb_data, "obj: normals", normals_res);
        }
    }
    return YETTY_OK(yetty_ymesh_glb_data, out);
}

/*=============================================================================
 * Dispatch
 *===========================================================================*/

struct yetty_ymesh_glb_data_result yetty_ymesh_load(const uint8_t *bytes, size_t len)
{
    if (!bytes || len < 6) {
        return YETTY_ERR(yetty_ymesh_glb_data, "ymesh_load: empty/too-short input");
    }
    if (memcmp(bytes, "glTF", 4) == 0) {
        return yetty_ymesh_glb_parse(bytes, len);
    }
    if (memcmp(bytes, "ply", 3) == 0 || memcmp(bytes, "PLY", 3) == 0) {
        return ply_parse(bytes, len);
    }
    /* ascii STL opens with "solid"; anything with OBJ-style v/f lines in
     * the first kilobyte is OBJ; otherwise try binary STL (its 80-byte
     * header is arbitrary bytes). */
    if (memcmp(bytes, "solid", 5) == 0) {
        /* Some binary STLs also open with "solid" — require an ascii
         * "facet" nearby to pick the text parser. */
        size_t scan = len < 1024 ? len : 1024;
        bool ascii_stl = false;
        for (size_t i = 0; i + 5 < scan; i++) {
            if (memcmp(bytes + i, "facet", 5) == 0) {
                ascii_stl = true;
                break;
            }
        }
        if (ascii_stl) {
            return stl_parse_ascii((const char *)bytes, len);
        }
        return stl_parse_binary(bytes, len);
    }
    {
        size_t scan = len < 1024 ? len : 1024;
        for (size_t i = 0; i + 2 < scan; i++) {
            bool line_start = i == 0 || bytes[i - 1] == '\n';
            if (line_start && bytes[i] == 'v' && (bytes[i + 1] == ' ' || bytes[i + 1] == 'n')) {
                return obj_parse((const char *)bytes, len);
            }
        }
    }
    return stl_parse_binary(bytes, len);
}
