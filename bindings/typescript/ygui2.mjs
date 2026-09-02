// @yetty/ydraw/ygui2 — the ygui2 drawable-contract widget toolkit for
// TypeScript/JavaScript: the node port of bindings/python/yetty/ygui2.py.
// HAND-WRITTEN (the generator never touches this) — the exposed C API is
// registered against the shared koffi runtime (runtime.mjs); this module
// is the ergonomic layer: App (the terminal host loop: raw stdin +
// alternate screen + interval ticks + HOLD/ACK teardown), Node builders
// (column.button({ label, onClick })), and callback registrations.
//
// A ygui2 app is a plain PTY client — run it INSIDE a yetty pane:
//
//     cd demo/ffi/ygui2/typescript && npm install
//     node counter.ts
//
// Ownership and liveness: the native tree is owned by the framework.
// Every Node wrapper is liveness-tracked; remove() invalidates the
// wrapper subtree (and unregisters its callbacks) after the native
// subtree is destroyed, and close() invalidates everything. A call on a
// dead node throws instead of touching freed memory. Callback exceptions
// are captured at the koffi boundary, stop the app, and reject from
// run().
//
// Colors are packed 0xAABBGGRR (the SDF fill word) or "#RRGGBB[AA]"
// strings — color() converts.
//
// Quit keys: Ctrl-C always quits (the host runs in raw mode and handles
// byte 0x03 itself). `q` quits while no text input holds focus.
import koffi from "koffi";
import { writeSync } from "node:fs";
import { hasSymbol, invoke, registerSignatures } from "./runtime.mjs";

koffi.struct("yetty_ygui2_layout", {
  basis: "float", grow: "float", cross_size: "float", min_main: "float",
  direction: "uint32", gap: "float",
  pad_left: "float", pad_top: "float", pad_right: "float", pad_bottom: "float",
});

const ClickCb = koffi.proto("void Ygui2ClickCb(void *widget, void *userdata)");
const SelectCb = koffi.proto("void Ygui2SelectCb(void *widget, uint32_t index, void *userdata)");
const KeyCb = koffi.proto("int Ygui2KeyCb(uint32_t key, uint32_t mods, void *userdata)");
const SinkFn = koffi.proto("void Ygui2SinkFn(void *bytes, size_t byteCount, void *userdata)");

registerSignatures({
  yetty_ygui2_framework_make:
    "yetty_result_view yetty_ygui2_framework_make()",
  yetty_ygui2_framework_dispose:
    "yetty_result_view yetty_ygui2_framework_dispose(void *obj)",
  yetty_ygui2_framework_root_create:
    "yetty_result_view yetty_ygui2_framework_root_create(void *obj, void *cls)",
  yetty_ygui2_framework_overlay_add:
    "yetty_result_view yetty_ygui2_framework_overlay_add(void *obj, void *cls)",
  yetty_ygui2_widget_add:
    "yetty_result_view yetty_ygui2_widget_add(void *parent, void *cls)",
  yetty_ygui2_row_add: "yetty_result_view yetty_ygui2_row_add(void *parent)",
  yetty_ygui2_column_add: "yetty_result_view yetty_ygui2_column_add(void *parent)",
  yetty_ygui2_framework_set_sink:
    "yetty_result_view yetty_ygui2_framework_set_sink(void *obj, Ygui2SinkFn *sink, void *userdata)",
  yetty_ygui2_framework_set_viewport:
    "yetty_result_view yetty_ygui2_framework_set_viewport(void *obj, float width, float height)",
  yetty_ygui2_framework_set_fullscreen:
    "yetty_result_view yetty_ygui2_framework_set_fullscreen(void *obj, int fullscreen)",
  yetty_ygui2_framework_content_scale:
    "yetty_result_view yetty_ygui2_framework_content_scale(void *obj, _Out_ float *outScale)",
  yetty_ygui2_framework_set_key_cb:
    "yetty_result_view yetty_ygui2_framework_set_key_cb(void *obj, Ygui2KeyCb *callback, void *userdata)",
  yetty_ygui2_framework_attach:
    "yetty_result_view yetty_ygui2_framework_attach(void *obj, int read_fd, int write_fd)",
  yetty_ygui2_framework_send_hold:
    "yetty_result_view yetty_ygui2_framework_send_hold(void *obj)",
  yetty_ygui2_framework_hold_ack_seen:
    "yetty_result_view yetty_ygui2_framework_hold_ack_seen(void *obj)",
  yetty_ygui2_framework_detach: "yetty_result_view yetty_ygui2_framework_detach(void *obj)",
  yetty_ygui2_framework_clear: "yetty_result_view yetty_ygui2_framework_clear(void *obj)",
  yetty_ygui2_framework_feed_input:
    "yetty_result_view yetty_ygui2_framework_feed_input(void *obj, const uint8_t *bytes, size_t byteCount)",
  yetty_ygui2_framework_feed_input_flush:
    "yetty_result_view yetty_ygui2_framework_feed_input_flush(void *obj)",
  yetty_ygui2_framework_is_dirty:
    "yetty_result_view yetty_ygui2_framework_is_dirty(void *obj)",
  yetty_ygui2_framework_feed_mouse_button:
    "yetty_result_view yetty_ygui2_framework_feed_mouse_button(void *obj, float x, float y, int button, int pressed, int mods)",
  yetty_ygui2_framework_feed_mouse_motion:
    "yetty_result_view yetty_ygui2_framework_feed_mouse_motion(void *obj, float x, float y, uint32_t buttonsHeld)",
  yetty_ygui2_framework_feed_mouse_scroll:
    "yetty_result_view yetty_ygui2_framework_feed_mouse_scroll(void *obj, float x, float y, float wheelDy)",
  yetty_ygui2_framework_emit: "yetty_result_view yetty_ygui2_framework_emit(void *obj)",

  yetty_ygui2_widget_layout_set:
    "yetty_result_view yetty_ygui2_widget_layout_set(void *obj, yetty_ygui2_layout *spec)",
  yetty_ygui2_widget_layout_copy:
    "yetty_result_view yetty_ygui2_widget_layout_copy(void *obj, _Out_ yetty_ygui2_layout *outSpec)",
  yetty_ygui2_widget_rect:
    "yetty_result_view yetty_ygui2_widget_rect(void *obj, _Out_ float *outX, _Out_ float *outY, _Out_ float *outW, _Out_ float *outH)",
  yetty_ygui2_widget_set_focusable:
    "yetty_result_view yetty_ygui2_widget_set_focusable(void *obj, int focusable)",
  yetty_ygui2_widget_set_visible:
    "yetty_result_view yetty_ygui2_widget_set_visible(void *obj, int visible)",
  yetty_ygui2_widget_set_position:
    "yetty_result_view yetty_ygui2_widget_set_position(void *obj, float x, float y)",
  yetty_ygui2_widget_set_size:
    "yetty_result_view yetty_ygui2_widget_set_size(void *obj, float w, float h)",
  yetty_ygui2_widget_remove: "yetty_result_view yetty_ygui2_widget_remove(void *obj)",

  yetty_ygui2_panel_class_get: "yetty_result_view yetty_ygui2_panel_class_get()",
  yetty_ygui2_label_class_get: "yetty_result_view yetty_ygui2_label_class_get()",
  yetty_ygui2_separator_class_get: "yetty_result_view yetty_ygui2_separator_class_get()",
  yetty_ygui2_button_class_get: "yetty_result_view yetty_ygui2_button_class_get()",
  yetty_ygui2_checkbox_class_get: "yetty_result_view yetty_ygui2_checkbox_class_get()",
  yetty_ygui2_toggle_class_get: "yetty_result_view yetty_ygui2_toggle_class_get()",
  yetty_ygui2_radio_class_get: "yetty_result_view yetty_ygui2_radio_class_get()",
  yetty_ygui2_slider_class_get: "yetty_result_view yetty_ygui2_slider_class_get()",
  yetty_ygui2_spinner_class_get: "yetty_result_view yetty_ygui2_spinner_class_get()",
  yetty_ygui2_progress_class_get: "yetty_result_view yetty_ygui2_progress_class_get()",
  yetty_ygui2_chip_class_get: "yetty_result_view yetty_ygui2_chip_class_get()",
  yetty_ygui2_statusbar_class_get: "yetty_result_view yetty_ygui2_statusbar_class_get()",
  yetty_ygui2_stepper_class_get: "yetty_result_view yetty_ygui2_stepper_class_get()",
  yetty_ygui2_textinput_class_get: "yetty_result_view yetty_ygui2_textinput_class_get()",
  yetty_ygui2_dropdown_class_get: "yetty_result_view yetty_ygui2_dropdown_class_get()",
  yetty_ygui2_scrollarea_class_get: "yetty_result_view yetty_ygui2_scrollarea_class_get()",
  yetty_ygui2_table_class_get: "yetty_result_view yetty_ygui2_table_class_get()",
  yetty_ygui2_dialog_class_get: "yetty_result_view yetty_ygui2_dialog_class_get()",
  yetty_ygui2_tooltip_class_get: "yetty_result_view yetty_ygui2_tooltip_class_get()",

  yetty_ygui2_panel_set_bg:
    "yetty_result_view yetty_ygui2_panel_set_bg(void *obj, uint32_t packedRgba)",
  yetty_ygui2_panel_set_border:
    "yetty_result_view yetty_ygui2_panel_set_border(void *obj, uint32_t packedRgba, float widthPx)",
  yetty_ygui2_label_set_text:
    "yetty_result_view yetty_ygui2_label_set_text(void *obj, const char *text)",
  yetty_ygui2_label_set_color:
    "yetty_result_view yetty_ygui2_label_set_color(void *obj, uint32_t packedRgba)",
  yetty_ygui2_button_set_label:
    "yetty_result_view yetty_ygui2_button_set_label(void *obj, const char *text)",
  yetty_ygui2_button_on_click_set:
    "yetty_result_view yetty_ygui2_button_on_click_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_checkbox_set_label:
    "yetty_result_view yetty_ygui2_checkbox_set_label(void *obj, const char *text)",
  yetty_ygui2_checkbox_set_checked:
    "yetty_result_view yetty_ygui2_checkbox_set_checked(void *obj, int checked)",
  yetty_ygui2_checkbox_checked:
    "yetty_result_view yetty_ygui2_checkbox_checked(void *obj)",
  yetty_ygui2_checkbox_on_toggle_set:
    "yetty_result_view yetty_ygui2_checkbox_on_toggle_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_toggle_set_label:
    "yetty_result_view yetty_ygui2_toggle_set_label(void *obj, const char *text)",
  yetty_ygui2_toggle_set_checked:
    "yetty_result_view yetty_ygui2_toggle_set_checked(void *obj, int checked)",
  yetty_ygui2_toggle_checked: "yetty_result_view yetty_ygui2_toggle_checked(void *obj)",
  yetty_ygui2_toggle_on_toggle_set:
    "yetty_result_view yetty_ygui2_toggle_on_toggle_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_radio_set_label:
    "yetty_result_view yetty_ygui2_radio_set_label(void *obj, const char *text)",
  yetty_ygui2_radio_set_selected:
    "yetty_result_view yetty_ygui2_radio_set_selected(void *obj, int selected)",
  yetty_ygui2_radio_on_select_set:
    "yetty_result_view yetty_ygui2_radio_on_select_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_slider_set_range:
    "yetty_result_view yetty_ygui2_slider_set_range(void *obj, float minimum, float maximum)",
  yetty_ygui2_slider_set_value:
    "yetty_result_view yetty_ygui2_slider_set_value(void *obj, float value)",
  yetty_ygui2_slider_value: "yetty_result_view yetty_ygui2_slider_value(void *obj)",
  yetty_ygui2_slider_on_change_set:
    "yetty_result_view yetty_ygui2_slider_on_change_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_spinner_configure:
    "yetty_result_view yetty_ygui2_spinner_configure(void *obj, float minimum, float maximum, float step)",
  yetty_ygui2_spinner_set_value:
    "yetty_result_view yetty_ygui2_spinner_set_value(void *obj, float value)",
  yetty_ygui2_spinner_value: "yetty_result_view yetty_ygui2_spinner_value(void *obj)",
  yetty_ygui2_spinner_on_change_set:
    "yetty_result_view yetty_ygui2_spinner_on_change_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_progress_set_value:
    "yetty_result_view yetty_ygui2_progress_set_value(void *obj, float value)",
  yetty_ygui2_progress_set_accent:
    "yetty_result_view yetty_ygui2_progress_set_accent(void *obj, uint32_t packedRgba)",
  yetty_ygui2_chip_set_label:
    "yetty_result_view yetty_ygui2_chip_set_label(void *obj, const char *text)",
  yetty_ygui2_chip_set_selectable:
    "yetty_result_view yetty_ygui2_chip_set_selectable(void *obj, int selectable)",
  yetty_ygui2_chip_set_selected:
    "yetty_result_view yetty_ygui2_chip_set_selected(void *obj, int selected)",
  yetty_ygui2_chip_on_toggle_set:
    "yetty_result_view yetty_ygui2_chip_on_toggle_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_statusbar_set_left:
    "yetty_result_view yetty_ygui2_statusbar_set_left(void *obj, const char *text)",
  yetty_ygui2_statusbar_set_right:
    "yetty_result_view yetty_ygui2_statusbar_set_right(void *obj, const char *text)",
  yetty_ygui2_stepper_set_count:
    "yetty_result_view yetty_ygui2_stepper_set_count(void *obj, uint32_t stepCount)",
  yetty_ygui2_stepper_set_current:
    "yetty_result_view yetty_ygui2_stepper_set_current(void *obj, uint32_t current)",
  yetty_ygui2_textinput_set_text:
    "yetty_result_view yetty_ygui2_textinput_set_text(void *obj, const char *text)",
  yetty_ygui2_textinput_set_placeholder:
    "yetty_result_view yetty_ygui2_textinput_set_placeholder(void *obj, const char *text)",
  yetty_ygui2_textinput_text_copy:
    "yetty_result_view yetty_ygui2_textinput_text_copy(void *obj, _Out_ uint8_t *outText, size_t outCapacity)",
  yetty_ygui2_textinput_on_submit_set:
    "yetty_result_view yetty_ygui2_textinput_on_submit_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_textinput_on_change_set:
    "yetty_result_view yetty_ygui2_textinput_on_change_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_dropdown_item_add:
    "yetty_result_view yetty_ygui2_dropdown_item_add(void *obj, const char *text)",
  yetty_ygui2_dropdown_set_selected:
    "yetty_result_view yetty_ygui2_dropdown_set_selected(void *obj, int selectedIndex)",
  yetty_ygui2_dropdown_on_change_set:
    "yetty_result_view yetty_ygui2_dropdown_on_change_set(void *obj, Ygui2SelectCb *callback, void *userdata)",
  yetty_ygui2_scrollarea_configure:
    "yetty_result_view yetty_ygui2_scrollarea_configure(void *obj, float wheelStep, float maxScroll)",
  yetty_ygui2_table_set_columns:
    "yetty_result_view yetty_ygui2_table_set_columns(void *obj, const char **headers, const float *widths, uint32_t count)",
  yetty_ygui2_table_clear_rows:
    "yetty_result_view yetty_ygui2_table_clear_rows(void *obj)",
  yetty_ygui2_table_add_row:
    "yetty_result_view yetty_ygui2_table_add_row(void *obj, const char **cells, uint32_t count)",
  yetty_ygui2_dialog_set_title:
    "yetty_result_view yetty_ygui2_dialog_set_title(void *obj, const char *text)",
  yetty_ygui2_dialog_on_close_set:
    "yetty_result_view yetty_ygui2_dialog_on_close_set(void *obj, Ygui2ClickCb *callback, void *userdata)",
  yetty_ygui2_tooltip_set_text:
    "yetty_result_view yetty_ygui2_tooltip_set_text(void *obj, const char *text)",
});

/** Packed 0xAABBGGRR wire color from a number or "#RRGGBB[AA]". */
export function color(value) {
  if (typeof value === "number") {
    return value >>> 0;
  }
  const text = value.replace(/^#/, "");
  const red = parseInt(text.slice(0, 2), 16);
  const green = parseInt(text.slice(2, 4), 16);
  const blue = parseInt(text.slice(4, 6), 16);
  const alpha = text.length >= 8 ? parseInt(text.slice(6, 8), 16) : 0xff;
  return ((alpha << 24) | (blue << 16) | (green << 8) | red) >>> 0;
}

// Value-result decoding: the success value shares the union's first
// word with the error's msg pointer, so the generic 48-byte view fits —
// the word comes back as a pointer whose ADDRESS carries the bits.
function wordBits(word) {
  return word === null || word === undefined ? 0n : koffi.address(word);
}

function floatFromWord(word) {
  const scratch = new DataView(new ArrayBuffer(4));
  scratch.setUint32(0, Number(BigInt.asUintN(32, wordBits(word))), true);
  return scratch.getFloat32(0, true);
}

function intFromWord(word) {
  return Number(BigInt.asIntN(32, wordBits(word)));
}

function classPtr(symbol) {
  return invoke(symbol);
}

const LAYOUT_KEYS = new Set(["basis", "grow", "cross", "crossSize", "minMain", "gap", "pad",
  "padLeft", "padTop", "padRight", "padBottom"]);

function applyLayout(handle, layoutSpec) {
  // Read-modify-write: partial specs must never clobber defaults the C
  // side installed (row/column ship with their direction preset).
  const spec = {};
  invoke("yetty_ygui2_widget_layout_copy", handle, spec);
  if (layoutSpec.pad !== undefined) {
    spec.pad_left = spec.pad_top = spec.pad_right = spec.pad_bottom = layoutSpec.pad;
  }
  const rename = {
    basis: "basis", grow: "grow", cross: "cross_size", crossSize: "cross_size",
    minMain: "min_main", gap: "gap", padLeft: "pad_left", padTop: "pad_top",
    padRight: "pad_right", padBottom: "pad_bottom",
  };
  for (const [key, field] of Object.entries(rename)) {
    if (layoutSpec[key] !== undefined) {
      spec[field] = layoutSpec[key];
    }
  }
  invoke("yetty_ygui2_widget_layout_set", handle, spec);
}

// Split a builder's options into layout and widget options; anything
// unknown is a typo — reject it loudly instead of silently ignoring it.
function splitOptions(options, known, where) {
  const layoutSpec = {};
  const widgetSpec = {};
  let haveLayout = false;
  for (const [key, value] of Object.entries(options ?? {})) {
    if (LAYOUT_KEYS.has(key)) {
      layoutSpec[key] = value;
      haveLayout = true;
    } else if (known.includes(key)) {
      widgetSpec[key] = value;
    } else {
      throw new Error(`${where}(): unknown option '${key}'`);
    }
  }
  return [haveLayout ? layoutSpec : null, widgetSpec];
}

/**
 * One widget in the tree. Builder methods create children; setters
 * forward to the exposed C API. Never constructed directly — start from
 * App.root. Wrappers are liveness-tracked: after remove() or close()
 * every affected Node throws instead of touching freed native memory.
 */
export class Node {
  constructor(app, handle, parent) {
    this.app = app;
    this.handle = handle;
    this.parent = parent;
    this.children = [];
    this.callbacks = [];
    this.invalidateHooks = [];
    this.aliveFlag = true;
    if (parent !== null) {
      parent.children.push(this);
    }
  }

  live() {
    if (!this.aliveFlag || !this.app.aliveFlag) {
      throw new Error("ygui2 node is dead (removed or app closed)");
    }
    return this.handle;
  }

  invalidate() {
    for (const child of this.children) {
      child.invalidate();
    }
    this.children = [];
    for (const registration of this.callbacks) {
      koffi.unregister(registration);
    }
    this.callbacks = [];
    for (const hook of this.invalidateHooks) {
      hook();
    }
    this.invalidateHooks = [];
    this.aliveFlag = false;
    this.handle = null;
  }

  // A builder step failed AFTER the native child existed: remove the
  // native side first (matching remove() ordering), then drop the
  // wrapper.
  configured(node, configure) {
    try {
      configure(node);
      return node;
    } catch (builderError) {
      if (node.handle !== null) {
        try {
          invoke("yetty_ygui2_widget_remove", node.handle);
        } catch {
          // The builder error is the one worth surfacing.
        }
      }
      const index = node.parent ? node.parent.children.indexOf(node) : -1;
      if (index >= 0) {
        node.parent.children.splice(index, 1);
      }
      node.invalidate();
      throw builderError;
    }
  }

  childNode(classSymbol, layoutSpec) {
    const handle = invoke("yetty_ygui2_widget_add", this.live(), classPtr(classSymbol));
    const node = new Node(this.app, handle, this);
    return this.configured(node, (fresh) => {
      if (layoutSpec) {
        applyLayout(fresh.handle, layoutSpec);
      }
    });
  }

  // -- tree building ---------------------------------------------------

  row(options) {
    const [layoutSpec] = splitOptions(options, [], "row");
    const handle = invoke("yetty_ygui2_row_add", this.live());
    const node = new Node(this.app, handle, this);
    return this.configured(node, (fresh) => {
      if (layoutSpec) {
        applyLayout(fresh.handle, layoutSpec);
      }
    });
  }

  column(options) {
    const [layoutSpec] = splitOptions(options, [], "column");
    const handle = invoke("yetty_ygui2_column_add", this.live());
    const node = new Node(this.app, handle, this);
    return this.configured(node, (fresh) => {
      if (layoutSpec) {
        applyLayout(fresh.handle, layoutSpec);
      }
    });
  }

  panel(options) {
    const [layoutSpec, opts] = splitOptions(options, ["bg", "border", "borderWidth"], "panel");
    const node = this.childNode("yetty_ygui2_panel_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      if (opts.bg !== undefined) {
        invoke("yetty_ygui2_panel_set_bg", fresh.handle, color(opts.bg));
      }
      if (opts.border !== undefined) {
        invoke("yetty_ygui2_panel_set_border", fresh.handle, color(opts.border),
          opts.borderWidth ?? 1.0);
      }
    });
  }

  label(options) {
    const [layoutSpec, opts] = splitOptions(options, ["text", "fg"], "label");
    const node = this.childNode("yetty_ygui2_label_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_label_set_text", fresh.handle, opts.text ?? "");
      if (opts.fg !== undefined) {
        invoke("yetty_ygui2_label_set_color", fresh.handle, color(opts.fg));
      }
    });
  }

  separator(options) {
    const [layoutSpec] = splitOptions(options, [], "separator");
    return this.childNode("yetty_ygui2_separator_class_get", layoutSpec);
  }

  button(options) {
    const [layoutSpec, opts] = splitOptions(options, ["label", "onClick"], "button");
    const node = this.childNode("yetty_ygui2_button_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_button_set_label", fresh.handle, opts.label ?? "");
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.onClick) {
        invoke("yetty_ygui2_button_on_click_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onClick), null);
      }
    });
  }

  checkbox(options) {
    const [layoutSpec, opts] = splitOptions(options, ["label", "checked", "onToggle"],
      "checkbox");
    const node = this.childNode("yetty_ygui2_checkbox_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_checkbox_set_label", fresh.handle, opts.label ?? "");
      if (opts.checked) {
        invoke("yetty_ygui2_checkbox_set_checked", fresh.handle, 1);
      }
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.onToggle) {
        invoke("yetty_ygui2_checkbox_on_toggle_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onToggle), null);
      }
    });
  }

  toggle(options) {
    const [layoutSpec, opts] = splitOptions(options, ["label", "checked", "onToggle"], "toggle");
    const node = this.childNode("yetty_ygui2_toggle_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_toggle_set_label", fresh.handle, opts.label ?? "");
      if (opts.checked) {
        invoke("yetty_ygui2_toggle_set_checked", fresh.handle, 1);
      }
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.onToggle) {
        invoke("yetty_ygui2_toggle_on_toggle_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onToggle), null);
      }
    });
  }

  radio(options) {
    const [layoutSpec, opts] = splitOptions(options,
      ["label", "group", "selected", "onSelect"], "radio");
    const node = this.childNode("yetty_ygui2_radio_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_radio_set_label", fresh.handle, opts.label ?? "");
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.selected) {
        invoke("yetty_ygui2_radio_set_selected", fresh.handle, 1);
      }
      if (opts.group) {
        opts.group.register(fresh, opts.onSelect ?? null);
      } else if (opts.onSelect) {
        invoke("yetty_ygui2_radio_on_select_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onSelect), null);
      }
    });
  }

  slider(options) {
    const [layoutSpec, opts] = splitOptions(options,
      ["value", "minimum", "maximum", "onChange"], "slider");
    const node = this.childNode("yetty_ygui2_slider_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      const minimum = opts.minimum ?? 0.0;
      const maximum = opts.maximum ?? 1.0;
      if (minimum !== 0.0 || maximum !== 1.0) {
        invoke("yetty_ygui2_slider_set_range", fresh.handle, minimum, maximum);
      }
      invoke("yetty_ygui2_slider_set_value", fresh.handle, opts.value ?? 0.0);
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.onChange) {
        invoke("yetty_ygui2_slider_on_change_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onChange), null);
      }
    });
  }

  spinner(options) {
    const [layoutSpec, opts] = splitOptions(options,
      ["value", "minimum", "maximum", "step", "onChange"], "spinner");
    const node = this.childNode("yetty_ygui2_spinner_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_spinner_configure", fresh.handle, opts.minimum ?? 0.0,
        opts.maximum ?? 100.0, opts.step ?? 1.0);
      invoke("yetty_ygui2_spinner_set_value", fresh.handle, opts.value ?? 0.0);
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.onChange) {
        invoke("yetty_ygui2_spinner_on_change_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onChange), null);
      }
    });
  }

  progress(options) {
    const [layoutSpec, opts] = splitOptions(options, ["value", "accent"], "progress");
    const node = this.childNode("yetty_ygui2_progress_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_progress_set_value", fresh.handle, opts.value ?? 0.0);
      if (opts.accent !== undefined) {
        invoke("yetty_ygui2_progress_set_accent", fresh.handle, color(opts.accent));
      }
    });
  }

  chip(options) {
    const [layoutSpec, opts] = splitOptions(options,
      ["label", "selectable", "selected", "onToggle"], "chip");
    const node = this.childNode("yetty_ygui2_chip_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_chip_set_label", fresh.handle, opts.label ?? "");
      if (opts.selectable) {
        invoke("yetty_ygui2_chip_set_selectable", fresh.handle, 1);
      }
      if (opts.selected) {
        invoke("yetty_ygui2_chip_set_selected", fresh.handle, 1);
      }
      if (opts.onToggle) {
        invoke("yetty_ygui2_chip_on_toggle_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onToggle), null);
      }
    });
  }

  statusbar(options) {
    const [layoutSpec, opts] = splitOptions(options, ["left", "right"], "statusbar");
    const node = this.childNode("yetty_ygui2_statusbar_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_statusbar_set_left", fresh.handle, opts.left ?? "");
      invoke("yetty_ygui2_statusbar_set_right", fresh.handle, opts.right ?? "");
    });
  }

  stepper(options) {
    const [layoutSpec, opts] = splitOptions(options, ["count", "current"], "stepper");
    const node = this.childNode("yetty_ygui2_stepper_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_stepper_set_count", fresh.handle, opts.count ?? 3);
      invoke("yetty_ygui2_stepper_set_current", fresh.handle, opts.current ?? 0);
    });
  }

  textinput(options) {
    const [layoutSpec, opts] = splitOptions(options,
      ["text", "placeholder", "onSubmit", "onChange"], "textinput");
    const node = this.childNode("yetty_ygui2_textinput_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      if (opts.text) {
        invoke("yetty_ygui2_textinput_set_text", fresh.handle, opts.text);
      }
      if (opts.placeholder) {
        invoke("yetty_ygui2_textinput_set_placeholder", fresh.handle, opts.placeholder);
      }
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.onSubmit) {
        invoke("yetty_ygui2_textinput_on_submit_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onSubmit), null);
      }
      if (opts.onChange) {
        invoke("yetty_ygui2_textinput_on_change_set", fresh.handle,
          this.app.clickRegistration(fresh, opts.onChange), null);
      }
    });
  }

  dropdown(options) {
    const [layoutSpec, opts] = splitOptions(options, ["items", "selected", "onChange"],
      "dropdown");
    const node = this.childNode("yetty_ygui2_dropdown_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      for (const item of opts.items ?? []) {
        invoke("yetty_ygui2_dropdown_item_add", fresh.handle, item);
      }
      if (opts.selected !== undefined && opts.selected >= 0) {
        invoke("yetty_ygui2_dropdown_set_selected", fresh.handle, opts.selected);
      }
      invoke("yetty_ygui2_widget_set_focusable", fresh.handle, 1);
      if (opts.onChange) {
        invoke("yetty_ygui2_dropdown_on_change_set", fresh.handle,
          this.app.selectRegistration(fresh, opts.onChange), null);
      }
    });
  }

  scrollarea(options) {
    const [layoutSpec, opts] = splitOptions(options, ["wheelStep", "maxScroll"], "scrollarea");
    const node = this.childNode("yetty_ygui2_scrollarea_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      invoke("yetty_ygui2_scrollarea_configure", fresh.handle, opts.wheelStep ?? 24.0,
        opts.maxScroll ?? 1000.0);
    });
  }

  table(options) {
    const [layoutSpec, opts] = splitOptions(options, ["columns", "widths"], "table");
    const node = this.childNode("yetty_ygui2_table_class_get", layoutSpec);
    return this.configured(node, (fresh) => {
      if ((opts.columns ?? []).length > 0) {
        fresh.setColumns(opts.columns, opts.widths);
      }
    });
  }

  // -- common setters --------------------------------------------------

  layout(options) {
    const [layoutSpec] = splitOptions(options, [], "layout");
    if (layoutSpec) {
      applyLayout(this.live(), layoutSpec);
    }
    return this;
  }

  setPosition(positionX, positionY) {
    invoke("yetty_ygui2_widget_set_position", this.live(), positionX, positionY);
    return this;
  }

  setSize(width, height) {
    invoke("yetty_ygui2_widget_set_size", this.live(), width, height);
    return this;
  }

  setVisible(visible) {
    invoke("yetty_ygui2_widget_set_visible", this.live(), visible ? 1 : 0);
    return this;
  }

  rect() {
    const outX = [0.0];
    const outY = [0.0];
    const outW = [0.0];
    const outH = [0.0];
    invoke("yetty_ygui2_widget_rect", this.live(), outX, outY, outW, outH);
    return { x: outX[0], y: outY[0], w: outW[0], h: outH[0] };
  }

  /**
   * Remove this widget (and its whole subtree) from the live tree. The
   * native removal runs first — if it rejects (e.g. a root), the
   * wrapper tree stays fully usable.
   */
  remove() {
    const handle = this.live();
    if (this.parent === null) {
      throw new Error("ygui2: cannot remove a root node");
    }
    invoke("yetty_ygui2_widget_remove", handle);
    const index = this.parent.children.indexOf(this);
    if (index >= 0) {
      this.parent.children.splice(index, 1);
    }
    this.invalidate();
  }

  // -- per-widget accessors (the C side rejects class mismatches) -------

  setText(text) {
    invoke("yetty_ygui2_label_set_text", this.live(), text);
    return this;
  }

  setValue(value) {
    invoke("yetty_ygui2_progress_set_value", this.live(), value);
    return this;
  }

  sliderValue() {
    return floatFromWord(invoke("yetty_ygui2_slider_value", this.live()));
  }

  spinnerValue() {
    return floatFromWord(invoke("yetty_ygui2_spinner_value", this.live()));
  }

  checkboxChecked() {
    return intFromWord(invoke("yetty_ygui2_checkbox_checked", this.live())) !== 0;
  }

  toggleChecked() {
    return intFromWord(invoke("yetty_ygui2_toggle_checked", this.live())) !== 0;
  }

  inputText() {
    const buffer = Buffer.alloc(256);
    invoke("yetty_ygui2_textinput_text_copy", this.live(), buffer, buffer.length);
    const terminator = buffer.indexOf(0);
    return buffer.toString("utf8", 0, terminator < 0 ? buffer.length : terminator);
  }

  status(options) {
    if (options.left !== undefined) {
      invoke("yetty_ygui2_statusbar_set_left", this.live(), options.left);
    }
    if (options.right !== undefined) {
      invoke("yetty_ygui2_statusbar_set_right", this.live(), options.right);
    }
    return this;
  }

  stepperCurrent(current) {
    invoke("yetty_ygui2_stepper_set_current", this.live(), current);
    return this;
  }

  setColumns(columns, widths) {
    const widthArray = Float32Array.from(columns.map(
      (ignored, index) => (widths ?? [])[index] ?? 0.0));
    invoke("yetty_ygui2_table_set_columns", this.live(), columns, widthArray, columns.length);
    return this;
  }

  clearRows() {
    invoke("yetty_ygui2_table_clear_rows", this.live());
    return this;
  }

  addRow(cells) {
    invoke("yetty_ygui2_table_add_row", this.live(), cells, cells.length);
    return this;
  }
}

/**
 * App-side radio-group semantics: selecting one clears the others (the
 * C widget is deliberately dumb about groups). A removed member's slot
 * is tombstoned, so a dynamic list retains nothing for dead radios.
 */
export class RadioGroup {
  constructor() {
    this.members = [];
  }

  register(node, onSelect) {
    const index = this.members.length;
    this.members.push({ node, onSelect });
    node.invalidateHooks.push(() => {
      this.members[index] = { node: null, onSelect: null };
    });
    invoke("yetty_ygui2_radio_on_select_set", node.handle,
      node.app.clickRegistration(node, () => {
        for (const [memberIndex, member] of this.members.entries()) {
          if (memberIndex !== index && member.node !== null && member.node.aliveFlag) {
            invoke("yetty_ygui2_radio_set_selected", member.node.handle, 0);
          }
        }
        const callback = this.members[index].onSelect;
        if (callback) {
          callback(index);
        }
      }), null);
  }
}

function writeStdout(text) {
  writeSync(1, text);
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

/**
 * The terminal host (node port of yguiapp2): raw stdin, alternate
 * screen, pane-input subscription, interval ticks with idle Esc flush,
 * emit-on-dirty, HOLD/ACK barrier + unsubscribe on every exit path.
 * Ctrl-C always quits; `q` quits while no text input holds focus.
 *
 *     const app = new App();
 *     const column = app.root.column({ grow: 1, gap: 8, pad: 16 });
 *     column.label({ text: "hello" });
 *     await app.run({ tick, tickMs: 250 });
 */
export class App {
  constructor(options = {}) {
    if (!hasSymbol("yetty_ygui2_framework_make")) {
      throw new Error(
        "the loaded libyetty_ffi.so does not export the ygui2 toolkit — rebuild it " +
        "(USE_DISTCC=1 make build-desktop-ytrace-release) or point YETTY_FFI_LIB " +
        "at a current build");
    }
    this.runningFlag = false;
    this.aliveFlag = false;
    this.callbackError = null;
    // Callbacks execute INSIDE native dispatch; disposing the framework
    // there is a native use-after-free when dispatch resumes. close()
    // defers while this depth is nonzero; the dispatch helpers drain the
    // deferred close after the native call has fully returned.
    this.inCallback = 0;
    this.closePending = false;
    this.appCallbacks = [];
    this.framework = invoke("yetty_ygui2_framework_make");
    this.aliveFlag = true;
    try {
      if (options.fullscreen === false) {
        this.setFullscreen(false);
      }
      this.viewport = App.probeViewport();
      const rootHandle = invoke("yetty_ygui2_framework_root_create", this.framework,
        classPtr("yetty_ygui2_panel_class_get"));
      invoke("yetty_ygui2_panel_set_bg", rootHandle, color("#0B1014"));
      this.root = new Node(this, rootHandle, null);
    } catch (constructionError) {
      try {
        this.close();
      } catch {
        // cleanup must not mask the construction error
      }
      throw constructionError;
    }
  }

  /**
   * Pane pixels — node exposes cell counts only, so this is the cell
   * estimate; the forwarded RESIZE envelope corrects it as soon as the
   * app attaches.
   */
  static probeViewport() {
    const cols = process.stdout.columns || 80;
    const rows = process.stdout.rows || 40;
    return [cols * 8.0, rows * 16.0];
  }

  // Callback registrations are rooted on the node that owns them
  // (unregistered with the node). Exceptions cannot cross the koffi
  // boundary: they are captured here, stop the app, and reject run().
  guard(callback, ...args) {
    this.inCallback++;
    try {
      callback(...args);
    } catch (callbackException) {
      if (this.callbackError === null) {
        this.callbackError = callbackException;
      }
      this.runningFlag = false;
    } finally {
      this.inCallback--;
    }
  }

  /**
   * Drain a close() requested from inside a native callback. Runs at the
   * dispatch boundaries (after every native call that can invoke user
   * callbacks has fully returned) — never inside the callback itself.
   */
  drainPendingClose() {
    if (this.closePending && this.inCallback === 0) {
      this.closePending = false;
      this.close();
    }
  }

  /**
   * Run one dispatch-capable native call, then drain any close() a
   * callback requested during it — even when the native call failed.
   */
  dispatch(symbol, ...args) {
    let result;
    let dispatchError = null;
    try {
      result = invoke(symbol, ...args);
    } catch (invokeException) {
      dispatchError = invokeException;
    }
    this.drainPendingClose();
    if (dispatchError !== null) {
      throw dispatchError;
    }
    return result;
  }

  clickRegistration(node, callback) {
    const registration = koffi.register((widget, userdata) => {
      this.guard(callback, node);
    }, koffi.pointer(ClickCb));
    node.callbacks.push(registration);
    return registration;
  }

  selectRegistration(node, callback) {
    const registration = koffi.register((widget, index, userdata) => {
      this.guard(callback, index);
    }, koffi.pointer(SelectCb));
    node.callbacks.push(registration);
    return registration;
  }

  // -- overlays --------------------------------------------------------

  overlay(classSymbol) {
    const handle = invoke("yetty_ygui2_framework_overlay_add", this.framework,
      classPtr(classSymbol));
    return new Node(this, handle, this.root);
  }

  dialog(options) {
    const [layoutSpec, opts] = splitOptions(options,
      ["title", "x", "y", "width", "height", "onClose"], "dialog");
    const node = this.overlay("yetty_ygui2_dialog_class_get");
    return this.root.configured(node, (overlay) => {
      invoke("yetty_ygui2_dialog_set_title", overlay.handle, opts.title ?? "");
      overlay.setPosition(opts.x ?? 100.0, opts.y ?? 80.0);
      overlay.setSize(opts.width ?? 280.0, opts.height ?? 150.0);
      applyLayout(overlay.handle, { gap: 6, padLeft: 12, padTop: 40, padRight: 12 });
      if (layoutSpec) {
        applyLayout(overlay.handle, layoutSpec);
      }
      if (opts.onClose) {
        invoke("yetty_ygui2_dialog_on_close_set", overlay.handle,
          this.clickRegistration(overlay, opts.onClose), null);
      }
      overlay.setVisible(false);
    });
  }

  tooltip(options) {
    const [layoutSpec, opts] = splitOptions(options, ["text", "x", "y", "width", "height"],
      "tooltip");
    const node = this.overlay("yetty_ygui2_tooltip_class_get");
    return this.root.configured(node, (overlay) => {
      invoke("yetty_ygui2_tooltip_set_text", overlay.handle, opts.text ?? "");
      overlay.setPosition(opts.x ?? 0.0, opts.y ?? 0.0);
      overlay.setSize(opts.width ?? 190.0, opts.height ?? 24.0);
      if (layoutSpec) {
        applyLayout(overlay.handle, layoutSpec);
      }
      overlay.setVisible(false);
    });
  }

  // -- lifecycle -------------------------------------------------------

  quit() {
    this.runningFlag = false;
  }

  /**
   * Idempotent teardown: clear the surface, unsubscribe pane input, and
   * dispose the native framework, then invalidate every wrapper.
   */
  close() {
    if (this.inCallback > 0 && this.aliveFlag) {
      // Native dispatch is still executing — defer the disposal to the
      // moment the native call returns (the dispatch helpers drain it).
      this.closePending = true;
      this.runningFlag = false;
      return;
    }
    if (!this.aliveFlag) {
      return;
    }
    this.aliveFlag = false;
    this.runningFlag = false;
    this.closePending = false;
    const errors = [];
    for (const [stepName, symbol] of [["clear", "yetty_ygui2_framework_clear"],
      ["detach", "yetty_ygui2_framework_detach"],
      ["dispose", "yetty_ygui2_framework_dispose"]]) {
      try {
        invoke(symbol, this.framework);
      } catch (stepError) {
        errors.push(`${stepName}: ${stepError.message ?? stepError}`);
      }
    }
    if (this.root) {
      this.root.invalidate();
    }
    for (const registration of this.appCallbacks) {
      koffi.unregister(registration);
    }
    this.appCallbacks = [];
    this.framework = null;
    if (errors.length > 0) {
      throw new Error("ygui2 close: " + errors.join("; "));
    }
  }

  /**
   * Headless capture hook (binding tests): route emitted envelopes into
   * a callback instead of an attached fd.
   */
  setSink(sink) {
    const registration = koffi.register((bytes, byteCount, userdata) => {
      const count = Number(byteCount);
      const payload = count > 0
        ? Buffer.from(koffi.decode(bytes, koffi.array("uint8_t", count)))
        : Buffer.alloc(0);
      this.guard(sink, payload);
    }, koffi.pointer(SinkFn));
    this.appCallbacks.push(registration);
    invoke("yetty_ygui2_framework_set_sink", this.framework, registration, null);
  }

  emit() {
    // The sink callback runs INSIDE this native call: a close() it
    // requests is drained only after the native emit has returned.
    this.dispatch("yetty_ygui2_framework_emit", this.framework);
  }

  setViewport(width, height) {
    invoke("yetty_ygui2_framework_set_viewport", this.framework, width, height);
  }

  /**
   * Reservation mode (strategy.md §5): fullscreen (default) reserves the
   * full supported viewport range; inline (false) reserves the declared
   * viewport height and lives in the scrollback flow. Must be chosen
   * BEFORE the first emit — the C side rejects the call once inserted.
   */
  setFullscreen(fullscreen) {
    invoke("yetty_ygui2_framework_set_fullscreen", this.framework, fullscreen ? 1 : 0);
  }

  /**
   * The committed HiDPI input divisor (1.0 until a pane-resize envelope
   * carries a different scale).
   */
  contentScale() {
    const outScale = [0.0];
    invoke("yetty_ygui2_framework_content_scale", this.framework, outScale);
    return outScale[0];
  }

  feedMouseButton(mouseX, mouseY, button, pressed, mods) {
    this.dispatch("yetty_ygui2_framework_feed_mouse_button", this.framework, mouseX, mouseY,
      button ?? 0, pressed ? 1 : 0, mods ?? 0);
  }

  feedMouseScroll(mouseX, mouseY, wheelDy) {
    this.dispatch("yetty_ygui2_framework_feed_mouse_scroll", this.framework, mouseX, mouseY,
      wheelDy);
  }

  emitIfDirty() {
    if (this.aliveFlag && intFromWord(invoke("yetty_ygui2_framework_is_dirty",
      this.framework)) !== 0) {
      this.emit();
    }
  }

  // -- the host loop ---------------------------------------------------

  async run(options = {}) {
    const tick = options.tick ?? null;
    let tickMs = options.tickMs ?? 250;
    if (tickMs <= 0) {
      tickMs = 250; // match the C runner's clamp
    }
    if (!this.aliveFlag) {
      throw new Error("ygui2 app is closed");
    }
    if (this.runningFlag) {
      throw new Error("ygui2 app is already running");
    }
    if (!process.stdin.isTTY) {
      throw new Error("ygui2 run: stdin is not a terminal");
    }

    let loopError = null;
    try {
      await this.runRaw(tick, tickMs);
    } catch (runException) {
      loopError = runException;
    }
    // Terminal restoration is never skippable and each step stands
    // alone: no restoration failure may mask the application exception.
    try {
      writeStdout("\x1b[?25h\x1b[?1049l");
    } catch {
      // best-effort restore
    }
    try {
      process.stdin.setRawMode(false);
      process.stdin.pause();
    } catch {
      // best-effort restore
    }
    if (this.callbackError !== null) {
      const primary = this.callbackError;
      this.callbackError = null;
      throw primary;
    }
    if (loopError !== null) {
      throw loopError;
    }
  }

  async runRaw(tick, tickMs) {
    let dataHandler = null;
    try {
      process.stdin.setRawMode(true);
      process.stdin.resume();
      writeStdout("\x1b[?1049h\x1b[?25l\x1b[H");
      this.runningFlag = true;

      const quitKey = koffi.register((key, mods, userdata) => {
        if (key === 0x71 /* q */ || key === 0x03) {
          this.runningFlag = false;
          return 1;
        }
        return 0;
      }, koffi.pointer(KeyCb));
      this.appCallbacks.push(quitKey);
      invoke("yetty_ygui2_framework_set_key_cb", this.framework, quitKey, null);
      invoke("yetty_ygui2_framework_attach", this.framework, 0, 1);
      this.setViewport(...this.viewport);
      this.emit();

      let wake = null;
      dataHandler = (chunk) => {
        if (!this.aliveFlag) {
          return;
        }
        try {
          this.dispatch("yetty_ygui2_framework_feed_input", this.framework, chunk, chunk.length);
          this.emitIfDirty();
        } catch (feedException) {
          if (this.callbackError === null) {
            this.callbackError = feedException;
          }
          this.runningFlag = false;
        }
        if (!this.runningFlag && wake) {
          wake();
        }
      };
      process.stdin.on("data", dataHandler);

      // Interval ticks: input arrives via the stdin events above; the
      // loop below only paces flush + tick + emit and waits.
      while (this.runningFlag && this.aliveFlag) {
        await new Promise((resolve) => {
          wake = resolve;
          setTimeout(resolve, tickMs);
        });
        wake = null;
        if (!this.runningFlag || !this.aliveFlag) {
          break;
        }
        this.dispatch("yetty_ygui2_framework_feed_input_flush", this.framework);
        if (!this.aliveFlag) {
          break;
        }
        if (tick !== null) {
          this.guard(tick);
          this.drainPendingClose();
          if (!this.aliveFlag) {
            break;
          }
        }
        this.emitIfDirty();
      }
    } finally {
      this.runningFlag = false;
      await this.teardown();
      if (dataHandler !== null) {
        process.stdin.off("data", dataHandler);
      }
    }
  }

  /**
   * Exit-window barrier then close(). The barrier is best-effort: no
   * failure may skip the unsubscribe/dispose in close().
   */
  async teardown() {
    if (!this.aliveFlag) {
      return;
    }
    try {
      invoke("yetty_ygui2_framework_send_hold", this.framework);
      const deadline = Date.now() + 500; // wall-clock bound
      while (Date.now() < deadline) {
        if (intFromWord(invoke("yetty_ygui2_framework_hold_ack_seen", this.framework)) !== 0) {
          break;
        }
        await delay(25); // the stdin handler keeps feeding during the wait
      }
    } catch {
      // a wedged barrier must never block the real teardown
    }
    this.close();
  }
}
