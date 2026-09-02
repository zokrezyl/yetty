-- yetty.ygui2 — Lua (LuaJIT) wrapper over the ygui2 drawable-contract
-- toolkit. HAND-WRITTEN (the generator never touches this) — the Lua port
-- of bindings/python/yetty/ygui2.py, same layer map:
--
--   * the exposed C API (yetty_ygui2_framework_make, widget_add, the
--     per-widget setters) is cdef'd here directly;
--   * this module is the ergonomic layer: `App` (the terminal host loop:
--     raw termios + alternate screen + poll loop + HOLD/ACK teardown),
--     `Node` builders (column:button{label = ..., on_click = fn}), and
--     callback trampolines.
--
-- A ygui2 app is a plain PTY client — run it INSIDE a yetty pane:
--
--     LUA_PATH="bindings/lua/?.lua;;" luajit demo/ffi/ygui2/lua/counter.lua
--
-- Ownership and liveness: the native tree is owned by the framework.
-- Every Node wrapper is liveness-tracked; node:remove() invalidates the
-- wrapper subtree (and frees its callbacks) after the native subtree is
-- destroyed, and app:close() invalidates everything. A call on a dead
-- node raises instead of touching freed memory. Callback errors are
-- captured at the FFI boundary (an uncaught Lua error may not cross into
-- C), stop the app, and re-raise from run().
--
-- Colors are packed 0xAABBGGRR (the SDF fill word) or "#RRGGBB[AA]"
-- strings — color() converts.
--
-- Quit keys: Ctrl-C always quits (the host runs with ISIG off and handles
-- byte 0x03 itself). `q` quits while no text input holds focus.

local ffi = require("ffi")
local bit = require("bit")
local rt = require("yetty.runtime")
require("yetty.generated._types")

ffi.cdef[[
typedef int (*yetty_ygui2_key_cb)(uint32_t, uint32_t, void *);
typedef void (*yetty_ygui2_click_cb)(struct yetty_yclass_object *, void *);
typedef void (*yetty_ygui2_select_cb)(struct yetty_yclass_object *, uint32_t, void *);
typedef void (*yetty_ygui2_sink_fn)(const uint8_t *, size_t, void *);

struct yetty_yclass_object_ptr_result yetty_ygui2_framework_make(void);
struct yetty_ycore_void_result yetty_ygui2_framework_dispose(struct yetty_yclass_object *);
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_root_create(
    struct yetty_yclass_object *, const struct yetty_yclass *);
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_overlay_add(
    struct yetty_yclass_object *, const struct yetty_yclass *);
struct yetty_yclass_object_ptr_result yetty_ygui2_widget_add(struct yetty_yclass_object *,
                                                             const struct yetty_yclass *);
struct yetty_yclass_object_ptr_result yetty_ygui2_row_add(struct yetty_yclass_object *);
struct yetty_yclass_object_ptr_result yetty_ygui2_column_add(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_framework_set_sink(struct yetty_yclass_object *,
                                                              yetty_ygui2_sink_fn, void *);
struct yetty_ycore_void_result yetty_ygui2_framework_set_viewport(struct yetty_yclass_object *,
                                                                  float, float);
struct yetty_ycore_void_result yetty_ygui2_framework_set_fullscreen(struct yetty_yclass_object *,
                                                                    int);
struct yetty_ycore_void_result yetty_ygui2_framework_content_scale(struct yetty_yclass_object *,
                                                                   float *);
struct yetty_ycore_void_result yetty_ygui2_framework_set_key_cb(struct yetty_yclass_object *,
                                                                yetty_ygui2_key_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_framework_attach(struct yetty_yclass_object *, int, int);
struct yetty_ycore_void_result yetty_ygui2_framework_send_hold(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ygui2_framework_hold_ack_seen(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_framework_detach(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_framework_clear(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_input(struct yetty_yclass_object *,
                                                                const uint8_t *, size_t);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_input_flush(struct yetty_yclass_object *);
struct yetty_ycore_int_result yetty_ygui2_framework_is_dirty(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_button(
    struct yetty_yclass_object *, float, float, int, int, int);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_motion(
    struct yetty_yclass_object *, float, float, uint32_t);
struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_scroll(
    struct yetty_yclass_object *, float, float, float);
struct yetty_ycore_void_result yetty_ygui2_framework_emit(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ygui2_widget_layout_set(struct yetty_yclass_object *,
                                                             const struct yetty_ygui2_layout *);
struct yetty_ycore_void_result yetty_ygui2_widget_layout_copy(struct yetty_yclass_object *,
                                                              struct yetty_ygui2_layout *);
struct yetty_ycore_void_result yetty_ygui2_widget_rect(struct yetty_yclass_object *, float *,
                                                       float *, float *, float *);
struct yetty_ycore_void_result yetty_ygui2_widget_set_focusable(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_ygui2_widget_set_visible(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_ygui2_widget_set_position(struct yetty_yclass_object *, float,
                                                               float);
struct yetty_ycore_void_result yetty_ygui2_widget_set_size(struct yetty_yclass_object *, float,
                                                           float);
struct yetty_ycore_void_result yetty_ygui2_widget_remove(struct yetty_yclass_object *);

struct yetty_yclass_ptr_result yetty_ygui2_panel_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_label_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_separator_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_button_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_checkbox_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_toggle_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_radio_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_slider_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_spinner_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_progress_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_chip_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_statusbar_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_stepper_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_textinput_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_dropdown_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_scrollarea_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_table_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_dialog_class_get(void);
struct yetty_yclass_ptr_result yetty_ygui2_tooltip_class_get(void);

struct yetty_ycore_void_result yetty_ygui2_panel_set_bg(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_ygui2_panel_set_border(struct yetty_yclass_object *, uint32_t,
                                                            float);
struct yetty_ycore_void_result yetty_ygui2_label_set_text(struct yetty_yclass_object *,
                                                          const char *);
struct yetty_ycore_void_result yetty_ygui2_label_set_color(struct yetty_yclass_object *, uint32_t);
struct yetty_ycore_void_result yetty_ygui2_separator_set_color(struct yetty_yclass_object *,
                                                               uint32_t);
struct yetty_ycore_void_result yetty_ygui2_button_set_label(struct yetty_yclass_object *,
                                                            const char *);
struct yetty_ycore_void_result yetty_ygui2_button_on_click_set(struct yetty_yclass_object *,
                                                               yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_checkbox_set_label(struct yetty_yclass_object *,
                                                              const char *);
struct yetty_ycore_void_result yetty_ygui2_checkbox_set_checked(struct yetty_yclass_object *, int);
struct yetty_ycore_int_result yetty_ygui2_checkbox_checked(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_checkbox_on_toggle_set(struct yetty_yclass_object *,
                                                                  yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_toggle_set_label(struct yetty_yclass_object *,
                                                            const char *);
struct yetty_ycore_void_result yetty_ygui2_toggle_set_checked(struct yetty_yclass_object *, int);
struct yetty_ycore_int_result yetty_ygui2_toggle_checked(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_toggle_on_toggle_set(struct yetty_yclass_object *,
                                                                yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_radio_set_label(struct yetty_yclass_object *,
                                                           const char *);
struct yetty_ycore_void_result yetty_ygui2_radio_set_selected(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_ygui2_radio_on_select_set(struct yetty_yclass_object *,
                                                               yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_slider_set_range(struct yetty_yclass_object *, float,
                                                            float);
struct yetty_ycore_void_result yetty_ygui2_slider_set_value(struct yetty_yclass_object *, float);
struct yetty_ycore_float_result yetty_ygui2_slider_value(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_slider_on_change_set(struct yetty_yclass_object *,
                                                                yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_spinner_configure(struct yetty_yclass_object *, float,
                                                             float, float);
struct yetty_ycore_void_result yetty_ygui2_spinner_set_value(struct yetty_yclass_object *, float);
struct yetty_ycore_float_result yetty_ygui2_spinner_value(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_spinner_on_change_set(struct yetty_yclass_object *,
                                                                 yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_progress_set_value(struct yetty_yclass_object *, float);
struct yetty_ycore_void_result yetty_ygui2_progress_set_accent(struct yetty_yclass_object *,
                                                               uint32_t);
struct yetty_ycore_void_result yetty_ygui2_chip_set_label(struct yetty_yclass_object *,
                                                          const char *);
struct yetty_ycore_void_result yetty_ygui2_chip_set_selectable(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_ygui2_chip_set_selected(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_ygui2_chip_on_toggle_set(struct yetty_yclass_object *,
                                                              yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_statusbar_set_left(struct yetty_yclass_object *,
                                                              const char *);
struct yetty_ycore_void_result yetty_ygui2_statusbar_set_right(struct yetty_yclass_object *,
                                                               const char *);
struct yetty_ycore_void_result yetty_ygui2_stepper_set_count(struct yetty_yclass_object *,
                                                             uint32_t);
struct yetty_ycore_void_result yetty_ygui2_stepper_set_current(struct yetty_yclass_object *,
                                                               uint32_t);
struct yetty_ycore_void_result yetty_ygui2_textinput_set_text(struct yetty_yclass_object *,
                                                              const char *);
struct yetty_ycore_void_result yetty_ygui2_textinput_set_placeholder(struct yetty_yclass_object *,
                                                                     const char *);
struct yetty_ycore_size_result yetty_ygui2_textinput_text_copy(struct yetty_yclass_object *,
                                                               char *, size_t);
struct yetty_ycore_void_result yetty_ygui2_textinput_on_submit_set(struct yetty_yclass_object *,
                                                                   yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_textinput_on_change_set(struct yetty_yclass_object *,
                                                                   yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_dropdown_item_add(struct yetty_yclass_object *,
                                                             const char *);
struct yetty_ycore_void_result yetty_ygui2_dropdown_set_selected(struct yetty_yclass_object *, int);
struct yetty_ycore_void_result yetty_ygui2_dropdown_on_change_set(struct yetty_yclass_object *,
                                                                  yetty_ygui2_select_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_scrollarea_configure(struct yetty_yclass_object *, float,
                                                                float);
struct yetty_ycore_void_result yetty_ygui2_table_set_columns(struct yetty_yclass_object *,
                                                             const char *const *, const float *,
                                                             uint32_t);
struct yetty_ycore_void_result yetty_ygui2_table_clear_rows(struct yetty_yclass_object *);
struct yetty_ycore_void_result yetty_ygui2_table_add_row(struct yetty_yclass_object *,
                                                         const char *const *, uint32_t);
struct yetty_ycore_void_result yetty_ygui2_dialog_set_title(struct yetty_yclass_object *,
                                                            const char *);
struct yetty_ycore_void_result yetty_ygui2_dialog_on_close_set(struct yetty_yclass_object *,
                                                               yetty_ygui2_click_cb, void *);
struct yetty_ycore_void_result yetty_ygui2_tooltip_set_text(struct yetty_yclass_object *,
                                                            const char *);

/* Host-loop OS glue (Linux ABI; renamed tags avoid clashing cdefs). */
struct yetty_luahost_winsize {
  unsigned short ws_row;
  unsigned short ws_col;
  unsigned short ws_xpixel;
  unsigned short ws_ypixel;
};
int ioctl(int, unsigned long, ...);
struct yetty_luahost_termios {
  unsigned int c_iflag;
  unsigned int c_oflag;
  unsigned int c_cflag;
  unsigned int c_lflag;
  unsigned char c_line;
  unsigned char c_cc[32];
  unsigned int c_ispeed;
  unsigned int c_ospeed;
};
int tcgetattr(int, struct yetty_luahost_termios *);
int tcsetattr(int, int, const struct yetty_luahost_termios *);
struct pollfd {
  int fd;
  short events;
  short revents;
};
int poll(struct pollfd *, unsigned long, int);
long read(int, void *, size_t);
long write(int, const void *, size_t);
struct yetty_luahost_timespec {
  long tv_sec;
  long tv_nsec;
};
int clock_gettime(int, struct yetty_luahost_timespec *);
long sysconf(int);
]]

local osc = ffi.C

-- Linux constant values (asm-generic): the host loop is Linux-only, like
-- the /proc-reading demos it drives.
local TCSANOW = 0
local TIOCGWINSZ = 0x5413
local CLOCK_MONOTONIC = 1
local POLLIN = 1
local IFLAG_CLEAR = bit.bor(0x100, 0x40, 0x400, 0x1000, 0x2, 0x10, 0x20)
-- ICRNL | INLCR | IXON | IXOFF | BRKINT | INPCK | ISTRIP
local LFLAG_CLEAR = bit.bor(0x2, 0x8, 0x1, 0x8000) -- ICANON | ECHO | ISIG | IEXTEN

local M = {}

--- Packed 0xAABBGGRR from a number (passed through) or "#RRGGBB[AA]".
function M.color(value)
  if type(value) == "number" then
    return value
  end
  local text = value:gsub("^#", "")
  local red = tonumber(text:sub(1, 2), 16)
  local green = tonumber(text:sub(3, 4), 16)
  local blue = tonumber(text:sub(5, 6), 16)
  local alpha = #text >= 8 and tonumber(text:sub(7, 8), 16) or 0xFF
  return bit.bor(bit.lshift(alpha, 24), bit.lshift(blue, 16), bit.lshift(green, 8), red)
end

local function check(res)
  rt.check(res)
  return res.value
end

local function class_ptr(getter_name)
  return check(rt.C()[getter_name]())
end

local function now_seconds()
  local time_point = ffi.new("struct yetty_luahost_timespec")
  osc.clock_gettime(CLOCK_MONOTONIC, time_point)
  return tonumber(time_point.tv_sec) + tonumber(time_point.tv_nsec) * 1e-9
end

local function write_stdout(text)
  osc.write(1, text, #text)
end

-- Layout kwargs accepted by every builder; `pad` fans out to all four
-- sides, `cross` is shorthand for cross_size. Partial specs read-modify-
-- write the widget's layout so C-side defaults (row/column direction)
-- survive.
local LAYOUT_KEYS = {
  basis = true, grow = true, cross = true, cross_size = true, min_main = true,
  gap = true, pad = true, pad_left = true, pad_top = true, pad_right = true,
  pad_bottom = true,
}

local function apply_layout(handle, layout_spec)
  local spec = ffi.new("struct yetty_ygui2_layout")
  check(rt.C().yetty_ygui2_widget_layout_copy(handle, spec))
  if layout_spec.pad then
    spec.pad_left = layout_spec.pad
    spec.pad_top = layout_spec.pad
    spec.pad_right = layout_spec.pad
    spec.pad_bottom = layout_spec.pad
  end
  if layout_spec.cross then
    spec.cross_size = layout_spec.cross
  end
  for key, value in pairs(layout_spec) do
    if key ~= "pad" and key ~= "cross" then
      spec[key] = value
    end
  end
  check(rt.C().yetty_ygui2_widget_layout_set(handle, spec))
end

-- Split a builder's option table into layout options and widget options,
-- rejecting typos loudly (an ignored misspelled option is a silent bug).
local function split_options(options, known, where)
  local layout_spec = {}
  local widget_spec = {}
  local have_layout = false
  for key, value in pairs(options or {}) do
    if LAYOUT_KEYS[key] then
      layout_spec[key] = value
      have_layout = true
    elseif known[key] then
      widget_spec[key] = value
    else
      error(where .. ": unknown option '" .. tostring(key) .. "'", 3)
    end
  end
  return have_layout and layout_spec or nil, widget_spec
end

local Node = {}
Node.__index = Node

local function node_new(app, handle, parent)
  local node = setmetatable({
    app = app,
    handle = handle,
    parent = parent,
    children = {},
    callbacks = {},
    invalidate_hooks = {},
    alive = true,
  }, Node)
  if parent then
    parent.children[#parent.children + 1] = node
  end
  return node
end

function Node:live()
  if not self.alive or not self.app.alive then
    error("ygui2 node is dead (removed or app closed)", 3)
  end
  return self.handle
end

function Node:invalidate()
  for _, child in ipairs(self.children) do
    child:invalidate()
  end
  self.children = {}
  for _, callback in ipairs(self.callbacks) do
    callback:free()
  end
  self.callbacks = {}
  for _, hook in ipairs(self.invalidate_hooks) do
    hook()
  end
  self.invalidate_hooks = {}
  self.alive = false
  self.handle = nil
end

-- A builder step failed AFTER the native child existed: remove the native
-- side first (matching remove() ordering), then drop the wrapper.
local function configured(parent, node, configure)
  local ok, builder_error = pcall(configure, node)
  if ok then
    return node
  end
  if node.handle ~= nil then
    rt.C().yetty_ygui2_widget_remove(node.handle)
  end
  for index, child in ipairs(parent.children) do
    if child == node then
      table.remove(parent.children, index)
      break
    end
  end
  node:invalidate()
  error(builder_error, 0)
end

local function child_node(parent, class_getter, layout_spec)
  local handle = check(rt.C().yetty_ygui2_widget_add(parent:live(), class_ptr(class_getter)))
  local node = node_new(parent.app, handle, parent)
  return configured(parent, node, function(fresh)
    if layout_spec then
      apply_layout(fresh.handle, layout_spec)
    end
  end)
end

-- -- tree building -----------------------------------------------------

function Node:row(options)
  local layout_spec = select(1, split_options(options, {}, "row"))
  local handle = check(rt.C().yetty_ygui2_row_add(self:live()))
  local node = node_new(self.app, handle, self)
  return configured(self, node, function(fresh)
    if layout_spec then
      apply_layout(fresh.handle, layout_spec)
    end
  end)
end

function Node:column(options)
  local layout_spec = select(1, split_options(options, {}, "column"))
  local handle = check(rt.C().yetty_ygui2_column_add(self:live()))
  local node = node_new(self.app, handle, self)
  return configured(self, node, function(fresh)
    if layout_spec then
      apply_layout(fresh.handle, layout_spec)
    end
  end)
end

function Node:panel(options)
  local layout_spec, opts = split_options(options, { bg = true, border = true,
                                                     border_width = true }, "panel")
  local node = child_node(self, "yetty_ygui2_panel_class_get", layout_spec)
  return configured(self, node, function(fresh)
    if opts.bg then
      check(rt.C().yetty_ygui2_panel_set_bg(fresh.handle, M.color(opts.bg)))
    end
    if opts.border then
      check(rt.C().yetty_ygui2_panel_set_border(fresh.handle, M.color(opts.border),
                                                opts.border_width or 1.0))
    end
  end)
end

function Node:label(options)
  local layout_spec, opts = split_options(options, { text = true, fg = true }, "label")
  local node = child_node(self, "yetty_ygui2_label_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_label_set_text(fresh.handle, opts.text or ""))
    if opts.fg then
      check(rt.C().yetty_ygui2_label_set_color(fresh.handle, M.color(opts.fg)))
    end
  end)
end

function Node:separator(options)
  local layout_spec = select(1, split_options(options, {}, "separator"))
  return child_node(self, "yetty_ygui2_separator_class_get", layout_spec)
end

function Node:button(options)
  local layout_spec, opts = split_options(options, { label = true, on_click = true }, "button")
  local node = child_node(self, "yetty_ygui2_button_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_button_set_label(fresh.handle, opts.label or ""))
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.on_click then
      check(rt.C().yetty_ygui2_button_on_click_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_click), nil))
    end
  end)
end

function Node:checkbox(options)
  local layout_spec, opts = split_options(options, { label = true, checked = true,
                                                     on_toggle = true }, "checkbox")
  local node = child_node(self, "yetty_ygui2_checkbox_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_checkbox_set_label(fresh.handle, opts.label or ""))
    if opts.checked then
      check(rt.C().yetty_ygui2_checkbox_set_checked(fresh.handle, 1))
    end
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.on_toggle then
      check(rt.C().yetty_ygui2_checkbox_on_toggle_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_toggle), nil))
    end
  end)
end

function Node:toggle(options)
  local layout_spec, opts = split_options(options, { label = true, checked = true,
                                                     on_toggle = true }, "toggle")
  local node = child_node(self, "yetty_ygui2_toggle_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_toggle_set_label(fresh.handle, opts.label or ""))
    if opts.checked then
      check(rt.C().yetty_ygui2_toggle_set_checked(fresh.handle, 1))
    end
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.on_toggle then
      check(rt.C().yetty_ygui2_toggle_on_toggle_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_toggle), nil))
    end
  end)
end

function Node:radio(options)
  local layout_spec, opts = split_options(options, { label = true, group = true, selected = true,
                                                     on_select = true }, "radio")
  local node = child_node(self, "yetty_ygui2_radio_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_radio_set_label(fresh.handle, opts.label or ""))
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.selected then
      check(rt.C().yetty_ygui2_radio_set_selected(fresh.handle, 1))
    end
    if opts.group then
      opts.group:register(fresh, opts.on_select)
    elseif opts.on_select then
      check(rt.C().yetty_ygui2_radio_on_select_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_select), nil))
    end
  end)
end

function Node:slider(options)
  local layout_spec, opts = split_options(options, { value = true, minimum = true, maximum = true,
                                                     on_change = true }, "slider")
  local node = child_node(self, "yetty_ygui2_slider_class_get", layout_spec)
  return configured(self, node, function(fresh)
    local minimum = opts.minimum or 0.0
    local maximum = opts.maximum or 1.0
    if minimum ~= 0.0 or maximum ~= 1.0 then
      check(rt.C().yetty_ygui2_slider_set_range(fresh.handle, minimum, maximum))
    end
    check(rt.C().yetty_ygui2_slider_set_value(fresh.handle, opts.value or 0.0))
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.on_change then
      check(rt.C().yetty_ygui2_slider_on_change_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_change), nil))
    end
  end)
end

function Node:spinner(options)
  local layout_spec, opts = split_options(options, { value = true, minimum = true, maximum = true,
                                                     step = true, on_change = true }, "spinner")
  local node = child_node(self, "yetty_ygui2_spinner_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_spinner_configure(fresh.handle, opts.minimum or 0.0,
                                               opts.maximum or 100.0, opts.step or 1.0))
    check(rt.C().yetty_ygui2_spinner_set_value(fresh.handle, opts.value or 0.0))
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.on_change then
      check(rt.C().yetty_ygui2_spinner_on_change_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_change), nil))
    end
  end)
end

function Node:progress(options)
  local layout_spec, opts = split_options(options, { value = true, accent = true }, "progress")
  local node = child_node(self, "yetty_ygui2_progress_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_progress_set_value(fresh.handle, opts.value or 0.0))
    if opts.accent then
      check(rt.C().yetty_ygui2_progress_set_accent(fresh.handle, M.color(opts.accent)))
    end
  end)
end

function Node:chip(options)
  local layout_spec, opts = split_options(options, { label = true, selectable = true,
                                                     selected = true, on_toggle = true }, "chip")
  local node = child_node(self, "yetty_ygui2_chip_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_chip_set_label(fresh.handle, opts.label or ""))
    if opts.selectable then
      check(rt.C().yetty_ygui2_chip_set_selectable(fresh.handle, 1))
    end
    if opts.selected then
      check(rt.C().yetty_ygui2_chip_set_selected(fresh.handle, 1))
    end
    if opts.on_toggle then
      check(rt.C().yetty_ygui2_chip_on_toggle_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_toggle), nil))
    end
  end)
end

function Node:statusbar(options)
  local layout_spec, opts = split_options(options, { left = true, right = true }, "statusbar")
  local node = child_node(self, "yetty_ygui2_statusbar_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_statusbar_set_left(fresh.handle, opts.left or ""))
    check(rt.C().yetty_ygui2_statusbar_set_right(fresh.handle, opts.right or ""))
  end)
end

function Node:stepper(options)
  local layout_spec, opts = split_options(options, { count = true, current = true }, "stepper")
  local node = child_node(self, "yetty_ygui2_stepper_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_stepper_set_count(fresh.handle, opts.count or 3))
    check(rt.C().yetty_ygui2_stepper_set_current(fresh.handle, opts.current or 0))
  end)
end

function Node:textinput(options)
  local layout_spec, opts = split_options(options, { text = true, placeholder = true,
                                                     on_submit = true, on_change = true },
                                          "textinput")
  local node = child_node(self, "yetty_ygui2_textinput_class_get", layout_spec)
  return configured(self, node, function(fresh)
    if opts.text and #opts.text > 0 then
      check(rt.C().yetty_ygui2_textinput_set_text(fresh.handle, opts.text))
    end
    if opts.placeholder and #opts.placeholder > 0 then
      check(rt.C().yetty_ygui2_textinput_set_placeholder(fresh.handle, opts.placeholder))
    end
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.on_submit then
      check(rt.C().yetty_ygui2_textinput_on_submit_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_submit), nil))
    end
    if opts.on_change then
      check(rt.C().yetty_ygui2_textinput_on_change_set(
          fresh.handle, self.app:click_trampoline(fresh, opts.on_change), nil))
    end
  end)
end

function Node:dropdown(options)
  local layout_spec, opts = split_options(options, { items = true, selected = true,
                                                     on_change = true }, "dropdown")
  local node = child_node(self, "yetty_ygui2_dropdown_class_get", layout_spec)
  return configured(self, node, function(fresh)
    for _, item in ipairs(opts.items or {}) do
      check(rt.C().yetty_ygui2_dropdown_item_add(fresh.handle, item))
    end
    if opts.selected and opts.selected >= 0 then
      check(rt.C().yetty_ygui2_dropdown_set_selected(fresh.handle, opts.selected))
    end
    check(rt.C().yetty_ygui2_widget_set_focusable(fresh.handle, 1))
    if opts.on_change then
      check(rt.C().yetty_ygui2_dropdown_on_change_set(
          fresh.handle, self.app:select_trampoline(fresh, opts.on_change), nil))
    end
  end)
end

function Node:scrollarea(options)
  local layout_spec, opts = split_options(options, { wheel_step = true, max_scroll = true },
                                          "scrollarea")
  local node = child_node(self, "yetty_ygui2_scrollarea_class_get", layout_spec)
  return configured(self, node, function(fresh)
    check(rt.C().yetty_ygui2_scrollarea_configure(fresh.handle, opts.wheel_step or 24.0,
                                                  opts.max_scroll or 1000.0))
  end)
end

function Node:table(options)
  local layout_spec, opts = split_options(options, { columns = true, widths = true }, "table")
  local node = child_node(self, "yetty_ygui2_table_class_get", layout_spec)
  return configured(self, node, function(fresh)
    if opts.columns and #opts.columns > 0 then
      fresh:set_columns(opts.columns, opts.widths)
    end
  end)
end

-- -- common setters ----------------------------------------------------

function Node:layout(options)
  local layout_spec = select(1, split_options(options, {}, "layout"))
  if layout_spec then
    apply_layout(self:live(), layout_spec)
  end
  return self
end

function Node:set_position(position_x, position_y)
  check(rt.C().yetty_ygui2_widget_set_position(self:live(), position_x, position_y))
  return self
end

function Node:set_size(width, height)
  check(rt.C().yetty_ygui2_widget_set_size(self:live(), width, height))
  return self
end

function Node:set_visible(visible)
  check(rt.C().yetty_ygui2_widget_set_visible(self:live(), visible and 1 or 0))
  return self
end

function Node:rect()
  local out = ffi.new("float[4]")
  check(rt.C().yetty_ygui2_widget_rect(self:live(), out, out + 1, out + 2, out + 3))
  return out[0], out[1], out[2], out[3]
end

--- Remove this widget (and its whole subtree) from the live tree. The
--- native removal runs first — if it rejects (e.g. a root), the wrapper
--- tree stays fully usable.
function Node:remove()
  local handle = self:live()
  if not self.parent then
    error("ygui2: cannot remove a root node", 2)
  end
  check(rt.C().yetty_ygui2_widget_remove(handle))
  for index, child in ipairs(self.parent.children) do
    if child == self then
      table.remove(self.parent.children, index)
      break
    end
  end
  self:invalidate()
end

-- -- per-widget accessors (the C side rejects class mismatches) ---------

function Node:set_text(text)
  check(rt.C().yetty_ygui2_label_set_text(self:live(), text))
  return self
end

function Node:set_value(value)
  check(rt.C().yetty_ygui2_progress_set_value(self:live(), value))
  return self
end

function Node:slider_value()
  return check(rt.C().yetty_ygui2_slider_value(self:live()))
end

function Node:spinner_value()
  return check(rt.C().yetty_ygui2_spinner_value(self:live()))
end

function Node:checkbox_checked()
  return check(rt.C().yetty_ygui2_checkbox_checked(self:live())) ~= 0
end

function Node:toggle_checked()
  return check(rt.C().yetty_ygui2_toggle_checked(self:live())) ~= 0
end

function Node:input_text()
  local buffer = ffi.new("char[256]")
  check(rt.C().yetty_ygui2_textinput_text_copy(self:live(), buffer, 256))
  return ffi.string(buffer)
end

function Node:status(options)
  if options.left then
    check(rt.C().yetty_ygui2_statusbar_set_left(self:live(), options.left))
  end
  if options.right then
    check(rt.C().yetty_ygui2_statusbar_set_right(self:live(), options.right))
  end
  return self
end

function Node:stepper_current(current)
  check(rt.C().yetty_ygui2_stepper_set_current(self:live(), current))
  return self
end

function Node:set_columns(columns, widths)
  local count = #columns
  local header_array = ffi.new("const char *[?]", count)
  local width_array = ffi.new("float[?]", count)
  for index = 1, count do
    header_array[index - 1] = columns[index]
    width_array[index - 1] = widths and widths[index] or 0.0
  end
  -- `columns` (a live Lua table of the strings) anchors the char*
  -- conversions for the duration of the call.
  check(rt.C().yetty_ygui2_table_set_columns(self:live(), header_array, width_array, count))
  return self
end

function Node:clear_rows()
  check(rt.C().yetty_ygui2_table_clear_rows(self:live()))
  return self
end

function Node:add_row(cells)
  local count = #cells
  local cell_array = ffi.new("const char *[?]", count)
  for index = 1, count do
    cell_array[index - 1] = cells[index]
  end
  check(rt.C().yetty_ygui2_table_add_row(self:live(), cell_array, count))
  return self
end

-- -- RadioGroup ---------------------------------------------------------
-- App-side radio-group semantics: selecting one clears the others (the C
-- widget is deliberately dumb about groups). A removed member's slot is
-- tombstoned, so a dynamic list retains nothing for dead radios.

local RadioGroup = {}
RadioGroup.__index = RadioGroup

function RadioGroup.new()
  return setmetatable({ members = {} }, RadioGroup)
end

function RadioGroup:register(node, on_select)
  local index = #self.members + 1
  self.members[index] = { node = node, on_select = on_select }
  node.invalidate_hooks[#node.invalidate_hooks + 1] = function()
    self.members[index] = { node = false, on_select = false }
  end
  local group = self
  check(rt.C().yetty_ygui2_radio_on_select_set(
      node.handle,
      node.app:click_trampoline(node, function()
        for member_index, member in ipairs(group.members) do
          if member_index ~= index and member.node and member.node.alive then
            check(rt.C().yetty_ygui2_radio_set_selected(member.node.handle, 0))
          end
        end
        local callback = group.members[index].on_select
        if callback then
          callback(index - 1) -- zero-based, matching the other bindings
        end
      end), nil))
end

M.RadioGroup = setmetatable(RadioGroup, { __call = RadioGroup.new })

-- -- App -----------------------------------------------------------------

local App = {}
App.__index = App

--- The terminal host (Lua port of yguiapp2): raw termios, alternate
--- screen, pane-input subscription, poll loop with idle Esc flush,
--- monotonic ticks, emit-on-dirty, HOLD/ACK barrier + unsubscribe on
--- every exit path. Ctrl-C always quits; `q` quits while no text input
--- holds focus. Options: `fullscreen = false` selects the inline
--- reservation mode (strategy.md §5) before the first emit.
function App.new(options)
  if not rt.has_symbol("yetty_ygui2_framework_make") then
    error("the loaded libyetty_ffi.so does not export the ygui2 toolkit — " ..
          "rebuild it (USE_DISTCC=1 make build-desktop-ytrace-release) or " ..
          "point YETTY_FFI_LIB at a current build")
  end
  options = options or {}
  local app = setmetatable({
    running = false,
    alive = false,
    callback_error = nil,
    in_callback = 0,
    close_pending = false,
    app_callbacks = {},
  }, App)
  app.framework = check(rt.C().yetty_ygui2_framework_make())
  app.alive = true
  local ok, construction_error = pcall(function()
    if options.fullscreen == false then
      app:set_fullscreen(false)
    end
    app.viewport_w, app.viewport_h = App.probe_viewport()
    local root_handle = check(rt.C().yetty_ygui2_framework_root_create(
        app.framework, class_ptr("yetty_ygui2_panel_class_get")))
    check(rt.C().yetty_ygui2_panel_set_bg(root_handle, M.color("#0B1014")))
    app.root = node_new(app, root_handle, nil)
  end)
  if not ok then
    pcall(App.close, app) -- cleanup must not mask the construction error
    error(construction_error, 0)
  end
  return app
end

--- Reservation mode (strategy.md §5): fullscreen (default) reserves the
--- full supported viewport range; inline (`false`) reserves the declared
--- viewport height and lives in the scrollback flow. Must be chosen
--- BEFORE the first emit — the C side rejects the call once inserted.
function App:set_fullscreen(fullscreen)
  check(rt.C().yetty_ygui2_framework_set_fullscreen(self.framework, fullscreen and 1 or 0))
end

--- The committed HiDPI input divisor (1.0 until a pane-resize envelope
--- carries a different scale).
function App:content_scale()
  local out_scale = ffi.new("float[1]")
  check(rt.C().yetty_ygui2_framework_content_scale(self.framework, out_scale))
  return out_scale[0]
end

--- Pane pixels from TIOCGWINSZ — exact ws_xpixel/ws_ypixel when the
--- terminal reports them, cell estimate as fallback. The forwarded RESIZE
--- envelope corrects either way.
function App.probe_viewport()
  local size = ffi.new("struct yetty_luahost_winsize")
  if osc.ioctl(1, TIOCGWINSZ, size) == 0 and size.ws_xpixel > 0 and size.ws_ypixel > 0 then
    return size.ws_xpixel + 0.0, size.ws_ypixel + 0.0
  end
  local cols = size.ws_col > 0 and size.ws_col or 80
  local rows = size.ws_row > 0 and size.ws_row or 40
  return cols * 8.0, rows * 16.0
end

-- Callback trampolines. FFI callback objects are rooted on the node that
-- owns them (freed with the node). Errors may not cross the FFI boundary
-- (LuaJIT panics on an uncaught callback error): they are captured here,
-- stop the app, and re-raise from run().
function App:guard(callback, ...)
  self.in_callback = self.in_callback + 1
  local ok, guard_error = pcall(callback, ...)
  self.in_callback = self.in_callback - 1
  if not ok then
    if self.callback_error == nil then
      self.callback_error = guard_error
    end
    self.running = false
  end
end

function App:click_trampoline(node, callback)
  local app = self
  local bridge = ffi.cast("yetty_ygui2_click_cb", function(widget_ptr, userdata)
    app:guard(callback, node)
  end)
  node.callbacks[#node.callbacks + 1] = bridge
  return bridge
end

function App:select_trampoline(node, callback)
  local app = self
  local bridge = ffi.cast("yetty_ygui2_select_cb", function(widget_ptr, index, userdata)
    app:guard(callback, tonumber(index))
  end)
  node.callbacks[#node.callbacks + 1] = bridge
  return bridge
end

-- -- overlays ------------------------------------------------------------

function App:overlay(class_getter)
  local handle = check(rt.C().yetty_ygui2_framework_overlay_add(self.framework,
                                                                class_ptr(class_getter)))
  return node_new(self, handle, self.root)
end

function App:dialog(options)
  local layout_spec, opts = split_options(options, { title = true, x = true, y = true,
                                                     width = true, height = true,
                                                     on_close = true }, "dialog")
  local node = self:overlay("yetty_ygui2_dialog_class_get")
  return configured(self.root, node, function(overlay)
    check(rt.C().yetty_ygui2_dialog_set_title(overlay.handle, opts.title or ""))
    overlay:set_position(opts.x or 100.0, opts.y or 80.0)
    overlay:set_size(opts.width or 280.0, opts.height or 150.0)
    apply_layout(overlay.handle, { gap = 6, pad_left = 12, pad_top = 40, pad_right = 12 })
    if layout_spec then
      apply_layout(overlay.handle, layout_spec)
    end
    if opts.on_close then
      check(rt.C().yetty_ygui2_dialog_on_close_set(
          overlay.handle, self:click_trampoline(overlay, opts.on_close), nil))
    end
    overlay:set_visible(false)
  end)
end

function App:tooltip(options)
  local layout_spec, opts = split_options(options, { text = true, x = true, y = true,
                                                     width = true, height = true }, "tooltip")
  local node = self:overlay("yetty_ygui2_tooltip_class_get")
  return configured(self.root, node, function(overlay)
    check(rt.C().yetty_ygui2_tooltip_set_text(overlay.handle, opts.text or ""))
    overlay:set_position(opts.x or 0.0, opts.y or 0.0)
    overlay:set_size(opts.width or 190.0, opts.height or 24.0)
    if layout_spec then
      apply_layout(overlay.handle, layout_spec)
    end
    overlay:set_visible(false)
  end)
end

-- -- lifecycle -----------------------------------------------------------

function App:quit()
  self.running = false
end

--- Drain a close() requested from inside a native callback. Runs at the
--- dispatch boundaries (after every native call that can invoke user
--- callbacks has fully returned) — never inside the callback itself.
function App:drain_pending_close()
  if self.close_pending and self.in_callback == 0 then
    self.close_pending = false
    self:close()
  end
end

--- Finish one dispatch-capable native call: CONSUME the native Result
--- first (check() flattens + destroys its cause chain — it must be
--- consumed even when the deferred close below throws, or the dispatch
--- error is masked and its chain leaked), then drain any close() a
--- callback requested, then surface both failures combined.
function App:dispatch_result(native_res)
  local dispatch_ok, dispatch_error = pcall(check, native_res)
  local drain_ok, drain_error = pcall(App.drain_pending_close, self)
  if not dispatch_ok and not drain_ok then
    error(tostring(dispatch_error) .. "; deferred close also failed: " .. tostring(drain_error), 0)
  elseif not dispatch_ok then
    error(dispatch_error, 0)
  elseif not drain_ok then
    error(drain_error, 0)
  end
end

--- Idempotent teardown: clear the surface, unsubscribe pane input, and
--- dispose the native framework, then invalidate every wrapper. Safe to
--- call from any state — INCLUDING widget/sink callbacks: native dispatch
--- is still executing there, so the actual disposal is DEFERRED to the
--- moment the native call returns (the host loop and the feed/emit
--- helpers drain it); the loop also stops immediately.
function App:close()
  if self.in_callback > 0 and self.alive then
    self.close_pending = true
    self.running = false
    return
  end
  if not self.alive then
    return
  end
  self.close_pending = false
  self.alive = false
  self.running = false
  local errors = {}
  local steps = {
    { "clear", rt.C().yetty_ygui2_framework_clear },
    { "detach", rt.C().yetty_ygui2_framework_detach },
    { "dispose", rt.C().yetty_ygui2_framework_dispose },
  }
  for _, step in ipairs(steps) do
    local step_name, step_fn = step[1], step[2]
    -- Every step's Result goes through check(): it flattens the cause
    -- chain into the message AND destroys the native chain — reading
    -- only .error.msg here leaked the causes of every wrapped teardown
    -- failure.
    local ok, step_error = pcall(function()
      check(step_fn(self.framework))
    end)
    if not ok then
      errors[#errors + 1] = step_name .. ": " .. tostring(step_error)
    end
  end
  if self.root then
    self.root:invalidate()
  end
  for _, callback in ipairs(self.app_callbacks) do
    callback:free()
  end
  self.app_callbacks = {}
  self.framework = nil
  if #errors > 0 then
    error("ygui2 close: " .. table.concat(errors, "; "), 2)
  end
end

-- Headless capture hook (binding tests): route emitted envelopes into a
-- Lua callback instead of an attached fd. The returned FFI callback is
-- rooted on the app for its life.
function App:set_sink(sink)
  local app = self
  local bridge = ffi.cast("yetty_ygui2_sink_fn", function(bytes, byte_count, userdata)
    app:guard(sink, ffi.string(bytes, tonumber(byte_count)))
  end)
  self.app_callbacks[#self.app_callbacks + 1] = bridge
  check(rt.C().yetty_ygui2_framework_set_sink(self.framework, bridge, nil))
end

function App:emit()
  -- The sink callback runs INSIDE this native call: a close() it requests
  -- is drained only after the native emit has fully returned.
  self:dispatch_result(rt.C().yetty_ygui2_framework_emit(self.framework))
end

function App:set_viewport(width, height)
  check(rt.C().yetty_ygui2_framework_set_viewport(self.framework, width, height))
end

function App:feed_mouse_button(mouse_x, mouse_y, button, pressed, mods)
  self:dispatch_result(rt.C().yetty_ygui2_framework_feed_mouse_button(
      self.framework, mouse_x, mouse_y, button or 0, pressed and 1 or 0, mods or 0))
end

function App:feed_mouse_scroll(mouse_x, mouse_y, wheel_dy)
  self:dispatch_result(
      rt.C().yetty_ygui2_framework_feed_mouse_scroll(self.framework, mouse_x, mouse_y, wheel_dy))
end

-- -- the host loop -------------------------------------------------------

local function raise_captured(app, loop_error)
  local failures = {}
  if app.callback_error ~= nil then
    failures[#failures + 1] = tostring(app.callback_error)
    app.callback_error = nil
  end
  if loop_error ~= nil then
    failures[#failures + 1] = tostring(loop_error)
  end
  if #failures > 0 then
    error(table.concat(failures, "; caused by: "), 0)
  end
end

function App:run(options)
  options = options or {}
  local tick = options.tick
  local tick_ms = options.tick_ms or 250
  if tick_ms <= 0 then
    tick_ms = 250 -- match the C runner's clamp
  end
  if not self.alive then
    error("ygui2 app is closed", 2)
  end
  if self.running then
    error("ygui2 app is already running", 2)
  end

  local saved = ffi.new("struct yetty_luahost_termios")
  if osc.tcgetattr(0, saved) ~= 0 then
    error("ygui2 run: stdin is not a terminal", 2)
  end
  local ok, loop_error = pcall(function()
    self:run_raw(tick, tick_ms)
  end)
  -- Terminal restoration is never skippable and each step stands alone.
  write_stdout("\27[?25h\27[?1049l")
  osc.tcsetattr(0, TCSANOW, saved)
  raise_captured(self, ok and nil or loop_error)
end

function App:run_raw(tick, tick_ms)
  local ok, loop_error = pcall(function()
    local raw = ffi.new("struct yetty_luahost_termios")
    osc.tcgetattr(0, raw)
    raw.c_iflag = bit.band(raw.c_iflag, bit.bnot(IFLAG_CLEAR))
    raw.c_lflag = bit.band(raw.c_lflag, bit.bnot(LFLAG_CLEAR))
    osc.tcsetattr(0, TCSANOW, raw)
    write_stdout("\27[?1049h\27[?25l\27[H")
    self.running = true

    local app = self
    local quit_key = ffi.cast("yetty_ygui2_key_cb", function(key, mods, userdata)
      if key == string.byte("q") or key == 0x03 then
        app.running = false
        return 1
      end
      return 0
    end)
    self.app_callbacks[#self.app_callbacks + 1] = quit_key
    check(rt.C().yetty_ygui2_framework_set_key_cb(self.framework, quit_key, nil))
    check(rt.C().yetty_ygui2_framework_attach(self.framework, 0, 1))
    self:set_viewport(self.viewport_w, self.viewport_h)
    self:emit()

    local poll_slot = ffi.new("struct pollfd[1]")
    poll_slot[0].fd = 0
    poll_slot[0].events = POLLIN
    local buffer = ffi.new("uint8_t[4096]")
    -- Monotonic tick schedule: input bursts must not drive the tick
    -- faster than tick_ms.
    local next_tick = now_seconds() + tick_ms / 1000.0
    while self.running do
      local timeout_ms = math.max(0, math.floor((next_tick - now_seconds()) * 1000.0))
      poll_slot[0].revents = 0
      local ready = osc.poll(poll_slot, 1, timeout_ms)
      if ready > 0 then
        local count = osc.read(0, buffer, 4096)
        if count <= 0 then
          break
        end
        self:dispatch_result(
            rt.C().yetty_ygui2_framework_feed_input(self.framework, buffer, count))
        if not self.alive then
          break
        end
      end
      local now = now_seconds()
      if now >= next_tick then
        self:dispatch_result(rt.C().yetty_ygui2_framework_feed_input_flush(self.framework))
        if not self.alive then
          break
        end
        if tick then
          self:guard(tick)
          if not self.alive then
            break
          end
        end
        next_tick = now + tick_ms / 1000.0
      end
      if self.alive and check(rt.C().yetty_ygui2_framework_is_dirty(self.framework)) ~= 0 then
        self:emit()
      end
    end
  end)
  self.running = false
  self:teardown()
  if not ok then
    error(loop_error, 0)
  end
end

--- Exit-window barrier then close(). The barrier is best-effort: no
--- failure may skip the unsubscribe/dispose in close().
function App:teardown()
  if not self.alive then
    return
  end
  pcall(function()
    check(rt.C().yetty_ygui2_framework_send_hold(self.framework))
    local deadline = now_seconds() + 0.5 -- wall-clock bound, not iterations
    local poll_slot = ffi.new("struct pollfd[1]")
    poll_slot[0].fd = 0
    poll_slot[0].events = POLLIN
    local buffer = ffi.new("uint8_t[4096]")
    while now_seconds() < deadline do
      if check(rt.C().yetty_ygui2_framework_hold_ack_seen(self.framework)) ~= 0 then
        break
      end
      poll_slot[0].revents = 0
      if osc.poll(poll_slot, 1, 50) > 0 then
        local count = osc.read(0, buffer, 4096)
        if count <= 0 then
          break
        end
        self:dispatch_result(
            rt.C().yetty_ygui2_framework_feed_input(self.framework, buffer, count))
        if not self.alive then
          break
        end
      end
    end
  end)
  self:close()
end

M.App = setmetatable(App, { __call = function(ignored_class, options)
  return App.new(options)
end })
M.Node = Node

return M
