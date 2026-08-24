// Package ydraw — the ydraw client interface for Go: one drawable
// list, immediate appends in call order; Add() manages nothing and
// returns nothing; font ids are user-chosen record fields.
//
// Feature gating: feature-gated classes (Video, when the build
// sets YETTY_ENABLE_FEATURE_YVIDEO=OFF) declare their native
// symbols weak, so this package links and runs against a
// feature-off libyetty_ffi.so. Discovery is Has<Class>()
// (e.g. HasVideo); constructing a gated class without its
// feature returns/panics with a feature-named error.
//
// GENERATED from model.yaml by tools/ffi-codegen/go/ffigen.py — do not edit.
package ydraw

// #cgo LDFLAGS: -lyetty_ffi
// #include <stdlib.h>
// #include <stdint.h>
// #include <stddef.h>
// struct yetty_yclass_object;
// struct yetty_ycore_error { const char *msg; const char *file; const char *func; int line; struct yetty_ycore_error *cause; };
// struct yetty_ycore_buffer { uint8_t *data; size_t capacity; size_t size; };
// struct yetty_ycore_void_result { int ok; union { int value; struct yetty_ycore_error error; }; };
// struct yetty_yclass_object_ptr_result { int ok; union { struct yetty_yclass_object *value; struct yetty_ycore_error error; }; };
// struct yetty_ycore_void_result yetty_api_yplot_add_buffer(struct yetty_yclass_object *obj, struct yetty_yclass_object *child);
// struct yetty_ycore_void_result yetty_api_yplot_add_function(struct yetty_yclass_object *obj, struct yetty_yclass_object *child);
// struct yetty_yclass_object_ptr_result yetty_api_yplot_buffer_create(void *ctx);
// struct yetty_ycore_void_result yetty_api_yplot_buffer_size_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_yclass_object_ptr_result yetty_api_yplot_curve_create(void *ctx);
// struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object *obj);
// struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(void *ctx);
// struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(void *ctx);
// struct yetty_ycore_void_result yetty_api_yplot_plot_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_api_yplot_plot_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_api_yplot_plot_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_api_yplot_plot_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_api_yplot_set_expression(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_api_yplot_set_noaxes(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_api_yplot_set_nogrid(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_api_yplot_set_nolabels(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_api_yplot_set_size(struct yetty_yclass_object *obj, float value0, float value1);
// struct yetty_ycore_void_result yetty_api_yplot_set_title(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_api_yplot_set_values(struct yetty_yclass_object *obj, struct yetty_ycore_buffer value);
// struct yetty_ycore_void_result yetty_api_yplot_set_view(struct yetty_yclass_object *obj, float value0, float value1, float value2, float value3);
// struct yetty_ycore_void_result yetty_api_yplot_set_x_label(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_api_yplot_set_x_range(struct yetty_yclass_object *obj, float value0, float value1);
// struct yetty_ycore_void_result yetty_api_yplot_set_y_label(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_api_yplot_set_y_range(struct yetty_yclass_object *obj, float value0, float value1);
// struct yetty_ycore_void_result yetty_yclass_object_free(struct yetty_yclass_object *obj);
// struct yetty_yclass_object_ptr_result yetty_ycomplex2_image_create(void *ctx);
// struct yetty_ycore_void_result yetty_ycomplex2_image_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_image_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_image_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_image_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_azimuth_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_create(void *ctx);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_elevation_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_wireframe_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_mesh_zoom_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_set_glb(struct yetty_yclass_object *obj, const char *value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_set_h264(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_ycomplex2_set_path(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_ycomplex2_set_source(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_ycomplex2_set_wgsl_path(struct yetty_yclass_object *obj, const char *value);
// struct yetty_yclass_object_ptr_result yetty_ycomplex2_shadertoy_create(void *ctx);
// struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ycomplex2_shadertoy_y_set(struct yetty_yclass_object *obj, float value);
// __attribute__((weak)) struct yetty_yclass_object_ptr_result yetty_ycomplex2_video_create(void *ctx);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_fps_set(struct yetty_yclass_object *obj, float value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_height_set(struct yetty_yclass_object *obj, float value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_id_set(struct yetty_yclass_object *obj, uint32_t value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_video_h_set(struct yetty_yclass_object *obj, uint32_t value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_video_w_set(struct yetty_yclass_object *obj, uint32_t value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_width_set(struct yetty_yclass_object *obj, float value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_x_set(struct yetty_yclass_object *obj, float value);
// __attribute__((weak)) struct yetty_ycore_void_result yetty_ycomplex2_video_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ydrawlist2_add(struct yetty_yclass_object *obj, struct yetty_yclass_object *child);
// struct yetty_ycore_void_result yetty_ydrawlist2_dcs_emit(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ydrawlist2_destroy(struct yetty_yclass_object *obj);
// struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_create(void *ctx);
// struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_create(void *ctx);
// struct yetty_ycore_void_result yetty_ydrawlist2_font_font_id_set(struct yetty_yclass_object *obj, int32_t value);
// struct yetty_ycore_void_result yetty_ydrawlist2_set_body(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_ydrawlist2_set_color(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_ydrawlist2_set_fill(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_ydrawlist2_set_name(struct yetty_yclass_object *obj, const char *value);
// struct yetty_ycore_void_result yetty_ydrawlist2_set_stroke(struct yetty_yclass_object *obj, const char *value);
// struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_create(void *ctx);
// struct yetty_ycore_void_result yetty_ydrawlist2_shape_id_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_ydrawlist2_shape_stroke_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ydrawlist2_shape_z_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_create(void *ctx);
// struct yetty_ycore_void_result yetty_ydrawlist2_text_font_id_set(struct yetty_yclass_object *obj, int32_t value);
// struct yetty_ycore_void_result yetty_ydrawlist2_text_font_size_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ydrawlist2_text_layer_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_ydrawlist2_text_rotation_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ydrawlist2_text_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ydrawlist2_text_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_arc_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_arc_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_arc_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_arc_thickness_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_z_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_z_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_corner_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_box_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_box_half_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_box_half_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_capsule_end_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_capsule_end_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_capsule_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_capsule_start_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_capsule_start_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_circle_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_circle_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_circle_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cross_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cross_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cross_corner_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_cross_half_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cross_half_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_half_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_z_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_egg_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_egg_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_egg_radius_inner_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_egg_radius_outer_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_heart_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_heart_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_heart_scale_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_hexagon_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_hexagram_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color0_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color1_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_corner_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x0_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x1_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y0_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y1_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_moon_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_moon_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_moon_offset_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_moon_radius_inner_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_moon_radius_outer_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_octogon_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_octogon_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_octogon_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_pentagon_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_pentagram_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pie_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_pie_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_pie_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_inner_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_outer_set(struct yetty_yclass_object *obj, uint32_t value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_corner_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cx_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cy_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ring_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ring_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_ring_normal_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ring_normal_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ring_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_ring_thickness_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_height_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_left_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_right_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_left_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_right_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_x_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_rounded_x_width_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_segment_end_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_segment_end_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_segment_start_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_segment_start_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_z_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_star_center_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_star_center_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_star_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_star_inner_ratio_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_star_num_points_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_star_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_torus_3d_major_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_torus_3d_minor_radius_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_z_set(struct yetty_yclass_object *obj, float value);
// struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_create(void *ctx);
// struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_y_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_x_set(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_y_set(struct yetty_yclass_object *obj, float value);
// static int yetty_bind_has_video(void) { return yetty_ycomplex2_video_create != 0; }
// static const char *yetty_bind_check(struct yetty_ycore_void_result result) { return result.ok ? (const char *)0 : (result.error.msg ? result.error.msg : "yetty error"); }
// static struct yetty_yclass_object *yetty_bind_object_value(struct yetty_yclass_object_ptr_result result) { return result.ok ? result.value : (struct yetty_yclass_object *)0; }
// static const char *yetty_bind_object_check(struct yetty_yclass_object_ptr_result result) { return result.ok ? (const char *)0 : (result.error.msg ? result.error.msg : "yetty create failed"); }
import "C"

import (
	"errors"
	"unsafe"
)

// Drawable is anything Add() can pack into a DrawableList.
type Drawable interface {
	materialize() (*C.struct_yetty_yclass_object, error)
	// release frees a materialized object through the class's own
	// destructor (a class with owned resources, like Plot's DSL
	// source buffer, declares a destroy slot; the plain object free
	// would leak those).
	release(object *C.struct_yetty_yclass_object)
}

func applyVoid(result C.struct_yetty_ycore_void_result) error {
	message := C.yetty_bind_check(result)
	if message != nil {
		return errors.New(C.GoString(message))
	}
	return nil
}

func createObject(result C.struct_yetty_yclass_object_ptr_result) (*C.struct_yetty_yclass_object, error) {
	message := C.yetty_bind_object_check(result)
	if message != nil {
		return nil, errors.New(C.GoString(message))
	}
	return C.yetty_bind_object_value(result), nil
}

func newBuffer(values []float64) C.struct_yetty_ycore_buffer {
	var buffer C.struct_yetty_ycore_buffer
	if len(values) == 0 {
		return buffer
	}
	byteCount := C.size_t(len(values) * 4)
	data := C.malloc(byteCount)
	floats := unsafe.Slice((*float32)(data), len(values))
	for index, value := range values {
		floats[index] = float32(value)
	}
	buffer.data = (*C.uint8_t)(data)
	buffer.size = byteCount
	buffer.capacity = byteCount
	return buffer
}

func freeBuffer(buffer C.struct_yetty_ycore_buffer) {
	if buffer.data != nil {
		C.free(unsafe.Pointer(buffer.data))
	}
}

// Font — yclass ydrawlist2:font as a value struct.
type Font struct {
	Name string
	FontID int32
}

func (value Font) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ydrawlist2_font_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Name != "" {
		text := C.CString(value.Name)
		err := applyVoid(C.yetty_ydrawlist2_set_name(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.FontID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_font_font_id_set(object, C.int32_t(value.FontID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Font) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Text — yclass ydrawlist2:text as a value struct.
type Text struct {
	Body string
	Color string
	X float64
	Y float64
	FontSize float64
	Layer uint32
	FontID int32
	Rotation float64
}

func (value Text) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ydrawlist2_text_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Body != "" {
		text := C.CString(value.Body)
		err := applyVoid(C.yetty_ydrawlist2_set_body(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Color != "" {
		text := C.CString(value.Color)
		err := applyVoid(C.yetty_ydrawlist2_set_color(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.X != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_text_x_set(object, C.float(value.X))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Y != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_text_y_set(object, C.float(value.Y))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.FontSize != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_text_font_size_set(object, C.float(value.FontSize))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Layer != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_text_layer_set(object, C.uint32_t(value.Layer))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.FontID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_text_font_id_set(object, C.int32_t(value.FontID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Rotation != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_text_rotation_set(object, C.float(value.Rotation))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Text) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Shape — yclass ydrawlist2:shape as a value struct.
type Shape struct {
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Shape) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ydrawlist2_shape_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Shape) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Circle — yclass ysdf2:circle as a value struct.
type Circle struct {
	CenterX float64
	CenterY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Circle) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_circle_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_circle_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_circle_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_circle_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Circle) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Box — yclass ysdf2:box as a value struct.
type Box struct {
	CenterX float64
	CenterY float64
	HalfWidth float64
	HalfHeight float64
	CornerRadius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Box) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_box_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfWidth != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_half_width_set(object, C.float(value.HalfWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfHeight != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_half_height_set(object, C.float(value.HalfHeight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CornerRadius != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_corner_radius_set(object, C.float(value.CornerRadius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Box) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Segment — yclass ysdf2:segment as a value struct.
type Segment struct {
	StartX float64
	StartY float64
	EndX float64
	EndY float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Segment) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_segment_create(nil))
	if err != nil {
		return nil, err
	}
	if value.StartX != 0 {
		if err := applyVoid(C.yetty_ysdf2_segment_start_x_set(object, C.float(value.StartX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StartY != 0 {
		if err := applyVoid(C.yetty_ysdf2_segment_start_y_set(object, C.float(value.StartY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.EndX != 0 {
		if err := applyVoid(C.yetty_ysdf2_segment_end_x_set(object, C.float(value.EndX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.EndY != 0 {
		if err := applyVoid(C.yetty_ysdf2_segment_end_y_set(object, C.float(value.EndY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Segment) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Triangle — yclass ysdf2:triangle as a value struct.
type Triangle struct {
	VertexAX float64
	VertexAY float64
	VertexBX float64
	VertexBY float64
	VertexCX float64
	VertexCY float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Triangle) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_triangle_create(nil))
	if err != nil {
		return nil, err
	}
	if value.VertexAX != 0 {
		if err := applyVoid(C.yetty_ysdf2_triangle_vertex_a_x_set(object, C.float(value.VertexAX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.VertexAY != 0 {
		if err := applyVoid(C.yetty_ysdf2_triangle_vertex_a_y_set(object, C.float(value.VertexAY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.VertexBX != 0 {
		if err := applyVoid(C.yetty_ysdf2_triangle_vertex_b_x_set(object, C.float(value.VertexBX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.VertexBY != 0 {
		if err := applyVoid(C.yetty_ysdf2_triangle_vertex_b_y_set(object, C.float(value.VertexBY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.VertexCX != 0 {
		if err := applyVoid(C.yetty_ysdf2_triangle_vertex_c_x_set(object, C.float(value.VertexCX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.VertexCY != 0 {
		if err := applyVoid(C.yetty_ysdf2_triangle_vertex_c_y_set(object, C.float(value.VertexCY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Triangle) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Ellipse — yclass ysdf2:ellipse as a value struct.
type Ellipse struct {
	CenterX float64
	CenterY float64
	RadiusX float64
	RadiusY float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Ellipse) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_ellipse_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_ellipse_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_ellipse_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusX != 0 {
		if err := applyVoid(C.yetty_ysdf2_ellipse_radius_x_set(object, C.float(value.RadiusX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusY != 0 {
		if err := applyVoid(C.yetty_ysdf2_ellipse_radius_y_set(object, C.float(value.RadiusY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Ellipse) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Arc — yclass ysdf2:arc as a value struct.
type Arc struct {
	CenterX float64
	CenterY float64
	ApertureX float64
	ApertureY float64
	Radius float64
	Thickness float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Arc) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_arc_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_arc_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_arc_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ApertureX != 0 {
		if err := applyVoid(C.yetty_ysdf2_arc_aperture_x_set(object, C.float(value.ApertureX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ApertureY != 0 {
		if err := applyVoid(C.yetty_ysdf2_arc_aperture_y_set(object, C.float(value.ApertureY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_arc_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Thickness != 0 {
		if err := applyVoid(C.yetty_ysdf2_arc_thickness_set(object, C.float(value.Thickness))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Arc) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// RoundedBox — yclass ysdf2:rounded_box as a value struct.
type RoundedBox struct {
	CenterX float64
	CenterY float64
	HalfWidth float64
	HalfHeight float64
	RadiusTopRight float64
	RadiusBottomRight float64
	RadiusTopLeft float64
	RadiusBottomLeft float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value RoundedBox) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_rounded_box_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfWidth != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_half_width_set(object, C.float(value.HalfWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfHeight != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_half_height_set(object, C.float(value.HalfHeight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusTopRight != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_radius_top_right_set(object, C.float(value.RadiusTopRight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusBottomRight != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_radius_bottom_right_set(object, C.float(value.RadiusBottomRight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusTopLeft != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_radius_top_left_set(object, C.float(value.RadiusTopLeft))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusBottomLeft != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_box_radius_bottom_left_set(object, C.float(value.RadiusBottomLeft))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value RoundedBox) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Rhombus — yclass ysdf2:rhombus as a value struct.
type Rhombus struct {
	CenterX float64
	CenterY float64
	HalfWidth float64
	HalfHeight float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Rhombus) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_rhombus_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_rhombus_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_rhombus_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfWidth != 0 {
		if err := applyVoid(C.yetty_ysdf2_rhombus_half_width_set(object, C.float(value.HalfWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfHeight != 0 {
		if err := applyVoid(C.yetty_ysdf2_rhombus_half_height_set(object, C.float(value.HalfHeight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Rhombus) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Pentagon — yclass ysdf2:pentagon as a value struct.
type Pentagon struct {
	CenterX float64
	CenterY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Pentagon) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_pentagon_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_pentagon_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_pentagon_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_pentagon_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Pentagon) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Hexagon — yclass ysdf2:hexagon as a value struct.
type Hexagon struct {
	CenterX float64
	CenterY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Hexagon) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_hexagon_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_hexagon_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_hexagon_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_hexagon_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Hexagon) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Star — yclass ysdf2:star as a value struct.
type Star struct {
	CenterX float64
	CenterY float64
	Radius float64
	NumPoints float64
	InnerRatio float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Star) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_star_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_star_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_star_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_star_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.NumPoints != 0 {
		if err := applyVoid(C.yetty_ysdf2_star_num_points_set(object, C.float(value.NumPoints))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.InnerRatio != 0 {
		if err := applyVoid(C.yetty_ysdf2_star_inner_ratio_set(object, C.float(value.InnerRatio))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Star) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Pie — yclass ysdf2:pie as a value struct.
type Pie struct {
	CenterX float64
	CenterY float64
	ApertureX float64
	ApertureY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Pie) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_pie_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_pie_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_pie_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ApertureX != 0 {
		if err := applyVoid(C.yetty_ysdf2_pie_aperture_x_set(object, C.float(value.ApertureX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ApertureY != 0 {
		if err := applyVoid(C.yetty_ysdf2_pie_aperture_y_set(object, C.float(value.ApertureY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_pie_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Pie) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Ring — yclass ysdf2:ring as a value struct.
type Ring struct {
	CenterX float64
	CenterY float64
	NormalX float64
	NormalY float64
	Radius float64
	Thickness float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Ring) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_ring_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_ring_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_ring_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.NormalX != 0 {
		if err := applyVoid(C.yetty_ysdf2_ring_normal_x_set(object, C.float(value.NormalX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.NormalY != 0 {
		if err := applyVoid(C.yetty_ysdf2_ring_normal_y_set(object, C.float(value.NormalY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_ring_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Thickness != 0 {
		if err := applyVoid(C.yetty_ysdf2_ring_thickness_set(object, C.float(value.Thickness))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Ring) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Heart — yclass ysdf2:heart as a value struct.
type Heart struct {
	CenterX float64
	CenterY float64
	Scale float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Heart) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_heart_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_heart_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_heart_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Scale != 0 {
		if err := applyVoid(C.yetty_ysdf2_heart_scale_set(object, C.float(value.Scale))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Heart) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Cross — yclass ysdf2:cross as a value struct.
type Cross struct {
	CenterX float64
	CenterY float64
	HalfWidth float64
	HalfHeight float64
	CornerRadius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Cross) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_cross_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_cross_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_cross_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfWidth != 0 {
		if err := applyVoid(C.yetty_ysdf2_cross_half_width_set(object, C.float(value.HalfWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfHeight != 0 {
		if err := applyVoid(C.yetty_ysdf2_cross_half_height_set(object, C.float(value.HalfHeight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CornerRadius != 0 {
		if err := applyVoid(C.yetty_ysdf2_cross_corner_radius_set(object, C.float(value.CornerRadius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Cross) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// RoundedX — yclass ysdf2:rounded_x as a value struct.
type RoundedX struct {
	CenterX float64
	CenterY float64
	Width float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value RoundedX) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_rounded_x_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_x_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_x_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Width != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_x_width_set(object, C.float(value.Width))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_rounded_x_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value RoundedX) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Capsule — yclass ysdf2:capsule as a value struct.
type Capsule struct {
	StartX float64
	StartY float64
	EndX float64
	EndY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Capsule) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_capsule_create(nil))
	if err != nil {
		return nil, err
	}
	if value.StartX != 0 {
		if err := applyVoid(C.yetty_ysdf2_capsule_start_x_set(object, C.float(value.StartX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StartY != 0 {
		if err := applyVoid(C.yetty_ysdf2_capsule_start_y_set(object, C.float(value.StartY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.EndX != 0 {
		if err := applyVoid(C.yetty_ysdf2_capsule_end_x_set(object, C.float(value.EndX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.EndY != 0 {
		if err := applyVoid(C.yetty_ysdf2_capsule_end_y_set(object, C.float(value.EndY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_capsule_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Capsule) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Moon — yclass ysdf2:moon as a value struct.
type Moon struct {
	CenterX float64
	CenterY float64
	Offset float64
	RadiusOuter float64
	RadiusInner float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Moon) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_moon_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_moon_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_moon_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Offset != 0 {
		if err := applyVoid(C.yetty_ysdf2_moon_offset_set(object, C.float(value.Offset))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusOuter != 0 {
		if err := applyVoid(C.yetty_ysdf2_moon_radius_outer_set(object, C.float(value.RadiusOuter))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusInner != 0 {
		if err := applyVoid(C.yetty_ysdf2_moon_radius_inner_set(object, C.float(value.RadiusInner))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Moon) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Egg — yclass ysdf2:egg as a value struct.
type Egg struct {
	CenterX float64
	CenterY float64
	RadiusOuter float64
	RadiusInner float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Egg) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_egg_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_egg_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_egg_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusOuter != 0 {
		if err := applyVoid(C.yetty_ysdf2_egg_radius_outer_set(object, C.float(value.RadiusOuter))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.RadiusInner != 0 {
		if err := applyVoid(C.yetty_ysdf2_egg_radius_inner_set(object, C.float(value.RadiusInner))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Egg) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Octogon — yclass ysdf2:octogon as a value struct.
type Octogon struct {
	CenterX float64
	CenterY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Octogon) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_octogon_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_octogon_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_octogon_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_octogon_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Octogon) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Hexagram — yclass ysdf2:hexagram as a value struct.
type Hexagram struct {
	CenterX float64
	CenterY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Hexagram) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_hexagram_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_hexagram_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_hexagram_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_hexagram_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Hexagram) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Pentagram — yclass ysdf2:pentagram as a value struct.
type Pentagram struct {
	CenterX float64
	CenterY float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Pentagram) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_pentagram_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_pentagram_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_pentagram_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_pentagram_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Pentagram) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// LinearGradientBox — yclass ysdf2:linear_gradient_box as a value struct.
type LinearGradientBox struct {
	CenterX float64
	CenterY float64
	HalfWidth float64
	HalfHeight float64
	CornerRadius float64
	GradX0 float64
	GradY0 float64
	GradX1 float64
	GradY1 float64
	Color0 uint32
	Color1 uint32
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value LinearGradientBox) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_linear_gradient_box_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfWidth != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_half_width_set(object, C.float(value.HalfWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfHeight != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_half_height_set(object, C.float(value.HalfHeight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CornerRadius != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_corner_radius_set(object, C.float(value.CornerRadius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.GradX0 != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_grad_x0_set(object, C.float(value.GradX0))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.GradY0 != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_grad_y0_set(object, C.float(value.GradY0))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.GradX1 != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_grad_x1_set(object, C.float(value.GradX1))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.GradY1 != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_grad_y1_set(object, C.float(value.GradY1))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Color0 != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_color0_set(object, C.uint32_t(value.Color0))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Color1 != 0 {
		if err := applyVoid(C.yetty_ysdf2_linear_gradient_box_color1_set(object, C.uint32_t(value.Color1))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value LinearGradientBox) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// RadialGradientBox — yclass ysdf2:radial_gradient_box as a value struct.
type RadialGradientBox struct {
	CenterX float64
	CenterY float64
	HalfWidth float64
	HalfHeight float64
	CornerRadius float64
	GradCx float64
	GradCy float64
	GradRadius float64
	ColorInner uint32
	ColorOuter uint32
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value RadialGradientBox) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_radial_gradient_box_create(nil))
	if err != nil {
		return nil, err
	}
	if value.CenterX != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_center_x_set(object, C.float(value.CenterX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CenterY != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_center_y_set(object, C.float(value.CenterY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfWidth != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_half_width_set(object, C.float(value.HalfWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfHeight != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_half_height_set(object, C.float(value.HalfHeight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.CornerRadius != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_corner_radius_set(object, C.float(value.CornerRadius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.GradCx != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_grad_cx_set(object, C.float(value.GradCx))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.GradCy != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_grad_cy_set(object, C.float(value.GradCy))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.GradRadius != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_grad_radius_set(object, C.float(value.GradRadius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ColorInner != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_color_inner_set(object, C.uint32_t(value.ColorInner))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ColorOuter != 0 {
		if err := applyVoid(C.yetty_ysdf2_radial_gradient_box_color_outer_set(object, C.uint32_t(value.ColorOuter))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value RadialGradientBox) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Sphere3d — yclass ysdf2:sphere_3d as a value struct.
type Sphere3d struct {
	PositionX float64
	PositionY float64
	PositionZ float64
	Radius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Sphere3d) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_sphere_3d_create(nil))
	if err != nil {
		return nil, err
	}
	if value.PositionX != 0 {
		if err := applyVoid(C.yetty_ysdf2_sphere_3d_position_x_set(object, C.float(value.PositionX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionY != 0 {
		if err := applyVoid(C.yetty_ysdf2_sphere_3d_position_y_set(object, C.float(value.PositionY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionZ != 0 {
		if err := applyVoid(C.yetty_ysdf2_sphere_3d_position_z_set(object, C.float(value.PositionZ))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_sphere_3d_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Sphere3d) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Box3d — yclass ysdf2:box_3d as a value struct.
type Box3d struct {
	PositionX float64
	PositionY float64
	PositionZ float64
	HalfSizeX float64
	HalfSizeY float64
	HalfSizeZ float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Box3d) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_box_3d_create(nil))
	if err != nil {
		return nil, err
	}
	if value.PositionX != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_3d_position_x_set(object, C.float(value.PositionX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionY != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_3d_position_y_set(object, C.float(value.PositionY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionZ != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_3d_position_z_set(object, C.float(value.PositionZ))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfSizeX != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_3d_half_size_x_set(object, C.float(value.HalfSizeX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfSizeY != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_3d_half_size_y_set(object, C.float(value.HalfSizeY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfSizeZ != 0 {
		if err := applyVoid(C.yetty_ysdf2_box_3d_half_size_z_set(object, C.float(value.HalfSizeZ))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Box3d) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Torus3d — yclass ysdf2:torus_3d as a value struct.
type Torus3d struct {
	PositionX float64
	PositionY float64
	PositionZ float64
	MajorRadius float64
	MinorRadius float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Torus3d) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_torus_3d_create(nil))
	if err != nil {
		return nil, err
	}
	if value.PositionX != 0 {
		if err := applyVoid(C.yetty_ysdf2_torus_3d_position_x_set(object, C.float(value.PositionX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionY != 0 {
		if err := applyVoid(C.yetty_ysdf2_torus_3d_position_y_set(object, C.float(value.PositionY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionZ != 0 {
		if err := applyVoid(C.yetty_ysdf2_torus_3d_position_z_set(object, C.float(value.PositionZ))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.MajorRadius != 0 {
		if err := applyVoid(C.yetty_ysdf2_torus_3d_major_radius_set(object, C.float(value.MajorRadius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.MinorRadius != 0 {
		if err := applyVoid(C.yetty_ysdf2_torus_3d_minor_radius_set(object, C.float(value.MinorRadius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Torus3d) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Cylinder3d — yclass ysdf2:cylinder_3d as a value struct.
type Cylinder3d struct {
	PositionX float64
	PositionY float64
	PositionZ float64
	Radius float64
	HalfHeight float64
	Fill string
	Stroke string
	ID uint32
	Z uint32
	StrokeWidth float64
}

func (value Cylinder3d) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ysdf2_cylinder_3d_create(nil))
	if err != nil {
		return nil, err
	}
	if value.PositionX != 0 {
		if err := applyVoid(C.yetty_ysdf2_cylinder_3d_position_x_set(object, C.float(value.PositionX))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionY != 0 {
		if err := applyVoid(C.yetty_ysdf2_cylinder_3d_position_y_set(object, C.float(value.PositionY))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.PositionZ != 0 {
		if err := applyVoid(C.yetty_ysdf2_cylinder_3d_position_z_set(object, C.float(value.PositionZ))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Radius != 0 {
		if err := applyVoid(C.yetty_ysdf2_cylinder_3d_radius_set(object, C.float(value.Radius))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.HalfHeight != 0 {
		if err := applyVoid(C.yetty_ysdf2_cylinder_3d_half_height_set(object, C.float(value.HalfHeight))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Fill != "" {
		text := C.CString(value.Fill)
		err := applyVoid(C.yetty_ydrawlist2_set_fill(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Stroke != "" {
		text := C.CString(value.Stroke)
		err := applyVoid(C.yetty_ydrawlist2_set_stroke(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Z != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_z_set(object, C.uint32_t(value.Z))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.StrokeWidth != 0 {
		if err := applyVoid(C.yetty_ydrawlist2_shape_stroke_width_set(object, C.float(value.StrokeWidth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Cylinder3d) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Curve — yclass api_yplot:curve as a value struct.
type Curve struct {
	Name string
	Color string
}

func (value Curve) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_api_yplot_curve_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Name != "" {
		text := C.CString(value.Name)
		err := applyVoid(C.yetty_api_yplot_set_name(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Color != "" {
		text := C.CString(value.Color)
		err := applyVoid(C.yetty_api_yplot_set_color(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Curve) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Function — yclass api_yplot:function as a value struct.
type Function struct {
	Body string
	Name string
	Color string
}

func (value Function) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_api_yplot_function_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Body != "" {
		text := C.CString(value.Body)
		err := applyVoid(C.yetty_api_yplot_set_body(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Name != "" {
		text := C.CString(value.Name)
		err := applyVoid(C.yetty_api_yplot_set_name(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Color != "" {
		text := C.CString(value.Color)
		err := applyVoid(C.yetty_api_yplot_set_color(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Function) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Buffer — yclass api_yplot:buffer as a value struct.
type Buffer struct {
	Values []float64
	Size uint32
	Name string
	Color string
}

func (value Buffer) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_api_yplot_buffer_create(nil))
	if err != nil {
		return nil, err
	}
	if len(value.Values) > 0 {
		buffer := newBuffer(value.Values)
		err := applyVoid(C.yetty_api_yplot_set_values(object, buffer))
		freeBuffer(buffer)
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Size != 0 {
		if err := applyVoid(C.yetty_api_yplot_buffer_size_set(object, C.uint32_t(value.Size))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Name != "" {
		text := C.CString(value.Name)
		err := applyVoid(C.yetty_api_yplot_set_name(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Color != "" {
		text := C.CString(value.Color)
		err := applyVoid(C.yetty_api_yplot_set_color(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Buffer) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Plot — yclass api_yplot:plot as a value struct.
type Plot struct {
	Source string
	Functions []Function
	Title string
	XLabel string
	YLabel string
	Size []float64
	XRange []float64
	YRange []float64
	Buffers []Buffer
	View []float64
	NoGrid bool
	NoAxes bool
	NoLabels bool
	X float64
	Y float64
	Width float64
	Height float64
}

func (value Plot) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_api_yplot_plot_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Source != "" {
		text := C.CString(value.Source)
		err := applyVoid(C.yetty_api_yplot_set_expression(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	for _, element := range value.Functions {
		child, err := element.materialize()
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
		err = applyVoid(C.yetty_api_yplot_add_function(object, child))
		element.release(child)
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.Title != "" {
		text := C.CString(value.Title)
		err := applyVoid(C.yetty_api_yplot_set_title(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.XLabel != "" {
		text := C.CString(value.XLabel)
		err := applyVoid(C.yetty_api_yplot_set_x_label(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.YLabel != "" {
		text := C.CString(value.YLabel)
		err := applyVoid(C.yetty_api_yplot_set_y_label(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if len(value.Size) > 0 {
		if len(value.Size) != 2 {
			err := errors.New("Plot: Size expects exactly 2 values")
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
		if err := applyVoid(C.yetty_api_yplot_set_size(object, C.float(value.Size[0]), C.float(value.Size[1]))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if len(value.XRange) > 0 {
		if len(value.XRange) != 2 {
			err := errors.New("Plot: XRange expects exactly 2 values")
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
		if err := applyVoid(C.yetty_api_yplot_set_x_range(object, C.float(value.XRange[0]), C.float(value.XRange[1]))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if len(value.YRange) > 0 {
		if len(value.YRange) != 2 {
			err := errors.New("Plot: YRange expects exactly 2 values")
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
		if err := applyVoid(C.yetty_api_yplot_set_y_range(object, C.float(value.YRange[0]), C.float(value.YRange[1]))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	for _, element := range value.Buffers {
		child, err := element.materialize()
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
		err = applyVoid(C.yetty_api_yplot_add_buffer(object, child))
		element.release(child)
		if err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if len(value.View) > 0 {
		if len(value.View) != 4 {
			err := errors.New("Plot: View expects exactly 4 values")
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
		if err := applyVoid(C.yetty_api_yplot_set_view(object, C.float(value.View[0]), C.float(value.View[1]), C.float(value.View[2]), C.float(value.View[3]))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.NoGrid {
		if err := applyVoid(C.yetty_api_yplot_set_nogrid(object, C.uint32_t(1))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.NoAxes {
		if err := applyVoid(C.yetty_api_yplot_set_noaxes(object, C.uint32_t(1))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.NoLabels {
		if err := applyVoid(C.yetty_api_yplot_set_nolabels(object, C.uint32_t(1))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.X != 0 {
		if err := applyVoid(C.yetty_api_yplot_plot_x_set(object, C.float(value.X))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.Y != 0 {
		if err := applyVoid(C.yetty_api_yplot_plot_y_set(object, C.float(value.Y))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.Width != 0 {
		if err := applyVoid(C.yetty_api_yplot_plot_width_set(object, C.float(value.Width))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	if value.Height != 0 {
		if err := applyVoid(C.yetty_api_yplot_plot_height_set(object, C.float(value.Height))); err != nil {
			_ = applyVoid(C.yetty_api_yplot_destroy(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Plot) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_api_yplot_destroy(object))
}

// Image — yclass ycomplex2:image as a value struct.
type Image struct {
	Path string
	X float64
	Y float64
	Width float64
	Height float64
}

func (value Image) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ycomplex2_image_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Path != "" {
		text := C.CString(value.Path)
		err := applyVoid(C.yetty_ycomplex2_set_path(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.X != 0 {
		if err := applyVoid(C.yetty_ycomplex2_image_x_set(object, C.float(value.X))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Y != 0 {
		if err := applyVoid(C.yetty_ycomplex2_image_y_set(object, C.float(value.Y))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Width != 0 {
		if err := applyVoid(C.yetty_ycomplex2_image_width_set(object, C.float(value.Width))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Height != 0 {
		if err := applyVoid(C.yetty_ycomplex2_image_height_set(object, C.float(value.Height))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Image) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Mesh — yclass ycomplex2:mesh as a value struct.
type Mesh struct {
	Path string
	X float64
	Y float64
	Width float64
	Height float64
	Azimuth float64
	Elevation float64
	Zoom float64
	Wireframe bool
}

func (value Mesh) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ycomplex2_mesh_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Path != "" {
		text := C.CString(value.Path)
		err := applyVoid(C.yetty_ycomplex2_set_glb(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.X != 0 {
		if err := applyVoid(C.yetty_ycomplex2_mesh_x_set(object, C.float(value.X))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Y != 0 {
		if err := applyVoid(C.yetty_ycomplex2_mesh_y_set(object, C.float(value.Y))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Width != 0 {
		if err := applyVoid(C.yetty_ycomplex2_mesh_width_set(object, C.float(value.Width))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Height != 0 {
		if err := applyVoid(C.yetty_ycomplex2_mesh_height_set(object, C.float(value.Height))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Azimuth != 0 {
		if err := applyVoid(C.yetty_ycomplex2_mesh_azimuth_set(object, C.float(value.Azimuth))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Elevation != 0 {
		if err := applyVoid(C.yetty_ycomplex2_mesh_elevation_set(object, C.float(value.Elevation))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Zoom != 0 {
		if err := applyVoid(C.yetty_ycomplex2_mesh_zoom_set(object, C.float(value.Zoom))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Wireframe {
		if err := applyVoid(C.yetty_ycomplex2_mesh_wireframe_set(object, C.uint32_t(1))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Mesh) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Shadertoy — yclass ycomplex2:shadertoy as a value struct.
type Shadertoy struct {
	Source string
	Path string
	X float64
	Y float64
	Width float64
	Height float64
}

func (value Shadertoy) materialize() (*C.struct_yetty_yclass_object, error) {
	object, err := createObject(C.yetty_ycomplex2_shadertoy_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Source != "" {
		text := C.CString(value.Source)
		err := applyVoid(C.yetty_ycomplex2_set_source(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Path != "" {
		text := C.CString(value.Path)
		err := applyVoid(C.yetty_ycomplex2_set_wgsl_path(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.X != 0 {
		if err := applyVoid(C.yetty_ycomplex2_shadertoy_x_set(object, C.float(value.X))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Y != 0 {
		if err := applyVoid(C.yetty_ycomplex2_shadertoy_y_set(object, C.float(value.Y))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Width != 0 {
		if err := applyVoid(C.yetty_ycomplex2_shadertoy_width_set(object, C.float(value.Width))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Height != 0 {
		if err := applyVoid(C.yetty_ycomplex2_shadertoy_height_set(object, C.float(value.Height))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Shadertoy) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// Video — yclass ycomplex2:video as a value struct.
type Video struct {
	Path string
	X float64
	Y float64
	Width float64
	Height float64
	ID uint32
	VideoW uint32
	VideoH uint32
	FPS float64
}

func (value Video) materialize() (*C.struct_yetty_yclass_object, error) {
	if C.yetty_bind_has_video() == 0 {
		return nil, errors.New("Video requires a build with the yvideo feature enabled (its native symbols are missing from libyetty_ffi.so)")
	}
	object, err := createObject(C.yetty_ycomplex2_video_create(nil))
	if err != nil {
		return nil, err
	}
	if value.Path != "" {
		text := C.CString(value.Path)
		err := applyVoid(C.yetty_ycomplex2_set_h264(object, text))
		C.free(unsafe.Pointer(text))
		if err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.X != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_x_set(object, C.float(value.X))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Y != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_y_set(object, C.float(value.Y))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Width != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_width_set(object, C.float(value.Width))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.Height != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_height_set(object, C.float(value.Height))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.ID != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_id_set(object, C.uint32_t(value.ID))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.VideoW != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_video_w_set(object, C.uint32_t(value.VideoW))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.VideoH != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_video_h_set(object, C.uint32_t(value.VideoH))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	if value.FPS != 0 {
		if err := applyVoid(C.yetty_ycomplex2_video_fps_set(object, C.float(value.FPS))); err != nil {
			_ = applyVoid(C.yetty_yclass_object_free(object))
			return nil, err
		}
	}
	return object, nil
}

func (value Video) release(object *C.struct_yetty_yclass_object) {
	_ = applyVoid(C.yetty_yclass_object_free(object))
}

// HasVideo reports whether the loaded libyetty_ffi.so carries the yvideo feature (feature-discovery contract).
func HasVideo() bool {
	return C.yetty_bind_has_video() != 0
}

// DrawableList — the drawable list: one list, immediate
// appends in call order.
type DrawableList struct {
	handle *C.struct_yetty_yclass_object
}

func NewDrawableList() *DrawableList {
	object, err := createObject(C.yetty_ydrawlist2_drawable_list_create(nil))
	if err != nil {
		panic(err)
	}
	return &DrawableList{handle: object}
}

// Add packs the drawable's record into the list,
// immediately. It manages nothing and returns nothing.
func (list *DrawableList) Add(drawable Drawable) {
	if list.handle == nil {
		panic("DrawableList: already destroyed")
	}
	object, err := drawable.materialize()
	if err != nil {
		panic(err)
	}
	err = applyVoid(C.yetty_ydrawlist2_add(list.handle, object))
	drawable.release(object)
	if err != nil {
		panic(err)
	}
}

func (list *DrawableList) DcsEmit() {
	if list.handle == nil {
		panic("DrawableList: already destroyed")
	}
	if err := applyVoid(C.yetty_ydrawlist2_dcs_emit(list.handle)); err != nil {
		panic(err)
	}
}

func (list *DrawableList) Destroy() {
	if list.handle != nil {
		_ = applyVoid(C.yetty_ydrawlist2_destroy(list.handle))
		list.handle = nil
	}
}

