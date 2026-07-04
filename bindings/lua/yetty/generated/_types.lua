-- Foundational + shared ABI types — GENERATED, do not edit.
local ffi = require("ffi")
ffi.cdef[[
typedef long __syscall_slong_t;
typedef long __time_t;
typedef long yetty_ycore_event_handler;
typedef long yetty_yrich_element_id;
struct yetty_yclass;
struct yetty_yclass_object;
struct yetty_yclass_rpc_session;
struct yetty_yconfig_config;
struct yetty_ycore_xthread_event_pipe;
struct yetty_ydraw_composite;
struct yetty_ydraw_composite_factory;
struct yetty_ydraw_drawable_list;
struct yetty_ydraw_target;
struct yetty_yevent_event_loop;
struct yetty_yfigure_figure;
struct yetty_yfont_font;
struct yetty_yframework;
struct yetty_ygui_emit_ctx;
struct yetty_ymgui_figure;
struct yetty_yplatform_pty_factory;
struct yetty_yrdawn_figure;
struct yetty_yrich_command;
struct yetty_yrich_operation;
struct yetty_yrich_slide;
struct yetty_yui_event;
struct yetty_yvterm_text_cell;
struct yetty_ywire_wire_statemachine;
struct ymusic_measure;
struct yetty_ycore_error {
  const char *msg;
  const char *file;
  const char *func;
  int line;
  struct yetty_ycore_error *cause;
};
struct yetty_ycore_pixel_size {
  float width;
  float height;
};
struct pixel_size_result {
  int ok;
  union {
    struct yetty_ycore_pixel_size value;
    struct yetty_ycore_error error;
  };
};
struct timespec {
  __time_t tv_sec;
  __syscall_slong_t tv_nsec;
};
struct uint32_result {
  int ok;
  union {
    uint32_t value;
    struct yetty_ycore_error error;
  };
};
struct vterm_uniforms {
  float grid_size[2];
  float cell_size[2];
  float scale;
  float baseline_y;
  float glyph_left;
  float pixel_range;
  uint32_t root_row;
  uint32_t cursor_col;
  uint32_t cursor_row;
  uint32_t cursor_visible;
  uint32_t sel_active;
  uint32_t sel_start_row;
  uint32_t sel_start_col;
  uint32_t sel_end_row;
  uint32_t sel_end_col;
  uint32_t ring_rows;
  float visual_zoom_scale;
  float visual_zoom_offset_x;
  float visual_zoom_offset_y;
  float time;
  float mouse_x;
  float mouse_y;
  uint32_t post_fx_index;
  float post_fx_p0;
  float post_fx_p1;
  float post_fx_p2;
  float post_fx_p3;
  float post_fx_p4;
  float post_fx_p5;
  uint32_t coord_fx_index;
  float coord_fx_p0;
  float coord_fx_p1;
  float coord_fx_p2;
  float coord_fx_p3;
  float coord_fx_p4;
  float coord_fx_p5;
  uint32_t pad_a;
  uint32_t pad_b;
};
struct yetty_context {
  struct yetty_yframework *runtime;
  struct yetty_yplatform_pty_factory *pty_factory;
  struct yetty_yevent_event_loop *event_loop;
};
struct yetty_yclass_ctx {
  struct yetty_yclass_rpc_session *session;
};
struct yetty_yclass_object_ptr_result {
  int ok;
  union {
    struct yetty_yclass_object *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yclass_ptr_result {
  int ok;
  union {
    const struct yetty_yclass *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yclass_void_ptr_result {
  int ok;
  union {
    void *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yconfig_config_ptr_result {
  int ok;
  union {
    struct yetty_yconfig_config *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_buffer {
  uint8_t *data;
  size_t capacity;
  size_t size;
};
struct yetty_ycore_char_ptr_result {
  int ok;
  union {
    char *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_const_char_ptr_result {
  int ok;
  union {
    const char *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_const_uint32_ptr_result {
  int ok;
  union {
    const uint32_t *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_const_uint8_ptr_result {
  int ok;
  union {
    const uint8_t *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_float_result {
  int ok;
  union {
    float value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_grid_size {
  uint32_t rows;
  uint32_t cols;
};
struct yetty_ycore_int_result {
  int ok;
  union {
    int value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_pixel_coord {
  float x;
  float y;
};
struct yetty_ycore_rectangle {
  struct yetty_ycore_pixel_coord min;
  struct yetty_ycore_pixel_coord max;
};
struct yetty_ycore_rectangle_result {
  int ok;
  union {
    struct yetty_ycore_rectangle value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_rgba {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};
struct yetty_ycore_size_result {
  int ok;
  union {
    size_t value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_uint32_result {
  int ok;
  union {
    uint32_t value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_void_result {
  int ok;
  union {
    int value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ycore_xthread_event_pipe_ptr_result {
  int ok;
  union {
    struct yetty_ycore_xthread_event_pipe *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ydraw_composite_const_ptr_ptr_result {
  int ok;
  union {
    struct yetty_ydraw_composite *const *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ydraw_drawable_list_result {
  int ok;
  union {
    struct yetty_ydraw_drawable_list *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yevent_event_listener {
  yetty_ycore_event_handler handler;
};
struct yetty_yfigure_figure_ptr_result {
  int ok;
  union {
    struct yetty_yfigure_figure *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yfigure_hit {
  uint32_t figure_id;
  float local_x;
  float local_y;
};
struct yetty_yfigure_hit_result {
  int ok;
  union {
    struct yetty_yfigure_hit value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygrid_factory_args {
  struct yetty_yfont_font *default_font;
  struct yetty_ydraw_composite_factory *composite_factory;
  int absolute_coords;
};
struct yetty_ygui_input_state {
  int st;
  char params[16];
  int params_len;
};
struct yetty_ygui_layout {
  int direction;
  int justify;
  int align;
  float gap;
  float padding_top;
  float padding_right;
  float padding_bottom;
  float padding_left;
  float width;
  float height;
  float flex_grow;
  float flex_shrink;
  float min_width;
  float max_width;
  float min_height;
  float max_height;
  int absolute;
  float pos_x;
  float pos_y;
  int hidden;
};
struct yetty_ygui_layout_const_ptr_result {
  int ok;
  union {
    const struct yetty_ygui_layout *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygui_yplot_config {
  float x_min;
  float x_max;
  float y_min;
  float y_max;
  uint32_t flags;
};
struct yetty_ymgui_figure_ptr_result {
  int ok;
  union {
    struct yetty_ymgui_figure *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yplatform_gpu_context {
  int instance;
  int surface;
  uint32_t surface_width;
  uint32_t surface_height;
  float content_scale;
  void *x11_display;
  unsigned long x11_window;
};
struct yetty_yplatform_gpu_context_const_ptr_result {
  int ok;
  union {
    const struct yetty_yplatform_gpu_context *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrdawn_figure_ptr_result {
  int ok;
  union {
    struct yetty_yrdawn_figure *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrender_buffer {
  uint8_t *data;
  size_t size;
  size_t capacity;
  char name[64];
  char wgsl_type[64];
  int readonly;
  int dirty;
};
struct yetty_yrender_shader_code {
  const char *data;
  size_t size;
  uint64_t hash;
};
struct yetty_yrender_texture {
  uint8_t *data;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  char name[64];
  char wgsl_type[64];
  char sampler_name[64];
  uint32_t sampler_filter;
  int dirty;
};
struct yetty_yrender_uniform {
  char name[64];
  int type;
  union {
    float f32;
    float vec2[2];
    float vec3[3];
    float vec4[4];
    float mat4[16];
    uint32_t u32;
    int32_t i32;
  };
};
struct yetty_yrender_gpu_resource_set {
  char namespace[64];
  struct yetty_ycore_pixel_size pixel_size;
  struct yetty_yrender_texture textures[4];
  size_t texture_count;
  struct yetty_yrender_buffer buffers[4];
  size_t buffer_count;
  struct yetty_yrender_uniform uniforms[32];
  size_t uniform_count;
  struct yetty_yrender_shader_code shader;
  struct yetty_yrender_gpu_resource_set *children[64];
  size_t children_count;
  uint32_t instance_count;
};
struct yetty_yrich_border {
  float width;
  uint32_t color;
  uint32_t style;
};
struct yetty_yrich_cell_addr {
  int32_t row;
  int32_t col;
};
struct yetty_yrich_cell_addr_result {
  int ok;
  union {
    struct yetty_yrich_cell_addr value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrich_cell_range {
  struct yetty_yrich_cell_addr start;
  struct yetty_yrich_cell_addr end;
};
struct yetty_yrich_drawable_list_ptr_result {
  int ok;
  union {
    struct yetty_ydraw_drawable_list *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrich_element_id_result {
  int ok;
  union {
    yetty_yrich_element_id value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrich_history {
  struct yetty_yrich_command **undo_stack;
  size_t undo_count;
  size_t undo_capacity;
  struct yetty_yrich_command **redo_stack;
  size_t redo_count;
  size_t redo_capacity;
  size_t max_size;
};
struct yetty_yrich_op_log {
  struct yetty_yrich_operation **ops;
  size_t count;
  size_t capacity;
  uint64_t current_ts;
};
struct yetty_yrich_operation_ptr_result {
  int ok;
  union {
    struct yetty_yrich_operation *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrich_rect {
  float x;
  float y;
  float w;
  float h;
};
struct yetty_yrich_selection_cells {
  struct yetty_yrich_cell_range range;
  struct yetty_yrich_cell_addr active;
};
struct yetty_yrich_selection_elements {
  yetty_yrich_element_id *ids;
  size_t count;
  size_t capacity;
};
struct yetty_yrich_selection_text {
  yetty_yrich_element_id element_id;
  int32_t start;
  int32_t end;
};
struct yetty_yrich_selection {
  uint32_t kind;
  union {
    struct yetty_yrich_selection_elements elements;
    struct yetty_yrich_selection_cells cells;
    struct yetty_yrich_selection_text text;
  } u;
};
struct yetty_yrich_selection_ptr_result {
  int ok;
  union {
    struct yetty_yrich_selection *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrich_slide_ptr_result {
  int ok;
  union {
    struct yetty_yrich_slide *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yrich_text_style {
  float font_size;
  uint32_t color;
  uint32_t bg_color;
  uint32_t format;
  int32_t font_id;
};
struct yetty_yvterm_text_cell_const_ptr_result {
  int ok;
  union {
    const struct yetty_yvterm_text_cell *value;
    struct yetty_ycore_error error;
  };
};
struct ymusic_staff {
  int clef;
  int key_fifths;
  int time_num;
  int time_den;
  struct ymusic_measure **measures;
  size_t count;
  size_t cap;
};
]]
local M = {}
M.yetty_yrender_uniform_type = {
  YETTY_YRENDER_UNIFORM_F32 = 0,
  YETTY_YRENDER_UNIFORM_VEC2 = 1,
  YETTY_YRENDER_UNIFORM_VEC3 = 2,
  YETTY_YRENDER_UNIFORM_VEC4 = 3,
  YETTY_YRENDER_UNIFORM_MAT4 = 4,
  YETTY_YRENDER_UNIFORM_U32 = 5,
  YETTY_YRENDER_UNIFORM_I32 = 6,
}
M.yetty_ygui_csi_state = {
  YETTY_YGUI_CSI_NORMAL = 0,
  YETTY_YGUI_CSI_ESC = 1,
  YETTY_YGUI_CSI_BRACKET = 2,
}
M.yetty_ygui_flex_align = {
  YETTY_YGUI_ALIGN_START = 0,
  YETTY_YGUI_ALIGN_CENTER = 1,
  YETTY_YGUI_ALIGN_END = 2,
  YETTY_YGUI_ALIGN_STRETCH = 3,
}
M.yetty_ygui_flex_direction = {
  YETTY_YGUI_FLEX_ROW = 0,
  YETTY_YGUI_FLEX_COLUMN = 1,
}
M.yetty_ygui_flex_justify = {
  YETTY_YGUI_JUSTIFY_START = 0,
  YETTY_YGUI_JUSTIFY_CENTER = 1,
  YETTY_YGUI_JUSTIFY_END = 2,
  YETTY_YGUI_JUSTIFY_SPACE_BETWEEN = 3,
}
M.ynode_drag_mode = {
  YNODE_DRAG_NONE = 0,
  YNODE_DRAG_MOVE = 1,
  YNODE_DRAG_LINK = 2,
  YNODE_DRAG_RESIZE = 3,
}
M.ymusic_clef = {
  YMUSIC_CLEF_TREBLE = 0,
  YMUSIC_CLEF_BASS = 1,
  YMUSIC_CLEF_ALTO = 2,
  YMUSIC_CLEF_TENOR = 3,
}
M.yetty_yrich_app_kind = {
  YETTY_YRICH_APP_YDOC = 0,
  YETTY_YRICH_APP_YSHEET = 1,
  YETTY_YRICH_APP_YSLIDE = 2,
}
return M
