-- Foundational + shared ABI types — GENERATED, do not edit.
local ffi = require("ffi")
ffi.cdef[[
typedef struct _IO_FILE FILE;
typedef long WGPUBuffer;
typedef long WGPUInstance;
typedef long WGPUSurface;
typedef long WGPUTexture;
typedef long WGPUTextureView;
typedef long _Atomic(int64_t);
typedef long __syscall_slong_t;
typedef long __time_t;
typedef long yetty_ipc_socket_t;
typedef long yetty_ycore_event_handler;
typedef long yetty_ycore_object_id;
typedef long yetty_ymux_daemon_spawn_fn;
typedef long yetty_ymux_engine_bell_fn;
typedef long yetty_ymux_engine_clipboard_fn;
typedef long yetty_ymux_engine_output_fn;
typedef long yetty_ymux_engine_rich_fn;
typedef long yetty_ymux_engine_scroll_out_fn;
typedef long yetty_ymux_engine_title_fn;
typedef long yetty_yrich_element_id;
struct engine_row;
struct yetty_platform_pty;
struct yetty_yclass;
struct yetty_yclass_object;
struct yetty_yclass_rpc_session;
struct yetty_yclass_transport;
struct yetty_yconfig_config;
struct yetty_ycore_xthread_event_pipe;
struct yetty_ydraw_complex_factory;
struct yetty_ydraw_drawable_list;
struct yetty_ydraw_drawable_list_registry;
struct yetty_ydraw_target;
struct yetty_yevent_event_loop;
struct yetty_yfigure_figure;
struct yetty_yfont_font;
struct yetty_yfont_ms_font;
struct yetty_yframework;
struct yetty_ygit_blob;
struct yetty_ygit_branches;
struct yetty_ygit_commit_detail;
struct yetty_ygit_diff;
struct yetty_ygit_log;
struct yetty_ygit_status;
struct yetty_ygui_emit_ctx;
struct yetty_ymgui_figure;
struct yetty_ymux_cell;
struct yetty_ymux_daemon;
struct yetty_ymux_resource_entry;
struct yetty_yplatform_pty_factory;
struct yetty_yrdawn_figure;
struct yetty_yrich_command;
struct yetty_yrich_keybinding;
struct yetty_yrich_operation;
struct yetty_yrich_slide;
struct yetty_yscene_scene;
struct yetty_yscene_vtermgrid_store_cell;
struct yetty_yui_event;
struct yetty_yui_view_ops;
struct yetty_yvterm_paint_leaf;
struct yetty_yvterm_rich_block;
struct yetty_yvterm_text_cell;
struct yetty_yvterm_tier_segment;
struct yetty_ywire_channel;
struct yetty_ywire_connection;
struct yetty_ywire_wire_statemachine;
struct ymusic_measure;
struct client_resource {
  uint64_t hash;
  uint32_t *words;
  uint32_t word_count;
};
struct daemon_connection {
  yetty_ipc_socket_t socket;
  struct yetty_yclass_object *session;
  uint32_t attachment_id;
  uint32_t pane_id;
  uint32_t capabilities;
  uint64_t sent_generation;
  uint64_t acked_generation;
  uint32_t fail_next_vtsink_tx;
  struct {
    uint32_t input_class;
    uint32_t byte_len;
    uint8_t *bytes;
  } chrome_queue;
  uint32_t chrome_queue_head;
  uint32_t chrome_queue_count;
  uint64_t chrome_intake_count;
  uint32_t chrome_intake_class;
  uint32_t overlay_applied_seq;
  uint32_t refuse_next_overlay;
  uint8_t copy_key_pending[16];
  uint32_t copy_key_pending_len;
  int copy_cursor_row;
  int copy_cursor_col;
  int copy_anchor_row;
  int copy_anchor_col;
  int copy_selecting;
  uint8_t *chrome_last_bytes;
  uint32_t chrome_last_len;
  uint32_t chrome_last_class;
  uint32_t sent_pane_modes;
  int sent_pane_modes_valid;
  struct yetty_yclass_rpc_session *vtsink_session;
  struct yetty_yclass_transport *vtsink_lane;
  struct yetty_yclass_object *vtsink_proxy;
  uint8_t *rx;
  size_t rx_len;
  uint8_t *tx;
  size_t tx_len;
  size_t tx_sent;
  uint64_t tx_total_sent;
  uint64_t slow_last_total_sent;
  uint32_t slow_recover_count;
  int want_close;
};
struct daemon_pane_pty {
  struct yetty_platform_pty *pty;
  struct yetty_ymux_daemon *daemon;
  struct yetty_yclass_object *session;
  uint32_t pane_id;
  uint32_t pty_rows;
  uint32_t pty_cols;
  uint8_t *out_queue;
  size_t out_queue_len;
  size_t out_queue_cap;
  struct yetty_ywire_wire_statemachine *rpc_sm;
  struct yetty_ywire_connection *rpc_connection;
  void *rpc_forward_state;
  uint32_t rpc_controller;
  struct {
    uint32_t channel_id;
    struct yetty_ywire_channel *channel;
  } rpc_channels;
  uint32_t rpc_channel_count;
};
struct daemon_session_entry {
  struct yetty_yclass_object *session;
  char name[64];
  uint64_t created_stamp;
};
struct engine_surface {
  struct engine_row *rows;
  uint32_t rows_count;
  uint32_t cols;
};
struct yetty_ycore_error {
  const char *msg;
  const char *file;
  const char *func;
  int line;
  struct yetty_ycore_error *cause;
};
struct float_result {
  int ok;
  union {
    float value;
    struct yetty_ycore_error error;
  };
};
struct history_builder {
  uint8_t *bytes;
  size_t byte_count;
  size_t byte_capacity;
  uint32_t *row_offsets;
  uint32_t row_count;
  uint32_t row_capacity;
  uint64_t first_row;
};
struct history_cache_entry {
  int valid;
  uint64_t first_row;
  uint32_t row_count;
  struct yetty_ymux_cell **row_cells;
  uint32_t *row_cols;
  uint64_t *row_logical_ids;
  uint32_t *row_logical_starts;
  uint8_t *row_continuations;
  uint64_t last_used_tick;
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
struct yetty_ycore_pixel_coord {
  float x;
  float y;
};
struct yetty_ycore_rectangle {
  struct yetty_ycore_pixel_coord min;
  struct yetty_ycore_pixel_coord max;
};
struct rectangle_result {
  int ok;
  union {
    struct yetty_ycore_rectangle value;
    struct yetty_ycore_error error;
  };
};
struct session_attachment_slot {
  struct yetty_yclass_object *attachment;
  struct yetty_yclass_object *projector;
  uint32_t attachment_id;
  uint32_t pane_id;
  uint32_t permissions;
  char token[64];
};
struct session_pane_slot {
  struct yetty_yclass_object *pane;
  uint32_t pane_id;
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
  uint32_t face_methods;
  uint32_t face_pad0;
  uint32_t face_pad1;
  uint32_t face_pad2;
  float face_params[6][4];
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
struct yetty_ycore_memtag {
  const char *name;
  int64_t live_bytes;
  int64_t peak_bytes;
  int64_t total_allocs;
  int64_t fail_after;
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
struct yetty_ycore_uint64_result {
  int ok;
  union {
    uint64_t value;
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
struct yetty_ygit_blob_ptr_result {
  int ok;
  union {
    struct yetty_ygit_blob *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygit_branches_ptr_result {
  int ok;
  union {
    struct yetty_ygit_branches *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygit_commit_detail_ptr_result {
  int ok;
  union {
    struct yetty_ygit_commit_detail *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygit_diff_ptr_result {
  int ok;
  union {
    struct yetty_ygit_diff *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygit_log_ptr_result {
  int ok;
  union {
    struct yetty_ygit_log *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygit_status_ptr_result {
  int ok;
  union {
    struct yetty_ygit_status *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ygui2_layout {
  float basis;
  float grow;
  float cross_size;
  float min_main;
  uint32_t direction;
  float gap;
  float pad_left;
  float pad_top;
  float pad_right;
  float pad_bottom;
};
struct yetty_ygui2_theme {
  uint32_t bg;
  uint32_t bg_lifted;
  uint32_t bg_row;
  uint32_t border;
  uint32_t text_muted;
  uint32_t text_secondary;
  uint32_t text_primary;
  uint32_t accent_deep;
  uint32_t accent;
  uint32_t accent_bright;
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
  int align_self;
  int wrap;
  float gap;
  float padding_top;
  float padding_right;
  float padding_bottom;
  float padding_left;
  float margin_top;
  float margin_right;
  float margin_bottom;
  float margin_left;
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
struct yetty_ymux_cell_const_ptr_result {
  int ok;
  union {
    const struct yetty_ymux_cell *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ymux_daemon_host {
  yetty_ymux_daemon_spawn_fn spawn;
  void *userdata;
};
struct yetty_ymux_engine_host {
  yetty_ymux_engine_output_fn output;
  yetty_ymux_engine_clipboard_fn clipboard;
  yetty_ymux_engine_bell_fn bell;
  yetty_ymux_engine_title_fn title;
  yetty_ymux_engine_scroll_out_fn scroll_out;
  yetty_ymux_engine_rich_fn rich;
  void *userdata;
};
struct yetty_ymux_history_row {
  const struct yetty_ymux_cell *cells;
  uint32_t cols;
  uint64_t logical_line_id;
  uint32_t logical_cell_start;
  int continuation;
};
struct yetty_ymux_history_row_result {
  int ok;
  union {
    struct yetty_ymux_history_row value;
    struct yetty_ycore_error error;
  };
};
struct yetty_ymux_resource_store {
  struct yetty_ymux_resource_entry *entries;
  uint32_t count;
  uint32_t capacity;
};
struct yetty_ymux_tty_caps {
  unsigned int colors_256;
  unsigned int colors_rgb;
  unsigned int ech;
  unsigned int insert_delete_line;
  unsigned int insert_line;
  unsigned int delete_line;
  unsigned int ich;
  unsigned int dch;
  unsigned int decstbm;
  unsigned int bce;
  unsigned int extended_underline;
  unsigned int underline_colour;
  unsigned int hyperlink;
  unsigned int acs;
  unsigned int mouse;
  unsigned int title;
  unsigned int clipboard;
  unsigned int focus;
  unsigned int cursor_style;
  unsigned int cursor_colour;
  unsigned int margins;
  unsigned int overline;
  unsigned int strikethrough;
  unsigned int osc7;
  unsigned int extkeys;
  unsigned int rectfill;
  unsigned int sixel;
  unsigned int sync;
  unsigned int noam;
  unsigned int xenl;
};
struct yetty_ymux_tty {
  struct yetty_ymux_tty_caps caps;
  const struct yetty_ymux_tty_term *term;
  uint32_t cx;
  uint32_t cy;
  uint32_t sx;
  uint32_t sy;
  uint32_t rupper;
  uint32_t rlower;
  uint32_t rleft;
  uint32_t rright;
  uint16_t cell_attr;
  char active_underline_colour[40];
  char active_link[1025];
  uint32_t active_link_id;
  int cell_fg;
  int cell_bg;
  uint16_t last_attr;
  int last_fg;
  int last_bg;
  int cursor_visible;
  int cursor_shape_param;
};
struct yetty_ymux_tty_term {
  char name[64];
  char *strings[46];
  int colors;
  uint8_t bools[3];
  unsigned int loaded_from_db;
  unsigned int bools_loaded;
};
struct yetty_yplatform_gpu_context {
  WGPUInstance instance;
  WGPUSurface surface;
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
  uint32_t generation;
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
  uint32_t generation;
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
  struct yetty_yrender_uniform uniforms[64];
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
struct yetty_yrich_keymap {
  struct yetty_yrich_keybinding *bindings;
  size_t count;
  size_t capacity;
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
  yetty_yrich_element_id focus_element_id;
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
struct yetty_yscene_factory_args {
  struct yetty_ydraw_complex_factory *complex_factory;
  struct yetty_yfont_font *default_font;
  struct yetty_yfont_font *bold_font;
  struct yetty_yfont_font *italic_font;
  struct yetty_yfont_font *bold_italic_font;
  int absolute_coords;
};
struct yetty_yscene_scene_ptr_result {
  int ok;
  union {
    struct yetty_yscene_scene *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yscene_vtermgrid_store {
  struct yetty_yscene_vtermgrid_store_cell *cells;
  uint32_t rows;
  uint32_t cols;
  uint32_t pen_fg;
  uint32_t pen_bg;
  uint16_t pen_attrs;
  int pen_protected;
  uint32_t default_fg;
  uint32_t default_bg;
  int global_reverse;
  uint32_t cursor_row;
  uint32_t cursor_col;
  int cursor_visible;
};
struct yetty_yterminal_terminal_context {
  struct yetty_context yetty_context;
  struct yetty_platform_pty *pty;
};
struct yetty_yui_rect {
  float x;
  float y;
  float w;
  float h;
};
struct yetty_yui_view {
  const struct yetty_yui_view_ops *ops;
  yetty_ycore_object_id id;
  struct yetty_yui_rect bounds;
};
struct yetty_yvterm_line {
  struct yetty_yvterm_text_cell *text_cells;
  struct yetty_yvterm_rich_handle *rich_blocks;
  uint32_t rich_block_count;
  uint32_t rich_block_capacity;
  uint32_t rich_coverage_count;
  uint32_t view_stamp;
  int dirty;
  int continuation;
};
struct yetty_yvterm_paint_plan {
  struct yetty_yvterm_paint_leaf *leaves;
  uint32_t leaf_count;
  uint32_t leaf_capacity;
  uint64_t built_stamp;
  int built;
  uint64_t build_count;
};
struct yetty_yvterm_rich_handle {
  uint32_t slot;
  uint32_t generation;
};
struct yetty_yvterm_rich_store {
  struct yetty_yvterm_rich_block *blocks;
  uint32_t block_capacity;
  uint32_t *free_slots;
  uint32_t free_count;
  uint32_t free_capacity;
  uint64_t next_paint_sequence;
  uint32_t live_count;
  int32_t ambient_paint_z[8];
  uint32_t ambient_paint_z_depth;
  uint32_t ambient_paint_z_overflow;
  uint64_t paint_generation[3];
  size_t journal_bytes_used;
  size_t journal_bytes_budget;
};
struct yetty_yvterm_screen {
  struct yetty_yvterm_line *lines;
  uint32_t line_count;
  uint32_t base;
  uint64_t total_scrolled;
};
struct yetty_yvterm_text_cell_const_ptr_result {
  int ok;
  union {
    const struct yetty_yvterm_text_cell *value;
    struct yetty_ycore_error error;
  };
};
struct yetty_yvterm_tier_builder {
  uint8_t *bytes;
  size_t byte_count;
  size_t byte_capacity;
  uint32_t *line_offsets;
  uint32_t line_count;
  uint32_t line_capacity;
  uint64_t first_line;
};
struct yetty_yvterm_tier_cache_entry {
  int valid;
  int zombie;
  uint32_t pin_stamp;
  uint64_t first_line;
  uint32_t line_count;
  struct yetty_yvterm_line *lines;
  uint64_t last_used_tick;
};
struct yetty_yvterm_tiers {
  struct yetty_yvterm_tier_segment *segments;
  uint32_t segment_head;
  uint32_t segment_count;
  uint32_t segment_capacity;
  struct yetty_yvterm_tier_builder builder;
  uint64_t archived_lines;
  uint64_t dropped_lines;
  size_t warm_bytes_used;
  size_t warm_bytes_budget;
  uint64_t file_bytes_budget;
  uint64_t total_line_cap;
  FILE *spill_file;
  uint64_t spill_file_size;
  int spill_disabled;
  uint64_t rich_clear_watermark;
  struct yetty_yvterm_tier_cache_entry cache[4];
  uint64_t cache_tick;
  struct yetty_ycore_memtag *memtag;
  uint32_t live_pin_stamp;
  struct yetty_yvterm_rich_store *rich_store;
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
struct yscene_cell {
  uint32_t *indices;
  uint32_t count;
  uint32_t capacity;
};
struct yvterm_font_face {
  struct yetty_yfont_ms_font *font;
  int method;
  char name[64];
  WGPUBuffer meta_buffer;
  size_t meta_capacity;
  WGPUTexture atlas_texture;
  WGPUTextureView atlas_view;
  uint32_t atlas_width;
  uint32_t atlas_height;
  uint32_t atlas_format;
  uint32_t bytes_per_pixel;
};
struct yvterm_font_range {
  uint32_t from;
  uint32_t to;
  uint32_t face;
};
]]
local M = {}
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
M.yetty_ygui_flex_align_self = {
  YETTY_YGUI_ALIGN_SELF_AUTO = 0,
  YETTY_YGUI_ALIGN_SELF_START = 1,
  YETTY_YGUI_ALIGN_SELF_CENTER = 2,
  YETTY_YGUI_ALIGN_SELF_END = 3,
  YETTY_YGUI_ALIGN_SELF_STRETCH = 4,
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
M.yetty_ygui_flex_wrap = {
  YETTY_YGUI_WRAP_NOWRAP = 0,
  YETTY_YGUI_WRAP_WRAP = 1,
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
M.yetty_ymux_key = {
  YETTY_YMUX_KEY_ENTER = 1,
  YETTY_YMUX_KEY_TAB = 2,
  YETTY_YMUX_KEY_BACKSPACE = 3,
  YETTY_YMUX_KEY_ESCAPE = 4,
  YETTY_YMUX_KEY_UP = 5,
  YETTY_YMUX_KEY_DOWN = 6,
  YETTY_YMUX_KEY_LEFT = 7,
  YETTY_YMUX_KEY_RIGHT = 8,
  YETTY_YMUX_KEY_INSERT = 9,
  YETTY_YMUX_KEY_DELETE = 10,
  YETTY_YMUX_KEY_HOME = 11,
  YETTY_YMUX_KEY_END = 12,
  YETTY_YMUX_KEY_PAGE_UP = 13,
  YETTY_YMUX_KEY_PAGE_DOWN = 14,
  YETTY_YMUX_KEY_KP_0 = 15,
  YETTY_YMUX_KEY_KP_1 = 16,
  YETTY_YMUX_KEY_KP_2 = 17,
  YETTY_YMUX_KEY_KP_3 = 18,
  YETTY_YMUX_KEY_KP_4 = 19,
  YETTY_YMUX_KEY_KP_5 = 20,
  YETTY_YMUX_KEY_KP_6 = 21,
  YETTY_YMUX_KEY_KP_7 = 22,
  YETTY_YMUX_KEY_KP_8 = 23,
  YETTY_YMUX_KEY_KP_9 = 24,
  YETTY_YMUX_KEY_KP_MULT = 25,
  YETTY_YMUX_KEY_KP_PLUS = 26,
  YETTY_YMUX_KEY_KP_COMMA = 27,
  YETTY_YMUX_KEY_KP_MINUS = 28,
  YETTY_YMUX_KEY_KP_PERIOD = 29,
  YETTY_YMUX_KEY_KP_DIVIDE = 30,
  YETTY_YMUX_KEY_KP_ENTER = 31,
  YETTY_YMUX_KEY_KP_EQUAL = 32,
  YETTY_YMUX_KEY_F1 = 33,
  YETTY_YMUX_KEY_F2 = 34,
  YETTY_YMUX_KEY_F3 = 35,
  YETTY_YMUX_KEY_F4 = 36,
  YETTY_YMUX_KEY_F5 = 37,
  YETTY_YMUX_KEY_F6 = 38,
  YETTY_YMUX_KEY_F7 = 39,
  YETTY_YMUX_KEY_F8 = 40,
  YETTY_YMUX_KEY_F9 = 41,
  YETTY_YMUX_KEY_F10 = 42,
  YETTY_YMUX_KEY_F11 = 43,
  YETTY_YMUX_KEY_F12 = 44,
}
M.yetty_yrich_app_kind = {
  YETTY_YRICH_APP_YDOC = 0,
  YETTY_YRICH_APP_YSHEET = 1,
  YETTY_YRICH_APP_YSLIDE = 2,
}
M.yetty_yrich_edit_mode = {
  YETTY_YRICH_MODE_DEFAULT = 0,
  YETTY_YRICH_MODE_VI_NORMAL = 1,
  YETTY_YRICH_MODE_VI_INSERT = 2,
  YETTY_YRICH_MODE_COUNT = 3,
}
M.yetty_yrender_uniform_type = {
  YETTY_YRENDER_UNIFORM_F32 = 0,
  YETTY_YRENDER_UNIFORM_VEC2 = 1,
  YETTY_YRENDER_UNIFORM_VEC3 = 2,
  YETTY_YRENDER_UNIFORM_VEC4 = 3,
  YETTY_YRENDER_UNIFORM_MAT4 = 4,
  YETTY_YRENDER_UNIFORM_U32 = 5,
  YETTY_YRENDER_UNIFORM_I32 = 6,
}
M.yvterm_font_method = {
  YVTERM_FONT_METHOD_MSDF = 0,
  YVTERM_FONT_METHOD_RASTER = 1,
  YVTERM_FONT_METHOD_RASTER_COLOR = 2,
}
return M
