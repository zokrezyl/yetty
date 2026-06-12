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

class timespec(Structure):
    pass
timespec._fields_ = [("tv_sec", c_void_p), ("tv_nsec", c_void_p)]

class yetty_ycore_error(Structure):
    pass
yetty_ycore_error._fields_ = [("msg", c_char_p), ("file", c_char_p), ("func", c_char_p), ("line", c_int), ("cause", c_void_p)]

class uint32_result_u1(Union):
    pass
uint32_result_u1._fields_ = [("value", c_uint32), ("error", yetty_ycore_error)]
class uint32_result(Structure):
    pass
uint32_result._anonymous_ = ('_anon1',)
uint32_result._fields_ = [("ok", c_int), ("_anon1", uint32_result_u1)]

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

class yetty_ycore_pixel_coord(Structure):
    pass
yetty_ycore_pixel_coord._fields_ = [("x", c_float), ("y", c_float)]

class yetty_ycore_pixel_size(Structure):
    pass
yetty_ycore_pixel_size._fields_ = [("width", c_float), ("height", c_float)]

class yetty_ycore_rectangle(Structure):
    pass
yetty_ycore_rectangle._fields_ = [("min", yetty_ycore_pixel_coord), ("max", yetty_ycore_pixel_coord)]

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

class yetty_ygui_layout(Structure):
    pass
yetty_ygui_layout._fields_ = [("direction", c_int), ("justify", c_int), ("align", c_int), ("gap", c_float), ("padding_top", c_float), ("padding_right", c_float), ("padding_bottom", c_float), ("padding_left", c_float), ("width", c_float), ("height", c_float), ("flex_grow", c_float), ("flex_shrink", c_float), ("min_width", c_float), ("max_width", c_float), ("min_height", c_float), ("max_height", c_float), ("absolute", c_int), ("pos_x", c_float), ("pos_y", c_float), ("hidden", c_int)]

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

class yetty_yrich_input_mods(Structure):
    pass
yetty_yrich_input_mods._fields_ = [("shift", c_bool), ("ctrl", c_bool), ("alt", c_bool), ("meta", c_bool)]

class ymusic_staff(Structure):
    pass
ymusic_staff._fields_ = [("clef", c_int), ("key_fifths", c_int), ("time_num", c_int), ("time_den", c_int), ("measures", c_void_p), ("count", c_size_t), ("cap", c_size_t)]

