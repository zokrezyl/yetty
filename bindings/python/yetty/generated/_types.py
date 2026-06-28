"""Foundational + shared ABI types — GENERATED, do not edit."""
from __future__ import annotations
from ctypes import (Structure, Union, c_bool, c_char, c_char_p, c_double,
    c_float, c_int, c_int8, c_int16, c_int32, c_int64, c_long, c_size_t,
    c_ssize_t, c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong,
    c_void_p)
from enum import IntEnum

class yetty_yrender_uniform_type(IntEnum):
    YETTY_YRENDER_UNIFORM_F32 = 0
    YETTY_YRENDER_UNIFORM_VEC2 = 1
    YETTY_YRENDER_UNIFORM_VEC3 = 2
    YETTY_YRENDER_UNIFORM_VEC4 = 3
    YETTY_YRENDER_UNIFORM_MAT4 = 4
    YETTY_YRENDER_UNIFORM_U32 = 5
    YETTY_YRENDER_UNIFORM_I32 = 6

class yetty_ygui_csi_state(IntEnum):
    YETTY_YGUI_CSI_NORMAL = 0
    YETTY_YGUI_CSI_ESC = 1
    YETTY_YGUI_CSI_BRACKET = 2

class yetty_ygui_flex_align(IntEnum):
    YETTY_YGUI_ALIGN_START = 0
    YETTY_YGUI_ALIGN_CENTER = 1
    YETTY_YGUI_ALIGN_END = 2
    YETTY_YGUI_ALIGN_STRETCH = 3

class yetty_ygui_flex_direction(IntEnum):
    YETTY_YGUI_FLEX_ROW = 0
    YETTY_YGUI_FLEX_COLUMN = 1

class yetty_ygui_flex_justify(IntEnum):
    YETTY_YGUI_JUSTIFY_START = 0
    YETTY_YGUI_JUSTIFY_CENTER = 1
    YETTY_YGUI_JUSTIFY_END = 2
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN = 3

class ynode_drag_mode(IntEnum):
    YNODE_DRAG_NONE = 0
    YNODE_DRAG_MOVE = 1
    YNODE_DRAG_LINK = 2
    YNODE_DRAG_RESIZE = 3

class ymusic_clef(IntEnum):
    YMUSIC_CLEF_TREBLE = 0
    YMUSIC_CLEF_BASS = 1
    YMUSIC_CLEF_ALTO = 2
    YMUSIC_CLEF_TENOR = 3

class yetty_yrich_app_kind(IntEnum):
    YETTY_YRICH_APP_YDOC = 0
    YETTY_YRICH_APP_YSHEET = 1
    YETTY_YRICH_APP_YSLIDE = 2

class yetty_ycore_error(Structure):
    pass
yetty_ycore_error._fields_ = [("msg", c_char_p), ("file", c_char_p), ("func", c_char_p), ("line", c_int), ("cause", c_void_p)]

class float_result_u1(Union):
    pass
float_result_u1._fields_ = [("value", c_float), ("error", yetty_ycore_error)]
class float_result(Structure):
    pass
float_result._anonymous_ = ('_anon1',)
float_result._fields_ = [("ok", c_int), ("_anon1", float_result_u1)]

class yetty_ycore_pixel_size(Structure):
    pass
yetty_ycore_pixel_size._fields_ = [("width", c_float), ("height", c_float)]

class pixel_size_result_u1(Union):
    pass
pixel_size_result_u1._fields_ = [("value", yetty_ycore_pixel_size), ("error", yetty_ycore_error)]
class pixel_size_result(Structure):
    pass
pixel_size_result._anonymous_ = ('_anon1',)
pixel_size_result._fields_ = [("ok", c_int), ("_anon1", pixel_size_result_u1)]

class yetty_ycore_pixel_coord(Structure):
    pass
yetty_ycore_pixel_coord._fields_ = [("x", c_float), ("y", c_float)]

class yetty_ycore_rectangle(Structure):
    pass
yetty_ycore_rectangle._fields_ = [("min", yetty_ycore_pixel_coord), ("max", yetty_ycore_pixel_coord)]

class rectangle_result_u1(Union):
    pass
rectangle_result_u1._fields_ = [("value", yetty_ycore_rectangle), ("error", yetty_ycore_error)]
class rectangle_result(Structure):
    pass
rectangle_result._anonymous_ = ('_anon1',)
rectangle_result._fields_ = [("ok", c_int), ("_anon1", rectangle_result_u1)]

class timespec(Structure):
    pass
timespec._fields_ = [("tv_sec", c_void_p), ("tv_nsec", c_void_p)]

class uint32_result_u1(Union):
    pass
uint32_result_u1._fields_ = [("value", c_uint32), ("error", yetty_ycore_error)]
class uint32_result(Structure):
    pass
uint32_result._anonymous_ = ('_anon1',)
uint32_result._fields_ = [("ok", c_int), ("_anon1", uint32_result_u1)]

class vterm_uniforms(Structure):
    pass
vterm_uniforms._fields_ = [("grid_size", (c_float * 2)), ("cell_size", (c_float * 2)), ("scale", c_float), ("baseline_y", c_float), ("glyph_left", c_float), ("pixel_range", c_float), ("root_row", c_uint32), ("cursor_col", c_uint32), ("cursor_row", c_uint32), ("cursor_visible", c_uint32), ("sel_active", c_uint32), ("sel_start_row", c_uint32), ("sel_start_col", c_uint32), ("sel_end_row", c_uint32), ("sel_end_col", c_uint32), ("ring_rows", c_uint32), ("visual_zoom_scale", c_float), ("visual_zoom_offset_x", c_float), ("visual_zoom_offset_y", c_float), ("pad_b", c_uint32), ("pad_c", c_uint32), ("pad_d", c_uint32)]

class yetty_context(Structure):
    pass
yetty_context._fields_ = [("runtime", c_void_p), ("pty_factory", c_void_p), ("event_loop", c_void_p)]

class yetty_yclass_ctx(Structure):
    pass
yetty_yclass_ctx._fields_ = [("session", c_void_p)]

class yetty_yclass_object_ptr_result_u1(Union):
    pass
yetty_yclass_object_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yclass_object_ptr_result(Structure):
    pass
yetty_yclass_object_ptr_result._anonymous_ = ('_anon1',)
yetty_yclass_object_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yclass_object_ptr_result_u1)]

class yetty_yclass_ptr_result_u1(Union):
    pass
yetty_yclass_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yclass_ptr_result(Structure):
    pass
yetty_yclass_ptr_result._anonymous_ = ('_anon1',)
yetty_yclass_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yclass_ptr_result_u1)]

class yetty_yclass_void_ptr_result_u1(Union):
    pass
yetty_yclass_void_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yclass_void_ptr_result(Structure):
    pass
yetty_yclass_void_ptr_result._anonymous_ = ('_anon1',)
yetty_yclass_void_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yclass_void_ptr_result_u1)]

class yetty_yconfig_config_ptr_result_u1(Union):
    pass
yetty_yconfig_config_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yconfig_config_ptr_result(Structure):
    pass
yetty_yconfig_config_ptr_result._anonymous_ = ('_anon1',)
yetty_yconfig_config_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yconfig_config_ptr_result_u1)]

class yetty_ycore_buffer(Structure):
    pass
yetty_ycore_buffer._fields_ = [("data", c_void_p), ("capacity", c_size_t), ("size", c_size_t)]

class yetty_ycore_char_ptr_result_u1(Union):
    pass
yetty_ycore_char_ptr_result_u1._fields_ = [("value", c_char_p), ("error", yetty_ycore_error)]
class yetty_ycore_char_ptr_result(Structure):
    pass
yetty_ycore_char_ptr_result._anonymous_ = ('_anon1',)
yetty_ycore_char_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_char_ptr_result_u1)]

class yetty_ycore_const_char_ptr_result_u1(Union):
    pass
yetty_ycore_const_char_ptr_result_u1._fields_ = [("value", c_char_p), ("error", yetty_ycore_error)]
class yetty_ycore_const_char_ptr_result(Structure):
    pass
yetty_ycore_const_char_ptr_result._anonymous_ = ('_anon1',)
yetty_ycore_const_char_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_const_char_ptr_result_u1)]

class yetty_ycore_const_uint32_ptr_result_u1(Union):
    pass
yetty_ycore_const_uint32_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ycore_const_uint32_ptr_result(Structure):
    pass
yetty_ycore_const_uint32_ptr_result._anonymous_ = ('_anon1',)
yetty_ycore_const_uint32_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_const_uint32_ptr_result_u1)]

class yetty_ycore_const_uint8_ptr_result_u1(Union):
    pass
yetty_ycore_const_uint8_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ycore_const_uint8_ptr_result(Structure):
    pass
yetty_ycore_const_uint8_ptr_result._anonymous_ = ('_anon1',)
yetty_ycore_const_uint8_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_const_uint8_ptr_result_u1)]

class yetty_ycore_float_result_u1(Union):
    pass
yetty_ycore_float_result_u1._fields_ = [("value", c_float), ("error", yetty_ycore_error)]
class yetty_ycore_float_result(Structure):
    pass
yetty_ycore_float_result._anonymous_ = ('_anon1',)
yetty_ycore_float_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_float_result_u1)]

class yetty_ycore_grid_size(Structure):
    pass
yetty_ycore_grid_size._fields_ = [("rows", c_uint32), ("cols", c_uint32)]

class yetty_ycore_int_result_u1(Union):
    pass
yetty_ycore_int_result_u1._fields_ = [("value", c_int), ("error", yetty_ycore_error)]
class yetty_ycore_int_result(Structure):
    pass
yetty_ycore_int_result._anonymous_ = ('_anon1',)
yetty_ycore_int_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_int_result_u1)]

class yetty_ycore_rectangle_result_u1(Union):
    pass
yetty_ycore_rectangle_result_u1._fields_ = [("value", yetty_ycore_rectangle), ("error", yetty_ycore_error)]
class yetty_ycore_rectangle_result(Structure):
    pass
yetty_ycore_rectangle_result._anonymous_ = ('_anon1',)
yetty_ycore_rectangle_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_rectangle_result_u1)]

class yetty_ycore_rgba(Structure):
    pass
yetty_ycore_rgba._fields_ = [("r", c_uint8), ("g", c_uint8), ("b", c_uint8), ("a", c_uint8)]

class yetty_ycore_size_result_u1(Union):
    pass
yetty_ycore_size_result_u1._fields_ = [("value", c_size_t), ("error", yetty_ycore_error)]
class yetty_ycore_size_result(Structure):
    pass
yetty_ycore_size_result._anonymous_ = ('_anon1',)
yetty_ycore_size_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_size_result_u1)]

class yetty_ycore_uint32_result_u1(Union):
    pass
yetty_ycore_uint32_result_u1._fields_ = [("value", c_uint32), ("error", yetty_ycore_error)]
class yetty_ycore_uint32_result(Structure):
    pass
yetty_ycore_uint32_result._anonymous_ = ('_anon1',)
yetty_ycore_uint32_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_uint32_result_u1)]

class yetty_ycore_void_result_u1(Union):
    pass
yetty_ycore_void_result_u1._fields_ = [("value", c_int), ("error", yetty_ycore_error)]
class yetty_ycore_void_result(Structure):
    pass
yetty_ycore_void_result._anonymous_ = ('_anon1',)
yetty_ycore_void_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_void_result_u1)]

class yetty_ycore_xthread_event_pipe_ptr_result_u1(Union):
    pass
yetty_ycore_xthread_event_pipe_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ycore_xthread_event_pipe_ptr_result(Structure):
    pass
yetty_ycore_xthread_event_pipe_ptr_result._anonymous_ = ('_anon1',)
yetty_ycore_xthread_event_pipe_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_xthread_event_pipe_ptr_result_u1)]

class yetty_ydraw_composite_const_ptr_ptr_result_u1(Union):
    pass
yetty_ydraw_composite_const_ptr_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ydraw_composite_const_ptr_ptr_result(Structure):
    pass
yetty_ydraw_composite_const_ptr_ptr_result._anonymous_ = ('_anon1',)
yetty_ydraw_composite_const_ptr_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ydraw_composite_const_ptr_ptr_result_u1)]

class yetty_ydraw_drawable_list_result_u1(Union):
    pass
yetty_ydraw_drawable_list_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ydraw_drawable_list_result(Structure):
    pass
yetty_ydraw_drawable_list_result._anonymous_ = ('_anon1',)
yetty_ydraw_drawable_list_result._fields_ = [("ok", c_int), ("_anon1", yetty_ydraw_drawable_list_result_u1)]

class yetty_yevent_event_listener(Structure):
    pass
yetty_yevent_event_listener._fields_ = [("handler", c_void_p)]

class yetty_yfigure_figure_ptr_result_u1(Union):
    pass
yetty_yfigure_figure_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yfigure_figure_ptr_result(Structure):
    pass
yetty_yfigure_figure_ptr_result._anonymous_ = ('_anon1',)
yetty_yfigure_figure_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yfigure_figure_ptr_result_u1)]

class yetty_yfigure_hit(Structure):
    pass
yetty_yfigure_hit._fields_ = [("figure_id", c_uint32), ("local_x", c_float), ("local_y", c_float)]

class yetty_yfigure_hit_result_u1(Union):
    pass
yetty_yfigure_hit_result_u1._fields_ = [("value", yetty_yfigure_hit), ("error", yetty_ycore_error)]
class yetty_yfigure_hit_result(Structure):
    pass
yetty_yfigure_hit_result._anonymous_ = ('_anon1',)
yetty_yfigure_hit_result._fields_ = [("ok", c_int), ("_anon1", yetty_yfigure_hit_result_u1)]

class yetty_ygrid_factory_args(Structure):
    pass
yetty_ygrid_factory_args._fields_ = [("default_font", c_void_p), ("composite_factory", c_void_p), ("absolute_coords", c_int)]

class yetty_ygui_input_state(Structure):
    pass
yetty_ygui_input_state._fields_ = [("st", c_int), ("params", (c_char * 16)), ("params_len", c_int)]

class yetty_ygui_layout(Structure):
    pass
yetty_ygui_layout._fields_ = [("direction", c_int), ("justify", c_int), ("align", c_int), ("gap", c_float), ("padding_top", c_float), ("padding_right", c_float), ("padding_bottom", c_float), ("padding_left", c_float), ("width", c_float), ("height", c_float), ("flex_grow", c_float), ("flex_shrink", c_float), ("min_width", c_float), ("max_width", c_float), ("min_height", c_float), ("max_height", c_float), ("absolute", c_int), ("pos_x", c_float), ("pos_y", c_float), ("hidden", c_int)]

class yetty_ygui_layout_const_ptr_result_u1(Union):
    pass
yetty_ygui_layout_const_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ygui_layout_const_ptr_result(Structure):
    pass
yetty_ygui_layout_const_ptr_result._anonymous_ = ('_anon1',)
yetty_ygui_layout_const_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ygui_layout_const_ptr_result_u1)]

class yetty_ygui_yplot_config(Structure):
    pass
yetty_ygui_yplot_config._fields_ = [("x_min", c_float), ("x_max", c_float), ("y_min", c_float), ("y_max", c_float), ("flags", c_uint32)]

class yetty_ymgui_figure_ptr_result_u1(Union):
    pass
yetty_ymgui_figure_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ymgui_figure_ptr_result(Structure):
    pass
yetty_ymgui_figure_ptr_result._anonymous_ = ('_anon1',)
yetty_ymgui_figure_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ymgui_figure_ptr_result_u1)]

class yetty_yplatform_gpu_context(Structure):
    pass
yetty_yplatform_gpu_context._fields_ = [("instance", c_int), ("surface", c_int), ("surface_width", c_uint32), ("surface_height", c_uint32), ("content_scale", c_float), ("x11_display", c_void_p), ("x11_window", c_ulong)]

class yetty_yplatform_gpu_context_const_ptr_result_u1(Union):
    pass
yetty_yplatform_gpu_context_const_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yplatform_gpu_context_const_ptr_result(Structure):
    pass
yetty_yplatform_gpu_context_const_ptr_result._anonymous_ = ('_anon1',)
yetty_yplatform_gpu_context_const_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yplatform_gpu_context_const_ptr_result_u1)]

class yetty_yrdawn_figure_ptr_result_u1(Union):
    pass
yetty_yrdawn_figure_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yrdawn_figure_ptr_result(Structure):
    pass
yetty_yrdawn_figure_ptr_result._anonymous_ = ('_anon1',)
yetty_yrdawn_figure_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yrdawn_figure_ptr_result_u1)]

class yetty_yrender_buffer(Structure):
    pass
yetty_yrender_buffer._fields_ = [("data", c_void_p), ("size", c_size_t), ("capacity", c_size_t), ("name", (c_char * 64)), ("wgsl_type", (c_char * 64)), ("readonly", c_int), ("dirty", c_int)]

class yetty_yrender_shader_code(Structure):
    pass
yetty_yrender_shader_code._fields_ = [("data", c_char_p), ("size", c_size_t), ("hash", c_uint64)]

class yetty_yrender_texture(Structure):
    pass
yetty_yrender_texture._fields_ = [("data", c_void_p), ("width", c_uint32), ("height", c_uint32), ("format", c_uint32), ("name", (c_char * 64)), ("wgsl_type", (c_char * 64)), ("sampler_name", (c_char * 64)), ("sampler_filter", c_uint32), ("dirty", c_int)]

class yetty_yrender_uniform_u1(Union):
    pass
yetty_yrender_uniform_u1._fields_ = [("f32", c_float), ("vec2", (c_float * 2)), ("vec3", (c_float * 3)), ("vec4", (c_float * 4)), ("mat4", (c_float * 16)), ("u32", c_uint32), ("i32", c_int32)]
class yetty_yrender_uniform(Structure):
    pass
yetty_yrender_uniform._anonymous_ = ('_anon1',)
yetty_yrender_uniform._fields_ = [("name", (c_char * 64)), ("type", c_int), ("_anon1", yetty_yrender_uniform_u1)]

class yetty_yrender_gpu_resource_set(Structure):
    pass
yetty_yrender_gpu_resource_set._fields_ = [("namespace", (c_char * 64)), ("pixel_size", yetty_ycore_pixel_size), ("textures", (yetty_yrender_texture * 4)), ("texture_count", c_size_t), ("buffers", (yetty_yrender_buffer * 4)), ("buffer_count", c_size_t), ("uniforms", (yetty_yrender_uniform * 32)), ("uniform_count", c_size_t), ("shader", yetty_yrender_shader_code), ("children", (c_void_p * 64)), ("children_count", c_size_t), ("instance_count", c_uint32)]

class yetty_yrich_border(Structure):
    pass
yetty_yrich_border._fields_ = [("width", c_float), ("color", c_uint32), ("style", c_uint32)]

class yetty_yrich_cell_addr(Structure):
    pass
yetty_yrich_cell_addr._fields_ = [("row", c_int32), ("col", c_int32)]

class yetty_yrich_cell_addr_result_u1(Union):
    pass
yetty_yrich_cell_addr_result_u1._fields_ = [("value", yetty_yrich_cell_addr), ("error", yetty_ycore_error)]
class yetty_yrich_cell_addr_result(Structure):
    pass
yetty_yrich_cell_addr_result._anonymous_ = ('_anon1',)
yetty_yrich_cell_addr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yrich_cell_addr_result_u1)]

class yetty_yrich_cell_range(Structure):
    pass
yetty_yrich_cell_range._fields_ = [("start", yetty_yrich_cell_addr), ("end", yetty_yrich_cell_addr)]

class yetty_yrich_drawable_list_ptr_result_u1(Union):
    pass
yetty_yrich_drawable_list_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yrich_drawable_list_ptr_result(Structure):
    pass
yetty_yrich_drawable_list_ptr_result._anonymous_ = ('_anon1',)
yetty_yrich_drawable_list_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yrich_drawable_list_ptr_result_u1)]

class yetty_yrich_element_id_result_u1(Union):
    pass
yetty_yrich_element_id_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yrich_element_id_result(Structure):
    pass
yetty_yrich_element_id_result._anonymous_ = ('_anon1',)
yetty_yrich_element_id_result._fields_ = [("ok", c_int), ("_anon1", yetty_yrich_element_id_result_u1)]

class yetty_yrich_history(Structure):
    pass
yetty_yrich_history._fields_ = [("undo_stack", c_void_p), ("undo_count", c_size_t), ("undo_capacity", c_size_t), ("redo_stack", c_void_p), ("redo_count", c_size_t), ("redo_capacity", c_size_t), ("max_size", c_size_t)]

class yetty_yrich_op_log(Structure):
    pass
yetty_yrich_op_log._fields_ = [("ops", c_void_p), ("count", c_size_t), ("capacity", c_size_t), ("current_ts", c_uint64)]

class yetty_yrich_operation_ptr_result_u1(Union):
    pass
yetty_yrich_operation_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yrich_operation_ptr_result(Structure):
    pass
yetty_yrich_operation_ptr_result._anonymous_ = ('_anon1',)
yetty_yrich_operation_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yrich_operation_ptr_result_u1)]

class yetty_yrich_rect(Structure):
    pass
yetty_yrich_rect._fields_ = [("x", c_float), ("y", c_float), ("w", c_float), ("h", c_float)]

class yetty_yrich_selection_cells(Structure):
    pass
yetty_yrich_selection_cells._fields_ = [("range", yetty_yrich_cell_range), ("active", yetty_yrich_cell_addr)]

class yetty_yrich_selection_elements(Structure):
    pass
yetty_yrich_selection_elements._fields_ = [("ids", c_void_p), ("count", c_size_t), ("capacity", c_size_t)]

class yetty_yrich_selection_text(Structure):
    pass
yetty_yrich_selection_text._fields_ = [("element_id", c_void_p), ("start", c_int32), ("end", c_int32)]

class yetty_yrich_selection_u1(Union):
    pass
yetty_yrich_selection_u1._fields_ = [("elements", yetty_yrich_selection_elements), ("cells", yetty_yrich_selection_cells), ("text", yetty_yrich_selection_text)]
class yetty_yrich_selection(Structure):
    pass
yetty_yrich_selection._fields_ = [("kind", c_uint32), ("u", yetty_yrich_selection_u1)]

class yetty_yrich_selection_ptr_result_u1(Union):
    pass
yetty_yrich_selection_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yrich_selection_ptr_result(Structure):
    pass
yetty_yrich_selection_ptr_result._anonymous_ = ('_anon1',)
yetty_yrich_selection_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yrich_selection_ptr_result_u1)]

class yetty_yrich_slide_ptr_result_u1(Union):
    pass
yetty_yrich_slide_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yrich_slide_ptr_result(Structure):
    pass
yetty_yrich_slide_ptr_result._anonymous_ = ('_anon1',)
yetty_yrich_slide_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yrich_slide_ptr_result_u1)]

class yetty_yrich_text_style(Structure):
    pass
yetty_yrich_text_style._fields_ = [("font_size", c_float), ("color", c_uint32), ("bg_color", c_uint32), ("format", c_uint32), ("font_id", c_int32)]

class yetty_yvterm_text_cell_const_ptr_result_u1(Union):
    pass
yetty_yvterm_text_cell_const_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yvterm_text_cell_const_ptr_result(Structure):
    pass
yetty_yvterm_text_cell_const_ptr_result._anonymous_ = ('_anon1',)
yetty_yvterm_text_cell_const_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yvterm_text_cell_const_ptr_result_u1)]

class ymusic_staff(Structure):
    pass
ymusic_staff._fields_ = [("clef", c_int), ("key_fifths", c_int), ("time_num", c_int), ("time_den", c_int), ("measures", c_void_p), ("count", c_size_t), ("cap", c_size_t)]

