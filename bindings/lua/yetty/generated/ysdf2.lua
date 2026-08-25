-- yetty.ysdf2 bindings — GENERATED from model.yaml, do not edit.
local ffi = require("ffi")
local rt = require("yetty.runtime")
require("yetty.generated._types")
require("yetty.generated.ydrawlist2")
local unpack = unpack or table.unpack
ffi.cdef[[
struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_circle_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_circle_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_circle_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_circle_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_circle_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_circle_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_box_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_half_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_half_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_half_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_half_height_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_corner_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_corner_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_segment_start_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_segment_start_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_segment_start_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_segment_start_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_segment_end_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_segment_end_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_segment_end_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_segment_end_y_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_triangle_vertex_a_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_triangle_vertex_a_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_triangle_vertex_b_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_triangle_vertex_b_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_triangle_vertex_c_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_triangle_vertex_c_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_y_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_ellipse_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ellipse_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ellipse_radius_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ellipse_radius_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_y_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_arc_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_arc_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_arc_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_arc_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_arc_aperture_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_arc_aperture_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_arc_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_arc_radius_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_arc_thickness_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_arc_thickness_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_rounded_box_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_box_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_box_half_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_box_half_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_height_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_box_radius_top_right_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_right_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_box_radius_bottom_right_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_right_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_box_radius_top_left_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_left_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_box_radius_bottom_left_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_left_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_rhombus_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rhombus_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rhombus_half_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rhombus_half_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_height_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_pentagon_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pentagon_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pentagon_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pentagon_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_hexagon_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_hexagon_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_hexagon_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_hexagon_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_star_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_star_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_star_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_star_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_star_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_star_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_star_radius_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_star_num_points_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_star_num_points_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_star_inner_ratio_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_star_inner_ratio_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_pie_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pie_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pie_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pie_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pie_aperture_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pie_aperture_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pie_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pie_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_ring_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ring_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ring_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ring_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ring_normal_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ring_normal_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ring_normal_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ring_normal_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ring_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ring_radius_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_ring_thickness_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_ring_thickness_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_heart_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_heart_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_heart_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_heart_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_heart_scale_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_heart_scale_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_cross_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cross_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cross_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cross_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cross_half_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cross_half_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cross_half_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cross_half_height_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cross_corner_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cross_corner_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_rounded_x_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_x_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_x_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_rounded_x_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_rounded_x_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_capsule_start_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_capsule_start_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_capsule_start_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_capsule_start_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_capsule_end_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_capsule_end_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_capsule_end_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_capsule_end_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_capsule_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_capsule_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_moon_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_moon_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_moon_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_moon_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_moon_offset_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_moon_offset_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_moon_radius_outer_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_moon_radius_outer_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_moon_radius_inner_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_moon_radius_inner_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_egg_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_egg_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_egg_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_egg_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_egg_radius_outer_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_egg_radius_outer_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_egg_radius_inner_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_egg_radius_inner_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_octogon_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_octogon_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_octogon_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_octogon_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_octogon_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_octogon_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_hexagram_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_hexagram_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_hexagram_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_hexagram_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_pentagram_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pentagram_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_pentagram_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_pentagram_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_linear_gradient_box_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_half_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_half_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_height_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_corner_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_corner_radius_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_grad_x0_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x0_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_grad_y0_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y0_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_grad_x1_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x1_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_linear_gradient_box_grad_y1_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y1_set(struct yetty_yclass_object *, float);
struct uint32_result yetty_ysdf2_linear_gradient_box_color0_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color0_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ysdf2_linear_gradient_box_color1_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color1_set(struct yetty_yclass_object *, uint32_t);
struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_radial_gradient_box_center_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_radial_gradient_box_center_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_radial_gradient_box_half_width_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_width_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_radial_gradient_box_half_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_height_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_radial_gradient_box_corner_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_corner_radius_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_radial_gradient_box_grad_cx_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cx_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_radial_gradient_box_grad_cy_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cy_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_radial_gradient_box_grad_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_radius_set(struct yetty_yclass_object *, float);
struct uint32_result yetty_ysdf2_radial_gradient_box_color_inner_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_inner_set(struct yetty_yclass_object *, uint32_t);
struct uint32_result yetty_ysdf2_radial_gradient_box_color_outer_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_outer_set(struct yetty_yclass_object *, uint32_t);
struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_sphere_3d_position_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_sphere_3d_position_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_sphere_3d_position_z_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_z_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_sphere_3d_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_box_3d_position_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_3d_position_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_3d_position_z_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_z_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_3d_half_size_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_3d_half_size_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_box_3d_half_size_z_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_z_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_torus_3d_position_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_torus_3d_position_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_torus_3d_position_z_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_z_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_torus_3d_major_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_major_radius_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_torus_3d_minor_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_torus_3d_minor_radius_set(struct yetty_yclass_object *, float);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_create(struct yetty_yclass_ctx *);
struct float_result yetty_ysdf2_cylinder_3d_position_x_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_x_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cylinder_3d_position_y_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_y_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cylinder_3d_position_z_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_z_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cylinder_3d_radius_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_radius_set(struct yetty_yclass_object *, float);
struct float_result yetty_ysdf2_cylinder_3d_half_height_get(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_half_height_set(struct yetty_yclass_object *, float);
]]
local M = {}
local Circle = {}
Circle.__prop_get = {}
Circle.__prop_set = {}
local Circle_instance_mt = {
  __index = function(obj, key)
    local member = Circle[key]
    if member ~= nil then return member end
    local getter = Circle.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Circle.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Circle.new()
  local res = rt.C().yetty_ysdf2_circle_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Circle_instance_mt)
  rt.own(obj, Circle)
  return obj
end
function Circle:set_fill(color)
  rt.live(self, "Circle:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Circle:set_stroke(color)
  rt.live(self, "Circle:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Circle:pack(list)
  rt.live(self, "Circle:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Circle.__prop_get.center_x = function(obj)
  rt.live(obj, "Circle.center_x")
  local res = rt.C().yetty_ysdf2_circle_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Circle.center_x")
  local res = rt.C().yetty_ysdf2_circle_center_x_set(obj.handle, value)
  rt.check(res)
end
Circle.__prop_get.center_y = function(obj)
  rt.live(obj, "Circle.center_y")
  local res = rt.C().yetty_ysdf2_circle_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Circle.center_y")
  local res = rt.C().yetty_ysdf2_circle_center_y_set(obj.handle, value)
  rt.check(res)
end
Circle.__prop_get.radius = function(obj)
  rt.live(obj, "Circle.radius")
  local res = rt.C().yetty_ysdf2_circle_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.radius = function(obj, value)
  rt.live(obj, "Circle.radius")
  local res = rt.C().yetty_ysdf2_circle_radius_set(obj.handle, value)
  rt.check(res)
end
Circle.__prop_get.id = function(obj)
  rt.live(obj, "Circle.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.id = function(obj, value)
  rt.live(obj, "Circle.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Circle.__prop_get.z = function(obj)
  rt.live(obj, "Circle.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.z = function(obj, value)
  rt.live(obj, "Circle.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Circle.__prop_get.fill = function(obj)
  rt.live(obj, "Circle.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.fill = function(obj, value)
  rt.live(obj, "Circle.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Circle.__prop_get.stroke = function(obj)
  rt.live(obj, "Circle.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Circle.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Circle.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Circle.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Circle.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Circle.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Circle:destroy()
  rt.object_free(self)
end
Circle.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Circle, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Circle = Circle
local Box = {}
Box.__prop_get = {}
Box.__prop_set = {}
local Box_instance_mt = {
  __index = function(obj, key)
    local member = Box[key]
    if member ~= nil then return member end
    local getter = Box.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Box.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Box.new()
  local res = rt.C().yetty_ysdf2_box_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Box_instance_mt)
  rt.own(obj, Box)
  return obj
end
function Box:set_fill(color)
  rt.live(self, "Box:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Box:set_stroke(color)
  rt.live(self, "Box:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Box:pack(list)
  rt.live(self, "Box:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Box.__prop_get.center_x = function(obj)
  rt.live(obj, "Box.center_x")
  local res = rt.C().yetty_ysdf2_box_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Box.center_x")
  local res = rt.C().yetty_ysdf2_box_center_x_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.center_y = function(obj)
  rt.live(obj, "Box.center_y")
  local res = rt.C().yetty_ysdf2_box_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Box.center_y")
  local res = rt.C().yetty_ysdf2_box_center_y_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.half_width = function(obj)
  rt.live(obj, "Box.half_width")
  local res = rt.C().yetty_ysdf2_box_half_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.half_width = function(obj, value)
  rt.live(obj, "Box.half_width")
  local res = rt.C().yetty_ysdf2_box_half_width_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.half_height = function(obj)
  rt.live(obj, "Box.half_height")
  local res = rt.C().yetty_ysdf2_box_half_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.half_height = function(obj, value)
  rt.live(obj, "Box.half_height")
  local res = rt.C().yetty_ysdf2_box_half_height_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.corner_radius = function(obj)
  rt.live(obj, "Box.corner_radius")
  local res = rt.C().yetty_ysdf2_box_corner_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.corner_radius = function(obj, value)
  rt.live(obj, "Box.corner_radius")
  local res = rt.C().yetty_ysdf2_box_corner_radius_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.id = function(obj)
  rt.live(obj, "Box.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.id = function(obj, value)
  rt.live(obj, "Box.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.z = function(obj)
  rt.live(obj, "Box.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.z = function(obj, value)
  rt.live(obj, "Box.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.fill = function(obj)
  rt.live(obj, "Box.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.fill = function(obj, value)
  rt.live(obj, "Box.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.stroke = function(obj)
  rt.live(obj, "Box.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Box.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Box.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Box.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Box.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Box.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Box:destroy()
  rt.object_free(self)
end
Box.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    corner_radius = true,
    fill = true,
    half_height = true,
    half_width = true,
    id = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Box, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Box = Box
local Segment = {}
Segment.__prop_get = {}
Segment.__prop_set = {}
local Segment_instance_mt = {
  __index = function(obj, key)
    local member = Segment[key]
    if member ~= nil then return member end
    local getter = Segment.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Segment.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Segment.new()
  local res = rt.C().yetty_ysdf2_segment_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Segment_instance_mt)
  rt.own(obj, Segment)
  return obj
end
function Segment:set_fill(color)
  rt.live(self, "Segment:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Segment:set_stroke(color)
  rt.live(self, "Segment:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Segment:pack(list)
  rt.live(self, "Segment:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Segment.__prop_get.start_x = function(obj)
  rt.live(obj, "Segment.start_x")
  local res = rt.C().yetty_ysdf2_segment_start_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.start_x = function(obj, value)
  rt.live(obj, "Segment.start_x")
  local res = rt.C().yetty_ysdf2_segment_start_x_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.start_y = function(obj)
  rt.live(obj, "Segment.start_y")
  local res = rt.C().yetty_ysdf2_segment_start_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.start_y = function(obj, value)
  rt.live(obj, "Segment.start_y")
  local res = rt.C().yetty_ysdf2_segment_start_y_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.end_x = function(obj)
  rt.live(obj, "Segment.end_x")
  local res = rt.C().yetty_ysdf2_segment_end_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.end_x = function(obj, value)
  rt.live(obj, "Segment.end_x")
  local res = rt.C().yetty_ysdf2_segment_end_x_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.end_y = function(obj)
  rt.live(obj, "Segment.end_y")
  local res = rt.C().yetty_ysdf2_segment_end_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.end_y = function(obj, value)
  rt.live(obj, "Segment.end_y")
  local res = rt.C().yetty_ysdf2_segment_end_y_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.id = function(obj)
  rt.live(obj, "Segment.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.id = function(obj, value)
  rt.live(obj, "Segment.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.z = function(obj)
  rt.live(obj, "Segment.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.z = function(obj, value)
  rt.live(obj, "Segment.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.fill = function(obj)
  rt.live(obj, "Segment.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.fill = function(obj, value)
  rt.live(obj, "Segment.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.stroke = function(obj)
  rt.live(obj, "Segment.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Segment.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Segment.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Segment.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Segment.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Segment.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Segment:destroy()
  rt.object_free(self)
end
Segment.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    end_x = true,
    end_y = true,
    fill = true,
    id = true,
    start_x = true,
    start_y = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Segment, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Segment = Segment
local Triangle = {}
Triangle.__prop_get = {}
Triangle.__prop_set = {}
local Triangle_instance_mt = {
  __index = function(obj, key)
    local member = Triangle[key]
    if member ~= nil then return member end
    local getter = Triangle.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Triangle.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Triangle.new()
  local res = rt.C().yetty_ysdf2_triangle_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Triangle_instance_mt)
  rt.own(obj, Triangle)
  return obj
end
function Triangle:set_fill(color)
  rt.live(self, "Triangle:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Triangle:set_stroke(color)
  rt.live(self, "Triangle:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Triangle:pack(list)
  rt.live(self, "Triangle:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Triangle.__prop_get.vertex_a_x = function(obj)
  rt.live(obj, "Triangle.vertex_a_x")
  local res = rt.C().yetty_ysdf2_triangle_vertex_a_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.vertex_a_x = function(obj, value)
  rt.live(obj, "Triangle.vertex_a_x")
  local res = rt.C().yetty_ysdf2_triangle_vertex_a_x_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.vertex_a_y = function(obj)
  rt.live(obj, "Triangle.vertex_a_y")
  local res = rt.C().yetty_ysdf2_triangle_vertex_a_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.vertex_a_y = function(obj, value)
  rt.live(obj, "Triangle.vertex_a_y")
  local res = rt.C().yetty_ysdf2_triangle_vertex_a_y_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.vertex_b_x = function(obj)
  rt.live(obj, "Triangle.vertex_b_x")
  local res = rt.C().yetty_ysdf2_triangle_vertex_b_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.vertex_b_x = function(obj, value)
  rt.live(obj, "Triangle.vertex_b_x")
  local res = rt.C().yetty_ysdf2_triangle_vertex_b_x_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.vertex_b_y = function(obj)
  rt.live(obj, "Triangle.vertex_b_y")
  local res = rt.C().yetty_ysdf2_triangle_vertex_b_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.vertex_b_y = function(obj, value)
  rt.live(obj, "Triangle.vertex_b_y")
  local res = rt.C().yetty_ysdf2_triangle_vertex_b_y_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.vertex_c_x = function(obj)
  rt.live(obj, "Triangle.vertex_c_x")
  local res = rt.C().yetty_ysdf2_triangle_vertex_c_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.vertex_c_x = function(obj, value)
  rt.live(obj, "Triangle.vertex_c_x")
  local res = rt.C().yetty_ysdf2_triangle_vertex_c_x_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.vertex_c_y = function(obj)
  rt.live(obj, "Triangle.vertex_c_y")
  local res = rt.C().yetty_ysdf2_triangle_vertex_c_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.vertex_c_y = function(obj, value)
  rt.live(obj, "Triangle.vertex_c_y")
  local res = rt.C().yetty_ysdf2_triangle_vertex_c_y_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.id = function(obj)
  rt.live(obj, "Triangle.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.id = function(obj, value)
  rt.live(obj, "Triangle.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.z = function(obj)
  rt.live(obj, "Triangle.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.z = function(obj, value)
  rt.live(obj, "Triangle.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.fill = function(obj)
  rt.live(obj, "Triangle.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.fill = function(obj, value)
  rt.live(obj, "Triangle.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.stroke = function(obj)
  rt.live(obj, "Triangle.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Triangle.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Triangle.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Triangle.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Triangle.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Triangle.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Triangle:destroy()
  rt.object_free(self)
end
Triangle.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    fill = true,
    id = true,
    stroke = true,
    stroke_width = true,
    vertex_a_x = true,
    vertex_a_y = true,
    vertex_b_x = true,
    vertex_b_y = true,
    vertex_c_x = true,
    vertex_c_y = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Triangle, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Triangle = Triangle
local Ellipse = {}
Ellipse.__prop_get = {}
Ellipse.__prop_set = {}
local Ellipse_instance_mt = {
  __index = function(obj, key)
    local member = Ellipse[key]
    if member ~= nil then return member end
    local getter = Ellipse.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ellipse.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ellipse.new()
  local res = rt.C().yetty_ysdf2_ellipse_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ellipse_instance_mt)
  rt.own(obj, Ellipse)
  return obj
end
function Ellipse:set_fill(color)
  rt.live(self, "Ellipse:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Ellipse:set_stroke(color)
  rt.live(self, "Ellipse:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Ellipse:pack(list)
  rt.live(self, "Ellipse:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Ellipse.__prop_get.center_x = function(obj)
  rt.live(obj, "Ellipse.center_x")
  local res = rt.C().yetty_ysdf2_ellipse_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Ellipse.center_x")
  local res = rt.C().yetty_ysdf2_ellipse_center_x_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.center_y = function(obj)
  rt.live(obj, "Ellipse.center_y")
  local res = rt.C().yetty_ysdf2_ellipse_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Ellipse.center_y")
  local res = rt.C().yetty_ysdf2_ellipse_center_y_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.radius_x = function(obj)
  rt.live(obj, "Ellipse.radius_x")
  local res = rt.C().yetty_ysdf2_ellipse_radius_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.radius_x = function(obj, value)
  rt.live(obj, "Ellipse.radius_x")
  local res = rt.C().yetty_ysdf2_ellipse_radius_x_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.radius_y = function(obj)
  rt.live(obj, "Ellipse.radius_y")
  local res = rt.C().yetty_ysdf2_ellipse_radius_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.radius_y = function(obj, value)
  rt.live(obj, "Ellipse.radius_y")
  local res = rt.C().yetty_ysdf2_ellipse_radius_y_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.id = function(obj)
  rt.live(obj, "Ellipse.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.id = function(obj, value)
  rt.live(obj, "Ellipse.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.z = function(obj)
  rt.live(obj, "Ellipse.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.z = function(obj, value)
  rt.live(obj, "Ellipse.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.fill = function(obj)
  rt.live(obj, "Ellipse.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.fill = function(obj, value)
  rt.live(obj, "Ellipse.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.stroke = function(obj)
  rt.live(obj, "Ellipse.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Ellipse.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Ellipse.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Ellipse.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Ellipse.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Ellipse.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Ellipse:destroy()
  rt.object_free(self)
end
Ellipse.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius_x = true,
    radius_y = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Ellipse, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ellipse = Ellipse
local Arc = {}
Arc.__prop_get = {}
Arc.__prop_set = {}
local Arc_instance_mt = {
  __index = function(obj, key)
    local member = Arc[key]
    if member ~= nil then return member end
    local getter = Arc.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Arc.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Arc.new()
  local res = rt.C().yetty_ysdf2_arc_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Arc_instance_mt)
  rt.own(obj, Arc)
  return obj
end
function Arc:set_fill(color)
  rt.live(self, "Arc:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Arc:set_stroke(color)
  rt.live(self, "Arc:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Arc:pack(list)
  rt.live(self, "Arc:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Arc.__prop_get.center_x = function(obj)
  rt.live(obj, "Arc.center_x")
  local res = rt.C().yetty_ysdf2_arc_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Arc.center_x")
  local res = rt.C().yetty_ysdf2_arc_center_x_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.center_y = function(obj)
  rt.live(obj, "Arc.center_y")
  local res = rt.C().yetty_ysdf2_arc_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Arc.center_y")
  local res = rt.C().yetty_ysdf2_arc_center_y_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.aperture_x = function(obj)
  rt.live(obj, "Arc.aperture_x")
  local res = rt.C().yetty_ysdf2_arc_aperture_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.aperture_x = function(obj, value)
  rt.live(obj, "Arc.aperture_x")
  local res = rt.C().yetty_ysdf2_arc_aperture_x_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.aperture_y = function(obj)
  rt.live(obj, "Arc.aperture_y")
  local res = rt.C().yetty_ysdf2_arc_aperture_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.aperture_y = function(obj, value)
  rt.live(obj, "Arc.aperture_y")
  local res = rt.C().yetty_ysdf2_arc_aperture_y_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.radius = function(obj)
  rt.live(obj, "Arc.radius")
  local res = rt.C().yetty_ysdf2_arc_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.radius = function(obj, value)
  rt.live(obj, "Arc.radius")
  local res = rt.C().yetty_ysdf2_arc_radius_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.thickness = function(obj)
  rt.live(obj, "Arc.thickness")
  local res = rt.C().yetty_ysdf2_arc_thickness_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.thickness = function(obj, value)
  rt.live(obj, "Arc.thickness")
  local res = rt.C().yetty_ysdf2_arc_thickness_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.id = function(obj)
  rt.live(obj, "Arc.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.id = function(obj, value)
  rt.live(obj, "Arc.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.z = function(obj)
  rt.live(obj, "Arc.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.z = function(obj, value)
  rt.live(obj, "Arc.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.fill = function(obj)
  rt.live(obj, "Arc.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.fill = function(obj, value)
  rt.live(obj, "Arc.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.stroke = function(obj)
  rt.live(obj, "Arc.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Arc.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Arc.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Arc.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Arc.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Arc.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Arc:destroy()
  rt.object_free(self)
end
Arc.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    aperture_x = true,
    aperture_y = true,
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    thickness = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Arc, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Arc = Arc
local RoundedBox = {}
RoundedBox.__prop_get = {}
RoundedBox.__prop_set = {}
local RoundedBox_instance_mt = {
  __index = function(obj, key)
    local member = RoundedBox[key]
    if member ~= nil then return member end
    local getter = RoundedBox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = RoundedBox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function RoundedBox.new()
  local res = rt.C().yetty_ysdf2_rounded_box_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, RoundedBox_instance_mt)
  rt.own(obj, RoundedBox)
  return obj
end
function RoundedBox:set_fill(color)
  rt.live(self, "RoundedBox:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function RoundedBox:set_stroke(color)
  rt.live(self, "RoundedBox:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function RoundedBox:pack(list)
  rt.live(self, "RoundedBox:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
RoundedBox.__prop_get.center_x = function(obj)
  rt.live(obj, "RoundedBox.center_x")
  local res = rt.C().yetty_ysdf2_rounded_box_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.center_x = function(obj, value)
  rt.live(obj, "RoundedBox.center_x")
  local res = rt.C().yetty_ysdf2_rounded_box_center_x_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.center_y = function(obj)
  rt.live(obj, "RoundedBox.center_y")
  local res = rt.C().yetty_ysdf2_rounded_box_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.center_y = function(obj, value)
  rt.live(obj, "RoundedBox.center_y")
  local res = rt.C().yetty_ysdf2_rounded_box_center_y_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.half_width = function(obj)
  rt.live(obj, "RoundedBox.half_width")
  local res = rt.C().yetty_ysdf2_rounded_box_half_width_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.half_width = function(obj, value)
  rt.live(obj, "RoundedBox.half_width")
  local res = rt.C().yetty_ysdf2_rounded_box_half_width_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.half_height = function(obj)
  rt.live(obj, "RoundedBox.half_height")
  local res = rt.C().yetty_ysdf2_rounded_box_half_height_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.half_height = function(obj, value)
  rt.live(obj, "RoundedBox.half_height")
  local res = rt.C().yetty_ysdf2_rounded_box_half_height_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.radius_top_right = function(obj)
  rt.live(obj, "RoundedBox.radius_top_right")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_top_right_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.radius_top_right = function(obj, value)
  rt.live(obj, "RoundedBox.radius_top_right")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_top_right_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.radius_bottom_right = function(obj)
  rt.live(obj, "RoundedBox.radius_bottom_right")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_bottom_right_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.radius_bottom_right = function(obj, value)
  rt.live(obj, "RoundedBox.radius_bottom_right")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_bottom_right_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.radius_top_left = function(obj)
  rt.live(obj, "RoundedBox.radius_top_left")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_top_left_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.radius_top_left = function(obj, value)
  rt.live(obj, "RoundedBox.radius_top_left")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_top_left_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.radius_bottom_left = function(obj)
  rt.live(obj, "RoundedBox.radius_bottom_left")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_bottom_left_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.radius_bottom_left = function(obj, value)
  rt.live(obj, "RoundedBox.radius_bottom_left")
  local res = rt.C().yetty_ysdf2_rounded_box_radius_bottom_left_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.id = function(obj)
  rt.live(obj, "RoundedBox.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.id = function(obj, value)
  rt.live(obj, "RoundedBox.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.z = function(obj)
  rt.live(obj, "RoundedBox.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.z = function(obj, value)
  rt.live(obj, "RoundedBox.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.fill = function(obj)
  rt.live(obj, "RoundedBox.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.fill = function(obj, value)
  rt.live(obj, "RoundedBox.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.stroke = function(obj)
  rt.live(obj, "RoundedBox.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.stroke = function(obj, value)
  rt.live(obj, "RoundedBox.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
RoundedBox.__prop_get.stroke_width = function(obj)
  rt.live(obj, "RoundedBox.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedBox.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "RoundedBox.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function RoundedBox:destroy()
  rt.object_free(self)
end
RoundedBox.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    half_height = true,
    half_width = true,
    id = true,
    radius_bottom_left = true,
    radius_bottom_right = true,
    radius_top_left = true,
    radius_top_right = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(RoundedBox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.RoundedBox = RoundedBox
local Rhombus = {}
Rhombus.__prop_get = {}
Rhombus.__prop_set = {}
local Rhombus_instance_mt = {
  __index = function(obj, key)
    local member = Rhombus[key]
    if member ~= nil then return member end
    local getter = Rhombus.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Rhombus.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Rhombus.new()
  local res = rt.C().yetty_ysdf2_rhombus_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Rhombus_instance_mt)
  rt.own(obj, Rhombus)
  return obj
end
function Rhombus:set_fill(color)
  rt.live(self, "Rhombus:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Rhombus:set_stroke(color)
  rt.live(self, "Rhombus:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Rhombus:pack(list)
  rt.live(self, "Rhombus:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Rhombus.__prop_get.center_x = function(obj)
  rt.live(obj, "Rhombus.center_x")
  local res = rt.C().yetty_ysdf2_rhombus_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Rhombus.center_x")
  local res = rt.C().yetty_ysdf2_rhombus_center_x_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.center_y = function(obj)
  rt.live(obj, "Rhombus.center_y")
  local res = rt.C().yetty_ysdf2_rhombus_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Rhombus.center_y")
  local res = rt.C().yetty_ysdf2_rhombus_center_y_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.half_width = function(obj)
  rt.live(obj, "Rhombus.half_width")
  local res = rt.C().yetty_ysdf2_rhombus_half_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.half_width = function(obj, value)
  rt.live(obj, "Rhombus.half_width")
  local res = rt.C().yetty_ysdf2_rhombus_half_width_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.half_height = function(obj)
  rt.live(obj, "Rhombus.half_height")
  local res = rt.C().yetty_ysdf2_rhombus_half_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.half_height = function(obj, value)
  rt.live(obj, "Rhombus.half_height")
  local res = rt.C().yetty_ysdf2_rhombus_half_height_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.id = function(obj)
  rt.live(obj, "Rhombus.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.id = function(obj, value)
  rt.live(obj, "Rhombus.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.z = function(obj)
  rt.live(obj, "Rhombus.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.z = function(obj, value)
  rt.live(obj, "Rhombus.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.fill = function(obj)
  rt.live(obj, "Rhombus.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.fill = function(obj, value)
  rt.live(obj, "Rhombus.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.stroke = function(obj)
  rt.live(obj, "Rhombus.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Rhombus.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Rhombus.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Rhombus.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Rhombus.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Rhombus.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Rhombus:destroy()
  rt.object_free(self)
end
Rhombus.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    half_height = true,
    half_width = true,
    id = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Rhombus, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Rhombus = Rhombus
local Pentagon = {}
Pentagon.__prop_get = {}
Pentagon.__prop_set = {}
local Pentagon_instance_mt = {
  __index = function(obj, key)
    local member = Pentagon[key]
    if member ~= nil then return member end
    local getter = Pentagon.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Pentagon.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Pentagon.new()
  local res = rt.C().yetty_ysdf2_pentagon_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Pentagon_instance_mt)
  rt.own(obj, Pentagon)
  return obj
end
function Pentagon:set_fill(color)
  rt.live(self, "Pentagon:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Pentagon:set_stroke(color)
  rt.live(self, "Pentagon:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Pentagon:pack(list)
  rt.live(self, "Pentagon:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Pentagon.__prop_get.center_x = function(obj)
  rt.live(obj, "Pentagon.center_x")
  local res = rt.C().yetty_ysdf2_pentagon_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Pentagon.center_x")
  local res = rt.C().yetty_ysdf2_pentagon_center_x_set(obj.handle, value)
  rt.check(res)
end
Pentagon.__prop_get.center_y = function(obj)
  rt.live(obj, "Pentagon.center_y")
  local res = rt.C().yetty_ysdf2_pentagon_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Pentagon.center_y")
  local res = rt.C().yetty_ysdf2_pentagon_center_y_set(obj.handle, value)
  rt.check(res)
end
Pentagon.__prop_get.radius = function(obj)
  rt.live(obj, "Pentagon.radius")
  local res = rt.C().yetty_ysdf2_pentagon_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.radius = function(obj, value)
  rt.live(obj, "Pentagon.radius")
  local res = rt.C().yetty_ysdf2_pentagon_radius_set(obj.handle, value)
  rt.check(res)
end
Pentagon.__prop_get.id = function(obj)
  rt.live(obj, "Pentagon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.id = function(obj, value)
  rt.live(obj, "Pentagon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Pentagon.__prop_get.z = function(obj)
  rt.live(obj, "Pentagon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.z = function(obj, value)
  rt.live(obj, "Pentagon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Pentagon.__prop_get.fill = function(obj)
  rt.live(obj, "Pentagon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.fill = function(obj, value)
  rt.live(obj, "Pentagon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Pentagon.__prop_get.stroke = function(obj)
  rt.live(obj, "Pentagon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Pentagon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Pentagon.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Pentagon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagon.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Pentagon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Pentagon:destroy()
  rt.object_free(self)
end
Pentagon.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Pentagon, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Pentagon = Pentagon
local Hexagon = {}
Hexagon.__prop_get = {}
Hexagon.__prop_set = {}
local Hexagon_instance_mt = {
  __index = function(obj, key)
    local member = Hexagon[key]
    if member ~= nil then return member end
    local getter = Hexagon.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Hexagon.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Hexagon.new()
  local res = rt.C().yetty_ysdf2_hexagon_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Hexagon_instance_mt)
  rt.own(obj, Hexagon)
  return obj
end
function Hexagon:set_fill(color)
  rt.live(self, "Hexagon:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Hexagon:set_stroke(color)
  rt.live(self, "Hexagon:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Hexagon:pack(list)
  rt.live(self, "Hexagon:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Hexagon.__prop_get.center_x = function(obj)
  rt.live(obj, "Hexagon.center_x")
  local res = rt.C().yetty_ysdf2_hexagon_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Hexagon.center_x")
  local res = rt.C().yetty_ysdf2_hexagon_center_x_set(obj.handle, value)
  rt.check(res)
end
Hexagon.__prop_get.center_y = function(obj)
  rt.live(obj, "Hexagon.center_y")
  local res = rt.C().yetty_ysdf2_hexagon_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Hexagon.center_y")
  local res = rt.C().yetty_ysdf2_hexagon_center_y_set(obj.handle, value)
  rt.check(res)
end
Hexagon.__prop_get.radius = function(obj)
  rt.live(obj, "Hexagon.radius")
  local res = rt.C().yetty_ysdf2_hexagon_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.radius = function(obj, value)
  rt.live(obj, "Hexagon.radius")
  local res = rt.C().yetty_ysdf2_hexagon_radius_set(obj.handle, value)
  rt.check(res)
end
Hexagon.__prop_get.id = function(obj)
  rt.live(obj, "Hexagon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.id = function(obj, value)
  rt.live(obj, "Hexagon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Hexagon.__prop_get.z = function(obj)
  rt.live(obj, "Hexagon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.z = function(obj, value)
  rt.live(obj, "Hexagon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Hexagon.__prop_get.fill = function(obj)
  rt.live(obj, "Hexagon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.fill = function(obj, value)
  rt.live(obj, "Hexagon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Hexagon.__prop_get.stroke = function(obj)
  rt.live(obj, "Hexagon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Hexagon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Hexagon.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Hexagon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagon.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Hexagon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Hexagon:destroy()
  rt.object_free(self)
end
Hexagon.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Hexagon, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Hexagon = Hexagon
local Star = {}
Star.__prop_get = {}
Star.__prop_set = {}
local Star_instance_mt = {
  __index = function(obj, key)
    local member = Star[key]
    if member ~= nil then return member end
    local getter = Star.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Star.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Star.new()
  local res = rt.C().yetty_ysdf2_star_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Star_instance_mt)
  rt.own(obj, Star)
  return obj
end
function Star:set_fill(color)
  rt.live(self, "Star:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Star:set_stroke(color)
  rt.live(self, "Star:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Star:pack(list)
  rt.live(self, "Star:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Star.__prop_get.center_x = function(obj)
  rt.live(obj, "Star.center_x")
  local res = rt.C().yetty_ysdf2_star_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Star.center_x")
  local res = rt.C().yetty_ysdf2_star_center_x_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.center_y = function(obj)
  rt.live(obj, "Star.center_y")
  local res = rt.C().yetty_ysdf2_star_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Star.center_y")
  local res = rt.C().yetty_ysdf2_star_center_y_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.radius = function(obj)
  rt.live(obj, "Star.radius")
  local res = rt.C().yetty_ysdf2_star_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.radius = function(obj, value)
  rt.live(obj, "Star.radius")
  local res = rt.C().yetty_ysdf2_star_radius_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.num_points = function(obj)
  rt.live(obj, "Star.num_points")
  local res = rt.C().yetty_ysdf2_star_num_points_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.num_points = function(obj, value)
  rt.live(obj, "Star.num_points")
  local res = rt.C().yetty_ysdf2_star_num_points_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.inner_ratio = function(obj)
  rt.live(obj, "Star.inner_ratio")
  local res = rt.C().yetty_ysdf2_star_inner_ratio_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.inner_ratio = function(obj, value)
  rt.live(obj, "Star.inner_ratio")
  local res = rt.C().yetty_ysdf2_star_inner_ratio_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.id = function(obj)
  rt.live(obj, "Star.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.id = function(obj, value)
  rt.live(obj, "Star.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.z = function(obj)
  rt.live(obj, "Star.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.z = function(obj, value)
  rt.live(obj, "Star.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.fill = function(obj)
  rt.live(obj, "Star.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.fill = function(obj, value)
  rt.live(obj, "Star.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.stroke = function(obj)
  rt.live(obj, "Star.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Star.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Star.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Star.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Star.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Star.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Star:destroy()
  rt.object_free(self)
end
Star.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    inner_ratio = true,
    num_points = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Star, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Star = Star
local Pie = {}
Pie.__prop_get = {}
Pie.__prop_set = {}
local Pie_instance_mt = {
  __index = function(obj, key)
    local member = Pie[key]
    if member ~= nil then return member end
    local getter = Pie.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Pie.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Pie.new()
  local res = rt.C().yetty_ysdf2_pie_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Pie_instance_mt)
  rt.own(obj, Pie)
  return obj
end
function Pie:set_fill(color)
  rt.live(self, "Pie:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Pie:set_stroke(color)
  rt.live(self, "Pie:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Pie:pack(list)
  rt.live(self, "Pie:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Pie.__prop_get.center_x = function(obj)
  rt.live(obj, "Pie.center_x")
  local res = rt.C().yetty_ysdf2_pie_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Pie.center_x")
  local res = rt.C().yetty_ysdf2_pie_center_x_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.center_y = function(obj)
  rt.live(obj, "Pie.center_y")
  local res = rt.C().yetty_ysdf2_pie_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Pie.center_y")
  local res = rt.C().yetty_ysdf2_pie_center_y_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.aperture_x = function(obj)
  rt.live(obj, "Pie.aperture_x")
  local res = rt.C().yetty_ysdf2_pie_aperture_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.aperture_x = function(obj, value)
  rt.live(obj, "Pie.aperture_x")
  local res = rt.C().yetty_ysdf2_pie_aperture_x_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.aperture_y = function(obj)
  rt.live(obj, "Pie.aperture_y")
  local res = rt.C().yetty_ysdf2_pie_aperture_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.aperture_y = function(obj, value)
  rt.live(obj, "Pie.aperture_y")
  local res = rt.C().yetty_ysdf2_pie_aperture_y_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.radius = function(obj)
  rt.live(obj, "Pie.radius")
  local res = rt.C().yetty_ysdf2_pie_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.radius = function(obj, value)
  rt.live(obj, "Pie.radius")
  local res = rt.C().yetty_ysdf2_pie_radius_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.id = function(obj)
  rt.live(obj, "Pie.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.id = function(obj, value)
  rt.live(obj, "Pie.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.z = function(obj)
  rt.live(obj, "Pie.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.z = function(obj, value)
  rt.live(obj, "Pie.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.fill = function(obj)
  rt.live(obj, "Pie.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.fill = function(obj, value)
  rt.live(obj, "Pie.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.stroke = function(obj)
  rt.live(obj, "Pie.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Pie.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Pie.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Pie.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Pie.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Pie.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Pie:destroy()
  rt.object_free(self)
end
Pie.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    aperture_x = true,
    aperture_y = true,
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Pie, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Pie = Pie
local Ring = {}
Ring.__prop_get = {}
Ring.__prop_set = {}
local Ring_instance_mt = {
  __index = function(obj, key)
    local member = Ring[key]
    if member ~= nil then return member end
    local getter = Ring.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Ring.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Ring.new()
  local res = rt.C().yetty_ysdf2_ring_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Ring_instance_mt)
  rt.own(obj, Ring)
  return obj
end
function Ring:set_fill(color)
  rt.live(self, "Ring:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Ring:set_stroke(color)
  rt.live(self, "Ring:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Ring:pack(list)
  rt.live(self, "Ring:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Ring.__prop_get.center_x = function(obj)
  rt.live(obj, "Ring.center_x")
  local res = rt.C().yetty_ysdf2_ring_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Ring.center_x")
  local res = rt.C().yetty_ysdf2_ring_center_x_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.center_y = function(obj)
  rt.live(obj, "Ring.center_y")
  local res = rt.C().yetty_ysdf2_ring_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Ring.center_y")
  local res = rt.C().yetty_ysdf2_ring_center_y_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.normal_x = function(obj)
  rt.live(obj, "Ring.normal_x")
  local res = rt.C().yetty_ysdf2_ring_normal_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.normal_x = function(obj, value)
  rt.live(obj, "Ring.normal_x")
  local res = rt.C().yetty_ysdf2_ring_normal_x_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.normal_y = function(obj)
  rt.live(obj, "Ring.normal_y")
  local res = rt.C().yetty_ysdf2_ring_normal_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.normal_y = function(obj, value)
  rt.live(obj, "Ring.normal_y")
  local res = rt.C().yetty_ysdf2_ring_normal_y_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.radius = function(obj)
  rt.live(obj, "Ring.radius")
  local res = rt.C().yetty_ysdf2_ring_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.radius = function(obj, value)
  rt.live(obj, "Ring.radius")
  local res = rt.C().yetty_ysdf2_ring_radius_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.thickness = function(obj)
  rt.live(obj, "Ring.thickness")
  local res = rt.C().yetty_ysdf2_ring_thickness_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.thickness = function(obj, value)
  rt.live(obj, "Ring.thickness")
  local res = rt.C().yetty_ysdf2_ring_thickness_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.id = function(obj)
  rt.live(obj, "Ring.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.id = function(obj, value)
  rt.live(obj, "Ring.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.z = function(obj)
  rt.live(obj, "Ring.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.z = function(obj, value)
  rt.live(obj, "Ring.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.fill = function(obj)
  rt.live(obj, "Ring.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.fill = function(obj, value)
  rt.live(obj, "Ring.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.stroke = function(obj)
  rt.live(obj, "Ring.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Ring.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Ring.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Ring.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Ring.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Ring.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Ring:destroy()
  rt.object_free(self)
end
Ring.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    normal_x = true,
    normal_y = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    thickness = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Ring, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Ring = Ring
local Heart = {}
Heart.__prop_get = {}
Heart.__prop_set = {}
local Heart_instance_mt = {
  __index = function(obj, key)
    local member = Heart[key]
    if member ~= nil then return member end
    local getter = Heart.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Heart.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Heart.new()
  local res = rt.C().yetty_ysdf2_heart_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Heart_instance_mt)
  rt.own(obj, Heart)
  return obj
end
function Heart:set_fill(color)
  rt.live(self, "Heart:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Heart:set_stroke(color)
  rt.live(self, "Heart:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Heart:pack(list)
  rt.live(self, "Heart:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Heart.__prop_get.center_x = function(obj)
  rt.live(obj, "Heart.center_x")
  local res = rt.C().yetty_ysdf2_heart_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Heart.center_x")
  local res = rt.C().yetty_ysdf2_heart_center_x_set(obj.handle, value)
  rt.check(res)
end
Heart.__prop_get.center_y = function(obj)
  rt.live(obj, "Heart.center_y")
  local res = rt.C().yetty_ysdf2_heart_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Heart.center_y")
  local res = rt.C().yetty_ysdf2_heart_center_y_set(obj.handle, value)
  rt.check(res)
end
Heart.__prop_get.scale = function(obj)
  rt.live(obj, "Heart.scale")
  local res = rt.C().yetty_ysdf2_heart_scale_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.scale = function(obj, value)
  rt.live(obj, "Heart.scale")
  local res = rt.C().yetty_ysdf2_heart_scale_set(obj.handle, value)
  rt.check(res)
end
Heart.__prop_get.id = function(obj)
  rt.live(obj, "Heart.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.id = function(obj, value)
  rt.live(obj, "Heart.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Heart.__prop_get.z = function(obj)
  rt.live(obj, "Heart.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.z = function(obj, value)
  rt.live(obj, "Heart.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Heart.__prop_get.fill = function(obj)
  rt.live(obj, "Heart.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.fill = function(obj, value)
  rt.live(obj, "Heart.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Heart.__prop_get.stroke = function(obj)
  rt.live(obj, "Heart.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Heart.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Heart.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Heart.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Heart.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Heart.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Heart:destroy()
  rt.object_free(self)
end
Heart.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    scale = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Heart, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Heart = Heart
local Cross = {}
Cross.__prop_get = {}
Cross.__prop_set = {}
local Cross_instance_mt = {
  __index = function(obj, key)
    local member = Cross[key]
    if member ~= nil then return member end
    local getter = Cross.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Cross.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Cross.new()
  local res = rt.C().yetty_ysdf2_cross_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Cross_instance_mt)
  rt.own(obj, Cross)
  return obj
end
function Cross:set_fill(color)
  rt.live(self, "Cross:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Cross:set_stroke(color)
  rt.live(self, "Cross:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Cross:pack(list)
  rt.live(self, "Cross:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Cross.__prop_get.center_x = function(obj)
  rt.live(obj, "Cross.center_x")
  local res = rt.C().yetty_ysdf2_cross_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Cross.center_x")
  local res = rt.C().yetty_ysdf2_cross_center_x_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.center_y = function(obj)
  rt.live(obj, "Cross.center_y")
  local res = rt.C().yetty_ysdf2_cross_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Cross.center_y")
  local res = rt.C().yetty_ysdf2_cross_center_y_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.half_width = function(obj)
  rt.live(obj, "Cross.half_width")
  local res = rt.C().yetty_ysdf2_cross_half_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.half_width = function(obj, value)
  rt.live(obj, "Cross.half_width")
  local res = rt.C().yetty_ysdf2_cross_half_width_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.half_height = function(obj)
  rt.live(obj, "Cross.half_height")
  local res = rt.C().yetty_ysdf2_cross_half_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.half_height = function(obj, value)
  rt.live(obj, "Cross.half_height")
  local res = rt.C().yetty_ysdf2_cross_half_height_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.corner_radius = function(obj)
  rt.live(obj, "Cross.corner_radius")
  local res = rt.C().yetty_ysdf2_cross_corner_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.corner_radius = function(obj, value)
  rt.live(obj, "Cross.corner_radius")
  local res = rt.C().yetty_ysdf2_cross_corner_radius_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.id = function(obj)
  rt.live(obj, "Cross.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.id = function(obj, value)
  rt.live(obj, "Cross.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.z = function(obj)
  rt.live(obj, "Cross.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.z = function(obj, value)
  rt.live(obj, "Cross.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.fill = function(obj)
  rt.live(obj, "Cross.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.fill = function(obj, value)
  rt.live(obj, "Cross.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.stroke = function(obj)
  rt.live(obj, "Cross.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Cross.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Cross.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Cross.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Cross.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Cross.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Cross:destroy()
  rt.object_free(self)
end
Cross.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    corner_radius = true,
    fill = true,
    half_height = true,
    half_width = true,
    id = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Cross, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Cross = Cross
local RoundedX = {}
RoundedX.__prop_get = {}
RoundedX.__prop_set = {}
local RoundedX_instance_mt = {
  __index = function(obj, key)
    local member = RoundedX[key]
    if member ~= nil then return member end
    local getter = RoundedX.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = RoundedX.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function RoundedX.new()
  local res = rt.C().yetty_ysdf2_rounded_x_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, RoundedX_instance_mt)
  rt.own(obj, RoundedX)
  return obj
end
function RoundedX:set_fill(color)
  rt.live(self, "RoundedX:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function RoundedX:set_stroke(color)
  rt.live(self, "RoundedX:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function RoundedX:pack(list)
  rt.live(self, "RoundedX:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
RoundedX.__prop_get.center_x = function(obj)
  rt.live(obj, "RoundedX.center_x")
  local res = rt.C().yetty_ysdf2_rounded_x_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.center_x = function(obj, value)
  rt.live(obj, "RoundedX.center_x")
  local res = rt.C().yetty_ysdf2_rounded_x_center_x_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.center_y = function(obj)
  rt.live(obj, "RoundedX.center_y")
  local res = rt.C().yetty_ysdf2_rounded_x_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.center_y = function(obj, value)
  rt.live(obj, "RoundedX.center_y")
  local res = rt.C().yetty_ysdf2_rounded_x_center_y_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.width = function(obj)
  rt.live(obj, "RoundedX.width")
  local res = rt.C().yetty_ysdf2_rounded_x_width_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.width = function(obj, value)
  rt.live(obj, "RoundedX.width")
  local res = rt.C().yetty_ysdf2_rounded_x_width_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.radius = function(obj)
  rt.live(obj, "RoundedX.radius")
  local res = rt.C().yetty_ysdf2_rounded_x_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.radius = function(obj, value)
  rt.live(obj, "RoundedX.radius")
  local res = rt.C().yetty_ysdf2_rounded_x_radius_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.id = function(obj)
  rt.live(obj, "RoundedX.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.id = function(obj, value)
  rt.live(obj, "RoundedX.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.z = function(obj)
  rt.live(obj, "RoundedX.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.z = function(obj, value)
  rt.live(obj, "RoundedX.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.fill = function(obj)
  rt.live(obj, "RoundedX.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.fill = function(obj, value)
  rt.live(obj, "RoundedX.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.stroke = function(obj)
  rt.live(obj, "RoundedX.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.stroke = function(obj, value)
  rt.live(obj, "RoundedX.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
RoundedX.__prop_get.stroke_width = function(obj)
  rt.live(obj, "RoundedX.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
RoundedX.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "RoundedX.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function RoundedX:destroy()
  rt.object_free(self)
end
RoundedX.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(RoundedX, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.RoundedX = RoundedX
local Capsule = {}
Capsule.__prop_get = {}
Capsule.__prop_set = {}
local Capsule_instance_mt = {
  __index = function(obj, key)
    local member = Capsule[key]
    if member ~= nil then return member end
    local getter = Capsule.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Capsule.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Capsule.new()
  local res = rt.C().yetty_ysdf2_capsule_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Capsule_instance_mt)
  rt.own(obj, Capsule)
  return obj
end
function Capsule:set_fill(color)
  rt.live(self, "Capsule:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Capsule:set_stroke(color)
  rt.live(self, "Capsule:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Capsule:pack(list)
  rt.live(self, "Capsule:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Capsule.__prop_get.start_x = function(obj)
  rt.live(obj, "Capsule.start_x")
  local res = rt.C().yetty_ysdf2_capsule_start_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.start_x = function(obj, value)
  rt.live(obj, "Capsule.start_x")
  local res = rt.C().yetty_ysdf2_capsule_start_x_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.start_y = function(obj)
  rt.live(obj, "Capsule.start_y")
  local res = rt.C().yetty_ysdf2_capsule_start_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.start_y = function(obj, value)
  rt.live(obj, "Capsule.start_y")
  local res = rt.C().yetty_ysdf2_capsule_start_y_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.end_x = function(obj)
  rt.live(obj, "Capsule.end_x")
  local res = rt.C().yetty_ysdf2_capsule_end_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.end_x = function(obj, value)
  rt.live(obj, "Capsule.end_x")
  local res = rt.C().yetty_ysdf2_capsule_end_x_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.end_y = function(obj)
  rt.live(obj, "Capsule.end_y")
  local res = rt.C().yetty_ysdf2_capsule_end_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.end_y = function(obj, value)
  rt.live(obj, "Capsule.end_y")
  local res = rt.C().yetty_ysdf2_capsule_end_y_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.radius = function(obj)
  rt.live(obj, "Capsule.radius")
  local res = rt.C().yetty_ysdf2_capsule_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.radius = function(obj, value)
  rt.live(obj, "Capsule.radius")
  local res = rt.C().yetty_ysdf2_capsule_radius_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.id = function(obj)
  rt.live(obj, "Capsule.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.id = function(obj, value)
  rt.live(obj, "Capsule.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.z = function(obj)
  rt.live(obj, "Capsule.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.z = function(obj, value)
  rt.live(obj, "Capsule.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.fill = function(obj)
  rt.live(obj, "Capsule.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.fill = function(obj, value)
  rt.live(obj, "Capsule.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.stroke = function(obj)
  rt.live(obj, "Capsule.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Capsule.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Capsule.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Capsule.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Capsule.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Capsule.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Capsule:destroy()
  rt.object_free(self)
end
Capsule.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    end_x = true,
    end_y = true,
    fill = true,
    id = true,
    radius = true,
    start_x = true,
    start_y = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Capsule, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Capsule = Capsule
local Moon = {}
Moon.__prop_get = {}
Moon.__prop_set = {}
local Moon_instance_mt = {
  __index = function(obj, key)
    local member = Moon[key]
    if member ~= nil then return member end
    local getter = Moon.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Moon.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Moon.new()
  local res = rt.C().yetty_ysdf2_moon_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Moon_instance_mt)
  rt.own(obj, Moon)
  return obj
end
function Moon:set_fill(color)
  rt.live(self, "Moon:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Moon:set_stroke(color)
  rt.live(self, "Moon:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Moon:pack(list)
  rt.live(self, "Moon:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Moon.__prop_get.center_x = function(obj)
  rt.live(obj, "Moon.center_x")
  local res = rt.C().yetty_ysdf2_moon_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Moon.center_x")
  local res = rt.C().yetty_ysdf2_moon_center_x_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.center_y = function(obj)
  rt.live(obj, "Moon.center_y")
  local res = rt.C().yetty_ysdf2_moon_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Moon.center_y")
  local res = rt.C().yetty_ysdf2_moon_center_y_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.offset = function(obj)
  rt.live(obj, "Moon.offset")
  local res = rt.C().yetty_ysdf2_moon_offset_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.offset = function(obj, value)
  rt.live(obj, "Moon.offset")
  local res = rt.C().yetty_ysdf2_moon_offset_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.radius_outer = function(obj)
  rt.live(obj, "Moon.radius_outer")
  local res = rt.C().yetty_ysdf2_moon_radius_outer_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.radius_outer = function(obj, value)
  rt.live(obj, "Moon.radius_outer")
  local res = rt.C().yetty_ysdf2_moon_radius_outer_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.radius_inner = function(obj)
  rt.live(obj, "Moon.radius_inner")
  local res = rt.C().yetty_ysdf2_moon_radius_inner_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.radius_inner = function(obj, value)
  rt.live(obj, "Moon.radius_inner")
  local res = rt.C().yetty_ysdf2_moon_radius_inner_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.id = function(obj)
  rt.live(obj, "Moon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.id = function(obj, value)
  rt.live(obj, "Moon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.z = function(obj)
  rt.live(obj, "Moon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.z = function(obj, value)
  rt.live(obj, "Moon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.fill = function(obj)
  rt.live(obj, "Moon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.fill = function(obj, value)
  rt.live(obj, "Moon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.stroke = function(obj)
  rt.live(obj, "Moon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Moon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Moon.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Moon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Moon.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Moon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Moon:destroy()
  rt.object_free(self)
end
Moon.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    offset = true,
    radius_inner = true,
    radius_outer = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Moon, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Moon = Moon
local Egg = {}
Egg.__prop_get = {}
Egg.__prop_set = {}
local Egg_instance_mt = {
  __index = function(obj, key)
    local member = Egg[key]
    if member ~= nil then return member end
    local getter = Egg.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Egg.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Egg.new()
  local res = rt.C().yetty_ysdf2_egg_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Egg_instance_mt)
  rt.own(obj, Egg)
  return obj
end
function Egg:set_fill(color)
  rt.live(self, "Egg:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Egg:set_stroke(color)
  rt.live(self, "Egg:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Egg:pack(list)
  rt.live(self, "Egg:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Egg.__prop_get.center_x = function(obj)
  rt.live(obj, "Egg.center_x")
  local res = rt.C().yetty_ysdf2_egg_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Egg.center_x")
  local res = rt.C().yetty_ysdf2_egg_center_x_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.center_y = function(obj)
  rt.live(obj, "Egg.center_y")
  local res = rt.C().yetty_ysdf2_egg_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Egg.center_y")
  local res = rt.C().yetty_ysdf2_egg_center_y_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.radius_outer = function(obj)
  rt.live(obj, "Egg.radius_outer")
  local res = rt.C().yetty_ysdf2_egg_radius_outer_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.radius_outer = function(obj, value)
  rt.live(obj, "Egg.radius_outer")
  local res = rt.C().yetty_ysdf2_egg_radius_outer_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.radius_inner = function(obj)
  rt.live(obj, "Egg.radius_inner")
  local res = rt.C().yetty_ysdf2_egg_radius_inner_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.radius_inner = function(obj, value)
  rt.live(obj, "Egg.radius_inner")
  local res = rt.C().yetty_ysdf2_egg_radius_inner_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.id = function(obj)
  rt.live(obj, "Egg.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.id = function(obj, value)
  rt.live(obj, "Egg.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.z = function(obj)
  rt.live(obj, "Egg.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.z = function(obj, value)
  rt.live(obj, "Egg.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.fill = function(obj)
  rt.live(obj, "Egg.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.fill = function(obj, value)
  rt.live(obj, "Egg.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.stroke = function(obj)
  rt.live(obj, "Egg.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Egg.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Egg.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Egg.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Egg.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Egg.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Egg:destroy()
  rt.object_free(self)
end
Egg.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius_inner = true,
    radius_outer = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Egg, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Egg = Egg
local Octogon = {}
Octogon.__prop_get = {}
Octogon.__prop_set = {}
local Octogon_instance_mt = {
  __index = function(obj, key)
    local member = Octogon[key]
    if member ~= nil then return member end
    local getter = Octogon.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Octogon.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Octogon.new()
  local res = rt.C().yetty_ysdf2_octogon_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Octogon_instance_mt)
  rt.own(obj, Octogon)
  return obj
end
function Octogon:set_fill(color)
  rt.live(self, "Octogon:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Octogon:set_stroke(color)
  rt.live(self, "Octogon:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Octogon:pack(list)
  rt.live(self, "Octogon:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Octogon.__prop_get.center_x = function(obj)
  rt.live(obj, "Octogon.center_x")
  local res = rt.C().yetty_ysdf2_octogon_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Octogon.center_x")
  local res = rt.C().yetty_ysdf2_octogon_center_x_set(obj.handle, value)
  rt.check(res)
end
Octogon.__prop_get.center_y = function(obj)
  rt.live(obj, "Octogon.center_y")
  local res = rt.C().yetty_ysdf2_octogon_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Octogon.center_y")
  local res = rt.C().yetty_ysdf2_octogon_center_y_set(obj.handle, value)
  rt.check(res)
end
Octogon.__prop_get.radius = function(obj)
  rt.live(obj, "Octogon.radius")
  local res = rt.C().yetty_ysdf2_octogon_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.radius = function(obj, value)
  rt.live(obj, "Octogon.radius")
  local res = rt.C().yetty_ysdf2_octogon_radius_set(obj.handle, value)
  rt.check(res)
end
Octogon.__prop_get.id = function(obj)
  rt.live(obj, "Octogon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.id = function(obj, value)
  rt.live(obj, "Octogon.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Octogon.__prop_get.z = function(obj)
  rt.live(obj, "Octogon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.z = function(obj, value)
  rt.live(obj, "Octogon.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Octogon.__prop_get.fill = function(obj)
  rt.live(obj, "Octogon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.fill = function(obj, value)
  rt.live(obj, "Octogon.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Octogon.__prop_get.stroke = function(obj)
  rt.live(obj, "Octogon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Octogon.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Octogon.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Octogon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Octogon.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Octogon.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Octogon:destroy()
  rt.object_free(self)
end
Octogon.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Octogon, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Octogon = Octogon
local Hexagram = {}
Hexagram.__prop_get = {}
Hexagram.__prop_set = {}
local Hexagram_instance_mt = {
  __index = function(obj, key)
    local member = Hexagram[key]
    if member ~= nil then return member end
    local getter = Hexagram.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Hexagram.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Hexagram.new()
  local res = rt.C().yetty_ysdf2_hexagram_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Hexagram_instance_mt)
  rt.own(obj, Hexagram)
  return obj
end
function Hexagram:set_fill(color)
  rt.live(self, "Hexagram:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Hexagram:set_stroke(color)
  rt.live(self, "Hexagram:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Hexagram:pack(list)
  rt.live(self, "Hexagram:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Hexagram.__prop_get.center_x = function(obj)
  rt.live(obj, "Hexagram.center_x")
  local res = rt.C().yetty_ysdf2_hexagram_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Hexagram.center_x")
  local res = rt.C().yetty_ysdf2_hexagram_center_x_set(obj.handle, value)
  rt.check(res)
end
Hexagram.__prop_get.center_y = function(obj)
  rt.live(obj, "Hexagram.center_y")
  local res = rt.C().yetty_ysdf2_hexagram_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Hexagram.center_y")
  local res = rt.C().yetty_ysdf2_hexagram_center_y_set(obj.handle, value)
  rt.check(res)
end
Hexagram.__prop_get.radius = function(obj)
  rt.live(obj, "Hexagram.radius")
  local res = rt.C().yetty_ysdf2_hexagram_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.radius = function(obj, value)
  rt.live(obj, "Hexagram.radius")
  local res = rt.C().yetty_ysdf2_hexagram_radius_set(obj.handle, value)
  rt.check(res)
end
Hexagram.__prop_get.id = function(obj)
  rt.live(obj, "Hexagram.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.id = function(obj, value)
  rt.live(obj, "Hexagram.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Hexagram.__prop_get.z = function(obj)
  rt.live(obj, "Hexagram.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.z = function(obj, value)
  rt.live(obj, "Hexagram.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Hexagram.__prop_get.fill = function(obj)
  rt.live(obj, "Hexagram.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.fill = function(obj, value)
  rt.live(obj, "Hexagram.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Hexagram.__prop_get.stroke = function(obj)
  rt.live(obj, "Hexagram.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Hexagram.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Hexagram.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Hexagram.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Hexagram.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Hexagram.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Hexagram:destroy()
  rt.object_free(self)
end
Hexagram.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Hexagram, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Hexagram = Hexagram
local Pentagram = {}
Pentagram.__prop_get = {}
Pentagram.__prop_set = {}
local Pentagram_instance_mt = {
  __index = function(obj, key)
    local member = Pentagram[key]
    if member ~= nil then return member end
    local getter = Pentagram.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Pentagram.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Pentagram.new()
  local res = rt.C().yetty_ysdf2_pentagram_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Pentagram_instance_mt)
  rt.own(obj, Pentagram)
  return obj
end
function Pentagram:set_fill(color)
  rt.live(self, "Pentagram:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Pentagram:set_stroke(color)
  rt.live(self, "Pentagram:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Pentagram:pack(list)
  rt.live(self, "Pentagram:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Pentagram.__prop_get.center_x = function(obj)
  rt.live(obj, "Pentagram.center_x")
  local res = rt.C().yetty_ysdf2_pentagram_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.center_x = function(obj, value)
  rt.live(obj, "Pentagram.center_x")
  local res = rt.C().yetty_ysdf2_pentagram_center_x_set(obj.handle, value)
  rt.check(res)
end
Pentagram.__prop_get.center_y = function(obj)
  rt.live(obj, "Pentagram.center_y")
  local res = rt.C().yetty_ysdf2_pentagram_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.center_y = function(obj, value)
  rt.live(obj, "Pentagram.center_y")
  local res = rt.C().yetty_ysdf2_pentagram_center_y_set(obj.handle, value)
  rt.check(res)
end
Pentagram.__prop_get.radius = function(obj)
  rt.live(obj, "Pentagram.radius")
  local res = rt.C().yetty_ysdf2_pentagram_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.radius = function(obj, value)
  rt.live(obj, "Pentagram.radius")
  local res = rt.C().yetty_ysdf2_pentagram_radius_set(obj.handle, value)
  rt.check(res)
end
Pentagram.__prop_get.id = function(obj)
  rt.live(obj, "Pentagram.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.id = function(obj, value)
  rt.live(obj, "Pentagram.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Pentagram.__prop_get.z = function(obj)
  rt.live(obj, "Pentagram.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.z = function(obj, value)
  rt.live(obj, "Pentagram.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Pentagram.__prop_get.fill = function(obj)
  rt.live(obj, "Pentagram.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.fill = function(obj, value)
  rt.live(obj, "Pentagram.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Pentagram.__prop_get.stroke = function(obj)
  rt.live(obj, "Pentagram.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Pentagram.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Pentagram.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Pentagram.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Pentagram.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Pentagram.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Pentagram:destroy()
  rt.object_free(self)
end
Pentagram.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    fill = true,
    id = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Pentagram, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Pentagram = Pentagram
local LinearGradientBox = {}
LinearGradientBox.__prop_get = {}
LinearGradientBox.__prop_set = {}
local LinearGradientBox_instance_mt = {
  __index = function(obj, key)
    local member = LinearGradientBox[key]
    if member ~= nil then return member end
    local getter = LinearGradientBox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = LinearGradientBox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function LinearGradientBox.new()
  local res = rt.C().yetty_ysdf2_linear_gradient_box_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, LinearGradientBox_instance_mt)
  rt.own(obj, LinearGradientBox)
  return obj
end
function LinearGradientBox:set_fill(color)
  rt.live(self, "LinearGradientBox:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function LinearGradientBox:set_stroke(color)
  rt.live(self, "LinearGradientBox:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function LinearGradientBox:pack(list)
  rt.live(self, "LinearGradientBox:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
LinearGradientBox.__prop_get.center_x = function(obj)
  rt.live(obj, "LinearGradientBox.center_x")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.center_x = function(obj, value)
  rt.live(obj, "LinearGradientBox.center_x")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_center_x_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.center_y = function(obj)
  rt.live(obj, "LinearGradientBox.center_y")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.center_y = function(obj, value)
  rt.live(obj, "LinearGradientBox.center_y")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_center_y_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.half_width = function(obj)
  rt.live(obj, "LinearGradientBox.half_width")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_half_width_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.half_width = function(obj, value)
  rt.live(obj, "LinearGradientBox.half_width")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_half_width_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.half_height = function(obj)
  rt.live(obj, "LinearGradientBox.half_height")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_half_height_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.half_height = function(obj, value)
  rt.live(obj, "LinearGradientBox.half_height")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_half_height_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.corner_radius = function(obj)
  rt.live(obj, "LinearGradientBox.corner_radius")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_corner_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.corner_radius = function(obj, value)
  rt.live(obj, "LinearGradientBox.corner_radius")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_corner_radius_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.grad_x0 = function(obj)
  rt.live(obj, "LinearGradientBox.grad_x0")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_x0_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.grad_x0 = function(obj, value)
  rt.live(obj, "LinearGradientBox.grad_x0")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_x0_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.grad_y0 = function(obj)
  rt.live(obj, "LinearGradientBox.grad_y0")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_y0_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.grad_y0 = function(obj, value)
  rt.live(obj, "LinearGradientBox.grad_y0")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_y0_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.grad_x1 = function(obj)
  rt.live(obj, "LinearGradientBox.grad_x1")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_x1_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.grad_x1 = function(obj, value)
  rt.live(obj, "LinearGradientBox.grad_x1")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_x1_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.grad_y1 = function(obj)
  rt.live(obj, "LinearGradientBox.grad_y1")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_y1_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.grad_y1 = function(obj, value)
  rt.live(obj, "LinearGradientBox.grad_y1")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_grad_y1_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.color0 = function(obj)
  rt.live(obj, "LinearGradientBox.color0")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_color0_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.color0 = function(obj, value)
  rt.live(obj, "LinearGradientBox.color0")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_color0_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.color1 = function(obj)
  rt.live(obj, "LinearGradientBox.color1")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_color1_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.color1 = function(obj, value)
  rt.live(obj, "LinearGradientBox.color1")
  local res = rt.C().yetty_ysdf2_linear_gradient_box_color1_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.id = function(obj)
  rt.live(obj, "LinearGradientBox.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.id = function(obj, value)
  rt.live(obj, "LinearGradientBox.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.z = function(obj)
  rt.live(obj, "LinearGradientBox.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.z = function(obj, value)
  rt.live(obj, "LinearGradientBox.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.fill = function(obj)
  rt.live(obj, "LinearGradientBox.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.fill = function(obj, value)
  rt.live(obj, "LinearGradientBox.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.stroke = function(obj)
  rt.live(obj, "LinearGradientBox.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.stroke = function(obj, value)
  rt.live(obj, "LinearGradientBox.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
LinearGradientBox.__prop_get.stroke_width = function(obj)
  rt.live(obj, "LinearGradientBox.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
LinearGradientBox.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "LinearGradientBox.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function LinearGradientBox:destroy()
  rt.object_free(self)
end
LinearGradientBox.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    color0 = true,
    color1 = true,
    corner_radius = true,
    fill = true,
    grad_x0 = true,
    grad_x1 = true,
    grad_y0 = true,
    grad_y1 = true,
    half_height = true,
    half_width = true,
    id = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(LinearGradientBox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.LinearGradientBox = LinearGradientBox
local RadialGradientBox = {}
RadialGradientBox.__prop_get = {}
RadialGradientBox.__prop_set = {}
local RadialGradientBox_instance_mt = {
  __index = function(obj, key)
    local member = RadialGradientBox[key]
    if member ~= nil then return member end
    local getter = RadialGradientBox.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = RadialGradientBox.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function RadialGradientBox.new()
  local res = rt.C().yetty_ysdf2_radial_gradient_box_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, RadialGradientBox_instance_mt)
  rt.own(obj, RadialGradientBox)
  return obj
end
function RadialGradientBox:set_fill(color)
  rt.live(self, "RadialGradientBox:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function RadialGradientBox:set_stroke(color)
  rt.live(self, "RadialGradientBox:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function RadialGradientBox:pack(list)
  rt.live(self, "RadialGradientBox:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
RadialGradientBox.__prop_get.center_x = function(obj)
  rt.live(obj, "RadialGradientBox.center_x")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_center_x_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.center_x = function(obj, value)
  rt.live(obj, "RadialGradientBox.center_x")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_center_x_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.center_y = function(obj)
  rt.live(obj, "RadialGradientBox.center_y")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_center_y_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.center_y = function(obj, value)
  rt.live(obj, "RadialGradientBox.center_y")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_center_y_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.half_width = function(obj)
  rt.live(obj, "RadialGradientBox.half_width")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_half_width_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.half_width = function(obj, value)
  rt.live(obj, "RadialGradientBox.half_width")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_half_width_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.half_height = function(obj)
  rt.live(obj, "RadialGradientBox.half_height")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_half_height_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.half_height = function(obj, value)
  rt.live(obj, "RadialGradientBox.half_height")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_half_height_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.corner_radius = function(obj)
  rt.live(obj, "RadialGradientBox.corner_radius")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_corner_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.corner_radius = function(obj, value)
  rt.live(obj, "RadialGradientBox.corner_radius")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_corner_radius_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.grad_cx = function(obj)
  rt.live(obj, "RadialGradientBox.grad_cx")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_grad_cx_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.grad_cx = function(obj, value)
  rt.live(obj, "RadialGradientBox.grad_cx")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_grad_cx_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.grad_cy = function(obj)
  rt.live(obj, "RadialGradientBox.grad_cy")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_grad_cy_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.grad_cy = function(obj, value)
  rt.live(obj, "RadialGradientBox.grad_cy")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_grad_cy_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.grad_radius = function(obj)
  rt.live(obj, "RadialGradientBox.grad_radius")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_grad_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.grad_radius = function(obj, value)
  rt.live(obj, "RadialGradientBox.grad_radius")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_grad_radius_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.color_inner = function(obj)
  rt.live(obj, "RadialGradientBox.color_inner")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_color_inner_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.color_inner = function(obj, value)
  rt.live(obj, "RadialGradientBox.color_inner")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_color_inner_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.color_outer = function(obj)
  rt.live(obj, "RadialGradientBox.color_outer")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_color_outer_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.color_outer = function(obj, value)
  rt.live(obj, "RadialGradientBox.color_outer")
  local res = rt.C().yetty_ysdf2_radial_gradient_box_color_outer_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.id = function(obj)
  rt.live(obj, "RadialGradientBox.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.id = function(obj, value)
  rt.live(obj, "RadialGradientBox.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.z = function(obj)
  rt.live(obj, "RadialGradientBox.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.z = function(obj, value)
  rt.live(obj, "RadialGradientBox.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.fill = function(obj)
  rt.live(obj, "RadialGradientBox.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.fill = function(obj, value)
  rt.live(obj, "RadialGradientBox.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.stroke = function(obj)
  rt.live(obj, "RadialGradientBox.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.stroke = function(obj, value)
  rt.live(obj, "RadialGradientBox.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
RadialGradientBox.__prop_get.stroke_width = function(obj)
  rt.live(obj, "RadialGradientBox.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
RadialGradientBox.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "RadialGradientBox.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function RadialGradientBox:destroy()
  rt.object_free(self)
end
RadialGradientBox.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    center_x = true,
    center_y = true,
    color_inner = true,
    color_outer = true,
    corner_radius = true,
    fill = true,
    grad_cx = true,
    grad_cy = true,
    grad_radius = true,
    half_height = true,
    half_width = true,
    id = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(RadialGradientBox, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.RadialGradientBox = RadialGradientBox
local Sphere3d = {}
Sphere3d.__prop_get = {}
Sphere3d.__prop_set = {}
local Sphere3d_instance_mt = {
  __index = function(obj, key)
    local member = Sphere3d[key]
    if member ~= nil then return member end
    local getter = Sphere3d.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Sphere3d.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Sphere3d.new()
  local res = rt.C().yetty_ysdf2_sphere_3d_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Sphere3d_instance_mt)
  rt.own(obj, Sphere3d)
  return obj
end
function Sphere3d:set_fill(color)
  rt.live(self, "Sphere3d:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Sphere3d:set_stroke(color)
  rt.live(self, "Sphere3d:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Sphere3d:pack(list)
  rt.live(self, "Sphere3d:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Sphere3d.__prop_get.position_x = function(obj)
  rt.live(obj, "Sphere3d.position_x")
  local res = rt.C().yetty_ysdf2_sphere_3d_position_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.position_x = function(obj, value)
  rt.live(obj, "Sphere3d.position_x")
  local res = rt.C().yetty_ysdf2_sphere_3d_position_x_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.position_y = function(obj)
  rt.live(obj, "Sphere3d.position_y")
  local res = rt.C().yetty_ysdf2_sphere_3d_position_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.position_y = function(obj, value)
  rt.live(obj, "Sphere3d.position_y")
  local res = rt.C().yetty_ysdf2_sphere_3d_position_y_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.position_z = function(obj)
  rt.live(obj, "Sphere3d.position_z")
  local res = rt.C().yetty_ysdf2_sphere_3d_position_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.position_z = function(obj, value)
  rt.live(obj, "Sphere3d.position_z")
  local res = rt.C().yetty_ysdf2_sphere_3d_position_z_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.radius = function(obj)
  rt.live(obj, "Sphere3d.radius")
  local res = rt.C().yetty_ysdf2_sphere_3d_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.radius = function(obj, value)
  rt.live(obj, "Sphere3d.radius")
  local res = rt.C().yetty_ysdf2_sphere_3d_radius_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.id = function(obj)
  rt.live(obj, "Sphere3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.id = function(obj, value)
  rt.live(obj, "Sphere3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.z = function(obj)
  rt.live(obj, "Sphere3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.z = function(obj, value)
  rt.live(obj, "Sphere3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.fill = function(obj)
  rt.live(obj, "Sphere3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.fill = function(obj, value)
  rt.live(obj, "Sphere3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.stroke = function(obj)
  rt.live(obj, "Sphere3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Sphere3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Sphere3d.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Sphere3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Sphere3d.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Sphere3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Sphere3d:destroy()
  rt.object_free(self)
end
Sphere3d.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    fill = true,
    id = true,
    position_x = true,
    position_y = true,
    position_z = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Sphere3d, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Sphere3d = Sphere3d
local Box3d = {}
Box3d.__prop_get = {}
Box3d.__prop_set = {}
local Box3d_instance_mt = {
  __index = function(obj, key)
    local member = Box3d[key]
    if member ~= nil then return member end
    local getter = Box3d.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Box3d.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Box3d.new()
  local res = rt.C().yetty_ysdf2_box_3d_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Box3d_instance_mt)
  rt.own(obj, Box3d)
  return obj
end
function Box3d:set_fill(color)
  rt.live(self, "Box3d:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Box3d:set_stroke(color)
  rt.live(self, "Box3d:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Box3d:pack(list)
  rt.live(self, "Box3d:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Box3d.__prop_get.position_x = function(obj)
  rt.live(obj, "Box3d.position_x")
  local res = rt.C().yetty_ysdf2_box_3d_position_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.position_x = function(obj, value)
  rt.live(obj, "Box3d.position_x")
  local res = rt.C().yetty_ysdf2_box_3d_position_x_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.position_y = function(obj)
  rt.live(obj, "Box3d.position_y")
  local res = rt.C().yetty_ysdf2_box_3d_position_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.position_y = function(obj, value)
  rt.live(obj, "Box3d.position_y")
  local res = rt.C().yetty_ysdf2_box_3d_position_y_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.position_z = function(obj)
  rt.live(obj, "Box3d.position_z")
  local res = rt.C().yetty_ysdf2_box_3d_position_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.position_z = function(obj, value)
  rt.live(obj, "Box3d.position_z")
  local res = rt.C().yetty_ysdf2_box_3d_position_z_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.half_size_x = function(obj)
  rt.live(obj, "Box3d.half_size_x")
  local res = rt.C().yetty_ysdf2_box_3d_half_size_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.half_size_x = function(obj, value)
  rt.live(obj, "Box3d.half_size_x")
  local res = rt.C().yetty_ysdf2_box_3d_half_size_x_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.half_size_y = function(obj)
  rt.live(obj, "Box3d.half_size_y")
  local res = rt.C().yetty_ysdf2_box_3d_half_size_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.half_size_y = function(obj, value)
  rt.live(obj, "Box3d.half_size_y")
  local res = rt.C().yetty_ysdf2_box_3d_half_size_y_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.half_size_z = function(obj)
  rt.live(obj, "Box3d.half_size_z")
  local res = rt.C().yetty_ysdf2_box_3d_half_size_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.half_size_z = function(obj, value)
  rt.live(obj, "Box3d.half_size_z")
  local res = rt.C().yetty_ysdf2_box_3d_half_size_z_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.id = function(obj)
  rt.live(obj, "Box3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.id = function(obj, value)
  rt.live(obj, "Box3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.z = function(obj)
  rt.live(obj, "Box3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.z = function(obj, value)
  rt.live(obj, "Box3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.fill = function(obj)
  rt.live(obj, "Box3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.fill = function(obj, value)
  rt.live(obj, "Box3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.stroke = function(obj)
  rt.live(obj, "Box3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Box3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Box3d.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Box3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Box3d.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Box3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Box3d:destroy()
  rt.object_free(self)
end
Box3d.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    fill = true,
    half_size_x = true,
    half_size_y = true,
    half_size_z = true,
    id = true,
    position_x = true,
    position_y = true,
    position_z = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Box3d, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Box3d = Box3d
local Torus3d = {}
Torus3d.__prop_get = {}
Torus3d.__prop_set = {}
local Torus3d_instance_mt = {
  __index = function(obj, key)
    local member = Torus3d[key]
    if member ~= nil then return member end
    local getter = Torus3d.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Torus3d.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Torus3d.new()
  local res = rt.C().yetty_ysdf2_torus_3d_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Torus3d_instance_mt)
  rt.own(obj, Torus3d)
  return obj
end
function Torus3d:set_fill(color)
  rt.live(self, "Torus3d:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Torus3d:set_stroke(color)
  rt.live(self, "Torus3d:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Torus3d:pack(list)
  rt.live(self, "Torus3d:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Torus3d.__prop_get.position_x = function(obj)
  rt.live(obj, "Torus3d.position_x")
  local res = rt.C().yetty_ysdf2_torus_3d_position_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.position_x = function(obj, value)
  rt.live(obj, "Torus3d.position_x")
  local res = rt.C().yetty_ysdf2_torus_3d_position_x_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.position_y = function(obj)
  rt.live(obj, "Torus3d.position_y")
  local res = rt.C().yetty_ysdf2_torus_3d_position_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.position_y = function(obj, value)
  rt.live(obj, "Torus3d.position_y")
  local res = rt.C().yetty_ysdf2_torus_3d_position_y_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.position_z = function(obj)
  rt.live(obj, "Torus3d.position_z")
  local res = rt.C().yetty_ysdf2_torus_3d_position_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.position_z = function(obj, value)
  rt.live(obj, "Torus3d.position_z")
  local res = rt.C().yetty_ysdf2_torus_3d_position_z_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.major_radius = function(obj)
  rt.live(obj, "Torus3d.major_radius")
  local res = rt.C().yetty_ysdf2_torus_3d_major_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.major_radius = function(obj, value)
  rt.live(obj, "Torus3d.major_radius")
  local res = rt.C().yetty_ysdf2_torus_3d_major_radius_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.minor_radius = function(obj)
  rt.live(obj, "Torus3d.minor_radius")
  local res = rt.C().yetty_ysdf2_torus_3d_minor_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.minor_radius = function(obj, value)
  rt.live(obj, "Torus3d.minor_radius")
  local res = rt.C().yetty_ysdf2_torus_3d_minor_radius_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.id = function(obj)
  rt.live(obj, "Torus3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.id = function(obj, value)
  rt.live(obj, "Torus3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.z = function(obj)
  rt.live(obj, "Torus3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.z = function(obj, value)
  rt.live(obj, "Torus3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.fill = function(obj)
  rt.live(obj, "Torus3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.fill = function(obj, value)
  rt.live(obj, "Torus3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.stroke = function(obj)
  rt.live(obj, "Torus3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Torus3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Torus3d.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Torus3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Torus3d.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Torus3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Torus3d:destroy()
  rt.object_free(self)
end
Torus3d.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    fill = true,
    id = true,
    major_radius = true,
    minor_radius = true,
    position_x = true,
    position_y = true,
    position_z = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Torus3d, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Torus3d = Torus3d
local Cylinder3d = {}
Cylinder3d.__prop_get = {}
Cylinder3d.__prop_set = {}
local Cylinder3d_instance_mt = {
  __index = function(obj, key)
    local member = Cylinder3d[key]
    if member ~= nil then return member end
    local getter = Cylinder3d.__prop_get[key]
    if getter then return getter(obj) end
    return nil
  end,
  __newindex = function(obj, key, value)
    local setter = Cylinder3d.__prop_set[key]
    if setter then setter(obj, value) else rawset(obj, key, value) end
  end,
}
function Cylinder3d.new()
  local res = rt.C().yetty_ysdf2_cylinder_3d_create(nil)
  rt.check(res)
  local obj = setmetatable({ handle = res.value }, Cylinder3d_instance_mt)
  rt.own(obj, Cylinder3d)
  return obj
end
function Cylinder3d:set_fill(color)
  rt.live(self, "Cylinder3d:set_fill")
  local res = rt.C().yetty_ydrawlist2_set_fill(self.handle, color)
  rt.check(res)
end
function Cylinder3d:set_stroke(color)
  rt.live(self, "Cylinder3d:set_stroke")
  local res = rt.C().yetty_ydrawlist2_set_stroke(self.handle, color)
  rt.check(res)
end
function Cylinder3d:pack(list)
  rt.live(self, "Cylinder3d:pack")
  local res = rt.C().yetty_ydrawlist2_pack(self.handle, list)
  rt.check(res)
end
Cylinder3d.__prop_get.position_x = function(obj)
  rt.live(obj, "Cylinder3d.position_x")
  local res = rt.C().yetty_ysdf2_cylinder_3d_position_x_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.position_x = function(obj, value)
  rt.live(obj, "Cylinder3d.position_x")
  local res = rt.C().yetty_ysdf2_cylinder_3d_position_x_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.position_y = function(obj)
  rt.live(obj, "Cylinder3d.position_y")
  local res = rt.C().yetty_ysdf2_cylinder_3d_position_y_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.position_y = function(obj, value)
  rt.live(obj, "Cylinder3d.position_y")
  local res = rt.C().yetty_ysdf2_cylinder_3d_position_y_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.position_z = function(obj)
  rt.live(obj, "Cylinder3d.position_z")
  local res = rt.C().yetty_ysdf2_cylinder_3d_position_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.position_z = function(obj, value)
  rt.live(obj, "Cylinder3d.position_z")
  local res = rt.C().yetty_ysdf2_cylinder_3d_position_z_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.radius = function(obj)
  rt.live(obj, "Cylinder3d.radius")
  local res = rt.C().yetty_ysdf2_cylinder_3d_radius_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.radius = function(obj, value)
  rt.live(obj, "Cylinder3d.radius")
  local res = rt.C().yetty_ysdf2_cylinder_3d_radius_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.half_height = function(obj)
  rt.live(obj, "Cylinder3d.half_height")
  local res = rt.C().yetty_ysdf2_cylinder_3d_half_height_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.half_height = function(obj, value)
  rt.live(obj, "Cylinder3d.half_height")
  local res = rt.C().yetty_ysdf2_cylinder_3d_half_height_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.id = function(obj)
  rt.live(obj, "Cylinder3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.id = function(obj, value)
  rt.live(obj, "Cylinder3d.id")
  local res = rt.C().yetty_ydrawlist2_shape_id_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.z = function(obj)
  rt.live(obj, "Cylinder3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.z = function(obj, value)
  rt.live(obj, "Cylinder3d.z")
  local res = rt.C().yetty_ydrawlist2_shape_z_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.fill = function(obj)
  rt.live(obj, "Cylinder3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.fill = function(obj, value)
  rt.live(obj, "Cylinder3d.fill")
  local res = rt.C().yetty_ydrawlist2_shape_fill_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.stroke = function(obj)
  rt.live(obj, "Cylinder3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.stroke = function(obj, value)
  rt.live(obj, "Cylinder3d.stroke")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_set(obj.handle, value)
  rt.check(res)
end
Cylinder3d.__prop_get.stroke_width = function(obj)
  rt.live(obj, "Cylinder3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_get(obj.handle)
  rt.check(res)
  return res.value
end
Cylinder3d.__prop_set.stroke_width = function(obj, value)
  rt.live(obj, "Cylinder3d.stroke_width")
  local res = rt.C().yetty_ydrawlist2_shape_stroke_width_set(obj.handle, value)
  rt.check(res)
end
function Cylinder3d:destroy()
  rt.object_free(self)
end
Cylinder3d.__spec = {
  setters = {
    fill = { fn = "set_fill", n = 1 },
    stroke = { fn = "set_stroke", n = 1 },
  },
  props = {
    fill = true,
    half_height = true,
    id = true,
    position_x = true,
    position_y = true,
    position_z = true,
    radius = true,
    stroke = true,
    stroke_width = true,
    z = true,
  },
  adders = {
  },
}
setmetatable(Cylinder3d, { __call = function(cls, spec)
  local obj = cls.new()
  if spec ~= nil then rt.apply_spec(obj, spec, cls.__spec) end
  return obj
end })
M.Cylinder3d = Cylinder3d
return M
