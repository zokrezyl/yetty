// @yetty/ydraw — the ydraw client interface: one drawable list,
// immediate appends in call order; add() manages nothing and
// returns nothing; font ids are user-chosen record fields.
//
// GENERATED from model.yaml by tools/ffi-codegen/typescript/ffigen.py — do not edit.
import * as runtime from "./runtime.mjs";

runtime.registerSignatures({
  yetty_api_yplot_add_buffer: "yetty_result_view yetty_api_yplot_add_buffer(void *obj, void *child)",
  yetty_api_yplot_add_function: "yetty_result_view yetty_api_yplot_add_function(void *obj, void *child)",
  yetty_api_yplot_buffer_create: "yetty_result_view yetty_api_yplot_buffer_create(void *ctx)",
  yetty_api_yplot_buffer_size_set: "yetty_result_view yetty_api_yplot_buffer_size_set(void *obj, uint32_t value)",
  yetty_api_yplot_curve_create: "yetty_result_view yetty_api_yplot_curve_create(void *ctx)",
  yetty_api_yplot_destroy: "yetty_result_view yetty_api_yplot_destroy(void *obj)",
  yetty_api_yplot_function_create: "yetty_result_view yetty_api_yplot_function_create(void *ctx)",
  yetty_api_yplot_plot_create: "yetty_result_view yetty_api_yplot_plot_create(void *ctx)",
  yetty_api_yplot_plot_height_set: "yetty_result_view yetty_api_yplot_plot_height_set(void *obj, float value)",
  yetty_api_yplot_plot_width_set: "yetty_result_view yetty_api_yplot_plot_width_set(void *obj, float value)",
  yetty_api_yplot_plot_x_set: "yetty_result_view yetty_api_yplot_plot_x_set(void *obj, float value)",
  yetty_api_yplot_plot_y_set: "yetty_result_view yetty_api_yplot_plot_y_set(void *obj, float value)",
  yetty_api_yplot_set_body: "yetty_result_view yetty_api_yplot_set_body(void *obj, const char *value)",
  yetty_api_yplot_set_color: "yetty_result_view yetty_api_yplot_set_color(void *obj, const char *value)",
  yetty_api_yplot_set_expression: "yetty_result_view yetty_api_yplot_set_expression(void *obj, const char *value)",
  yetty_api_yplot_set_name: "yetty_result_view yetty_api_yplot_set_name(void *obj, const char *value)",
  yetty_api_yplot_set_noaxes: "yetty_result_view yetty_api_yplot_set_noaxes(void *obj, uint32_t value)",
  yetty_api_yplot_set_nogrid: "yetty_result_view yetty_api_yplot_set_nogrid(void *obj, uint32_t value)",
  yetty_api_yplot_set_nolabels: "yetty_result_view yetty_api_yplot_set_nolabels(void *obj, uint32_t value)",
  yetty_api_yplot_set_size: "yetty_result_view yetty_api_yplot_set_size(void *obj, float value0, float value1)",
  yetty_api_yplot_set_title: "yetty_result_view yetty_api_yplot_set_title(void *obj, const char *value)",
  yetty_api_yplot_set_values: "yetty_result_view yetty_api_yplot_set_values(void *obj, yetty_ycore_buffer value)",
  yetty_api_yplot_set_view: "yetty_result_view yetty_api_yplot_set_view(void *obj, float value0, float value1, float value2, float value3)",
  yetty_api_yplot_set_x_label: "yetty_result_view yetty_api_yplot_set_x_label(void *obj, const char *value)",
  yetty_api_yplot_set_x_range: "yetty_result_view yetty_api_yplot_set_x_range(void *obj, float value0, float value1)",
  yetty_api_yplot_set_y_label: "yetty_result_view yetty_api_yplot_set_y_label(void *obj, const char *value)",
  yetty_api_yplot_set_y_range: "yetty_result_view yetty_api_yplot_set_y_range(void *obj, float value0, float value1)",
  yetty_yclass_object_free: "yetty_result_view yetty_yclass_object_free(void *obj)",
  yetty_ycomplex2_image_create: "yetty_result_view yetty_ycomplex2_image_create(void *ctx)",
  yetty_ycomplex2_image_height_set: "yetty_result_view yetty_ycomplex2_image_height_set(void *obj, float value)",
  yetty_ycomplex2_image_width_set: "yetty_result_view yetty_ycomplex2_image_width_set(void *obj, float value)",
  yetty_ycomplex2_image_x_set: "yetty_result_view yetty_ycomplex2_image_x_set(void *obj, float value)",
  yetty_ycomplex2_image_y_set: "yetty_result_view yetty_ycomplex2_image_y_set(void *obj, float value)",
  yetty_ycomplex2_mesh_azimuth_set: "yetty_result_view yetty_ycomplex2_mesh_azimuth_set(void *obj, float value)",
  yetty_ycomplex2_mesh_create: "yetty_result_view yetty_ycomplex2_mesh_create(void *ctx)",
  yetty_ycomplex2_mesh_elevation_set: "yetty_result_view yetty_ycomplex2_mesh_elevation_set(void *obj, float value)",
  yetty_ycomplex2_mesh_height_set: "yetty_result_view yetty_ycomplex2_mesh_height_set(void *obj, float value)",
  yetty_ycomplex2_mesh_width_set: "yetty_result_view yetty_ycomplex2_mesh_width_set(void *obj, float value)",
  yetty_ycomplex2_mesh_wireframe_set: "yetty_result_view yetty_ycomplex2_mesh_wireframe_set(void *obj, uint32_t value)",
  yetty_ycomplex2_mesh_x_set: "yetty_result_view yetty_ycomplex2_mesh_x_set(void *obj, float value)",
  yetty_ycomplex2_mesh_y_set: "yetty_result_view yetty_ycomplex2_mesh_y_set(void *obj, float value)",
  yetty_ycomplex2_mesh_zoom_set: "yetty_result_view yetty_ycomplex2_mesh_zoom_set(void *obj, float value)",
  yetty_ycomplex2_set_glb: "yetty_result_view yetty_ycomplex2_set_glb(void *obj, const char *value)",
  yetty_ycomplex2_set_h264: "yetty_result_view yetty_ycomplex2_set_h264(void *obj, const char *value)",
  yetty_ycomplex2_set_path: "yetty_result_view yetty_ycomplex2_set_path(void *obj, const char *value)",
  yetty_ycomplex2_set_source: "yetty_result_view yetty_ycomplex2_set_source(void *obj, const char *value)",
  yetty_ycomplex2_set_wgsl_path: "yetty_result_view yetty_ycomplex2_set_wgsl_path(void *obj, const char *value)",
  yetty_ycomplex2_shadertoy_create: "yetty_result_view yetty_ycomplex2_shadertoy_create(void *ctx)",
  yetty_ycomplex2_shadertoy_height_set: "yetty_result_view yetty_ycomplex2_shadertoy_height_set(void *obj, float value)",
  yetty_ycomplex2_shadertoy_width_set: "yetty_result_view yetty_ycomplex2_shadertoy_width_set(void *obj, float value)",
  yetty_ycomplex2_shadertoy_x_set: "yetty_result_view yetty_ycomplex2_shadertoy_x_set(void *obj, float value)",
  yetty_ycomplex2_shadertoy_y_set: "yetty_result_view yetty_ycomplex2_shadertoy_y_set(void *obj, float value)",
  yetty_ycomplex2_video_create: "yetty_result_view yetty_ycomplex2_video_create(void *ctx)",
  yetty_ycomplex2_video_fps_set: "yetty_result_view yetty_ycomplex2_video_fps_set(void *obj, float value)",
  yetty_ycomplex2_video_height_set: "yetty_result_view yetty_ycomplex2_video_height_set(void *obj, float value)",
  yetty_ycomplex2_video_id_set: "yetty_result_view yetty_ycomplex2_video_id_set(void *obj, uint32_t value)",
  yetty_ycomplex2_video_video_h_set: "yetty_result_view yetty_ycomplex2_video_video_h_set(void *obj, uint32_t value)",
  yetty_ycomplex2_video_video_w_set: "yetty_result_view yetty_ycomplex2_video_video_w_set(void *obj, uint32_t value)",
  yetty_ycomplex2_video_width_set: "yetty_result_view yetty_ycomplex2_video_width_set(void *obj, float value)",
  yetty_ycomplex2_video_x_set: "yetty_result_view yetty_ycomplex2_video_x_set(void *obj, float value)",
  yetty_ycomplex2_video_y_set: "yetty_result_view yetty_ycomplex2_video_y_set(void *obj, float value)",
  yetty_ydrawlist2_add: "yetty_result_view yetty_ydrawlist2_add(void *obj, void *child)",
  yetty_ydrawlist2_dcs_emit: "yetty_result_view yetty_ydrawlist2_dcs_emit(void *obj)",
  yetty_ydrawlist2_destroy: "yetty_result_view yetty_ydrawlist2_destroy(void *obj)",
  yetty_ydrawlist2_drawable_create: "yetty_result_view yetty_ydrawlist2_drawable_create(void *ctx)",
  yetty_ydrawlist2_drawable_list_create: "yetty_result_view yetty_ydrawlist2_drawable_list_create(void *ctx)",
  yetty_ydrawlist2_font_create: "yetty_result_view yetty_ydrawlist2_font_create(void *ctx)",
  yetty_ydrawlist2_font_font_id_set: "yetty_result_view yetty_ydrawlist2_font_font_id_set(void *obj, int32_t value)",
  yetty_ydrawlist2_set_body: "yetty_result_view yetty_ydrawlist2_set_body(void *obj, const char *value)",
  yetty_ydrawlist2_set_color: "yetty_result_view yetty_ydrawlist2_set_color(void *obj, const char *value)",
  yetty_ydrawlist2_set_fill: "yetty_result_view yetty_ydrawlist2_set_fill(void *obj, const char *value)",
  yetty_ydrawlist2_set_name: "yetty_result_view yetty_ydrawlist2_set_name(void *obj, const char *value)",
  yetty_ydrawlist2_set_stroke: "yetty_result_view yetty_ydrawlist2_set_stroke(void *obj, const char *value)",
  yetty_ydrawlist2_shape_create: "yetty_result_view yetty_ydrawlist2_shape_create(void *ctx)",
  yetty_ydrawlist2_shape_id_set: "yetty_result_view yetty_ydrawlist2_shape_id_set(void *obj, uint32_t value)",
  yetty_ydrawlist2_shape_stroke_width_set: "yetty_result_view yetty_ydrawlist2_shape_stroke_width_set(void *obj, float value)",
  yetty_ydrawlist2_shape_z_set: "yetty_result_view yetty_ydrawlist2_shape_z_set(void *obj, uint32_t value)",
  yetty_ydrawlist2_text_create: "yetty_result_view yetty_ydrawlist2_text_create(void *ctx)",
  yetty_ydrawlist2_text_font_id_set: "yetty_result_view yetty_ydrawlist2_text_font_id_set(void *obj, int32_t value)",
  yetty_ydrawlist2_text_font_size_set: "yetty_result_view yetty_ydrawlist2_text_font_size_set(void *obj, float value)",
  yetty_ydrawlist2_text_layer_set: "yetty_result_view yetty_ydrawlist2_text_layer_set(void *obj, uint32_t value)",
  yetty_ydrawlist2_text_rotation_set: "yetty_result_view yetty_ydrawlist2_text_rotation_set(void *obj, float value)",
  yetty_ydrawlist2_text_x_set: "yetty_result_view yetty_ydrawlist2_text_x_set(void *obj, float value)",
  yetty_ydrawlist2_text_y_set: "yetty_result_view yetty_ydrawlist2_text_y_set(void *obj, float value)",
  yetty_ysdf2_arc_aperture_x_set: "yetty_result_view yetty_ysdf2_arc_aperture_x_set(void *obj, float value)",
  yetty_ysdf2_arc_aperture_y_set: "yetty_result_view yetty_ysdf2_arc_aperture_y_set(void *obj, float value)",
  yetty_ysdf2_arc_center_x_set: "yetty_result_view yetty_ysdf2_arc_center_x_set(void *obj, float value)",
  yetty_ysdf2_arc_center_y_set: "yetty_result_view yetty_ysdf2_arc_center_y_set(void *obj, float value)",
  yetty_ysdf2_arc_create: "yetty_result_view yetty_ysdf2_arc_create(void *ctx)",
  yetty_ysdf2_arc_radius_set: "yetty_result_view yetty_ysdf2_arc_radius_set(void *obj, float value)",
  yetty_ysdf2_arc_thickness_set: "yetty_result_view yetty_ysdf2_arc_thickness_set(void *obj, float value)",
  yetty_ysdf2_box_3d_create: "yetty_result_view yetty_ysdf2_box_3d_create(void *ctx)",
  yetty_ysdf2_box_3d_half_size_x_set: "yetty_result_view yetty_ysdf2_box_3d_half_size_x_set(void *obj, float value)",
  yetty_ysdf2_box_3d_half_size_y_set: "yetty_result_view yetty_ysdf2_box_3d_half_size_y_set(void *obj, float value)",
  yetty_ysdf2_box_3d_half_size_z_set: "yetty_result_view yetty_ysdf2_box_3d_half_size_z_set(void *obj, float value)",
  yetty_ysdf2_box_3d_position_x_set: "yetty_result_view yetty_ysdf2_box_3d_position_x_set(void *obj, float value)",
  yetty_ysdf2_box_3d_position_y_set: "yetty_result_view yetty_ysdf2_box_3d_position_y_set(void *obj, float value)",
  yetty_ysdf2_box_3d_position_z_set: "yetty_result_view yetty_ysdf2_box_3d_position_z_set(void *obj, float value)",
  yetty_ysdf2_box_center_x_set: "yetty_result_view yetty_ysdf2_box_center_x_set(void *obj, float value)",
  yetty_ysdf2_box_center_y_set: "yetty_result_view yetty_ysdf2_box_center_y_set(void *obj, float value)",
  yetty_ysdf2_box_corner_radius_set: "yetty_result_view yetty_ysdf2_box_corner_radius_set(void *obj, float value)",
  yetty_ysdf2_box_create: "yetty_result_view yetty_ysdf2_box_create(void *ctx)",
  yetty_ysdf2_box_half_height_set: "yetty_result_view yetty_ysdf2_box_half_height_set(void *obj, float value)",
  yetty_ysdf2_box_half_width_set: "yetty_result_view yetty_ysdf2_box_half_width_set(void *obj, float value)",
  yetty_ysdf2_capsule_create: "yetty_result_view yetty_ysdf2_capsule_create(void *ctx)",
  yetty_ysdf2_capsule_end_x_set: "yetty_result_view yetty_ysdf2_capsule_end_x_set(void *obj, float value)",
  yetty_ysdf2_capsule_end_y_set: "yetty_result_view yetty_ysdf2_capsule_end_y_set(void *obj, float value)",
  yetty_ysdf2_capsule_radius_set: "yetty_result_view yetty_ysdf2_capsule_radius_set(void *obj, float value)",
  yetty_ysdf2_capsule_start_x_set: "yetty_result_view yetty_ysdf2_capsule_start_x_set(void *obj, float value)",
  yetty_ysdf2_capsule_start_y_set: "yetty_result_view yetty_ysdf2_capsule_start_y_set(void *obj, float value)",
  yetty_ysdf2_circle_center_x_set: "yetty_result_view yetty_ysdf2_circle_center_x_set(void *obj, float value)",
  yetty_ysdf2_circle_center_y_set: "yetty_result_view yetty_ysdf2_circle_center_y_set(void *obj, float value)",
  yetty_ysdf2_circle_create: "yetty_result_view yetty_ysdf2_circle_create(void *ctx)",
  yetty_ysdf2_circle_radius_set: "yetty_result_view yetty_ysdf2_circle_radius_set(void *obj, float value)",
  yetty_ysdf2_cross_center_x_set: "yetty_result_view yetty_ysdf2_cross_center_x_set(void *obj, float value)",
  yetty_ysdf2_cross_center_y_set: "yetty_result_view yetty_ysdf2_cross_center_y_set(void *obj, float value)",
  yetty_ysdf2_cross_corner_radius_set: "yetty_result_view yetty_ysdf2_cross_corner_radius_set(void *obj, float value)",
  yetty_ysdf2_cross_create: "yetty_result_view yetty_ysdf2_cross_create(void *ctx)",
  yetty_ysdf2_cross_half_height_set: "yetty_result_view yetty_ysdf2_cross_half_height_set(void *obj, float value)",
  yetty_ysdf2_cross_half_width_set: "yetty_result_view yetty_ysdf2_cross_half_width_set(void *obj, float value)",
  yetty_ysdf2_cylinder_3d_create: "yetty_result_view yetty_ysdf2_cylinder_3d_create(void *ctx)",
  yetty_ysdf2_cylinder_3d_half_height_set: "yetty_result_view yetty_ysdf2_cylinder_3d_half_height_set(void *obj, float value)",
  yetty_ysdf2_cylinder_3d_position_x_set: "yetty_result_view yetty_ysdf2_cylinder_3d_position_x_set(void *obj, float value)",
  yetty_ysdf2_cylinder_3d_position_y_set: "yetty_result_view yetty_ysdf2_cylinder_3d_position_y_set(void *obj, float value)",
  yetty_ysdf2_cylinder_3d_position_z_set: "yetty_result_view yetty_ysdf2_cylinder_3d_position_z_set(void *obj, float value)",
  yetty_ysdf2_cylinder_3d_radius_set: "yetty_result_view yetty_ysdf2_cylinder_3d_radius_set(void *obj, float value)",
  yetty_ysdf2_egg_center_x_set: "yetty_result_view yetty_ysdf2_egg_center_x_set(void *obj, float value)",
  yetty_ysdf2_egg_center_y_set: "yetty_result_view yetty_ysdf2_egg_center_y_set(void *obj, float value)",
  yetty_ysdf2_egg_create: "yetty_result_view yetty_ysdf2_egg_create(void *ctx)",
  yetty_ysdf2_egg_radius_inner_set: "yetty_result_view yetty_ysdf2_egg_radius_inner_set(void *obj, float value)",
  yetty_ysdf2_egg_radius_outer_set: "yetty_result_view yetty_ysdf2_egg_radius_outer_set(void *obj, float value)",
  yetty_ysdf2_ellipse_center_x_set: "yetty_result_view yetty_ysdf2_ellipse_center_x_set(void *obj, float value)",
  yetty_ysdf2_ellipse_center_y_set: "yetty_result_view yetty_ysdf2_ellipse_center_y_set(void *obj, float value)",
  yetty_ysdf2_ellipse_create: "yetty_result_view yetty_ysdf2_ellipse_create(void *ctx)",
  yetty_ysdf2_ellipse_radius_x_set: "yetty_result_view yetty_ysdf2_ellipse_radius_x_set(void *obj, float value)",
  yetty_ysdf2_ellipse_radius_y_set: "yetty_result_view yetty_ysdf2_ellipse_radius_y_set(void *obj, float value)",
  yetty_ysdf2_heart_center_x_set: "yetty_result_view yetty_ysdf2_heart_center_x_set(void *obj, float value)",
  yetty_ysdf2_heart_center_y_set: "yetty_result_view yetty_ysdf2_heart_center_y_set(void *obj, float value)",
  yetty_ysdf2_heart_create: "yetty_result_view yetty_ysdf2_heart_create(void *ctx)",
  yetty_ysdf2_heart_scale_set: "yetty_result_view yetty_ysdf2_heart_scale_set(void *obj, float value)",
  yetty_ysdf2_hexagon_center_x_set: "yetty_result_view yetty_ysdf2_hexagon_center_x_set(void *obj, float value)",
  yetty_ysdf2_hexagon_center_y_set: "yetty_result_view yetty_ysdf2_hexagon_center_y_set(void *obj, float value)",
  yetty_ysdf2_hexagon_create: "yetty_result_view yetty_ysdf2_hexagon_create(void *ctx)",
  yetty_ysdf2_hexagon_radius_set: "yetty_result_view yetty_ysdf2_hexagon_radius_set(void *obj, float value)",
  yetty_ysdf2_hexagram_center_x_set: "yetty_result_view yetty_ysdf2_hexagram_center_x_set(void *obj, float value)",
  yetty_ysdf2_hexagram_center_y_set: "yetty_result_view yetty_ysdf2_hexagram_center_y_set(void *obj, float value)",
  yetty_ysdf2_hexagram_create: "yetty_result_view yetty_ysdf2_hexagram_create(void *ctx)",
  yetty_ysdf2_hexagram_radius_set: "yetty_result_view yetty_ysdf2_hexagram_radius_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_center_x_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_center_x_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_center_y_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_center_y_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_color0_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_color0_set(void *obj, uint32_t value)",
  yetty_ysdf2_linear_gradient_box_color1_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_color1_set(void *obj, uint32_t value)",
  yetty_ysdf2_linear_gradient_box_corner_radius_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_corner_radius_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_create: "yetty_result_view yetty_ysdf2_linear_gradient_box_create(void *ctx)",
  yetty_ysdf2_linear_gradient_box_grad_x0_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_grad_x0_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_grad_x1_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_grad_x1_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_grad_y0_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_grad_y0_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_grad_y1_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_grad_y1_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_half_height_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_half_height_set(void *obj, float value)",
  yetty_ysdf2_linear_gradient_box_half_width_set: "yetty_result_view yetty_ysdf2_linear_gradient_box_half_width_set(void *obj, float value)",
  yetty_ysdf2_moon_center_x_set: "yetty_result_view yetty_ysdf2_moon_center_x_set(void *obj, float value)",
  yetty_ysdf2_moon_center_y_set: "yetty_result_view yetty_ysdf2_moon_center_y_set(void *obj, float value)",
  yetty_ysdf2_moon_create: "yetty_result_view yetty_ysdf2_moon_create(void *ctx)",
  yetty_ysdf2_moon_offset_set: "yetty_result_view yetty_ysdf2_moon_offset_set(void *obj, float value)",
  yetty_ysdf2_moon_radius_inner_set: "yetty_result_view yetty_ysdf2_moon_radius_inner_set(void *obj, float value)",
  yetty_ysdf2_moon_radius_outer_set: "yetty_result_view yetty_ysdf2_moon_radius_outer_set(void *obj, float value)",
  yetty_ysdf2_octogon_center_x_set: "yetty_result_view yetty_ysdf2_octogon_center_x_set(void *obj, float value)",
  yetty_ysdf2_octogon_center_y_set: "yetty_result_view yetty_ysdf2_octogon_center_y_set(void *obj, float value)",
  yetty_ysdf2_octogon_create: "yetty_result_view yetty_ysdf2_octogon_create(void *ctx)",
  yetty_ysdf2_octogon_radius_set: "yetty_result_view yetty_ysdf2_octogon_radius_set(void *obj, float value)",
  yetty_ysdf2_pentagon_center_x_set: "yetty_result_view yetty_ysdf2_pentagon_center_x_set(void *obj, float value)",
  yetty_ysdf2_pentagon_center_y_set: "yetty_result_view yetty_ysdf2_pentagon_center_y_set(void *obj, float value)",
  yetty_ysdf2_pentagon_create: "yetty_result_view yetty_ysdf2_pentagon_create(void *ctx)",
  yetty_ysdf2_pentagon_radius_set: "yetty_result_view yetty_ysdf2_pentagon_radius_set(void *obj, float value)",
  yetty_ysdf2_pentagram_center_x_set: "yetty_result_view yetty_ysdf2_pentagram_center_x_set(void *obj, float value)",
  yetty_ysdf2_pentagram_center_y_set: "yetty_result_view yetty_ysdf2_pentagram_center_y_set(void *obj, float value)",
  yetty_ysdf2_pentagram_create: "yetty_result_view yetty_ysdf2_pentagram_create(void *ctx)",
  yetty_ysdf2_pentagram_radius_set: "yetty_result_view yetty_ysdf2_pentagram_radius_set(void *obj, float value)",
  yetty_ysdf2_pie_aperture_x_set: "yetty_result_view yetty_ysdf2_pie_aperture_x_set(void *obj, float value)",
  yetty_ysdf2_pie_aperture_y_set: "yetty_result_view yetty_ysdf2_pie_aperture_y_set(void *obj, float value)",
  yetty_ysdf2_pie_center_x_set: "yetty_result_view yetty_ysdf2_pie_center_x_set(void *obj, float value)",
  yetty_ysdf2_pie_center_y_set: "yetty_result_view yetty_ysdf2_pie_center_y_set(void *obj, float value)",
  yetty_ysdf2_pie_create: "yetty_result_view yetty_ysdf2_pie_create(void *ctx)",
  yetty_ysdf2_pie_radius_set: "yetty_result_view yetty_ysdf2_pie_radius_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_center_x_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_center_x_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_center_y_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_center_y_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_color_inner_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_color_inner_set(void *obj, uint32_t value)",
  yetty_ysdf2_radial_gradient_box_color_outer_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_color_outer_set(void *obj, uint32_t value)",
  yetty_ysdf2_radial_gradient_box_corner_radius_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_corner_radius_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_create: "yetty_result_view yetty_ysdf2_radial_gradient_box_create(void *ctx)",
  yetty_ysdf2_radial_gradient_box_grad_cx_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_grad_cx_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_grad_cy_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_grad_cy_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_grad_radius_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_grad_radius_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_half_height_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_half_height_set(void *obj, float value)",
  yetty_ysdf2_radial_gradient_box_half_width_set: "yetty_result_view yetty_ysdf2_radial_gradient_box_half_width_set(void *obj, float value)",
  yetty_ysdf2_rhombus_center_x_set: "yetty_result_view yetty_ysdf2_rhombus_center_x_set(void *obj, float value)",
  yetty_ysdf2_rhombus_center_y_set: "yetty_result_view yetty_ysdf2_rhombus_center_y_set(void *obj, float value)",
  yetty_ysdf2_rhombus_create: "yetty_result_view yetty_ysdf2_rhombus_create(void *ctx)",
  yetty_ysdf2_rhombus_half_height_set: "yetty_result_view yetty_ysdf2_rhombus_half_height_set(void *obj, float value)",
  yetty_ysdf2_rhombus_half_width_set: "yetty_result_view yetty_ysdf2_rhombus_half_width_set(void *obj, float value)",
  yetty_ysdf2_ring_center_x_set: "yetty_result_view yetty_ysdf2_ring_center_x_set(void *obj, float value)",
  yetty_ysdf2_ring_center_y_set: "yetty_result_view yetty_ysdf2_ring_center_y_set(void *obj, float value)",
  yetty_ysdf2_ring_create: "yetty_result_view yetty_ysdf2_ring_create(void *ctx)",
  yetty_ysdf2_ring_normal_x_set: "yetty_result_view yetty_ysdf2_ring_normal_x_set(void *obj, float value)",
  yetty_ysdf2_ring_normal_y_set: "yetty_result_view yetty_ysdf2_ring_normal_y_set(void *obj, float value)",
  yetty_ysdf2_ring_radius_set: "yetty_result_view yetty_ysdf2_ring_radius_set(void *obj, float value)",
  yetty_ysdf2_ring_thickness_set: "yetty_result_view yetty_ysdf2_ring_thickness_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_center_x_set: "yetty_result_view yetty_ysdf2_rounded_box_center_x_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_center_y_set: "yetty_result_view yetty_ysdf2_rounded_box_center_y_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_create: "yetty_result_view yetty_ysdf2_rounded_box_create(void *ctx)",
  yetty_ysdf2_rounded_box_half_height_set: "yetty_result_view yetty_ysdf2_rounded_box_half_height_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_half_width_set: "yetty_result_view yetty_ysdf2_rounded_box_half_width_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_radius_bottom_left_set: "yetty_result_view yetty_ysdf2_rounded_box_radius_bottom_left_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_radius_bottom_right_set: "yetty_result_view yetty_ysdf2_rounded_box_radius_bottom_right_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_radius_top_left_set: "yetty_result_view yetty_ysdf2_rounded_box_radius_top_left_set(void *obj, float value)",
  yetty_ysdf2_rounded_box_radius_top_right_set: "yetty_result_view yetty_ysdf2_rounded_box_radius_top_right_set(void *obj, float value)",
  yetty_ysdf2_rounded_x_center_x_set: "yetty_result_view yetty_ysdf2_rounded_x_center_x_set(void *obj, float value)",
  yetty_ysdf2_rounded_x_center_y_set: "yetty_result_view yetty_ysdf2_rounded_x_center_y_set(void *obj, float value)",
  yetty_ysdf2_rounded_x_create: "yetty_result_view yetty_ysdf2_rounded_x_create(void *ctx)",
  yetty_ysdf2_rounded_x_radius_set: "yetty_result_view yetty_ysdf2_rounded_x_radius_set(void *obj, float value)",
  yetty_ysdf2_rounded_x_width_set: "yetty_result_view yetty_ysdf2_rounded_x_width_set(void *obj, float value)",
  yetty_ysdf2_segment_create: "yetty_result_view yetty_ysdf2_segment_create(void *ctx)",
  yetty_ysdf2_segment_end_x_set: "yetty_result_view yetty_ysdf2_segment_end_x_set(void *obj, float value)",
  yetty_ysdf2_segment_end_y_set: "yetty_result_view yetty_ysdf2_segment_end_y_set(void *obj, float value)",
  yetty_ysdf2_segment_start_x_set: "yetty_result_view yetty_ysdf2_segment_start_x_set(void *obj, float value)",
  yetty_ysdf2_segment_start_y_set: "yetty_result_view yetty_ysdf2_segment_start_y_set(void *obj, float value)",
  yetty_ysdf2_sphere_3d_create: "yetty_result_view yetty_ysdf2_sphere_3d_create(void *ctx)",
  yetty_ysdf2_sphere_3d_position_x_set: "yetty_result_view yetty_ysdf2_sphere_3d_position_x_set(void *obj, float value)",
  yetty_ysdf2_sphere_3d_position_y_set: "yetty_result_view yetty_ysdf2_sphere_3d_position_y_set(void *obj, float value)",
  yetty_ysdf2_sphere_3d_position_z_set: "yetty_result_view yetty_ysdf2_sphere_3d_position_z_set(void *obj, float value)",
  yetty_ysdf2_sphere_3d_radius_set: "yetty_result_view yetty_ysdf2_sphere_3d_radius_set(void *obj, float value)",
  yetty_ysdf2_star_center_x_set: "yetty_result_view yetty_ysdf2_star_center_x_set(void *obj, float value)",
  yetty_ysdf2_star_center_y_set: "yetty_result_view yetty_ysdf2_star_center_y_set(void *obj, float value)",
  yetty_ysdf2_star_create: "yetty_result_view yetty_ysdf2_star_create(void *ctx)",
  yetty_ysdf2_star_inner_ratio_set: "yetty_result_view yetty_ysdf2_star_inner_ratio_set(void *obj, float value)",
  yetty_ysdf2_star_num_points_set: "yetty_result_view yetty_ysdf2_star_num_points_set(void *obj, float value)",
  yetty_ysdf2_star_radius_set: "yetty_result_view yetty_ysdf2_star_radius_set(void *obj, float value)",
  yetty_ysdf2_torus_3d_create: "yetty_result_view yetty_ysdf2_torus_3d_create(void *ctx)",
  yetty_ysdf2_torus_3d_major_radius_set: "yetty_result_view yetty_ysdf2_torus_3d_major_radius_set(void *obj, float value)",
  yetty_ysdf2_torus_3d_minor_radius_set: "yetty_result_view yetty_ysdf2_torus_3d_minor_radius_set(void *obj, float value)",
  yetty_ysdf2_torus_3d_position_x_set: "yetty_result_view yetty_ysdf2_torus_3d_position_x_set(void *obj, float value)",
  yetty_ysdf2_torus_3d_position_y_set: "yetty_result_view yetty_ysdf2_torus_3d_position_y_set(void *obj, float value)",
  yetty_ysdf2_torus_3d_position_z_set: "yetty_result_view yetty_ysdf2_torus_3d_position_z_set(void *obj, float value)",
  yetty_ysdf2_triangle_create: "yetty_result_view yetty_ysdf2_triangle_create(void *ctx)",
  yetty_ysdf2_triangle_vertex_a_x_set: "yetty_result_view yetty_ysdf2_triangle_vertex_a_x_set(void *obj, float value)",
  yetty_ysdf2_triangle_vertex_a_y_set: "yetty_result_view yetty_ysdf2_triangle_vertex_a_y_set(void *obj, float value)",
  yetty_ysdf2_triangle_vertex_b_x_set: "yetty_result_view yetty_ysdf2_triangle_vertex_b_x_set(void *obj, float value)",
  yetty_ysdf2_triangle_vertex_b_y_set: "yetty_result_view yetty_ysdf2_triangle_vertex_b_y_set(void *obj, float value)",
  yetty_ysdf2_triangle_vertex_c_x_set: "yetty_result_view yetty_ysdf2_triangle_vertex_c_x_set(void *obj, float value)",
  yetty_ysdf2_triangle_vertex_c_y_set: "yetty_result_view yetty_ysdf2_triangle_vertex_c_y_set(void *obj, float value)",
});

// The abstract drawable base — a marker class; every concrete
// class below is a Drawable in the draw-list sense.
export class Drawable {
  constructor() {
    this.handle = null;
  }
}

const FONT_SPEC = {
  className: "Font",
  create: "yetty_ydrawlist2_font_create",
  primary: null,
  destroy: null,
  fields: {
    name: { kind: "cstr", sym: "yetty_ydrawlist2_set_name" },
    fontId: { kind: "scalar", sym: "yetty_ydrawlist2_font_font_id_set" },
  },
};

export class Font extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(FONT_SPEC, primaryOrOptions, options);
    runtime.adopt(this, FONT_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, FONT_SPEC);
  }
}

const TEXT_SPEC = {
  className: "Text",
  create: "yetty_ydrawlist2_text_create",
  primary: "yetty_ydrawlist2_set_body",
  destroy: null,
  fields: {
    body: { kind: "cstr", sym: "yetty_ydrawlist2_set_body" },
    color: { kind: "cstr", sym: "yetty_ydrawlist2_set_color" },
    x: { kind: "scalar", sym: "yetty_ydrawlist2_text_x_set" },
    y: { kind: "scalar", sym: "yetty_ydrawlist2_text_y_set" },
    fontSize: { kind: "scalar", sym: "yetty_ydrawlist2_text_font_size_set" },
    layer: { kind: "scalar", sym: "yetty_ydrawlist2_text_layer_set" },
    fontId: { kind: "scalar", sym: "yetty_ydrawlist2_text_font_id_set" },
    rotation: { kind: "scalar", sym: "yetty_ydrawlist2_text_rotation_set" },
  },
};

export class Text extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(TEXT_SPEC, primaryOrOptions, options);
    runtime.adopt(this, TEXT_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, TEXT_SPEC);
  }
}

const SHAPE_SPEC = {
  className: "Shape",
  create: "yetty_ydrawlist2_shape_create",
  primary: null,
  destroy: null,
  fields: {
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Shape extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(SHAPE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, SHAPE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, SHAPE_SPEC);
  }
}

const CIRCLE_SPEC = {
  className: "Circle",
  create: "yetty_ysdf2_circle_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_circle_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_circle_center_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_circle_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Circle extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(CIRCLE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, CIRCLE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, CIRCLE_SPEC);
  }
}

const BOX_SPEC = {
  className: "Box",
  create: "yetty_ysdf2_box_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_box_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_box_center_y_set" },
    halfWidth: { kind: "scalar", sym: "yetty_ysdf2_box_half_width_set" },
    halfHeight: { kind: "scalar", sym: "yetty_ysdf2_box_half_height_set" },
    cornerRadius: { kind: "scalar", sym: "yetty_ysdf2_box_corner_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Box extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(BOX_SPEC, primaryOrOptions, options);
    runtime.adopt(this, BOX_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, BOX_SPEC);
  }
}

const SEGMENT_SPEC = {
  className: "Segment",
  create: "yetty_ysdf2_segment_create",
  primary: null,
  destroy: null,
  fields: {
    startX: { kind: "scalar", sym: "yetty_ysdf2_segment_start_x_set" },
    startY: { kind: "scalar", sym: "yetty_ysdf2_segment_start_y_set" },
    endX: { kind: "scalar", sym: "yetty_ysdf2_segment_end_x_set" },
    endY: { kind: "scalar", sym: "yetty_ysdf2_segment_end_y_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Segment extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(SEGMENT_SPEC, primaryOrOptions, options);
    runtime.adopt(this, SEGMENT_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, SEGMENT_SPEC);
  }
}

const TRIANGLE_SPEC = {
  className: "Triangle",
  create: "yetty_ysdf2_triangle_create",
  primary: null,
  destroy: null,
  fields: {
    vertexAX: { kind: "scalar", sym: "yetty_ysdf2_triangle_vertex_a_x_set" },
    vertexAY: { kind: "scalar", sym: "yetty_ysdf2_triangle_vertex_a_y_set" },
    vertexBX: { kind: "scalar", sym: "yetty_ysdf2_triangle_vertex_b_x_set" },
    vertexBY: { kind: "scalar", sym: "yetty_ysdf2_triangle_vertex_b_y_set" },
    vertexCX: { kind: "scalar", sym: "yetty_ysdf2_triangle_vertex_c_x_set" },
    vertexCY: { kind: "scalar", sym: "yetty_ysdf2_triangle_vertex_c_y_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Triangle extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(TRIANGLE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, TRIANGLE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, TRIANGLE_SPEC);
  }
}

const ELLIPSE_SPEC = {
  className: "Ellipse",
  create: "yetty_ysdf2_ellipse_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_ellipse_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_ellipse_center_y_set" },
    radiusX: { kind: "scalar", sym: "yetty_ysdf2_ellipse_radius_x_set" },
    radiusY: { kind: "scalar", sym: "yetty_ysdf2_ellipse_radius_y_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Ellipse extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(ELLIPSE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, ELLIPSE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, ELLIPSE_SPEC);
  }
}

const ARC_SPEC = {
  className: "Arc",
  create: "yetty_ysdf2_arc_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_arc_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_arc_center_y_set" },
    apertureX: { kind: "scalar", sym: "yetty_ysdf2_arc_aperture_x_set" },
    apertureY: { kind: "scalar", sym: "yetty_ysdf2_arc_aperture_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_arc_radius_set" },
    thickness: { kind: "scalar", sym: "yetty_ysdf2_arc_thickness_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Arc extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(ARC_SPEC, primaryOrOptions, options);
    runtime.adopt(this, ARC_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, ARC_SPEC);
  }
}

const ROUNDED_BOX_SPEC = {
  className: "RoundedBox",
  create: "yetty_ysdf2_rounded_box_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_center_y_set" },
    halfWidth: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_half_width_set" },
    halfHeight: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_half_height_set" },
    radiusTopRight: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_radius_top_right_set" },
    radiusBottomRight: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_radius_bottom_right_set" },
    radiusTopLeft: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_radius_top_left_set" },
    radiusBottomLeft: { kind: "scalar", sym: "yetty_ysdf2_rounded_box_radius_bottom_left_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class RoundedBox extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(ROUNDED_BOX_SPEC, primaryOrOptions, options);
    runtime.adopt(this, ROUNDED_BOX_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, ROUNDED_BOX_SPEC);
  }
}

const RHOMBUS_SPEC = {
  className: "Rhombus",
  create: "yetty_ysdf2_rhombus_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_rhombus_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_rhombus_center_y_set" },
    halfWidth: { kind: "scalar", sym: "yetty_ysdf2_rhombus_half_width_set" },
    halfHeight: { kind: "scalar", sym: "yetty_ysdf2_rhombus_half_height_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Rhombus extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(RHOMBUS_SPEC, primaryOrOptions, options);
    runtime.adopt(this, RHOMBUS_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, RHOMBUS_SPEC);
  }
}

const PENTAGON_SPEC = {
  className: "Pentagon",
  create: "yetty_ysdf2_pentagon_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_pentagon_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_pentagon_center_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_pentagon_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Pentagon extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(PENTAGON_SPEC, primaryOrOptions, options);
    runtime.adopt(this, PENTAGON_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, PENTAGON_SPEC);
  }
}

const HEXAGON_SPEC = {
  className: "Hexagon",
  create: "yetty_ysdf2_hexagon_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_hexagon_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_hexagon_center_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_hexagon_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Hexagon extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(HEXAGON_SPEC, primaryOrOptions, options);
    runtime.adopt(this, HEXAGON_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, HEXAGON_SPEC);
  }
}

const STAR_SPEC = {
  className: "Star",
  create: "yetty_ysdf2_star_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_star_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_star_center_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_star_radius_set" },
    numPoints: { kind: "scalar", sym: "yetty_ysdf2_star_num_points_set" },
    innerRatio: { kind: "scalar", sym: "yetty_ysdf2_star_inner_ratio_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Star extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(STAR_SPEC, primaryOrOptions, options);
    runtime.adopt(this, STAR_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, STAR_SPEC);
  }
}

const PIE_SPEC = {
  className: "Pie",
  create: "yetty_ysdf2_pie_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_pie_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_pie_center_y_set" },
    apertureX: { kind: "scalar", sym: "yetty_ysdf2_pie_aperture_x_set" },
    apertureY: { kind: "scalar", sym: "yetty_ysdf2_pie_aperture_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_pie_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Pie extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(PIE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, PIE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, PIE_SPEC);
  }
}

const RING_SPEC = {
  className: "Ring",
  create: "yetty_ysdf2_ring_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_ring_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_ring_center_y_set" },
    normalX: { kind: "scalar", sym: "yetty_ysdf2_ring_normal_x_set" },
    normalY: { kind: "scalar", sym: "yetty_ysdf2_ring_normal_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_ring_radius_set" },
    thickness: { kind: "scalar", sym: "yetty_ysdf2_ring_thickness_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Ring extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(RING_SPEC, primaryOrOptions, options);
    runtime.adopt(this, RING_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, RING_SPEC);
  }
}

const HEART_SPEC = {
  className: "Heart",
  create: "yetty_ysdf2_heart_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_heart_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_heart_center_y_set" },
    scale: { kind: "scalar", sym: "yetty_ysdf2_heart_scale_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Heart extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(HEART_SPEC, primaryOrOptions, options);
    runtime.adopt(this, HEART_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, HEART_SPEC);
  }
}

const CROSS_SPEC = {
  className: "Cross",
  create: "yetty_ysdf2_cross_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_cross_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_cross_center_y_set" },
    halfWidth: { kind: "scalar", sym: "yetty_ysdf2_cross_half_width_set" },
    halfHeight: { kind: "scalar", sym: "yetty_ysdf2_cross_half_height_set" },
    cornerRadius: { kind: "scalar", sym: "yetty_ysdf2_cross_corner_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Cross extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(CROSS_SPEC, primaryOrOptions, options);
    runtime.adopt(this, CROSS_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, CROSS_SPEC);
  }
}

const ROUNDED_X_SPEC = {
  className: "RoundedX",
  create: "yetty_ysdf2_rounded_x_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_rounded_x_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_rounded_x_center_y_set" },
    width: { kind: "scalar", sym: "yetty_ysdf2_rounded_x_width_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_rounded_x_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class RoundedX extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(ROUNDED_X_SPEC, primaryOrOptions, options);
    runtime.adopt(this, ROUNDED_X_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, ROUNDED_X_SPEC);
  }
}

const CAPSULE_SPEC = {
  className: "Capsule",
  create: "yetty_ysdf2_capsule_create",
  primary: null,
  destroy: null,
  fields: {
    startX: { kind: "scalar", sym: "yetty_ysdf2_capsule_start_x_set" },
    startY: { kind: "scalar", sym: "yetty_ysdf2_capsule_start_y_set" },
    endX: { kind: "scalar", sym: "yetty_ysdf2_capsule_end_x_set" },
    endY: { kind: "scalar", sym: "yetty_ysdf2_capsule_end_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_capsule_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Capsule extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(CAPSULE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, CAPSULE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, CAPSULE_SPEC);
  }
}

const MOON_SPEC = {
  className: "Moon",
  create: "yetty_ysdf2_moon_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_moon_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_moon_center_y_set" },
    offset: { kind: "scalar", sym: "yetty_ysdf2_moon_offset_set" },
    radiusOuter: { kind: "scalar", sym: "yetty_ysdf2_moon_radius_outer_set" },
    radiusInner: { kind: "scalar", sym: "yetty_ysdf2_moon_radius_inner_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Moon extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(MOON_SPEC, primaryOrOptions, options);
    runtime.adopt(this, MOON_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, MOON_SPEC);
  }
}

const EGG_SPEC = {
  className: "Egg",
  create: "yetty_ysdf2_egg_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_egg_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_egg_center_y_set" },
    radiusOuter: { kind: "scalar", sym: "yetty_ysdf2_egg_radius_outer_set" },
    radiusInner: { kind: "scalar", sym: "yetty_ysdf2_egg_radius_inner_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Egg extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(EGG_SPEC, primaryOrOptions, options);
    runtime.adopt(this, EGG_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, EGG_SPEC);
  }
}

const OCTOGON_SPEC = {
  className: "Octogon",
  create: "yetty_ysdf2_octogon_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_octogon_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_octogon_center_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_octogon_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Octogon extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(OCTOGON_SPEC, primaryOrOptions, options);
    runtime.adopt(this, OCTOGON_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, OCTOGON_SPEC);
  }
}

const HEXAGRAM_SPEC = {
  className: "Hexagram",
  create: "yetty_ysdf2_hexagram_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_hexagram_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_hexagram_center_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_hexagram_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Hexagram extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(HEXAGRAM_SPEC, primaryOrOptions, options);
    runtime.adopt(this, HEXAGRAM_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, HEXAGRAM_SPEC);
  }
}

const PENTAGRAM_SPEC = {
  className: "Pentagram",
  create: "yetty_ysdf2_pentagram_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_pentagram_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_pentagram_center_y_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_pentagram_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Pentagram extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(PENTAGRAM_SPEC, primaryOrOptions, options);
    runtime.adopt(this, PENTAGRAM_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, PENTAGRAM_SPEC);
  }
}

const LINEAR_GRADIENT_BOX_SPEC = {
  className: "LinearGradientBox",
  create: "yetty_ysdf2_linear_gradient_box_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_center_y_set" },
    halfWidth: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_half_width_set" },
    halfHeight: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_half_height_set" },
    cornerRadius: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_corner_radius_set" },
    gradX0: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_grad_x0_set" },
    gradY0: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_grad_y0_set" },
    gradX1: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_grad_x1_set" },
    gradY1: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_grad_y1_set" },
    color0: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_color0_set" },
    color1: { kind: "scalar", sym: "yetty_ysdf2_linear_gradient_box_color1_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class LinearGradientBox extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(LINEAR_GRADIENT_BOX_SPEC, primaryOrOptions, options);
    runtime.adopt(this, LINEAR_GRADIENT_BOX_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, LINEAR_GRADIENT_BOX_SPEC);
  }
}

const RADIAL_GRADIENT_BOX_SPEC = {
  className: "RadialGradientBox",
  create: "yetty_ysdf2_radial_gradient_box_create",
  primary: null,
  destroy: null,
  fields: {
    centerX: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_center_x_set" },
    centerY: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_center_y_set" },
    halfWidth: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_half_width_set" },
    halfHeight: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_half_height_set" },
    cornerRadius: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_corner_radius_set" },
    gradCx: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_grad_cx_set" },
    gradCy: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_grad_cy_set" },
    gradRadius: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_grad_radius_set" },
    colorInner: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_color_inner_set" },
    colorOuter: { kind: "scalar", sym: "yetty_ysdf2_radial_gradient_box_color_outer_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class RadialGradientBox extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(RADIAL_GRADIENT_BOX_SPEC, primaryOrOptions, options);
    runtime.adopt(this, RADIAL_GRADIENT_BOX_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, RADIAL_GRADIENT_BOX_SPEC);
  }
}

const SPHERE_3D_SPEC = {
  className: "Sphere3d",
  create: "yetty_ysdf2_sphere_3d_create",
  primary: null,
  destroy: null,
  fields: {
    positionX: { kind: "scalar", sym: "yetty_ysdf2_sphere_3d_position_x_set" },
    positionY: { kind: "scalar", sym: "yetty_ysdf2_sphere_3d_position_y_set" },
    positionZ: { kind: "scalar", sym: "yetty_ysdf2_sphere_3d_position_z_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_sphere_3d_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Sphere3d extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(SPHERE_3D_SPEC, primaryOrOptions, options);
    runtime.adopt(this, SPHERE_3D_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, SPHERE_3D_SPEC);
  }
}

const BOX_3D_SPEC = {
  className: "Box3d",
  create: "yetty_ysdf2_box_3d_create",
  primary: null,
  destroy: null,
  fields: {
    positionX: { kind: "scalar", sym: "yetty_ysdf2_box_3d_position_x_set" },
    positionY: { kind: "scalar", sym: "yetty_ysdf2_box_3d_position_y_set" },
    positionZ: { kind: "scalar", sym: "yetty_ysdf2_box_3d_position_z_set" },
    halfSizeX: { kind: "scalar", sym: "yetty_ysdf2_box_3d_half_size_x_set" },
    halfSizeY: { kind: "scalar", sym: "yetty_ysdf2_box_3d_half_size_y_set" },
    halfSizeZ: { kind: "scalar", sym: "yetty_ysdf2_box_3d_half_size_z_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Box3d extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(BOX_3D_SPEC, primaryOrOptions, options);
    runtime.adopt(this, BOX_3D_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, BOX_3D_SPEC);
  }
}

const TORUS_3D_SPEC = {
  className: "Torus3d",
  create: "yetty_ysdf2_torus_3d_create",
  primary: null,
  destroy: null,
  fields: {
    positionX: { kind: "scalar", sym: "yetty_ysdf2_torus_3d_position_x_set" },
    positionY: { kind: "scalar", sym: "yetty_ysdf2_torus_3d_position_y_set" },
    positionZ: { kind: "scalar", sym: "yetty_ysdf2_torus_3d_position_z_set" },
    majorRadius: { kind: "scalar", sym: "yetty_ysdf2_torus_3d_major_radius_set" },
    minorRadius: { kind: "scalar", sym: "yetty_ysdf2_torus_3d_minor_radius_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Torus3d extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(TORUS_3D_SPEC, primaryOrOptions, options);
    runtime.adopt(this, TORUS_3D_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, TORUS_3D_SPEC);
  }
}

const CYLINDER_3D_SPEC = {
  className: "Cylinder3d",
  create: "yetty_ysdf2_cylinder_3d_create",
  primary: null,
  destroy: null,
  fields: {
    positionX: { kind: "scalar", sym: "yetty_ysdf2_cylinder_3d_position_x_set" },
    positionY: { kind: "scalar", sym: "yetty_ysdf2_cylinder_3d_position_y_set" },
    positionZ: { kind: "scalar", sym: "yetty_ysdf2_cylinder_3d_position_z_set" },
    radius: { kind: "scalar", sym: "yetty_ysdf2_cylinder_3d_radius_set" },
    halfHeight: { kind: "scalar", sym: "yetty_ysdf2_cylinder_3d_half_height_set" },
    fill: { kind: "cstr", sym: "yetty_ydrawlist2_set_fill" },
    stroke: { kind: "cstr", sym: "yetty_ydrawlist2_set_stroke" },
    id: { kind: "scalar", sym: "yetty_ydrawlist2_shape_id_set" },
    z: { kind: "scalar", sym: "yetty_ydrawlist2_shape_z_set" },
    strokeWidth: { kind: "scalar", sym: "yetty_ydrawlist2_shape_stroke_width_set" },
  },
};

export class Cylinder3d extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(CYLINDER_3D_SPEC, primaryOrOptions, options);
    runtime.adopt(this, CYLINDER_3D_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, CYLINDER_3D_SPEC);
  }
}

const CURVE_SPEC = {
  className: "Curve",
  create: "yetty_api_yplot_curve_create",
  primary: null,
  destroy: null,
  fields: {
    name: { kind: "cstr", sym: "yetty_api_yplot_set_name" },
    color: { kind: "cstr", sym: "yetty_api_yplot_set_color" },
  },
};

export class Curve extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(CURVE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, CURVE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, CURVE_SPEC);
  }
}

const FUNCTION_SPEC = {
  className: "Function",
  create: "yetty_api_yplot_function_create",
  primary: "yetty_api_yplot_set_body",
  destroy: null,
  fields: {
    body: { kind: "cstr", sym: "yetty_api_yplot_set_body" },
    name: { kind: "cstr", sym: "yetty_api_yplot_set_name" },
    color: { kind: "cstr", sym: "yetty_api_yplot_set_color" },
  },
};

export class Function extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(FUNCTION_SPEC, primaryOrOptions, options);
    runtime.adopt(this, FUNCTION_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, FUNCTION_SPEC);
  }
}

const BUFFER_SPEC = {
  className: "Buffer",
  create: "yetty_api_yplot_buffer_create",
  primary: "yetty_api_yplot_set_name",
  destroy: null,
  fields: {
    values: { kind: "buffer", sym: "yetty_api_yplot_set_values" },
    size: { kind: "scalar", sym: "yetty_api_yplot_buffer_size_set" },
    name: { kind: "cstr", sym: "yetty_api_yplot_set_name" },
    color: { kind: "cstr", sym: "yetty_api_yplot_set_color" },
  },
};

export class Buffer extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(BUFFER_SPEC, primaryOrOptions, options);
    runtime.adopt(this, BUFFER_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, BUFFER_SPEC);
  }
}

const PLOT_SPEC = {
  className: "Plot",
  create: "yetty_api_yplot_plot_create",
  primary: "yetty_api_yplot_set_expression",
  destroy: "yetty_api_yplot_destroy",
  fields: {
    expression: { kind: "cstr", sym: "yetty_api_yplot_set_expression" },
    functions: { kind: "adder", sym: "yetty_api_yplot_add_function" },
    title: { kind: "cstr", sym: "yetty_api_yplot_set_title" },
    xLabel: { kind: "cstr", sym: "yetty_api_yplot_set_x_label" },
    yLabel: { kind: "cstr", sym: "yetty_api_yplot_set_y_label" },
    size: { kind: "multi", sym: "yetty_api_yplot_set_size", n: 2 },
    xRange: { kind: "multi", sym: "yetty_api_yplot_set_x_range", n: 2 },
    yRange: { kind: "multi", sym: "yetty_api_yplot_set_y_range", n: 2 },
    buffers: { kind: "adder", sym: "yetty_api_yplot_add_buffer" },
    view: { kind: "multi", sym: "yetty_api_yplot_set_view", n: 4 },
    noGrid: { kind: "scalar", sym: "yetty_api_yplot_set_nogrid" },
    noAxes: { kind: "scalar", sym: "yetty_api_yplot_set_noaxes" },
    noLabels: { kind: "scalar", sym: "yetty_api_yplot_set_nolabels" },
    x: { kind: "scalar", sym: "yetty_api_yplot_plot_x_set" },
    y: { kind: "scalar", sym: "yetty_api_yplot_plot_y_set" },
    width: { kind: "scalar", sym: "yetty_api_yplot_plot_width_set" },
    height: { kind: "scalar", sym: "yetty_api_yplot_plot_height_set" },
  },
};

export class Plot extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(PLOT_SPEC, primaryOrOptions, options);
    runtime.adopt(this, PLOT_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, PLOT_SPEC);
  }
}

const IMAGE_SPEC = {
  className: "Image",
  create: "yetty_ycomplex2_image_create",
  primary: "yetty_ycomplex2_set_path",
  destroy: null,
  fields: {
    path: { kind: "cstr", sym: "yetty_ycomplex2_set_path" },
    x: { kind: "scalar", sym: "yetty_ycomplex2_image_x_set" },
    y: { kind: "scalar", sym: "yetty_ycomplex2_image_y_set" },
    width: { kind: "scalar", sym: "yetty_ycomplex2_image_width_set" },
    height: { kind: "scalar", sym: "yetty_ycomplex2_image_height_set" },
  },
};

export class Image extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(IMAGE_SPEC, primaryOrOptions, options);
    runtime.adopt(this, IMAGE_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, IMAGE_SPEC);
  }
}

const MESH_SPEC = {
  className: "Mesh",
  create: "yetty_ycomplex2_mesh_create",
  primary: "yetty_ycomplex2_set_glb",
  destroy: null,
  fields: {
    glb: { kind: "cstr", sym: "yetty_ycomplex2_set_glb" },
    x: { kind: "scalar", sym: "yetty_ycomplex2_mesh_x_set" },
    y: { kind: "scalar", sym: "yetty_ycomplex2_mesh_y_set" },
    width: { kind: "scalar", sym: "yetty_ycomplex2_mesh_width_set" },
    height: { kind: "scalar", sym: "yetty_ycomplex2_mesh_height_set" },
    azimuth: { kind: "scalar", sym: "yetty_ycomplex2_mesh_azimuth_set" },
    elevation: { kind: "scalar", sym: "yetty_ycomplex2_mesh_elevation_set" },
    zoom: { kind: "scalar", sym: "yetty_ycomplex2_mesh_zoom_set" },
    wireframe: { kind: "scalar", sym: "yetty_ycomplex2_mesh_wireframe_set" },
  },
};

export class Mesh extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(MESH_SPEC, primaryOrOptions, options);
    runtime.adopt(this, MESH_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, MESH_SPEC);
  }
}

const SHADERTOY_SPEC = {
  className: "Shadertoy",
  create: "yetty_ycomplex2_shadertoy_create",
  primary: "yetty_ycomplex2_set_wgsl_path",
  destroy: null,
  fields: {
    source: { kind: "cstr", sym: "yetty_ycomplex2_set_source" },
    wgslPath: { kind: "cstr", sym: "yetty_ycomplex2_set_wgsl_path" },
    x: { kind: "scalar", sym: "yetty_ycomplex2_shadertoy_x_set" },
    y: { kind: "scalar", sym: "yetty_ycomplex2_shadertoy_y_set" },
    width: { kind: "scalar", sym: "yetty_ycomplex2_shadertoy_width_set" },
    height: { kind: "scalar", sym: "yetty_ycomplex2_shadertoy_height_set" },
  },
};

export class Shadertoy extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    this.handle = runtime.construct(SHADERTOY_SPEC, primaryOrOptions, options);
    runtime.adopt(this, SHADERTOY_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, SHADERTOY_SPEC);
  }
}

const VIDEO_SPEC = {
  className: "Video",
  create: "yetty_ycomplex2_video_create",
  primary: "yetty_ycomplex2_set_h264",
  destroy: null,
  fields: {
    h264: { kind: "cstr", sym: "yetty_ycomplex2_set_h264" },
    x: { kind: "scalar", sym: "yetty_ycomplex2_video_x_set" },
    y: { kind: "scalar", sym: "yetty_ycomplex2_video_y_set" },
    width: { kind: "scalar", sym: "yetty_ycomplex2_video_width_set" },
    height: { kind: "scalar", sym: "yetty_ycomplex2_video_height_set" },
    id: { kind: "scalar", sym: "yetty_ycomplex2_video_id_set" },
    videoW: { kind: "scalar", sym: "yetty_ycomplex2_video_video_w_set" },
    videoH: { kind: "scalar", sym: "yetty_ycomplex2_video_video_h_set" },
    fps: { kind: "scalar", sym: "yetty_ycomplex2_video_fps_set" },
  },
};

// Discovery for the feature-gated Video class (yvideo build feature).
export function hasVideo() {
  return runtime.hasSymbol("yetty_ycomplex2_video_create");
}

export class Video extends Drawable {
  constructor(primaryOrOptions, options) {
    super();
    runtime.requireFeature("Video", "yetty_ycomplex2_video_create", "yvideo");
    this.handle = runtime.construct(VIDEO_SPEC, primaryOrOptions, options);
    runtime.adopt(this, VIDEO_SPEC);
  }

  destroy() {
    runtime.destroyObject(this, VIDEO_SPEC);
  }
}

const DRAWABLE_LIST_SPEC = {
  className: "DrawableList",
  create: "yetty_ydrawlist2_drawable_list_create",
  primary: null,
  destroy: "yetty_ydrawlist2_destroy",
  fields: {},
};

// DrawableList — the drawable list: one list, immediate
// appends in call order.
export class DrawableList {
  constructor() {
    this.handle = runtime.invoke(DRAWABLE_LIST_SPEC.create, null);
    runtime.adopt(this, DRAWABLE_LIST_SPEC);
  }

  // add packs the drawable's record into the list,
  // immediately. It manages nothing and returns nothing.
  add(drawable) {
    runtime.requireHandle(this, "DrawableList");
    runtime.invoke("yetty_ydrawlist2_add", this.handle, drawable.handle);
  }

  dcsEmit() {
    runtime.requireHandle(this, "DrawableList");
    runtime.invoke("yetty_ydrawlist2_dcs_emit", this.handle);
  }

  destroy() {
    runtime.destroyObject(this, DRAWABLE_LIST_SPEC);
  }
}
