/* GENERATED — do not edit. */
/* Object API for regular class(es) `circle, box, segment, triangle, ellipse, arc, rounded_box, rhombus, pentagon, hexagon, star, pie, ring, heart, cross, rounded_x, capsule, moon, egg, octogon, hexagram, pentagram, linear_gradient_box, radial_gradient_box, sphere_3d, box_3d, torus_3d, cylinder_3d` (implementation module: ysdf2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YSDF2_SHAPES_H
#define YETTY_YCLASSGEN_API_YSDF2_SHAPES_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ysdf2_circle_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_segment_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_triangle_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_ellipse_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_arc_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_rounded_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_rhombus_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_pentagon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_hexagon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_star_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_pie_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_ring_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_heart_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_cross_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_rounded_x_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_capsule_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_moon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_egg_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_octogon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_hexagram_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_pentagram_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_linear_gradient_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_radial_gradient_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_sphere_3d_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_box_3d_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_torus_3d_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_cylinder_3d_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_circle;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CIRCLE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CIRCLE_PTR_RESULT
struct yetty_ysdf2_circle_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_circle *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_circle_ptr_result yetty_ysdf2_circle_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_to(struct yetty_ysdf2_circle *data);
struct float_result yetty_ysdf2_circle_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_circle_center_x_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_circle_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_circle_center_y_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_circle_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_circle_radius_set(struct yetty_yclass_object *obj,
                                                             float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_box;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_BOX_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_BOX_PTR_RESULT
struct yetty_ysdf2_box_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_box *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_box_ptr_result yetty_ysdf2_box_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_to(struct yetty_ysdf2_box *data);
struct float_result yetty_ysdf2_box_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_center_x_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_box_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_center_y_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_box_half_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_half_width_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_box_half_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_half_height_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_box_corner_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_corner_radius_set(struct yetty_yclass_object *obj,
                                                                 float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_segment;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_SEGMENT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_SEGMENT_PTR_RESULT
struct yetty_ysdf2_segment_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_segment *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_segment_ptr_result yetty_ysdf2_segment_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_to(struct yetty_ysdf2_segment *data);
struct float_result yetty_ysdf2_segment_start_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_segment_start_x_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_segment_start_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_segment_start_y_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_segment_end_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_segment_end_x_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_segment_end_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_segment_end_y_set(struct yetty_yclass_object *obj,
                                                             float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_triangle;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_TRIANGLE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_TRIANGLE_PTR_RESULT
struct yetty_ysdf2_triangle_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_triangle *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_triangle_ptr_result yetty_ysdf2_triangle_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_to(struct yetty_ysdf2_triangle *data);
struct float_result yetty_ysdf2_triangle_vertex_a_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_x_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_triangle_vertex_a_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_y_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_triangle_vertex_b_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_x_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_triangle_vertex_b_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_y_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_triangle_vertex_c_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_x_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_triangle_vertex_c_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_y_set(struct yetty_yclass_object *obj,
                                                                   float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_ellipse;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ELLIPSE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ELLIPSE_PTR_RESULT
struct yetty_ysdf2_ellipse_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_ellipse *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_ellipse_ptr_result yetty_ysdf2_ellipse_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_to(struct yetty_ysdf2_ellipse *data);
struct float_result yetty_ysdf2_ellipse_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_x_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_ellipse_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_y_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_ellipse_radius_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_x_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_ellipse_radius_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_y_set(struct yetty_yclass_object *obj,
                                                                float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_arc;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ARC_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ARC_PTR_RESULT
struct yetty_ysdf2_arc_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_arc *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_arc_ptr_result yetty_ysdf2_arc_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_to(struct yetty_ysdf2_arc *data);
struct float_result yetty_ysdf2_arc_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_arc_center_x_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_arc_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_arc_center_y_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_arc_aperture_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_x_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_arc_aperture_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_y_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_arc_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_arc_radius_set(struct yetty_yclass_object *obj,
                                                          float value);
struct float_result yetty_ysdf2_arc_thickness_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_arc_thickness_set(struct yetty_yclass_object *obj,
                                                             float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_rounded_box;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ROUNDED_BOX_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ROUNDED_BOX_PTR_RESULT
struct yetty_ysdf2_rounded_box_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_rounded_box *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_rounded_box_ptr_result yetty_ysdf2_rounded_box_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_to(
    struct yetty_ysdf2_rounded_box *data);
struct float_result yetty_ysdf2_rounded_box_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_x_set(struct yetty_yclass_object *obj,
                                                                    float value);
struct float_result yetty_ysdf2_rounded_box_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_y_set(struct yetty_yclass_object *obj,
                                                                    float value);
struct float_result yetty_ysdf2_rounded_box_half_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_width_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_rounded_box_half_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_height_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_rounded_box_radius_top_right_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_right_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_rounded_box_radius_bottom_right_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_right_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_rounded_box_radius_top_left_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_left_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_rounded_box_radius_bottom_left_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_left_set(
    struct yetty_yclass_object *obj, float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_rhombus;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_RHOMBUS_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_RHOMBUS_PTR_RESULT
struct yetty_ysdf2_rhombus_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_rhombus *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_rhombus_ptr_result yetty_ysdf2_rhombus_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_to(struct yetty_ysdf2_rhombus *data);
struct float_result yetty_ysdf2_rhombus_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_x_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_rhombus_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_y_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_rhombus_half_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_width_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_rhombus_half_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_height_set(struct yetty_yclass_object *obj,
                                                                   float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_pentagon;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_PENTAGON_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_PENTAGON_PTR_RESULT
struct yetty_ysdf2_pentagon_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_pentagon *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_pentagon_ptr_result yetty_ysdf2_pentagon_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_to(struct yetty_ysdf2_pentagon *data);
struct float_result yetty_ysdf2_pentagon_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_x_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_pentagon_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_y_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_pentagon_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pentagon_radius_set(struct yetty_yclass_object *obj,
                                                               float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_hexagon;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_HEXAGON_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_HEXAGON_PTR_RESULT
struct yetty_ysdf2_hexagon_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_hexagon *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_hexagon_ptr_result yetty_ysdf2_hexagon_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_to(struct yetty_ysdf2_hexagon *data);
struct float_result yetty_ysdf2_hexagon_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_x_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_hexagon_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_y_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_hexagon_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_hexagon_radius_set(struct yetty_yclass_object *obj,
                                                              float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_star;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_STAR_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_STAR_PTR_RESULT
struct yetty_ysdf2_star_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_star *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_star_ptr_result yetty_ysdf2_star_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_star_to(struct yetty_ysdf2_star *data);
struct float_result yetty_ysdf2_star_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_star_center_x_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_star_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_star_center_y_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_star_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_star_radius_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ysdf2_star_num_points_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_star_num_points_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_star_inner_ratio_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_star_inner_ratio_set(struct yetty_yclass_object *obj,
                                                                float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_pie;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_PIE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_PIE_PTR_RESULT
struct yetty_ysdf2_pie_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_pie *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_pie_ptr_result yetty_ysdf2_pie_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_to(struct yetty_ysdf2_pie *data);
struct float_result yetty_ysdf2_pie_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pie_center_x_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_pie_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pie_center_y_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_pie_aperture_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_x_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_pie_aperture_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_y_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_pie_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pie_radius_set(struct yetty_yclass_object *obj,
                                                          float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_ring;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_RING_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_RING_PTR_RESULT
struct yetty_ysdf2_ring_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_ring *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_ring_ptr_result yetty_ysdf2_ring_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_to(struct yetty_ysdf2_ring *data);
struct float_result yetty_ysdf2_ring_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ring_center_x_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_ring_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ring_center_y_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_ring_normal_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ring_normal_x_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_ring_normal_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ring_normal_y_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_ring_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ring_radius_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ysdf2_ring_thickness_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_ring_thickness_set(struct yetty_yclass_object *obj,
                                                              float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_heart;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_HEART_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_HEART_PTR_RESULT
struct yetty_ysdf2_heart_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_heart *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_heart_ptr_result yetty_ysdf2_heart_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_to(struct yetty_ysdf2_heart *data);
struct float_result yetty_ysdf2_heart_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_heart_center_x_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_heart_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_heart_center_y_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_heart_scale_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_heart_scale_set(struct yetty_yclass_object *obj,
                                                           float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_cross;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CROSS_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CROSS_PTR_RESULT
struct yetty_ysdf2_cross_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_cross *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_cross_ptr_result yetty_ysdf2_cross_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_to(struct yetty_ysdf2_cross *data);
struct float_result yetty_ysdf2_cross_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cross_center_x_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_cross_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cross_center_y_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_ysdf2_cross_half_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cross_half_width_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_cross_half_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cross_half_height_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_cross_corner_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cross_corner_radius_set(struct yetty_yclass_object *obj,
                                                                   float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_rounded_x;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ROUNDED_X_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_ROUNDED_X_PTR_RESULT
struct yetty_ysdf2_rounded_x_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_rounded_x *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_rounded_x_ptr_result yetty_ysdf2_rounded_x_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_to(struct yetty_ysdf2_rounded_x *data);
struct float_result yetty_ysdf2_rounded_x_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_x_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_rounded_x_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_y_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_rounded_x_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_width_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_rounded_x_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_radius_set(struct yetty_yclass_object *obj,
                                                                float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_capsule;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CAPSULE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CAPSULE_PTR_RESULT
struct yetty_ysdf2_capsule_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_capsule *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_capsule_ptr_result yetty_ysdf2_capsule_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_to(struct yetty_ysdf2_capsule *data);
struct float_result yetty_ysdf2_capsule_start_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_capsule_start_x_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_capsule_start_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_capsule_start_y_set(struct yetty_yclass_object *obj,
                                                               float value);
struct float_result yetty_ysdf2_capsule_end_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_capsule_end_x_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_capsule_end_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_capsule_end_y_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_capsule_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_capsule_radius_set(struct yetty_yclass_object *obj,
                                                              float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_moon;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_MOON_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_MOON_PTR_RESULT
struct yetty_ysdf2_moon_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_moon *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_moon_ptr_result yetty_ysdf2_moon_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_to(struct yetty_ysdf2_moon *data);
struct float_result yetty_ysdf2_moon_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_moon_center_x_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_moon_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_moon_center_y_set(struct yetty_yclass_object *obj,
                                                             float value);
struct float_result yetty_ysdf2_moon_offset_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_moon_offset_set(struct yetty_yclass_object *obj,
                                                           float value);
struct float_result yetty_ysdf2_moon_radius_outer_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_moon_radius_outer_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_moon_radius_inner_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_moon_radius_inner_set(struct yetty_yclass_object *obj,
                                                                 float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_egg;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_EGG_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_EGG_PTR_RESULT
struct yetty_ysdf2_egg_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_egg *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_egg_ptr_result yetty_ysdf2_egg_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_to(struct yetty_ysdf2_egg *data);
struct float_result yetty_ysdf2_egg_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_egg_center_x_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_egg_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_egg_center_y_set(struct yetty_yclass_object *obj,
                                                            float value);
struct float_result yetty_ysdf2_egg_radius_outer_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_egg_radius_outer_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_egg_radius_inner_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_egg_radius_inner_set(struct yetty_yclass_object *obj,
                                                                float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_octogon;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_OCTOGON_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_OCTOGON_PTR_RESULT
struct yetty_ysdf2_octogon_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_octogon *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_octogon_ptr_result yetty_ysdf2_octogon_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_to(struct yetty_ysdf2_octogon *data);
struct float_result yetty_ysdf2_octogon_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_octogon_center_x_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_octogon_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_octogon_center_y_set(struct yetty_yclass_object *obj,
                                                                float value);
struct float_result yetty_ysdf2_octogon_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_octogon_radius_set(struct yetty_yclass_object *obj,
                                                              float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_hexagram;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_HEXAGRAM_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_HEXAGRAM_PTR_RESULT
struct yetty_ysdf2_hexagram_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_hexagram *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_hexagram_ptr_result yetty_ysdf2_hexagram_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_to(struct yetty_ysdf2_hexagram *data);
struct float_result yetty_ysdf2_hexagram_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_x_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_hexagram_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_y_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_hexagram_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_hexagram_radius_set(struct yetty_yclass_object *obj,
                                                               float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_pentagram;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_PENTAGRAM_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_PENTAGRAM_PTR_RESULT
struct yetty_ysdf2_pentagram_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_pentagram *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_pentagram_ptr_result yetty_ysdf2_pentagram_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_to(struct yetty_ysdf2_pentagram *data);
struct float_result yetty_ysdf2_pentagram_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_x_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_pentagram_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_y_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_pentagram_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_pentagram_radius_set(struct yetty_yclass_object *obj,
                                                                float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_linear_gradient_box;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_LINEAR_GRADIENT_BOX_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_LINEAR_GRADIENT_BOX_PTR_RESULT
struct yetty_ysdf2_linear_gradient_box_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_linear_gradient_box *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_linear_gradient_box_ptr_result yetty_ysdf2_linear_gradient_box_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_to(
    struct yetty_ysdf2_linear_gradient_box *data);
struct float_result yetty_ysdf2_linear_gradient_box_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_x_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_y_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_half_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_width_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_half_height_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_height_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_corner_radius_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_corner_radius_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_grad_x0_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x0_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_grad_y0_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y0_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_grad_x1_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x1_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_linear_gradient_box_grad_y1_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y1_set(
    struct yetty_yclass_object *obj, float value);
struct uint32_result yetty_ysdf2_linear_gradient_box_color0_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color0_set(
    struct yetty_yclass_object *obj, uint32_t value);
struct uint32_result yetty_ysdf2_linear_gradient_box_color1_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color1_set(
    struct yetty_yclass_object *obj, uint32_t value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_radial_gradient_box;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_RADIAL_GRADIENT_BOX_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_RADIAL_GRADIENT_BOX_PTR_RESULT
struct yetty_ysdf2_radial_gradient_box_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_radial_gradient_box *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_radial_gradient_box_ptr_result yetty_ysdf2_radial_gradient_box_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_to(
    struct yetty_ysdf2_radial_gradient_box *data);
struct float_result yetty_ysdf2_radial_gradient_box_center_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_x_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_radial_gradient_box_center_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_y_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_radial_gradient_box_half_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_width_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_radial_gradient_box_half_height_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_height_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_radial_gradient_box_corner_radius_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_corner_radius_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_radial_gradient_box_grad_cx_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cx_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_radial_gradient_box_grad_cy_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cy_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_radial_gradient_box_grad_radius_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_radius_set(
    struct yetty_yclass_object *obj, float value);
struct uint32_result yetty_ysdf2_radial_gradient_box_color_inner_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_inner_set(
    struct yetty_yclass_object *obj, uint32_t value);
struct uint32_result yetty_ysdf2_radial_gradient_box_color_outer_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_outer_set(
    struct yetty_yclass_object *obj, uint32_t value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_sphere_3d;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_SPHERE_3D_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_SPHERE_3D_PTR_RESULT
struct yetty_ysdf2_sphere_3d_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_sphere_3d *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_sphere_3d_ptr_result yetty_ysdf2_sphere_3d_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_to(struct yetty_ysdf2_sphere_3d *data);
struct float_result yetty_ysdf2_sphere_3d_position_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_x_set(struct yetty_yclass_object *obj,
                                                                    float value);
struct float_result yetty_ysdf2_sphere_3d_position_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_y_set(struct yetty_yclass_object *obj,
                                                                    float value);
struct float_result yetty_ysdf2_sphere_3d_position_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_z_set(struct yetty_yclass_object *obj,
                                                                    float value);
struct float_result yetty_ysdf2_sphere_3d_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_radius_set(struct yetty_yclass_object *obj,
                                                                float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_box_3d;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_BOX_3D_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_BOX_3D_PTR_RESULT
struct yetty_ysdf2_box_3d_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_box_3d *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_box_3d_ptr_result yetty_ysdf2_box_3d_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_to(struct yetty_ysdf2_box_3d *data);
struct float_result yetty_ysdf2_box_3d_position_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_x_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_box_3d_position_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_y_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_box_3d_position_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_z_set(struct yetty_yclass_object *obj,
                                                                 float value);
struct float_result yetty_ysdf2_box_3d_half_size_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_x_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_box_3d_half_size_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_y_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_box_3d_half_size_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_z_set(struct yetty_yclass_object *obj,
                                                                  float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_torus_3d;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_TORUS_3D_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_TORUS_3D_PTR_RESULT
struct yetty_ysdf2_torus_3d_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_torus_3d *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_torus_3d_ptr_result yetty_ysdf2_torus_3d_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_to(struct yetty_ysdf2_torus_3d *data);
struct float_result yetty_ysdf2_torus_3d_position_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_x_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_torus_3d_position_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_y_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_torus_3d_position_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_z_set(struct yetty_yclass_object *obj,
                                                                   float value);
struct float_result yetty_ysdf2_torus_3d_major_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_major_radius_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_torus_3d_minor_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_minor_radius_set(
    struct yetty_yclass_object *obj, float value);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ysdf2_cylinder_3d;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CYLINDER_3D_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSDF2_CYLINDER_3D_PTR_RESULT
struct yetty_ysdf2_cylinder_3d_ptr_result {
    int ok;
    union {
        struct yetty_ysdf2_cylinder_3d *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ysdf2_cylinder_3d_ptr_result yetty_ysdf2_cylinder_3d_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_to(
    struct yetty_ysdf2_cylinder_3d *data);
struct float_result yetty_ysdf2_cylinder_3d_position_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_x_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_cylinder_3d_position_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_y_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_cylinder_3d_position_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_z_set(
    struct yetty_yclass_object *obj, float value);
struct float_result yetty_ysdf2_cylinder_3d_radius_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_radius_set(struct yetty_yclass_object *obj,
                                                                  float value);
struct float_result yetty_ysdf2_cylinder_3d_half_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_half_height_set(
    struct yetty_yclass_object *obj, float value);

struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_star_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
