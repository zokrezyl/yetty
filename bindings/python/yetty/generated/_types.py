"""Foundational + shared ABI types — GENERATED, do not edit."""
from __future__ import annotations
from ctypes import (Structure, Union, c_bool, c_char, c_char_p, c_double,
    c_float, c_int, c_int8, c_int16, c_int32, c_int64, c_long, c_size_t,
    c_ssize_t, c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong,
    c_void_p)
from enum import IntEnum

class yetty_ygui_csi_state(IntEnum):
    YETTY_YGUI_CSI_NORMAL = 0
    YETTY_YGUI_CSI_ESC = 1
    YETTY_YGUI_CSI_BRACKET = 2

class yetty_ygui_flex_align(IntEnum):
    YETTY_YGUI_ALIGN_START = 0
    YETTY_YGUI_ALIGN_CENTER = 1
    YETTY_YGUI_ALIGN_END = 2
    YETTY_YGUI_ALIGN_STRETCH = 3

class yetty_ygui_flex_align_self(IntEnum):
    YETTY_YGUI_ALIGN_SELF_AUTO = 0
    YETTY_YGUI_ALIGN_SELF_START = 1
    YETTY_YGUI_ALIGN_SELF_CENTER = 2
    YETTY_YGUI_ALIGN_SELF_END = 3
    YETTY_YGUI_ALIGN_SELF_STRETCH = 4

class yetty_ygui_flex_direction(IntEnum):
    YETTY_YGUI_FLEX_ROW = 0
    YETTY_YGUI_FLEX_COLUMN = 1

class yetty_ygui_flex_justify(IntEnum):
    YETTY_YGUI_JUSTIFY_START = 0
    YETTY_YGUI_JUSTIFY_CENTER = 1
    YETTY_YGUI_JUSTIFY_END = 2
    YETTY_YGUI_JUSTIFY_SPACE_BETWEEN = 3

class yetty_ygui_flex_wrap(IntEnum):
    YETTY_YGUI_WRAP_NOWRAP = 0
    YETTY_YGUI_WRAP_WRAP = 1

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

class yetty_ymux_key(IntEnum):
    YETTY_YMUX_KEY_ENTER = 1
    YETTY_YMUX_KEY_TAB = 2
    YETTY_YMUX_KEY_BACKSPACE = 3
    YETTY_YMUX_KEY_ESCAPE = 4
    YETTY_YMUX_KEY_UP = 5
    YETTY_YMUX_KEY_DOWN = 6
    YETTY_YMUX_KEY_LEFT = 7
    YETTY_YMUX_KEY_RIGHT = 8
    YETTY_YMUX_KEY_INSERT = 9
    YETTY_YMUX_KEY_DELETE = 10
    YETTY_YMUX_KEY_HOME = 11
    YETTY_YMUX_KEY_END = 12
    YETTY_YMUX_KEY_PAGE_UP = 13
    YETTY_YMUX_KEY_PAGE_DOWN = 14
    YETTY_YMUX_KEY_KP_0 = 15
    YETTY_YMUX_KEY_KP_1 = 16
    YETTY_YMUX_KEY_KP_2 = 17
    YETTY_YMUX_KEY_KP_3 = 18
    YETTY_YMUX_KEY_KP_4 = 19
    YETTY_YMUX_KEY_KP_5 = 20
    YETTY_YMUX_KEY_KP_6 = 21
    YETTY_YMUX_KEY_KP_7 = 22
    YETTY_YMUX_KEY_KP_8 = 23
    YETTY_YMUX_KEY_KP_9 = 24
    YETTY_YMUX_KEY_KP_MULT = 25
    YETTY_YMUX_KEY_KP_PLUS = 26
    YETTY_YMUX_KEY_KP_COMMA = 27
    YETTY_YMUX_KEY_KP_MINUS = 28
    YETTY_YMUX_KEY_KP_PERIOD = 29
    YETTY_YMUX_KEY_KP_DIVIDE = 30
    YETTY_YMUX_KEY_KP_ENTER = 31
    YETTY_YMUX_KEY_KP_EQUAL = 32
    YETTY_YMUX_KEY_F1 = 33
    YETTY_YMUX_KEY_F2 = 34
    YETTY_YMUX_KEY_F3 = 35
    YETTY_YMUX_KEY_F4 = 36
    YETTY_YMUX_KEY_F5 = 37
    YETTY_YMUX_KEY_F6 = 38
    YETTY_YMUX_KEY_F7 = 39
    YETTY_YMUX_KEY_F8 = 40
    YETTY_YMUX_KEY_F9 = 41
    YETTY_YMUX_KEY_F10 = 42
    YETTY_YMUX_KEY_F11 = 43
    YETTY_YMUX_KEY_F12 = 44

class yetty_yrich_app_kind(IntEnum):
    YETTY_YRICH_APP_YDOC = 0
    YETTY_YRICH_APP_YSHEET = 1
    YETTY_YRICH_APP_YSLIDE = 2

class yetty_yrich_edit_mode(IntEnum):
    YETTY_YRICH_MODE_DEFAULT = 0
    YETTY_YRICH_MODE_VI_NORMAL = 1
    YETTY_YRICH_MODE_VI_INSERT = 2
    YETTY_YRICH_MODE_COUNT = 3

class yetty_yrender_uniform_type(IntEnum):
    YETTY_YRENDER_UNIFORM_F32 = 0
    YETTY_YRENDER_UNIFORM_VEC2 = 1
    YETTY_YRENDER_UNIFORM_VEC3 = 2
    YETTY_YRENDER_UNIFORM_VEC4 = 3
    YETTY_YRENDER_UNIFORM_MAT4 = 4
    YETTY_YRENDER_UNIFORM_U32 = 5
    YETTY_YRENDER_UNIFORM_I32 = 6

class yvterm_font_method(IntEnum):
    YVTERM_FONT_METHOD_MSDF = 0
    YVTERM_FONT_METHOD_RASTER = 1
    YVTERM_FONT_METHOD_RASTER_COLOR = 2

class client_resource(Structure):
    pass
client_resource._fields_ = [("hash", c_uint64), ("words", c_void_p), ("word_count", c_uint32)]

class daemon_connection_u1(Structure):
    pass
daemon_connection_u1._fields_ = [("input_class", c_uint32), ("byte_len", c_uint32), ("bytes", c_void_p)]
class daemon_connection(Structure):
    pass
daemon_connection._fields_ = [("socket", c_void_p), ("session", c_void_p), ("attachment_id", c_uint32), ("pane_id", c_uint32), ("capabilities", c_uint32), ("sent_generation", c_uint64), ("acked_generation", c_uint64), ("fail_next_vtsink_tx", c_uint32), ("chrome_queue", daemon_connection_u1), ("chrome_queue_head", c_uint32), ("chrome_queue_count", c_uint32), ("chrome_intake_count", c_uint64), ("chrome_intake_class", c_uint32), ("overlay_applied_seq", c_uint32), ("refuse_next_overlay", c_uint32), ("copy_key_pending", (c_uint8 * 16)), ("copy_key_pending_len", c_uint32), ("copy_cursor_row", c_int), ("copy_cursor_col", c_int), ("copy_anchor_row", c_int), ("copy_anchor_col", c_int), ("copy_selecting", c_int), ("chrome_last_bytes", c_void_p), ("chrome_last_len", c_uint32), ("chrome_last_class", c_uint32), ("sent_pane_modes", c_uint32), ("sent_pane_modes_valid", c_int), ("vtsink_session", c_void_p), ("vtsink_lane", c_void_p), ("vtsink_proxy", c_void_p), ("rx", c_void_p), ("rx_len", c_size_t), ("tx", c_void_p), ("tx_len", c_size_t), ("tx_sent", c_size_t), ("tx_total_sent", c_uint64), ("slow_last_total_sent", c_uint64), ("slow_recover_count", c_uint32), ("want_close", c_int)]

class daemon_pane_pty_u1(Structure):
    pass
daemon_pane_pty_u1._fields_ = [("channel_id", c_uint32), ("channel", c_void_p)]
class daemon_pane_pty(Structure):
    pass
daemon_pane_pty._fields_ = [("pty", c_void_p), ("daemon", c_void_p), ("session", c_void_p), ("pane_id", c_uint32), ("pty_rows", c_uint32), ("pty_cols", c_uint32), ("out_queue", c_void_p), ("out_queue_len", c_size_t), ("out_queue_cap", c_size_t), ("rpc_sm", c_void_p), ("rpc_connection", c_void_p), ("rpc_forward_state", c_void_p), ("rpc_controller", c_uint32), ("rpc_channels", daemon_pane_pty_u1), ("rpc_channel_count", c_uint32)]

class daemon_session_entry(Structure):
    pass
daemon_session_entry._fields_ = [("session", c_void_p), ("name", (c_char * 64)), ("created_stamp", c_uint64)]

class engine_surface(Structure):
    pass
engine_surface._fields_ = [("rows", c_void_p), ("rows_count", c_uint32), ("cols", c_uint32)]

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

class history_builder(Structure):
    pass
history_builder._fields_ = [("bytes", c_void_p), ("byte_count", c_size_t), ("byte_capacity", c_size_t), ("row_offsets", c_void_p), ("row_count", c_uint32), ("row_capacity", c_uint32), ("first_row", c_uint64)]

class history_cache_entry(Structure):
    pass
history_cache_entry._fields_ = [("valid", c_int), ("first_row", c_uint64), ("row_count", c_uint32), ("row_cells", c_void_p), ("row_cols", c_void_p), ("row_logical_ids", c_void_p), ("row_logical_starts", c_void_p), ("row_continuations", c_void_p), ("last_used_tick", c_uint64)]

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

class session_attachment_slot(Structure):
    pass
session_attachment_slot._fields_ = [("attachment", c_void_p), ("projector", c_void_p), ("attachment_id", c_uint32), ("pane_id", c_uint32), ("permissions", c_uint32), ("token", (c_char * 64))]

class session_pane_slot(Structure):
    pass
session_pane_slot._fields_ = [("pane", c_void_p), ("pane_id", c_uint32)]

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
vterm_uniforms._fields_ = [("grid_size", (c_float * 2)), ("cell_size", (c_float * 2)), ("scale", c_float), ("baseline_y", c_float), ("glyph_left", c_float), ("pixel_range", c_float), ("root_row", c_uint32), ("cursor_col", c_uint32), ("cursor_row", c_uint32), ("cursor_visible", c_uint32), ("sel_active", c_uint32), ("sel_start_row", c_uint32), ("sel_start_col", c_uint32), ("sel_end_row", c_uint32), ("sel_end_col", c_uint32), ("ring_rows", c_uint32), ("visual_zoom_scale", c_float), ("visual_zoom_offset_x", c_float), ("visual_zoom_offset_y", c_float), ("time", c_float), ("mouse_x", c_float), ("mouse_y", c_float), ("post_fx_index", c_uint32), ("post_fx_p0", c_float), ("post_fx_p1", c_float), ("post_fx_p2", c_float), ("post_fx_p3", c_float), ("post_fx_p4", c_float), ("post_fx_p5", c_float), ("coord_fx_index", c_uint32), ("coord_fx_p0", c_float), ("coord_fx_p1", c_float), ("coord_fx_p2", c_float), ("coord_fx_p3", c_float), ("coord_fx_p4", c_float), ("coord_fx_p5", c_float), ("pad_a", c_uint32), ("pad_b", c_uint32), ("face_methods", c_uint32), ("face_pad0", c_uint32), ("face_pad1", c_uint32), ("face_pad2", c_uint32), ("face_params", ((c_float * 6) * 4))]

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
yetty_ycore_char_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
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

class yetty_ycore_memtag(Structure):
    pass
yetty_ycore_memtag._fields_ = [("name", c_char_p), ("live_bytes", c_void_p), ("peak_bytes", c_void_p), ("total_allocs", c_void_p), ("fail_after", c_void_p)]

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

class yetty_ycore_uint64_result_u1(Union):
    pass
yetty_ycore_uint64_result_u1._fields_ = [("value", c_uint64), ("error", yetty_ycore_error)]
class yetty_ycore_uint64_result(Structure):
    pass
yetty_ycore_uint64_result._anonymous_ = ('_anon1',)
yetty_ycore_uint64_result._fields_ = [("ok", c_int), ("_anon1", yetty_ycore_uint64_result_u1)]

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

class yetty_ygit_blob_ptr_result_u1(Union):
    pass
yetty_ygit_blob_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ygit_blob_ptr_result(Structure):
    pass
yetty_ygit_blob_ptr_result._anonymous_ = ('_anon1',)
yetty_ygit_blob_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ygit_blob_ptr_result_u1)]

class yetty_ygit_branches_ptr_result_u1(Union):
    pass
yetty_ygit_branches_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ygit_branches_ptr_result(Structure):
    pass
yetty_ygit_branches_ptr_result._anonymous_ = ('_anon1',)
yetty_ygit_branches_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ygit_branches_ptr_result_u1)]

class yetty_ygit_commit_detail_ptr_result_u1(Union):
    pass
yetty_ygit_commit_detail_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ygit_commit_detail_ptr_result(Structure):
    pass
yetty_ygit_commit_detail_ptr_result._anonymous_ = ('_anon1',)
yetty_ygit_commit_detail_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ygit_commit_detail_ptr_result_u1)]

class yetty_ygit_diff_ptr_result_u1(Union):
    pass
yetty_ygit_diff_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ygit_diff_ptr_result(Structure):
    pass
yetty_ygit_diff_ptr_result._anonymous_ = ('_anon1',)
yetty_ygit_diff_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ygit_diff_ptr_result_u1)]

class yetty_ygit_log_ptr_result_u1(Union):
    pass
yetty_ygit_log_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ygit_log_ptr_result(Structure):
    pass
yetty_ygit_log_ptr_result._anonymous_ = ('_anon1',)
yetty_ygit_log_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ygit_log_ptr_result_u1)]

class yetty_ygit_status_ptr_result_u1(Union):
    pass
yetty_ygit_status_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ygit_status_ptr_result(Structure):
    pass
yetty_ygit_status_ptr_result._anonymous_ = ('_anon1',)
yetty_ygit_status_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ygit_status_ptr_result_u1)]

class yetty_ygui2_layout(Structure):
    pass
yetty_ygui2_layout._fields_ = [("basis", c_float), ("grow", c_float), ("cross_size", c_float), ("min_main", c_float), ("direction", c_uint32), ("gap", c_float), ("pad_left", c_float), ("pad_top", c_float), ("pad_right", c_float), ("pad_bottom", c_float)]

class yetty_ygui2_theme(Structure):
    pass
yetty_ygui2_theme._fields_ = [("bg", c_uint32), ("bg_lifted", c_uint32), ("bg_row", c_uint32), ("border", c_uint32), ("text_muted", c_uint32), ("text_secondary", c_uint32), ("text_primary", c_uint32), ("accent_deep", c_uint32), ("accent", c_uint32), ("accent_bright", c_uint32)]

class yetty_ygui_input_state(Structure):
    pass
yetty_ygui_input_state._fields_ = [("st", c_int), ("params", (c_char * 16)), ("params_len", c_int)]

class yetty_ygui_layout(Structure):
    pass
yetty_ygui_layout._fields_ = [("direction", c_int), ("justify", c_int), ("align", c_int), ("align_self", c_int), ("wrap", c_int), ("gap", c_float), ("padding_top", c_float), ("padding_right", c_float), ("padding_bottom", c_float), ("padding_left", c_float), ("margin_top", c_float), ("margin_right", c_float), ("margin_bottom", c_float), ("margin_left", c_float), ("width", c_float), ("height", c_float), ("flex_grow", c_float), ("flex_shrink", c_float), ("min_width", c_float), ("max_width", c_float), ("min_height", c_float), ("max_height", c_float), ("absolute", c_int), ("pos_x", c_float), ("pos_y", c_float), ("hidden", c_int)]

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

class yetty_ymux_cell_const_ptr_result_u1(Union):
    pass
yetty_ymux_cell_const_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_ymux_cell_const_ptr_result(Structure):
    pass
yetty_ymux_cell_const_ptr_result._anonymous_ = ('_anon1',)
yetty_ymux_cell_const_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_ymux_cell_const_ptr_result_u1)]

class yetty_ymux_daemon_host(Structure):
    pass
yetty_ymux_daemon_host._fields_ = [("spawn", c_void_p), ("userdata", c_void_p)]

class yetty_ymux_engine_host(Structure):
    pass
yetty_ymux_engine_host._fields_ = [("output", c_void_p), ("clipboard", c_void_p), ("bell", c_void_p), ("title", c_void_p), ("scroll_out", c_void_p), ("rich", c_void_p), ("userdata", c_void_p)]

class yetty_ymux_history_row(Structure):
    pass
yetty_ymux_history_row._fields_ = [("cells", c_void_p), ("cols", c_uint32), ("logical_line_id", c_uint64), ("logical_cell_start", c_uint32), ("continuation", c_int)]

class yetty_ymux_history_row_result_u1(Union):
    pass
yetty_ymux_history_row_result_u1._fields_ = [("value", yetty_ymux_history_row), ("error", yetty_ycore_error)]
class yetty_ymux_history_row_result(Structure):
    pass
yetty_ymux_history_row_result._anonymous_ = ('_anon1',)
yetty_ymux_history_row_result._fields_ = [("ok", c_int), ("_anon1", yetty_ymux_history_row_result_u1)]

class yetty_ymux_resource_store(Structure):
    pass
yetty_ymux_resource_store._fields_ = [("entries", c_void_p), ("count", c_uint32), ("capacity", c_uint32)]

class yetty_ymux_tty_caps(Structure):
    pass
yetty_ymux_tty_caps._fields_ = [("colors_256", c_uint), ("colors_rgb", c_uint), ("ech", c_uint), ("insert_delete_line", c_uint), ("insert_line", c_uint), ("delete_line", c_uint), ("ich", c_uint), ("dch", c_uint), ("decstbm", c_uint), ("bce", c_uint), ("extended_underline", c_uint), ("underline_colour", c_uint), ("hyperlink", c_uint), ("acs", c_uint), ("mouse", c_uint), ("title", c_uint), ("clipboard", c_uint), ("focus", c_uint), ("cursor_style", c_uint), ("cursor_colour", c_uint), ("margins", c_uint), ("overline", c_uint), ("strikethrough", c_uint), ("osc7", c_uint), ("extkeys", c_uint), ("rectfill", c_uint), ("sixel", c_uint), ("sync", c_uint), ("noam", c_uint), ("xenl", c_uint)]

class yetty_ymux_tty(Structure):
    pass
yetty_ymux_tty._fields_ = [("caps", yetty_ymux_tty_caps), ("term", c_void_p), ("cx", c_uint32), ("cy", c_uint32), ("sx", c_uint32), ("sy", c_uint32), ("rupper", c_uint32), ("rlower", c_uint32), ("rleft", c_uint32), ("rright", c_uint32), ("cell_attr", c_uint16), ("active_underline_colour", (c_char * 40)), ("active_link", (c_char * 1025)), ("active_link_id", c_uint32), ("cell_fg", c_int), ("cell_bg", c_int), ("last_attr", c_uint16), ("last_fg", c_int), ("last_bg", c_int), ("cursor_visible", c_int), ("cursor_shape_param", c_int)]

class yetty_ymux_tty_term(Structure):
    pass
yetty_ymux_tty_term._fields_ = [("name", (c_char * 64)), ("strings", (c_void_p * 46)), ("colors", c_int), ("bools", (c_uint8 * 3)), ("loaded_from_db", c_uint), ("bools_loaded", c_uint)]

class yetty_yplatform_gpu_context(Structure):
    pass
yetty_yplatform_gpu_context._fields_ = [("instance", c_void_p), ("surface", c_void_p), ("surface_width", c_uint32), ("surface_height", c_uint32), ("content_scale", c_float), ("x11_display", c_void_p), ("x11_window", c_ulong)]

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
yetty_yrender_buffer._fields_ = [("data", c_void_p), ("size", c_size_t), ("capacity", c_size_t), ("name", (c_char * 64)), ("wgsl_type", (c_char * 64)), ("readonly", c_int), ("dirty", c_int), ("generation", c_uint32)]

class yetty_yrender_shader_code(Structure):
    pass
yetty_yrender_shader_code._fields_ = [("data", c_char_p), ("size", c_size_t), ("hash", c_uint64)]

class yetty_yrender_texture(Structure):
    pass
yetty_yrender_texture._fields_ = [("data", c_void_p), ("width", c_uint32), ("height", c_uint32), ("format", c_uint32), ("name", (c_char * 64)), ("wgsl_type", (c_char * 64)), ("sampler_name", (c_char * 64)), ("sampler_filter", c_uint32), ("dirty", c_int), ("generation", c_uint32)]

class yetty_yrender_uniform_u1(Union):
    pass
yetty_yrender_uniform_u1._fields_ = [("f32", c_float), ("vec2", (c_float * 2)), ("vec3", (c_float * 3)), ("vec4", (c_float * 4)), ("mat4", (c_float * 16)), ("u32", c_uint32), ("i32", c_int32)]
class yetty_yrender_uniform(Structure):
    pass
yetty_yrender_uniform._anonymous_ = ('_anon1',)
yetty_yrender_uniform._fields_ = [("name", (c_char * 64)), ("type", c_int), ("_anon1", yetty_yrender_uniform_u1)]

class yetty_yrender_gpu_resource_set(Structure):
    pass
yetty_yrender_gpu_resource_set._fields_ = [("namespace", (c_char * 64)), ("pixel_size", yetty_ycore_pixel_size), ("textures", (yetty_yrender_texture * 4)), ("texture_count", c_size_t), ("buffers", (yetty_yrender_buffer * 4)), ("buffer_count", c_size_t), ("uniforms", (yetty_yrender_uniform * 64)), ("uniform_count", c_size_t), ("shader", yetty_yrender_shader_code), ("children", (c_void_p * 64)), ("children_count", c_size_t), ("instance_count", c_uint32)]

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

class yetty_yrich_keymap(Structure):
    pass
yetty_yrich_keymap._fields_ = [("bindings", c_void_p), ("count", c_size_t), ("capacity", c_size_t)]

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
yetty_yrich_selection_text._fields_ = [("element_id", c_void_p), ("start", c_int32), ("focus_element_id", c_void_p), ("end", c_int32)]

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

class yetty_yscene_factory_args(Structure):
    pass
yetty_yscene_factory_args._fields_ = [("complex_factory", c_void_p), ("default_font", c_void_p), ("bold_font", c_void_p), ("italic_font", c_void_p), ("bold_italic_font", c_void_p), ("absolute_coords", c_int)]

class yetty_yscene_scene_ptr_result_u1(Union):
    pass
yetty_yscene_scene_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yscene_scene_ptr_result(Structure):
    pass
yetty_yscene_scene_ptr_result._anonymous_ = ('_anon1',)
yetty_yscene_scene_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yscene_scene_ptr_result_u1)]

class yetty_yscene_vtermgrid_store(Structure):
    pass
yetty_yscene_vtermgrid_store._fields_ = [("cells", c_void_p), ("rows", c_uint32), ("cols", c_uint32), ("pen_fg", c_uint32), ("pen_bg", c_uint32), ("pen_attrs", c_uint16), ("pen_protected", c_int), ("default_fg", c_uint32), ("default_bg", c_uint32), ("global_reverse", c_int), ("cursor_row", c_uint32), ("cursor_col", c_uint32), ("cursor_visible", c_int)]

class yetty_yterminal_terminal_context(Structure):
    pass
yetty_yterminal_terminal_context._fields_ = [("yetty_context", yetty_context), ("pty", c_void_p)]

class yetty_yui_rect(Structure):
    pass
yetty_yui_rect._fields_ = [("x", c_float), ("y", c_float), ("w", c_float), ("h", c_float)]

class yetty_yui_view(Structure):
    pass
yetty_yui_view._fields_ = [("ops", c_void_p), ("id", c_void_p), ("bounds", yetty_yui_rect)]

class yetty_yvterm_line(Structure):
    pass
yetty_yvterm_line._fields_ = [("text_cells", c_void_p), ("rich_blocks", c_void_p), ("rich_block_count", c_uint32), ("rich_block_capacity", c_uint32), ("rich_coverage_count", c_uint32), ("view_stamp", c_uint32), ("dirty", c_int), ("continuation", c_int)]

class yetty_yvterm_paint_plan(Structure):
    pass
yetty_yvterm_paint_plan._fields_ = [("leaves", c_void_p), ("leaf_count", c_uint32), ("leaf_capacity", c_uint32), ("built_stamp", c_uint64), ("built", c_int), ("build_count", c_uint64)]

class yetty_yvterm_rich_handle(Structure):
    pass
yetty_yvterm_rich_handle._fields_ = [("slot", c_uint32), ("generation", c_uint32)]

class yetty_yvterm_rich_store(Structure):
    pass
yetty_yvterm_rich_store._fields_ = [("blocks", c_void_p), ("block_capacity", c_uint32), ("free_slots", c_void_p), ("free_count", c_uint32), ("free_capacity", c_uint32), ("next_paint_sequence", c_uint64), ("live_count", c_uint32), ("ambient_paint_z", (c_int32 * 8)), ("ambient_paint_z_depth", c_uint32), ("ambient_paint_z_overflow", c_uint32), ("paint_generation", (c_uint64 * 3)), ("journal_bytes_used", c_size_t), ("journal_bytes_budget", c_size_t)]

class yetty_yvterm_screen(Structure):
    pass
yetty_yvterm_screen._fields_ = [("lines", c_void_p), ("line_count", c_uint32), ("base", c_uint32), ("total_scrolled", c_uint64)]

class yetty_yvterm_text_cell_const_ptr_result_u1(Union):
    pass
yetty_yvterm_text_cell_const_ptr_result_u1._fields_ = [("value", c_void_p), ("error", yetty_ycore_error)]
class yetty_yvterm_text_cell_const_ptr_result(Structure):
    pass
yetty_yvterm_text_cell_const_ptr_result._anonymous_ = ('_anon1',)
yetty_yvterm_text_cell_const_ptr_result._fields_ = [("ok", c_int), ("_anon1", yetty_yvterm_text_cell_const_ptr_result_u1)]

class yetty_yvterm_tier_builder(Structure):
    pass
yetty_yvterm_tier_builder._fields_ = [("bytes", c_void_p), ("byte_count", c_size_t), ("byte_capacity", c_size_t), ("line_offsets", c_void_p), ("line_count", c_uint32), ("line_capacity", c_uint32), ("first_line", c_uint64)]

class yetty_yvterm_tier_cache_entry(Structure):
    pass
yetty_yvterm_tier_cache_entry._fields_ = [("valid", c_int), ("zombie", c_int), ("pin_stamp", c_uint32), ("first_line", c_uint64), ("line_count", c_uint32), ("lines", c_void_p), ("last_used_tick", c_uint64)]

class yetty_yvterm_tiers(Structure):
    pass
yetty_yvterm_tiers._fields_ = [("segments", c_void_p), ("segment_head", c_uint32), ("segment_count", c_uint32), ("segment_capacity", c_uint32), ("builder", yetty_yvterm_tier_builder), ("archived_lines", c_uint64), ("dropped_lines", c_uint64), ("warm_bytes_used", c_size_t), ("warm_bytes_budget", c_size_t), ("file_bytes_budget", c_uint64), ("total_line_cap", c_uint64), ("spill_file", c_void_p), ("spill_file_size", c_uint64), ("spill_disabled", c_int), ("rich_clear_watermark", c_uint64), ("cache", (yetty_yvterm_tier_cache_entry * 4)), ("cache_tick", c_uint64), ("memtag", c_void_p), ("live_pin_stamp", c_uint32), ("rich_store", c_void_p)]

class ymusic_staff(Structure):
    pass
ymusic_staff._fields_ = [("clef", c_int), ("key_fifths", c_int), ("time_num", c_int), ("time_den", c_int), ("measures", c_void_p), ("count", c_size_t), ("cap", c_size_t)]

class yscene_cell(Structure):
    pass
yscene_cell._fields_ = [("indices", c_void_p), ("count", c_uint32), ("capacity", c_uint32)]

class yvterm_font_face(Structure):
    pass
yvterm_font_face._fields_ = [("font", c_void_p), ("method", c_int), ("name", (c_char * 64)), ("meta_buffer", c_void_p), ("meta_capacity", c_size_t), ("atlas_texture", c_void_p), ("atlas_view", c_void_p), ("atlas_width", c_uint32), ("atlas_height", c_uint32), ("atlas_format", c_uint32), ("bytes_per_pixel", c_uint32)]

class yvterm_font_range(Structure):
    pass
yvterm_font_range._fields_ = [("from", c_uint32), ("to", c_uint32), ("face", c_uint32)]
