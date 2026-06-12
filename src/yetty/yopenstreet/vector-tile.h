#ifndef YETTY_YOPENSTREET_VECTOR_TILE_H
#define YETTY_YOPENSTREET_VECTOR_TILE_H

/*
 * vector-tile.h — module-internal Mapbox Vector Tile (MVT) decoder.
 *
 * A minimal hand-rolled protobuf-subset reader for the MVT 2.x layout
 * (https://github.com/mapbox/vector-tile-spec): varint / zigzag /
 * length-delimited fields only, no libprotobuf. Decodes the tile into a
 * flat in-memory model; geometry command streams (MoveTo / LineTo /
 * ClosePath) are expanded into point rings up front so the renderer
 * never touches the wire encoding.
 *
 * Only the two tag values the renderer styles by are extracted per
 * feature: "kind" (the shortbread schema's class key) and "name".
 * The bytes are untrusted network input — every read is bounds-checked.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

enum yetty_yopenstreet_vt_geom {
    YETTY_YOPENSTREET_VT_POINT = 1,
    YETTY_YOPENSTREET_VT_LINESTRING = 2,
    YETTY_YOPENSTREET_VT_POLYGON = 3,
};

struct yetty_yopenstreet_vt_point {
    float x; /* tile-local, 0..extent (may exceed: buffer geometry) */
    float y;
};

/* One MoveTo-started run: a linestring part, or one polygon ring. */
struct yetty_yopenstreet_vt_ring {
    struct yetty_yopenstreet_vt_point *points;
    uint32_t point_count;
};

struct yetty_yopenstreet_vt_feature {
    uint32_t geom_type; /* enum yetty_yopenstreet_vt_geom */
    const char *kind;   /* borrowed from the layer's value table; NULL if absent */
    const char *name;   /* idem */
    struct yetty_yopenstreet_vt_ring *rings;
    uint32_t ring_count;
};

struct yetty_yopenstreet_vt_layer {
    char *name;
    uint32_t extent; /* tile-local coordinate span, normally 4096 */
    struct yetty_yopenstreet_vt_feature *features;
    uint32_t feature_count;
    /* Backing string tables the feature kind/name pointers alias. */
    char **keys;
    uint32_t key_count;
    char **values; /* non-string protobuf values decode to NULL slots */
    uint32_t value_count;
};

struct yetty_yopenstreet_vt_tile {
    struct yetty_yopenstreet_vt_layer *layers;
    uint32_t layer_count;
};

/* Parse `len` bytes of MVT protobuf into *out_tile (caller-zeroed).
 * On error the partially-built tile is freed; *out_tile is valid (possibly
 * empty) only on OK. */
struct yetty_ycore_void_result yetty_yopenstreet_vt_parse(
    const uint8_t *bytes, size_t len, struct yetty_yopenstreet_vt_tile *out_tile);

void yetty_yopenstreet_vt_free(struct yetty_yopenstreet_vt_tile *tile);

#endif /* YETTY_YOPENSTREET_VECTOR_TILE_H */
