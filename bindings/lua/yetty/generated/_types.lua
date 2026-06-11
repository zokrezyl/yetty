-- Foundational + shared ABI types — GENERATED, do not edit.
local ffi = require("ffi")
ffi.cdef[[
typedef long __syscall_slong_t;
typedef long __time_t;
typedef long yetty_ycore_event_handler;
struct yetty_yclass_ctx;
struct yetty_yclass_object;
struct yetty_ycore_xthread_event_pipe;
struct yetty_ydraw_drawable_list;
struct yetty_ydraw_target;
struct yetty_yfigure_figure;
struct yetty_ygui_emit_ctx;
struct yetty_ygui_event_subscription;
struct yetty_ygui_framework;
struct yetty_ymgui_figure;
struct yetty_yui_event;
struct yetty_ywire_wire_statemachine;
struct ymusic_measure;
struct timespec {
  __time_t tv_sec;
  __syscall_slong_t tv_nsec;
};
struct yetty_ycore_error {
  const char *msg;
  const char *file;
  const char *func;
  int line;
  struct yetty_ycore_error *cause;
};
struct uint32_result {
  int ok;
  union {
    uint32_t value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yclass_object_ptr_result {
  int ok;
  union {
    struct yetty_yclass_object *value;
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
struct yetty_ycore_pixel_size {
  float width;
  float height;
};
struct yetty_ycore_rectangle {
  struct yetty_ycore_pixel_coord min;
  struct yetty_ycore_pixel_coord max;
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
struct yetty_ygui_object_ptr_result {
  int ok;
  union {
    struct yetty_yclass_object *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygui_tree {
  struct yetty_yclass_object *parent;
  struct yetty_yclass_object *first_child;
  struct yetty_yclass_object *next_sibling;
  uint32_t id;
  uint32_t figure_kind;
  int32_t figure_z;
  int floating;
  int dirty;
  int hovered;
  struct yetty_ygui_framework *framework;
  struct yetty_ygui_event_subscription *subscriptions;
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
struct yetty_yrich_input_mods {
  bool shift;
  bool ctrl;
  bool alt;
  bool meta;
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
return M
