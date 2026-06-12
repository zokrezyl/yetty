/*
 * vector-tile.c — minimal MVT (Mapbox Vector Tile) decoder.
 *
 * See vector-tile.h. Protobuf message numbers (vector_tile.proto, v2.1):
 *
 *   Tile:    3 = layers (len-delimited, repeated)
 *   Layer:   1 = name (string)        2 = features (len, repeated)
 *            3 = keys (string, rep.)  4 = values (len, repeated)
 *            5 = extent (varint)      15 = version (varint)
 *   Feature: 1 = id (varint)          2 = tags (packed varints)
 *            3 = type (varint)        4 = geometry (packed varints)
 *   Value:   1 = string_value — the only variant we keep; the numeric /
 *            bool variants land as NULL table slots (the renderer styles
 *            by "kind"/"name", both strings).
 *
 * Geometry command stream: u32 = (command_id & 0x7) | (count << 3);
 * MoveTo=1 (2 zigzag params), LineTo=2 (2 params), ClosePath=7 (0).
 * Coordinates are cumulative deltas across the whole feature.
 */

#include "vector-tile.h"

#include <stdlib.h>
#include <string.h>

/* Hard caps against malicious / corrupt tiles. */
#define VT_MAX_LAYERS 64u
#define VT_MAX_FEATURES_PER_LAYER 65536u
#define VT_MAX_POINTS_PER_FEATURE 262144u
#define VT_MAX_STRING_TABLE 65536u

/*=============================================================================
 * Bounded protobuf reader
 *===========================================================================*/

struct vt_reader {
    const uint8_t *bytes;
    size_t len;
    size_t pos;
};

static struct yetty_ycore_void_result vt_read_varint(struct vt_reader *reader, uint64_t *out_value)
{
    uint64_t value = 0;
    unsigned shift = 0;
    while (reader->pos < reader->len && shift < 64) {
        uint8_t byte = reader->bytes[reader->pos++];
        value |= (uint64_t)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) {
            *out_value = value;
            return YETTY_OK_VOID();
        }
        shift += 7;
    }
    return YETTY_ERR(yetty_ycore_void, "mvt: truncated varint");
}

static int64_t vt_zigzag_decode(uint64_t value)
{
    return (int64_t)(value >> 1) ^ -(int64_t)(value & 1);
}

/* Skip one field of the given protobuf wire type. */
static struct yetty_ycore_void_result vt_skip_field(struct vt_reader *reader, uint32_t wire_type)
{
    uint64_t skip_value = 0;
    switch (wire_type) {
    case 0: /* varint */
        return vt_read_varint(reader, &skip_value);
    case 1: /* fixed64 */
        if (reader->len - reader->pos < 8) {
            return YETTY_ERR(yetty_ycore_void, "mvt: truncated fixed64");
        }
        reader->pos += 8;
        return YETTY_OK_VOID();
    case 2: { /* length-delimited */
        struct yetty_ycore_void_result len_res = vt_read_varint(reader, &skip_value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, len_res, "mvt: skip length");
        if (skip_value > reader->len - reader->pos) {
            return YETTY_ERR(yetty_ycore_void, "mvt: skip length out of bounds");
        }
        reader->pos += (size_t)skip_value;
        return YETTY_OK_VOID();
    }
    case 5: /* fixed32 */
        if (reader->len - reader->pos < 4) {
            return YETTY_ERR(yetty_ycore_void, "mvt: truncated fixed32");
        }
        reader->pos += 4;
        return YETTY_OK_VOID();
    default:
        return YETTY_ERR(yetty_ycore_void, "mvt: unsupported wire type");
    }
}

/* Read a length-delimited slice, returning a sub-reader over it. */
static struct yetty_ycore_void_result vt_read_slice(struct vt_reader *reader,
                                                    struct vt_reader *out_slice)
{
    uint64_t slice_len = 0;
    struct yetty_ycore_void_result len_res = vt_read_varint(reader, &slice_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, len_res, "mvt: slice length");
    if (slice_len > reader->len - reader->pos) {
        return YETTY_ERR(yetty_ycore_void, "mvt: slice out of bounds");
    }
    out_slice->bytes = reader->bytes + reader->pos;
    out_slice->len = (size_t)slice_len;
    out_slice->pos = 0;
    reader->pos += (size_t)slice_len;
    return YETTY_OK_VOID();
}

static char *vt_strdup_slice(const struct vt_reader *slice)
{
    char *copy = malloc(slice->len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, slice->bytes, slice->len);
    copy[slice->len] = '\0';
    return copy;
}

/*=============================================================================
 * Growable arrays
 *===========================================================================*/

static void *vt_grow(void *array, uint32_t needed_count, uint32_t *capacity, size_t element_size)
{
    if (needed_count <= *capacity) {
        return array;
    }
    uint32_t new_capacity = *capacity ? *capacity * 2 : 16;
    while (new_capacity < needed_count) {
        new_capacity *= 2;
    }
    void *new_array = realloc(array, (size_t)new_capacity * element_size);
    if (new_array) {
        *capacity = new_capacity;
    }
    return new_array;
}

/*=============================================================================
 * Geometry command stream → point rings
 *===========================================================================*/

static struct yetty_ycore_void_result vt_parse_geometry(
    struct vt_reader *geometry, struct yetty_yopenstreet_vt_feature *feature)
{
    uint32_t ring_capacity = 0;
    uint32_t point_capacity = 0; /* capacity of the CURRENT ring */
    struct yetty_yopenstreet_vt_ring *current_ring = NULL;
    int64_t cursor_x = 0;
    int64_t cursor_y = 0;
    uint32_t total_points = 0;

    while (geometry->pos < geometry->len) {
        uint64_t command_word = 0;
        struct yetty_ycore_void_result cmd_res = vt_read_varint(geometry, &command_word);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cmd_res, "mvt: geometry command");
        uint32_t command_id = (uint32_t)(command_word & 0x7);
        uint32_t repeat_count = (uint32_t)(command_word >> 3);

        if (command_id == 7) { /* ClosePath — ring already complete */
            current_ring = NULL;
            point_capacity = 0;
            continue;
        }
        if (command_id != 1 && command_id != 2) {
            return YETTY_ERR(yetty_ycore_void, "mvt: unknown geometry command");
        }

        for (uint32_t repeat = 0; repeat < repeat_count; repeat++) {
            uint64_t raw_dx = 0;
            uint64_t raw_dy = 0;
            struct yetty_ycore_void_result dx_res = vt_read_varint(geometry, &raw_dx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, dx_res, "mvt: geometry dx");
            struct yetty_ycore_void_result dy_res = vt_read_varint(geometry, &raw_dy);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, dy_res, "mvt: geometry dy");
            cursor_x += vt_zigzag_decode(raw_dx);
            cursor_y += vt_zigzag_decode(raw_dy);

            if (++total_points > VT_MAX_POINTS_PER_FEATURE) {
                return YETTY_ERR(yetty_ycore_void, "mvt: feature point count over cap");
            }

            if (command_id == 1 /* MoveTo */) {
                struct yetty_yopenstreet_vt_ring *grown_rings =
                    vt_grow(feature->rings, feature->ring_count + 1, &ring_capacity,
                            sizeof(struct yetty_yopenstreet_vt_ring));
                if (!grown_rings) {
                    return YETTY_ERR(yetty_ycore_void, "mvt: ring array alloc failed");
                }
                feature->rings = grown_rings;
                current_ring = &feature->rings[feature->ring_count++];
                memset(current_ring, 0, sizeof(*current_ring));
                point_capacity = 0;
            }
            if (!current_ring) {
                return YETTY_ERR(yetty_ycore_void, "mvt: LineTo before MoveTo");
            }
            struct yetty_yopenstreet_vt_point *grown_points =
                vt_grow(current_ring->points, current_ring->point_count + 1, &point_capacity,
                        sizeof(struct yetty_yopenstreet_vt_point));
            if (!grown_points) {
                return YETTY_ERR(yetty_ycore_void, "mvt: point array alloc failed");
            }
            current_ring->points = grown_points;
            current_ring->points[current_ring->point_count].x = (float)cursor_x;
            current_ring->points[current_ring->point_count].y = (float)cursor_y;
            current_ring->point_count++;
        }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Feature / Value / Layer / Tile
 *===========================================================================*/

static void vt_feature_free(struct yetty_yopenstreet_vt_feature *feature)
{
    for (uint32_t i = 0; i < feature->ring_count; i++) {
        free(feature->rings[i].points);
    }
    free(feature->rings);
}

/* Tags are (key index, value index) pairs into the layer tables; resolve
 * just "kind" and "name". */
static void vt_resolve_feature_tags(const struct yetty_yopenstreet_vt_layer *layer,
                                    const uint32_t *tag_pairs, uint32_t tag_pair_count,
                                    struct yetty_yopenstreet_vt_feature *feature)
{
    for (uint32_t i = 0; i + 1 < tag_pair_count * 2; i += 2) {
        uint32_t key_index = tag_pairs[i];
        uint32_t value_index = tag_pairs[i + 1];
        if (key_index >= layer->key_count || value_index >= layer->value_count) {
            continue;
        }
        const char *key = layer->keys[key_index];
        const char *value = layer->values[value_index];
        if (!key || !value) {
            continue;
        }
        if (strcmp(key, "kind") == 0) {
            feature->kind = value;
        } else if (strcmp(key, "name") == 0) {
            feature->name = value;
        }
    }
}

static struct yetty_ycore_void_result vt_parse_feature(struct vt_reader *reader,
                                                       struct yetty_yopenstreet_vt_layer *layer,
                                                       struct yetty_yopenstreet_vt_feature *feature)
{
    uint32_t *tag_pairs = NULL;
    uint32_t tag_pair_words = 0;
    uint32_t tag_capacity = 0;
    struct vt_reader geometry_slice = {0};
    int have_geometry = 0;

    while (reader->pos < reader->len) {
        uint64_t field_header = 0;
        struct yetty_ycore_void_result header_res = vt_read_varint(reader, &field_header);
        if (YETTY_IS_ERR(header_res)) {
            free(tag_pairs);
            return YETTY_ERR(yetty_ycore_void, "mvt: feature field header", header_res);
        }
        uint32_t field_number = (uint32_t)(field_header >> 3);
        uint32_t wire_type = (uint32_t)(field_header & 0x7);

        if (field_number == 3 && wire_type == 0) { /* type */
            uint64_t geom_type = 0;
            struct yetty_ycore_void_result type_res = vt_read_varint(reader, &geom_type);
            if (YETTY_IS_ERR(type_res)) {
                free(tag_pairs);
                return YETTY_ERR(yetty_ycore_void, "mvt: feature type", type_res);
            }
            feature->geom_type = (uint32_t)geom_type;
        } else if (field_number == 2 && wire_type == 2) { /* packed tags */
            struct vt_reader tags_slice = {0};
            struct yetty_ycore_void_result slice_res = vt_read_slice(reader, &tags_slice);
            if (YETTY_IS_ERR(slice_res)) {
                free(tag_pairs);
                return YETTY_ERR(yetty_ycore_void, "mvt: tags slice", slice_res);
            }
            while (tags_slice.pos < tags_slice.len) {
                uint64_t tag_word = 0;
                struct yetty_ycore_void_result tag_res = vt_read_varint(&tags_slice, &tag_word);
                if (YETTY_IS_ERR(tag_res)) {
                    free(tag_pairs);
                    return YETTY_ERR(yetty_ycore_void, "mvt: tag varint", tag_res);
                }
                uint32_t *grown_tags =
                    vt_grow(tag_pairs, tag_pair_words + 1, &tag_capacity, sizeof(uint32_t));
                if (!grown_tags) {
                    free(tag_pairs);
                    return YETTY_ERR(yetty_ycore_void, "mvt: tag array alloc failed");
                }
                tag_pairs = grown_tags;
                tag_pairs[tag_pair_words++] = (uint32_t)tag_word;
            }
        } else if (field_number == 4 && wire_type == 2) { /* geometry */
            struct yetty_ycore_void_result slice_res = vt_read_slice(reader, &geometry_slice);
            if (YETTY_IS_ERR(slice_res)) {
                free(tag_pairs);
                return YETTY_ERR(yetty_ycore_void, "mvt: geometry slice", slice_res);
            }
            have_geometry = 1;
        } else {
            struct yetty_ycore_void_result skip_res = vt_skip_field(reader, wire_type);
            if (YETTY_IS_ERR(skip_res)) {
                free(tag_pairs);
                return YETTY_ERR(yetty_ycore_void, "mvt: feature skip", skip_res);
            }
        }
    }

    if (have_geometry) {
        struct yetty_ycore_void_result geom_res = vt_parse_geometry(&geometry_slice, feature);
        if (YETTY_IS_ERR(geom_res)) {
            free(tag_pairs);
            vt_feature_free(feature);
            memset(feature, 0, sizeof(*feature));
            return YETTY_ERR(yetty_ycore_void, "mvt: geometry parse", geom_res);
        }
    }
    vt_resolve_feature_tags(layer, tag_pairs, tag_pair_words / 2, feature);
    free(tag_pairs);
    return YETTY_OK_VOID();
}

/* Value message: keep field 1 (string), anything else → NULL slot. */
static struct yetty_ycore_void_result vt_parse_value(struct vt_reader *reader, char **out_string)
{
    *out_string = NULL;
    while (reader->pos < reader->len) {
        uint64_t field_header = 0;
        struct yetty_ycore_void_result header_res = vt_read_varint(reader, &field_header);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, header_res, "mvt: value field header");
        uint32_t field_number = (uint32_t)(field_header >> 3);
        uint32_t wire_type = (uint32_t)(field_header & 0x7);
        if (field_number == 1 && wire_type == 2) {
            struct vt_reader string_slice = {0};
            struct yetty_ycore_void_result slice_res = vt_read_slice(reader, &string_slice);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, slice_res, "mvt: value string slice");
            free(*out_string);
            *out_string = vt_strdup_slice(&string_slice);
            if (!*out_string) {
                return YETTY_ERR(yetty_ycore_void, "mvt: value string alloc failed");
            }
        } else {
            struct yetty_ycore_void_result skip_res = vt_skip_field(reader, wire_type);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, skip_res, "mvt: value skip");
        }
    }
    return YETTY_OK_VOID();
}

static void vt_layer_free(struct yetty_yopenstreet_vt_layer *layer)
{
    for (uint32_t i = 0; i < layer->feature_count; i++) {
        vt_feature_free(&layer->features[i]);
    }
    free(layer->features);
    for (uint32_t i = 0; i < layer->key_count; i++) {
        free(layer->keys[i]);
    }
    free(layer->keys);
    for (uint32_t i = 0; i < layer->value_count; i++) {
        free(layer->values[i]);
    }
    free(layer->values);
    free(layer->name);
}

static struct yetty_ycore_void_result vt_parse_layer(struct vt_reader *reader,
                                                     struct yetty_yopenstreet_vt_layer *layer)
{
    layer->extent = 4096;
    uint32_t feature_capacity = 0;
    uint32_t key_capacity = 0;
    uint32_t value_capacity = 0;

    /* Two passes are avoided by deferring tag resolution: features parsed
     * before the full key/value tables exist would resolve against holes.
     * MVT writers emit name/keys/values before features in practice, but
     * the spec doesn't promise it — so collect feature slices first,
     * parse them after the tables are complete. */
    struct vt_reader *feature_slices = NULL;
    uint32_t feature_slice_count = 0;
    uint32_t feature_slice_capacity = 0;
    struct yetty_ycore_void_result result = YETTY_OK_VOID();

    while (reader->pos < reader->len) {
        uint64_t field_header = 0;
        result = vt_read_varint(reader, &field_header);
        if (YETTY_IS_ERR(result)) {
            break;
        }
        uint32_t field_number = (uint32_t)(field_header >> 3);
        uint32_t wire_type = (uint32_t)(field_header & 0x7);

        if (field_number == 1 && wire_type == 2) { /* name */
            struct vt_reader name_slice = {0};
            result = vt_read_slice(reader, &name_slice);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            free(layer->name);
            layer->name = vt_strdup_slice(&name_slice);
            if (!layer->name) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: layer name alloc failed");
                break;
            }
        } else if (field_number == 2 && wire_type == 2) { /* feature */
            struct vt_reader feature_slice = {0};
            result = vt_read_slice(reader, &feature_slice);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            if (feature_slice_count >= VT_MAX_FEATURES_PER_LAYER) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: feature count over cap");
                break;
            }
            struct vt_reader *grown_slices =
                vt_grow(feature_slices, feature_slice_count + 1, &feature_slice_capacity,
                        sizeof(struct vt_reader));
            if (!grown_slices) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: feature slice array alloc failed");
                break;
            }
            feature_slices = grown_slices;
            feature_slices[feature_slice_count++] = feature_slice;
        } else if (field_number == 3 && wire_type == 2) { /* key */
            struct vt_reader key_slice = {0};
            result = vt_read_slice(reader, &key_slice);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            if (layer->key_count >= VT_MAX_STRING_TABLE) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: key table over cap");
                break;
            }
            char **grown_keys =
                vt_grow(layer->keys, layer->key_count + 1, &key_capacity, sizeof(char *));
            if (!grown_keys) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: key table alloc failed");
                break;
            }
            layer->keys = grown_keys;
            layer->keys[layer->key_count] = vt_strdup_slice(&key_slice);
            if (!layer->keys[layer->key_count]) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: key string alloc failed");
                break;
            }
            layer->key_count++;
        } else if (field_number == 4 && wire_type == 2) { /* value */
            struct vt_reader value_slice = {0};
            result = vt_read_slice(reader, &value_slice);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            if (layer->value_count >= VT_MAX_STRING_TABLE) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: value table over cap");
                break;
            }
            char **grown_values =
                vt_grow(layer->values, layer->value_count + 1, &value_capacity, sizeof(char *));
            if (!grown_values) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: value table alloc failed");
                break;
            }
            layer->values = grown_values;
            char *value_string = NULL;
            result = vt_parse_value(&value_slice, &value_string);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            layer->values[layer->value_count++] = value_string; /* may be NULL (non-string) */
        } else if (field_number == 5 && wire_type == 0) {       /* extent */
            uint64_t extent = 0;
            result = vt_read_varint(reader, &extent);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            if (extent == 0 || extent > 65536) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: extent out of range");
                break;
            }
            layer->extent = (uint32_t)extent;
        } else {
            result = vt_skip_field(reader, wire_type);
            if (YETTY_IS_ERR(result)) {
                break;
            }
        }
    }

    if (YETTY_IS_OK(result)) {
        for (uint32_t i = 0; i < feature_slice_count; i++) {
            struct yetty_yopenstreet_vt_feature *grown_features =
                vt_grow(layer->features, layer->feature_count + 1, &feature_capacity,
                        sizeof(struct yetty_yopenstreet_vt_feature));
            if (!grown_features) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: feature array alloc failed");
                break;
            }
            layer->features = grown_features;
            struct yetty_yopenstreet_vt_feature *feature = &layer->features[layer->feature_count];
            memset(feature, 0, sizeof(*feature));
            result = vt_parse_feature(&feature_slices[i], layer, feature);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            layer->feature_count++;
        }
    }

    free(feature_slices);
    return result;
}

struct yetty_ycore_void_result yetty_yopenstreet_vt_parse(
    const uint8_t *bytes, size_t len, struct yetty_yopenstreet_vt_tile *out_tile)
{
    if (!bytes || len == 0 || !out_tile) {
        return YETTY_ERR(yetty_ycore_void, "mvt: NULL input");
    }
    memset(out_tile, 0, sizeof(*out_tile));

    struct vt_reader reader = {.bytes = bytes, .len = len, .pos = 0};
    uint32_t layer_capacity = 0;
    struct yetty_ycore_void_result result = YETTY_OK_VOID();

    while (reader.pos < reader.len) {
        uint64_t field_header = 0;
        result = vt_read_varint(&reader, &field_header);
        if (YETTY_IS_ERR(result)) {
            break;
        }
        uint32_t field_number = (uint32_t)(field_header >> 3);
        uint32_t wire_type = (uint32_t)(field_header & 0x7);

        if (field_number == 3 && wire_type == 2) { /* layer */
            struct vt_reader layer_slice = {0};
            result = vt_read_slice(&reader, &layer_slice);
            if (YETTY_IS_ERR(result)) {
                break;
            }
            if (out_tile->layer_count >= VT_MAX_LAYERS) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: layer count over cap");
                break;
            }
            struct yetty_yopenstreet_vt_layer *grown_layers =
                vt_grow(out_tile->layers, out_tile->layer_count + 1, &layer_capacity,
                        sizeof(struct yetty_yopenstreet_vt_layer));
            if (!grown_layers) {
                result = YETTY_ERR(yetty_ycore_void, "mvt: layer array alloc failed");
                break;
            }
            out_tile->layers = grown_layers;
            struct yetty_yopenstreet_vt_layer *layer = &out_tile->layers[out_tile->layer_count];
            memset(layer, 0, sizeof(*layer));
            /* Count the slot immediately so an error inside the layer
             * parse still frees the partial layer in vt_free. */
            out_tile->layer_count++;
            result = vt_parse_layer(&layer_slice, layer);
            if (YETTY_IS_ERR(result)) {
                break;
            }
        } else {
            result = vt_skip_field(&reader, wire_type);
            if (YETTY_IS_ERR(result)) {
                break;
            }
        }
    }

    if (YETTY_IS_ERR(result)) {
        yetty_yopenstreet_vt_free(out_tile);
        return YETTY_ERR(yetty_ycore_void, "mvt: tile parse failed", result);
    }
    return YETTY_OK_VOID();
}

void yetty_yopenstreet_vt_free(struct yetty_yopenstreet_vt_tile *tile)
{
    if (!tile) {
        return;
    }
    for (uint32_t i = 0; i < tile->layer_count; i++) {
        vt_layer_free(&tile->layers[i]);
    }
    free(tile->layers);
    memset(tile, 0, sizeof(*tile));
}
