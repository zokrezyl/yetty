// Package ygui2 — the ygui2 drawable-contract widget toolkit for Go:
// the Go port of bindings/python/yetty/ygui2.py. HAND-WRITTEN (no
// generator touches this): the exposed C API is declared in the cgo
// preamble; this package is the ergonomic layer — App (the terminal
// host loop: raw termios + alternate screen + poll loop + HOLD/ACK
// teardown), Node builders (column.Button(ButtonOpts{...})), and
// callback bridges.
//
// A ygui2 app is a plain PTY client — run it INSIDE a yetty pane:
//
//	CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi" \
//	LD_LIBRARY_PATH=<build>/src/yetty/yffi go run counter.go
//
// Ownership and liveness: the native tree is owned by the framework.
// Every Node wrapper is liveness-tracked; Remove() invalidates the
// wrapper subtree (and drops its callbacks) after the native subtree is
// destroyed, and Close() invalidates everything. A call on a dead node
// panics instead of touching freed memory. Builder/setter failures
// panic (construction is programmer error); Run returns an error and
// captures callback panics at the cgo boundary — a panic may not cross
// into C.
//
// Colors are packed 0xAABBGGRR (the SDF fill word) or "#RRGGBB[AA]"
// strings — Color() converts.
//
// Quit keys: Ctrl-C always quits (the host runs with ISIG off and
// handles byte 0x03 itself). `q` quits while no text input holds focus.
package ygui2

// #cgo LDFLAGS: -lyetty_ffi
// #include <poll.h>
// #include <stdint.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/ioctl.h>
// #include <termios.h>
// #include <unistd.h>
// struct yetty_yclass;
// struct yetty_yclass_object;
// struct yetty_ydraw_drawable_list;
// struct yetty_ycore_error { const char *msg; const char *file; const char *func; int line; struct yetty_ycore_error *cause; };
// struct yetty_ycore_void_result { int ok; union { int value; struct yetty_ycore_error error; }; };
// struct yetty_ycore_int_result { int ok; union { int value; struct yetty_ycore_error error; }; };
// struct yetty_ycore_float_result { int ok; union { float value; struct yetty_ycore_error error; }; };
// struct yetty_ycore_size_result { int ok; union { size_t value; struct yetty_ycore_error error; }; };
// struct yetty_yclass_ptr_result { int ok; union { const struct yetty_yclass *value; struct yetty_ycore_error error; }; };
// struct yetty_yclass_object_ptr_result { int ok; union { struct yetty_yclass_object *value; struct yetty_ycore_error error; }; };
// struct yetty_ygui2_layout { float basis; float grow; float cross_size; float min_main; uint32_t direction; float gap; float pad_left; float pad_top; float pad_right; float pad_bottom; };
// typedef int (*yetty_ygui2_key_cb)(uint32_t, uint32_t, void *);
// typedef void (*yetty_ygui2_click_cb)(struct yetty_yclass_object *, void *);
// typedef void (*yetty_ygui2_select_cb)(struct yetty_yclass_object *, uint32_t, void *);
// typedef void (*yetty_ygui2_sink_fn)(const uint8_t *, size_t, void *);
//
// struct yetty_yclass_object_ptr_result yetty_ygui2_framework_make(void);
// struct yetty_ycore_void_result yetty_ygui2_framework_dispose(struct yetty_yclass_object *obj);
// struct yetty_yclass_object_ptr_result yetty_ygui2_framework_root_create(struct yetty_yclass_object *obj, const struct yetty_yclass *cls);
// struct yetty_yclass_object_ptr_result yetty_ygui2_framework_overlay_add(struct yetty_yclass_object *obj, const struct yetty_yclass *cls);
// struct yetty_yclass_object_ptr_result yetty_ygui2_widget_add(struct yetty_yclass_object *parent, const struct yetty_yclass *cls);
// struct yetty_yclass_object_ptr_result yetty_ygui2_row_add(struct yetty_yclass_object *parent);
// struct yetty_yclass_object_ptr_result yetty_ygui2_column_add(struct yetty_yclass_object *parent);
// struct yetty_ycore_void_result yetty_ygui2_framework_set_sink(struct yetty_yclass_object *obj, yetty_ygui2_sink_fn sink, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_framework_set_viewport(struct yetty_yclass_object *obj, float width, float height);
// struct yetty_ycore_void_result yetty_ygui2_framework_set_fullscreen(struct yetty_yclass_object *obj, int fullscreen);
// struct yetty_ycore_void_result yetty_ygui2_framework_content_scale(struct yetty_yclass_object *obj, float *out_scale);
// void yetty_ycore_error_destroy(struct yetty_ycore_error err);
// struct yetty_ycore_void_result yetty_ygui2_framework_set_key_cb(struct yetty_yclass_object *obj, yetty_ygui2_key_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_framework_attach(struct yetty_yclass_object *obj, int read_fd, int write_fd);
// struct yetty_ycore_void_result yetty_ygui2_framework_send_hold(struct yetty_yclass_object *obj);
// struct yetty_ycore_int_result yetty_ygui2_framework_hold_ack_seen(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_framework_detach(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_framework_clear(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_framework_feed_input(struct yetty_yclass_object *obj, const uint8_t *bytes, size_t byte_count);
// struct yetty_ycore_void_result yetty_ygui2_framework_feed_input_flush(struct yetty_yclass_object *obj);
// struct yetty_ycore_int_result yetty_ygui2_framework_is_dirty(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_button(struct yetty_yclass_object *obj, float x, float y, int button, int pressed, int mods);
// struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_motion(struct yetty_yclass_object *obj, float x, float y, uint32_t buttons_held);
// struct yetty_ycore_void_result yetty_ygui2_framework_feed_mouse_scroll(struct yetty_yclass_object *obj, float x, float y, float wheel_dy);
// struct yetty_ycore_void_result yetty_ygui2_framework_emit(struct yetty_yclass_object *obj);
//
// struct yetty_ycore_void_result yetty_ygui2_widget_layout_set(struct yetty_yclass_object *obj, const struct yetty_ygui2_layout *spec);
// struct yetty_ycore_void_result yetty_ygui2_widget_layout_copy(struct yetty_yclass_object *obj, struct yetty_ygui2_layout *out_spec);
// struct yetty_ycore_void_result yetty_ygui2_widget_rect(struct yetty_yclass_object *obj, float *out_x, float *out_y, float *out_w, float *out_h);
// struct yetty_ycore_void_result yetty_ygui2_widget_set_focusable(struct yetty_yclass_object *obj, int focusable);
// struct yetty_ycore_void_result yetty_ygui2_widget_set_visible(struct yetty_yclass_object *obj, int visible);
// struct yetty_ycore_void_result yetty_ygui2_widget_set_position(struct yetty_yclass_object *obj, float x, float y);
// struct yetty_ycore_void_result yetty_ygui2_widget_set_size(struct yetty_yclass_object *obj, float w, float h);
// struct yetty_ycore_void_result yetty_ygui2_widget_remove(struct yetty_yclass_object *obj);
//
// struct yetty_yclass_ptr_result yetty_ygui2_panel_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_label_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_separator_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_button_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_checkbox_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_toggle_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_radio_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_slider_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_spinner_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_progress_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_chip_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_statusbar_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_stepper_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_textinput_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_dropdown_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_scrollarea_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_table_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_dialog_class_get(void);
// struct yetty_yclass_ptr_result yetty_ygui2_tooltip_class_get(void);
//
// struct yetty_ycore_void_result yetty_ygui2_panel_set_bg(struct yetty_yclass_object *obj, uint32_t packed_rgba);
// struct yetty_ycore_void_result yetty_ygui2_panel_set_border(struct yetty_yclass_object *obj, uint32_t packed_rgba, float width_px);
// struct yetty_ycore_void_result yetty_ygui2_label_set_text(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_label_set_color(struct yetty_yclass_object *obj, uint32_t packed_rgba);
// struct yetty_ycore_void_result yetty_ygui2_button_set_label(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_button_on_click_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_checkbox_set_label(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_checkbox_set_checked(struct yetty_yclass_object *obj, int checked);
// struct yetty_ycore_int_result yetty_ygui2_checkbox_checked(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_checkbox_on_toggle_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_toggle_set_label(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_toggle_set_checked(struct yetty_yclass_object *obj, int checked);
// struct yetty_ycore_int_result yetty_ygui2_toggle_checked(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_toggle_on_toggle_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_radio_set_label(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_radio_set_selected(struct yetty_yclass_object *obj, int selected);
// struct yetty_ycore_void_result yetty_ygui2_radio_on_select_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_slider_set_range(struct yetty_yclass_object *obj, float minimum, float maximum);
// struct yetty_ycore_void_result yetty_ygui2_slider_set_value(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_float_result yetty_ygui2_slider_value(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_slider_on_change_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_spinner_configure(struct yetty_yclass_object *obj, float minimum, float maximum, float step);
// struct yetty_ycore_void_result yetty_ygui2_spinner_set_value(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_float_result yetty_ygui2_spinner_value(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_spinner_on_change_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_progress_set_value(struct yetty_yclass_object *obj, float value);
// struct yetty_ycore_void_result yetty_ygui2_progress_set_accent(struct yetty_yclass_object *obj, uint32_t packed_rgba);
// struct yetty_ycore_void_result yetty_ygui2_chip_set_label(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_chip_set_selectable(struct yetty_yclass_object *obj, int selectable);
// struct yetty_ycore_void_result yetty_ygui2_chip_set_selected(struct yetty_yclass_object *obj, int selected);
// struct yetty_ycore_void_result yetty_ygui2_chip_on_toggle_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_statusbar_set_left(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_statusbar_set_right(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_stepper_set_count(struct yetty_yclass_object *obj, uint32_t step_count);
// struct yetty_ycore_void_result yetty_ygui2_stepper_set_current(struct yetty_yclass_object *obj, uint32_t current);
// struct yetty_ycore_void_result yetty_ygui2_textinput_set_text(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_textinput_set_placeholder(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_size_result yetty_ygui2_textinput_text_copy(struct yetty_yclass_object *obj, char *out_text, size_t out_capacity);
// struct yetty_ycore_void_result yetty_ygui2_textinput_on_submit_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_textinput_on_change_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_dropdown_item_add(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_dropdown_set_selected(struct yetty_yclass_object *obj, int selected_index);
// struct yetty_ycore_void_result yetty_ygui2_dropdown_on_change_set(struct yetty_yclass_object *obj, yetty_ygui2_select_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_scrollarea_configure(struct yetty_yclass_object *obj, float wheel_step, float max_scroll);
// struct yetty_ycore_void_result yetty_ygui2_table_set_columns(struct yetty_yclass_object *obj, const char *const *headers, const float *widths, uint32_t count);
// struct yetty_ycore_void_result yetty_ygui2_table_clear_rows(struct yetty_yclass_object *obj);
// struct yetty_ycore_void_result yetty_ygui2_table_add_row(struct yetty_yclass_object *obj, const char *const *cells, uint32_t count);
// struct yetty_ycore_void_result yetty_ygui2_dialog_set_title(struct yetty_yclass_object *obj, const char *text);
// struct yetty_ycore_void_result yetty_ygui2_dialog_on_close_set(struct yetty_yclass_object *obj, yetty_ygui2_click_cb callback, void *userdata);
// struct yetty_ycore_void_result yetty_ygui2_tooltip_set_text(struct yetty_yclass_object *obj, const char *text);
//
// extern void yettyGoYgui2Click(struct yetty_yclass_object *widget, void *userdata);
// extern void yettyGoYgui2Select(struct yetty_yclass_object *widget, uint32_t index, void *userdata);
// extern int yettyGoYgui2Key(uint32_t key, uint32_t mods, void *userdata);
// extern void yettyGoYgui2Sink(uint8_t *bytes, size_t byte_count, void *userdata);
//
// static struct yetty_ycore_error yetty_bind_void_error(struct yetty_ycore_void_result result) { return result.error; }
// static struct yetty_ycore_error yetty_bind_object_error(struct yetty_yclass_object_ptr_result result) { return result.error; }
// static struct yetty_yclass_object *yetty_bind_object_value(struct yetty_yclass_object_ptr_result result) { return result.ok ? result.value : (struct yetty_yclass_object *)0; }
// static struct yetty_ycore_error yetty_bind_class_error(struct yetty_yclass_ptr_result result) { return result.error; }
// static const struct yetty_yclass *yetty_bind_class_value(struct yetty_yclass_ptr_result result) { return result.ok ? result.value : (const struct yetty_yclass *)0; }
// static struct yetty_ycore_error yetty_bind_int_error(struct yetty_ycore_int_result result) { return result.error; }
// static int yetty_bind_int_value(struct yetty_ycore_int_result result) { return result.ok ? result.value : 0; }
// static struct yetty_ycore_error yetty_bind_float_error(struct yetty_ycore_float_result result) { return result.error; }
// static float yetty_bind_float_value(struct yetty_ycore_float_result result) { return result.ok ? result.value : 0.0f; }
// static struct yetty_ycore_error yetty_bind_size_error(struct yetty_ycore_size_result result) { return result.error; }
//
// static yetty_ygui2_click_cb yetty_bind_click_bridge(void) { return yettyGoYgui2Click; }
// static yetty_ygui2_select_cb yetty_bind_select_bridge(void) { return yettyGoYgui2Select; }
// static yetty_ygui2_key_cb yetty_bind_key_bridge(void) { return yettyGoYgui2Key; }
// static yetty_ygui2_sink_fn yetty_bind_sink_bridge(void) { return (yetty_ygui2_sink_fn)yettyGoYgui2Sink; }
// static void *yetty_bind_id_to_ptr(uint64_t id) { return (void *)(uintptr_t)id; }
// static uint64_t yetty_bind_ptr_to_id(void *userdata) { return (uint64_t)(uintptr_t)userdata; }
//
// static int yetty_bind_term_raw(int fd, struct termios *saved) {
//     if (tcgetattr(fd, saved)) { return -1; }
//     struct termios raw = *saved;
//     raw.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF | BRKINT | INPCK | ISTRIP);
//     raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
//     return tcsetattr(fd, TCSANOW, &raw);
// }
// static int yetty_bind_term_restore(int fd, const struct termios *saved) { return tcsetattr(fd, TCSANOW, saved); }
// static int yetty_bind_poll_stdin(int timeout_ms) { struct pollfd slot = {0, POLLIN, 0}; return poll(&slot, 1, timeout_ms); }
// static void yetty_bind_winsize(unsigned short *out_rows_cols_px) {
//     struct winsize size; memset(&size, 0, sizeof size);
//     ioctl(1, TIOCGWINSZ, &size);
//     out_rows_cols_px[0] = size.ws_row; out_rows_cols_px[1] = size.ws_col;
//     out_rows_cols_px[2] = size.ws_xpixel; out_rows_cols_px[3] = size.ws_ypixel;
// }
import "C"

import (
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
	"unsafe"
)

// Color returns the packed 0xAABBGGRR wire color from "#RRGGBB[AA]".
func Color(value string) uint32 {
	text := strings.TrimPrefix(value, "#")
	red, _ := strconv.ParseUint(text[0:2], 16, 8)
	green, _ := strconv.ParseUint(text[2:4], 16, 8)
	blue, _ := strconv.ParseUint(text[4:6], 16, 8)
	alpha := uint64(0xFF)
	if len(text) >= 8 {
		alpha, _ = strconv.ParseUint(text[6:8], 16, 8)
	}
	return uint32(alpha<<24 | blue<<16 | green<<8 | red)
}

func packColor(value string) C.uint32_t {
	return C.uint32_t(Color(value))
}

// takeError flattens a native error's message and cause chain into one
// string, then destroys the native chain: the Result contract makes the
// receiver own the heap-linked causes — reading only the top message
// would leak one allocation per cause on every failed call.
func takeError(errorValue C.struct_yetty_ycore_error, fallback string) string {
	message := fallback
	if errorValue.msg != nil {
		message = C.GoString(errorValue.msg)
	}
	cause := errorValue.cause
	for guard := 0; cause != nil && guard < 64; guard++ {
		if cause.msg != nil {
			message += "; caused by: " + C.GoString(cause.msg)
		}
		cause = cause.cause
	}
	C.yetty_ycore_error_destroy(errorValue)
	return message
}

func checkVoid(result C.struct_yetty_ycore_void_result, what string) {
	if result.ok == 0 {
		panic(fmt.Sprintf("ygui2 %s: %s", what,
			takeError(C.yetty_bind_void_error(result), "yetty error")))
	}
}

func objectValue(result C.struct_yetty_yclass_object_ptr_result,
	what string) *C.struct_yetty_yclass_object {
	if result.ok == 0 {
		panic(fmt.Sprintf("ygui2 %s: %s", what,
			takeError(C.yetty_bind_object_error(result), "yetty create failed")))
	}
	return C.yetty_bind_object_value(result)
}

func classValue(result C.struct_yetty_yclass_ptr_result, what string) *C.struct_yetty_yclass {
	if result.ok == 0 {
		panic(fmt.Sprintf("ygui2 %s: %s", what,
			takeError(C.yetty_bind_class_error(result), "yetty class_get failed")))
	}
	return C.yetty_bind_class_value(result)
}

func checkInt(result C.struct_yetty_ycore_int_result, what string) C.int {
	if result.ok == 0 {
		panic(fmt.Sprintf("ygui2 %s: %s", what,
			takeError(C.yetty_bind_int_error(result), "yetty error")))
	}
	return C.yetty_bind_int_value(result)
}

func checkFloat(result C.struct_yetty_ycore_float_result, what string) C.float {
	if result.ok == 0 {
		panic(fmt.Sprintf("ygui2 %s: %s", what,
			takeError(C.yetty_bind_float_error(result), "yetty error")))
	}
	return C.yetty_bind_float_value(result)
}

func checkSize(result C.struct_yetty_ycore_size_result, what string) {
	if result.ok == 0 {
		panic(fmt.Sprintf("ygui2 %s: %s", what,
			takeError(C.yetty_bind_size_error(result), "yetty error")))
	}
}

// Float returns a pointer to value — the explicit-presence marker for
// optional numeric options whose default is not zero (e.g. a spinner's
// Maximum, a scrollarea's WheelStep, a dialog's X). A nil field means
// "use the documented default"; Float(0) means an explicit zero.
func Float(value float64) *float64 {
	return &value
}

func cString(text string) *C.char {
	return C.CString(text)
}

// The shared callback registry: bridges receive an id via userdata (the
// id-to-pointer round trip happens in C, never as a Go uintptr cast) and
// invoke the registered closure. Closures own the guard logic (recover +
// stop), so a callback panic never crosses the cgo boundary.
var callbackRegistry = struct {
	sync.Mutex
	nextID  uint64
	clicks  map[uint64]func()
	selects map[uint64]func(int)
	keys    map[uint64]func(uint32, uint32) int
	sinks   map[uint64]func([]byte)
}{
	nextID:  1,
	clicks:  map[uint64]func(){},
	selects: map[uint64]func(int){},
	keys:    map[uint64]func(uint32, uint32) int{},
	sinks:   map[uint64]func([]byte){},
}

func registerClick(callback func()) uint64 {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	id := callbackRegistry.nextID
	callbackRegistry.nextID++
	callbackRegistry.clicks[id] = callback
	return id
}

func registerSelect(callback func(int)) uint64 {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	id := callbackRegistry.nextID
	callbackRegistry.nextID++
	callbackRegistry.selects[id] = callback
	return id
}

func registerKey(callback func(uint32, uint32) int) uint64 {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	id := callbackRegistry.nextID
	callbackRegistry.nextID++
	callbackRegistry.keys[id] = callback
	return id
}

func registerSink(callback func([]byte)) uint64 {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	id := callbackRegistry.nextID
	callbackRegistry.nextID++
	callbackRegistry.sinks[id] = callback
	return id
}

func dropCallback(id uint64) {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	delete(callbackRegistry.clicks, id)
	delete(callbackRegistry.selects, id)
	delete(callbackRegistry.keys, id)
	delete(callbackRegistry.sinks, id)
}

func lookupClick(id uint64) func() {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	return callbackRegistry.clicks[id]
}

func lookupSelect(id uint64) func(int) {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	return callbackRegistry.selects[id]
}

func lookupKey(id uint64) func(uint32, uint32) int {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	return callbackRegistry.keys[id]
}

func lookupSink(id uint64) func([]byte) {
	callbackRegistry.Lock()
	defer callbackRegistry.Unlock()
	return callbackRegistry.sinks[id]
}

// Layout — flex layout parameters. A passed Layout is EXPLICIT in every
// field: all values (zeros included) are written, so zero padding, gap,
// grow, basis, cross-size, and minimum are all expressible — every
// exposed field's widget default is zero anyway, so an omitted field
// equals the default. Only the container direction (row/column preset)
// is preserved from the widget. Omitting the variadic Layout argument on
// a builder applies nothing at all. Pad fans out to all four sides
// (specific Pad* sides override it); Cross is the cross_size shorthand.
type Layout struct {
	Basis    float32
	Grow     float32
	Cross    float32
	MinMain  float32
	Gap      float32
	Pad      float32
	PadLeft  float32
	PadTop   float32
	PadRight float32
	PadBot   float32
}

func applyLayout(handle *C.struct_yetty_yclass_object, layout Layout) {
	// Copy first: the direction preset (row/column) lives C-side and must
	// survive; every other exposed field is written explicitly.
	var spec C.struct_yetty_ygui2_layout
	checkVoid(C.yetty_ygui2_widget_layout_copy(handle, &spec), "layout copy")
	spec.basis = C.float(layout.Basis)
	spec.grow = C.float(layout.Grow)
	spec.cross_size = C.float(layout.Cross)
	spec.min_main = C.float(layout.MinMain)
	spec.gap = C.float(layout.Gap)
	spec.pad_left = C.float(layout.Pad)
	spec.pad_top = C.float(layout.Pad)
	spec.pad_right = C.float(layout.Pad)
	spec.pad_bottom = C.float(layout.Pad)
	if layout.PadLeft != 0 {
		spec.pad_left = C.float(layout.PadLeft)
	}
	if layout.PadTop != 0 {
		spec.pad_top = C.float(layout.PadTop)
	}
	if layout.PadRight != 0 {
		spec.pad_right = C.float(layout.PadRight)
	}
	if layout.PadBot != 0 {
		spec.pad_bottom = C.float(layout.PadBot)
	}
	checkVoid(C.yetty_ygui2_widget_layout_set(handle, &spec), "layout set")
}

func applyLayouts(handle *C.struct_yetty_yclass_object, layouts []Layout) {
	if len(layouts) > 0 {
		applyLayout(handle, layouts[0])
	}
}

// Node — one widget in the tree. Builder methods create children;
// setters forward to the exposed C API. Never constructed directly —
// start from App.Root. Wrappers are liveness-tracked: after Remove() or
// Close() every affected Node panics instead of touching freed memory.
type Node struct {
	app             *App
	handle          *C.struct_yetty_yclass_object
	parent          *Node
	children        []*Node
	callbackIDs     []uint64
	invalidateHooks []func()
	alive           bool
}

func nodeNew(app *App, handle *C.struct_yetty_yclass_object, parent *Node) *Node {
	node := &Node{app: app, handle: handle, parent: parent, alive: true}
	if parent != nil {
		parent.children = append(parent.children, node)
	}
	return node
}

func (node *Node) live() *C.struct_yetty_yclass_object {
	if !node.alive || !node.app.alive {
		panic("ygui2 node is dead (removed or app closed)")
	}
	return node.handle
}

func (node *Node) invalidate() {
	for _, child := range node.children {
		child.invalidate()
	}
	node.children = nil
	for _, id := range node.callbackIDs {
		dropCallback(id)
	}
	node.callbackIDs = nil
	for _, hook := range node.invalidateHooks {
		hook()
	}
	node.invalidateHooks = nil
	node.alive = false
	node.handle = nil
}

func (node *Node) ownClick(callback func()) (C.yetty_ygui2_click_cb, unsafe.Pointer) {
	id := registerClick(callback)
	node.callbackIDs = append(node.callbackIDs, id)
	return C.yetty_bind_click_bridge(), C.yetty_bind_id_to_ptr(C.uint64_t(id))
}

func (node *Node) ownSelect(callback func(int)) (C.yetty_ygui2_select_cb, unsafe.Pointer) {
	id := registerSelect(callback)
	node.callbackIDs = append(node.callbackIDs, id)
	return C.yetty_bind_select_bridge(), C.yetty_bind_id_to_ptr(C.uint64_t(id))
}

func (node *Node) childNode(class C.struct_yetty_yclass_ptr_result, what string,
	layouts []Layout) *Node {
	handle := objectValue(C.yetty_ygui2_widget_add(node.live(), classValue(class, what)), what)
	child := nodeNew(node.app, handle, node)
	applyLayouts(handle, layouts)
	return child
}

// -- tree building ------------------------------------------------------

func (node *Node) Row(layouts ...Layout) *Node {
	handle := objectValue(C.yetty_ygui2_row_add(node.live()), "row")
	child := nodeNew(node.app, handle, node)
	applyLayouts(handle, layouts)
	return child
}

func (node *Node) Column(layouts ...Layout) *Node {
	handle := objectValue(C.yetty_ygui2_column_add(node.live()), "column")
	child := nodeNew(node.app, handle, node)
	applyLayouts(handle, layouts)
	return child
}

type PanelOpts struct {
	Bg          string
	Border      string
	BorderWidth float32
}

func (node *Node) Panel(opts PanelOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_panel_class_get(), "panel", layouts)
	if opts.Bg != "" {
		checkVoid(C.yetty_ygui2_panel_set_bg(child.handle, packColor(opts.Bg)), "panel bg")
	}
	if opts.Border != "" {
		width := opts.BorderWidth
		if width == 0 {
			width = 1.0
		}
		checkVoid(C.yetty_ygui2_panel_set_border(child.handle, packColor(opts.Border),
			C.float(width)), "panel border")
	}
	return child
}

type LabelOpts struct {
	Text string
	Fg   string
}

func (node *Node) Label(opts LabelOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_label_class_get(), "label", layouts)
	text := cString(opts.Text)
	defer C.free(unsafe.Pointer(text))
	checkVoid(C.yetty_ygui2_label_set_text(child.handle, text), "label text")
	if opts.Fg != "" {
		checkVoid(C.yetty_ygui2_label_set_color(child.handle, packColor(opts.Fg)), "label color")
	}
	return child
}

func (node *Node) Separator(layouts ...Layout) *Node {
	return node.childNode(C.yetty_ygui2_separator_class_get(), "separator", layouts)
}

type ButtonOpts struct {
	Label   string
	OnClick func()
}

func (node *Node) Button(opts ButtonOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_button_class_get(), "button", layouts)
	label := cString(opts.Label)
	defer C.free(unsafe.Pointer(label))
	checkVoid(C.yetty_ygui2_button_set_label(child.handle, label), "button label")
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "button focusable")
	if opts.OnClick != nil {
		app := node.app
		callback := opts.OnClick
		bridge, userdata := child.ownClick(func() { app.guard(callback) })
		checkVoid(C.yetty_ygui2_button_on_click_set(child.handle, bridge, userdata),
			"button on_click")
	}
	return child
}

type CheckboxOpts struct {
	Label    string
	Checked  bool
	OnToggle func(*Node)
}

func (node *Node) Checkbox(opts CheckboxOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_checkbox_class_get(), "checkbox", layouts)
	label := cString(opts.Label)
	defer C.free(unsafe.Pointer(label))
	checkVoid(C.yetty_ygui2_checkbox_set_label(child.handle, label), "checkbox label")
	if opts.Checked {
		checkVoid(C.yetty_ygui2_checkbox_set_checked(child.handle, 1), "checkbox checked")
	}
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "checkbox focusable")
	if opts.OnToggle != nil {
		app := node.app
		callback := opts.OnToggle
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(child) }) })
		checkVoid(C.yetty_ygui2_checkbox_on_toggle_set(child.handle, bridge, userdata),
			"checkbox on_toggle")
	}
	return child
}

type ToggleOpts struct {
	Label    string
	Checked  bool
	OnToggle func(*Node)
}

func (node *Node) Toggle(opts ToggleOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_toggle_class_get(), "toggle", layouts)
	label := cString(opts.Label)
	defer C.free(unsafe.Pointer(label))
	checkVoid(C.yetty_ygui2_toggle_set_label(child.handle, label), "toggle label")
	if opts.Checked {
		checkVoid(C.yetty_ygui2_toggle_set_checked(child.handle, 1), "toggle checked")
	}
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "toggle focusable")
	if opts.OnToggle != nil {
		app := node.app
		callback := opts.OnToggle
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(child) }) })
		checkVoid(C.yetty_ygui2_toggle_on_toggle_set(child.handle, bridge, userdata),
			"toggle on_toggle")
	}
	return child
}

type RadioOpts struct {
	Label    string
	Group    *RadioGroup
	Selected bool
	OnSelect func(int)
}

func (node *Node) Radio(opts RadioOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_radio_class_get(), "radio", layouts)
	label := cString(opts.Label)
	defer C.free(unsafe.Pointer(label))
	checkVoid(C.yetty_ygui2_radio_set_label(child.handle, label), "radio label")
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "radio focusable")
	if opts.Selected {
		checkVoid(C.yetty_ygui2_radio_set_selected(child.handle, 1), "radio selected")
	}
	if opts.Group != nil {
		opts.Group.register(child, opts.OnSelect)
	} else if opts.OnSelect != nil {
		app := node.app
		callback := opts.OnSelect
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(0) }) })
		checkVoid(C.yetty_ygui2_radio_on_select_set(child.handle, bridge, userdata),
			"radio on_select")
	}
	return child
}

type SliderOpts struct {
	Value    float64
	Minimum  float64  // explicit; zero is the default anyway
	Maximum  *float64 // nil = 1.0 (default != 0, so presence is a pointer)
	OnChange func(*Node)
}

func (node *Node) Slider(opts SliderOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_slider_class_get(), "slider", layouts)
	maximum := 1.0
	if opts.Maximum != nil {
		maximum = *opts.Maximum
	}
	if opts.Minimum != 0.0 || maximum != 1.0 {
		checkVoid(C.yetty_ygui2_slider_set_range(child.handle, C.float(opts.Minimum),
			C.float(maximum)), "slider range")
	}
	checkVoid(C.yetty_ygui2_slider_set_value(child.handle, C.float(opts.Value)), "slider value")
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "slider focusable")
	if opts.OnChange != nil {
		app := node.app
		callback := opts.OnChange
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(child) }) })
		checkVoid(C.yetty_ygui2_slider_on_change_set(child.handle, bridge, userdata),
			"slider on_change")
	}
	return child
}

type SpinnerOpts struct {
	Value    float64
	Minimum  float64  // explicit; zero is the default anyway
	Maximum  *float64 // nil = 100.0; Float(0) is an explicit zero maximum
	Step     *float64 // nil = 1.0
	OnChange func(*Node)
}

func (node *Node) Spinner(opts SpinnerOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_spinner_class_get(), "spinner", layouts)
	maximum := 100.0
	if opts.Maximum != nil {
		maximum = *opts.Maximum
	}
	step := 1.0
	if opts.Step != nil {
		step = *opts.Step
	}
	checkVoid(C.yetty_ygui2_spinner_configure(child.handle, C.float(opts.Minimum),
		C.float(maximum), C.float(step)), "spinner configure")
	checkVoid(C.yetty_ygui2_spinner_set_value(child.handle, C.float(opts.Value)), "spinner value")
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "spinner focusable")
	if opts.OnChange != nil {
		app := node.app
		callback := opts.OnChange
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(child) }) })
		checkVoid(C.yetty_ygui2_spinner_on_change_set(child.handle, bridge, userdata),
			"spinner on_change")
	}
	return child
}

type ProgressOpts struct {
	Value  float64
	Accent string
}

func (node *Node) Progress(opts ProgressOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_progress_class_get(), "progress", layouts)
	checkVoid(C.yetty_ygui2_progress_set_value(child.handle, C.float(opts.Value)),
		"progress value")
	if opts.Accent != "" {
		checkVoid(C.yetty_ygui2_progress_set_accent(child.handle, packColor(opts.Accent)),
			"progress accent")
	}
	return child
}

type ChipOpts struct {
	Label      string
	Selectable bool
	Selected   bool
	OnToggle   func(*Node)
}

func (node *Node) Chip(opts ChipOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_chip_class_get(), "chip", layouts)
	label := cString(opts.Label)
	defer C.free(unsafe.Pointer(label))
	checkVoid(C.yetty_ygui2_chip_set_label(child.handle, label), "chip label")
	if opts.Selectable {
		checkVoid(C.yetty_ygui2_chip_set_selectable(child.handle, 1), "chip selectable")
	}
	if opts.Selected {
		checkVoid(C.yetty_ygui2_chip_set_selected(child.handle, 1), "chip selected")
	}
	if opts.OnToggle != nil {
		app := node.app
		callback := opts.OnToggle
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(child) }) })
		checkVoid(C.yetty_ygui2_chip_on_toggle_set(child.handle, bridge, userdata),
			"chip on_toggle")
	}
	return child
}

type StatusbarOpts struct {
	Left  string
	Right string
}

func (node *Node) Statusbar(opts StatusbarOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_statusbar_class_get(), "statusbar", layouts)
	left := cString(opts.Left)
	defer C.free(unsafe.Pointer(left))
	right := cString(opts.Right)
	defer C.free(unsafe.Pointer(right))
	checkVoid(C.yetty_ygui2_statusbar_set_left(child.handle, left), "statusbar left")
	checkVoid(C.yetty_ygui2_statusbar_set_right(child.handle, right), "statusbar right")
	return child
}

type StepperOpts struct {
	Count   int
	Current int
}

func (node *Node) Stepper(opts StepperOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_stepper_class_get(), "stepper", layouts)
	count := opts.Count
	if count == 0 {
		count = 3
	}
	checkVoid(C.yetty_ygui2_stepper_set_count(child.handle, C.uint32_t(count)), "stepper count")
	checkVoid(C.yetty_ygui2_stepper_set_current(child.handle, C.uint32_t(opts.Current)),
		"stepper current")
	return child
}

type TextinputOpts struct {
	Text        string
	Placeholder string
	OnSubmit    func(*Node)
	OnChange    func(*Node)
}

func (node *Node) Textinput(opts TextinputOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_textinput_class_get(), "textinput", layouts)
	if opts.Text != "" {
		text := cString(opts.Text)
		checkVoid(C.yetty_ygui2_textinput_set_text(child.handle, text), "textinput text")
		C.free(unsafe.Pointer(text))
	}
	if opts.Placeholder != "" {
		placeholder := cString(opts.Placeholder)
		checkVoid(C.yetty_ygui2_textinput_set_placeholder(child.handle, placeholder),
			"textinput placeholder")
		C.free(unsafe.Pointer(placeholder))
	}
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "textinput focusable")
	app := node.app
	if opts.OnSubmit != nil {
		callback := opts.OnSubmit
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(child) }) })
		checkVoid(C.yetty_ygui2_textinput_on_submit_set(child.handle, bridge, userdata),
			"textinput on_submit")
	}
	if opts.OnChange != nil {
		callback := opts.OnChange
		bridge, userdata := child.ownClick(func() { app.guard(func() { callback(child) }) })
		checkVoid(C.yetty_ygui2_textinput_on_change_set(child.handle, bridge, userdata),
			"textinput on_change")
	}
	return child
}

type DropdownOpts struct {
	Items    []string
	Selected int // -1 (or 0 with no items) = none
	OnChange func(int)
}

func (node *Node) Dropdown(opts DropdownOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_dropdown_class_get(), "dropdown", layouts)
	for _, item := range opts.Items {
		text := cString(item)
		checkVoid(C.yetty_ygui2_dropdown_item_add(child.handle, text), "dropdown item")
		C.free(unsafe.Pointer(text))
	}
	if opts.Selected > 0 || (opts.Selected == 0 && len(opts.Items) > 0) {
		checkVoid(C.yetty_ygui2_dropdown_set_selected(child.handle, C.int(opts.Selected)),
			"dropdown selected")
	}
	checkVoid(C.yetty_ygui2_widget_set_focusable(child.handle, 1), "dropdown focusable")
	if opts.OnChange != nil {
		app := node.app
		callback := opts.OnChange
		id := registerSelect(func(index int) { app.guard(func() { callback(index) }) })
		child.callbackIDs = append(child.callbackIDs, id)
		checkVoid(C.yetty_ygui2_dropdown_on_change_set(child.handle, C.yetty_bind_select_bridge(),
			C.yetty_bind_id_to_ptr(C.uint64_t(id))), "dropdown on_change")
	}
	return child
}

type ScrollareaOpts struct {
	WheelStep *float64 // nil = 24.0; Float(0) disables wheel stepping
	MaxScroll *float64 // nil = 1000.0; Float(0) = clamp to the measured limit
}

func (node *Node) Scrollarea(opts ScrollareaOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_scrollarea_class_get(), "scrollarea", layouts)
	wheelStep := 24.0
	if opts.WheelStep != nil {
		wheelStep = *opts.WheelStep
	}
	maxScroll := 1000.0
	if opts.MaxScroll != nil {
		maxScroll = *opts.MaxScroll
	}
	checkVoid(C.yetty_ygui2_scrollarea_configure(child.handle, C.float(wheelStep),
		C.float(maxScroll)), "scrollarea configure")
	return child
}

type TableOpts struct {
	Columns []string
	Widths  []float32
}

func (node *Node) Table(opts TableOpts, layouts ...Layout) *Node {
	child := node.childNode(C.yetty_ygui2_table_class_get(), "table", layouts)
	if len(opts.Columns) > 0 {
		child.SetColumns(opts.Columns, opts.Widths)
	}
	return child
}

// -- common setters -----------------------------------------------------

func (node *Node) Layout(layout Layout) *Node {
	applyLayout(node.live(), layout)
	return node
}

func (node *Node) SetPosition(positionX, positionY float64) *Node {
	checkVoid(C.yetty_ygui2_widget_set_position(node.live(), C.float(positionX),
		C.float(positionY)), "set_position")
	return node
}

func (node *Node) SetSize(width, height float64) *Node {
	checkVoid(C.yetty_ygui2_widget_set_size(node.live(), C.float(width), C.float(height)),
		"set_size")
	return node
}

func (node *Node) SetVisible(visible bool) *Node {
	flag := C.int(0)
	if visible {
		flag = 1
	}
	checkVoid(C.yetty_ygui2_widget_set_visible(node.live(), flag), "set_visible")
	return node
}

func (node *Node) Rect() (float64, float64, float64, float64) {
	var out [4]C.float
	checkVoid(C.yetty_ygui2_widget_rect(node.live(), &out[0], &out[1], &out[2], &out[3]), "rect")
	return float64(out[0]), float64(out[1]), float64(out[2]), float64(out[3])
}

// Remove removes this widget (and its whole subtree) from the live tree.
// The native removal runs first — if it rejects (e.g. a root), the
// wrapper tree stays fully usable.
func (node *Node) Remove() {
	handle := node.live()
	if node.parent == nil {
		panic("ygui2: cannot remove a root node")
	}
	checkVoid(C.yetty_ygui2_widget_remove(handle), "remove")
	siblings := node.parent.children
	for index, child := range siblings {
		if child == node {
			node.parent.children = append(siblings[:index], siblings[index+1:]...)
			break
		}
	}
	node.invalidate()
}

// -- per-widget accessors (the C side rejects class mismatches) ----------

func (node *Node) SetText(text string) *Node {
	value := cString(text)
	defer C.free(unsafe.Pointer(value))
	checkVoid(C.yetty_ygui2_label_set_text(node.live(), value), "set_text")
	return node
}

func (node *Node) SetValue(value float64) *Node {
	checkVoid(C.yetty_ygui2_progress_set_value(node.live(), C.float(value)), "set_value")
	return node
}

func (node *Node) SliderValue() float64 {
	return float64(checkFloat(C.yetty_ygui2_slider_value(node.live()), "slider_value"))
}

func (node *Node) SpinnerValue() float64 {
	return float64(checkFloat(C.yetty_ygui2_spinner_value(node.live()), "spinner_value"))
}

func (node *Node) CheckboxChecked() bool {
	return checkInt(C.yetty_ygui2_checkbox_checked(node.live()), "checkbox_checked") != 0
}

func (node *Node) ToggleChecked() bool {
	return checkInt(C.yetty_ygui2_toggle_checked(node.live()), "toggle_checked") != 0
}

func (node *Node) InputText() string {
	var buffer [256]C.char
	checkSize(C.yetty_ygui2_textinput_text_copy(node.live(), &buffer[0], 256), "input_text")
	return C.GoString(&buffer[0])
}

func (node *Node) SetStatusLeft(text string) *Node {
	value := cString(text)
	defer C.free(unsafe.Pointer(value))
	checkVoid(C.yetty_ygui2_statusbar_set_left(node.live(), value), "status left")
	return node
}

func (node *Node) SetStatusRight(text string) *Node {
	value := cString(text)
	defer C.free(unsafe.Pointer(value))
	checkVoid(C.yetty_ygui2_statusbar_set_right(node.live(), value), "status right")
	return node
}

func (node *Node) StepperCurrent(current int) *Node {
	checkVoid(C.yetty_ygui2_stepper_set_current(node.live(), C.uint32_t(current)),
		"stepper_current")
	return node
}

func stringArray(values []string) ([]*C.char, **C.char, func()) {
	held := make([]*C.char, len(values))
	for index, value := range values {
		held[index] = cString(value)
	}
	release := func() {
		for _, item := range held {
			C.free(unsafe.Pointer(item))
		}
	}
	return held, (**C.char)(unsafe.Pointer(&held[0])), release
}

func (node *Node) SetColumns(columns []string, widths []float32) *Node {
	held, array, release := stringArray(columns)
	defer release()
	widthArray := make([]C.float, len(columns))
	for index := range columns {
		if index < len(widths) {
			widthArray[index] = C.float(widths[index])
		}
	}
	checkVoid(C.yetty_ygui2_table_set_columns(node.live(), array, &widthArray[0],
		C.uint32_t(len(held))), "set_columns")
	return node
}

func (node *Node) ClearRows() *Node {
	checkVoid(C.yetty_ygui2_table_clear_rows(node.live()), "clear_rows")
	return node
}

func (node *Node) AddRow(cells []string) *Node {
	held, array, release := stringArray(cells)
	defer release()
	checkVoid(C.yetty_ygui2_table_add_row(node.live(), array, C.uint32_t(len(held))), "add_row")
	return node
}

// RadioGroup — app-side radio-group semantics: selecting one clears the
// others (the C widget is deliberately dumb about groups). A removed
// member's slot is tombstoned, so a dynamic list retains nothing for
// dead radios.
type RadioGroup struct {
	members []radioMember
}

type radioMember struct {
	node     *Node
	onSelect func(int)
}

func NewRadioGroup() *RadioGroup {
	return &RadioGroup{}
}

func (group *RadioGroup) register(node *Node, onSelect func(int)) {
	index := len(group.members)
	group.members = append(group.members, radioMember{node: node, onSelect: onSelect})
	node.invalidateHooks = append(node.invalidateHooks, func() {
		group.members[index] = radioMember{}
	})
	app := node.app
	bridge, userdata := node.ownClick(func() {
		app.guard(func() {
			for memberIndex, member := range group.members {
				if memberIndex != index && member.node != nil && member.node.alive {
					checkVoid(C.yetty_ygui2_radio_set_selected(member.node.handle, 0),
						"radio clear")
				}
			}
			if callback := group.members[index].onSelect; callback != nil {
				callback(index)
			}
		})
	})
	checkVoid(C.yetty_ygui2_radio_on_select_set(node.handle, bridge, userdata), "radio on_select")
}

// App — the terminal host (Go port of yguiapp2): raw termios, alternate
// screen, pane-input subscription, poll loop with idle Esc flush,
// monotonic ticks, emit-on-dirty, HOLD/ACK barrier + unsubscribe on
// every exit path. Ctrl-C always quits; `q` quits while no text input
// holds focus.
type App struct {
	framework *C.struct_yetty_yclass_object
	Root      *Node
	running   bool
	alive     bool
	// Callbacks execute INSIDE native dispatch; disposing the framework
	// there is a native use-after-free when dispatch resumes. Close()
	// defers while this depth is nonzero; the dispatch helpers drain the
	// deferred close after the native call has fully returned.
	inCallback    int
	closePending  bool
	callbackError error
	appCallbacks  []uint64
	viewportW     float64
	viewportH     float64
}

func NewApp() *App {
	app := &App{}
	app.framework = objectValue(C.yetty_ygui2_framework_make(), "framework_make")
	app.alive = true
	app.viewportW, app.viewportH = probeViewport()
	rootHandle := objectValue(C.yetty_ygui2_framework_root_create(app.framework,
		classValue(C.yetty_ygui2_panel_class_get(), "panel class")), "root")
	checkVoid(C.yetty_ygui2_panel_set_bg(rootHandle, packColor("#0B1014")), "root bg")
	app.Root = nodeNew(app, rootHandle, nil)
	return app
}

// probeViewport — pane pixels from TIOCGWINSZ: exact ws_xpixel/ws_ypixel
// when the terminal reports them, cell estimate as fallback. The
// forwarded RESIZE envelope corrects either way.
func probeViewport() (float64, float64) {
	var fields [4]C.ushort
	C.yetty_bind_winsize(&fields[0])
	rows, cols := float64(fields[0]), float64(fields[1])
	xpixel, ypixel := float64(fields[2]), float64(fields[3])
	if xpixel > 0 && ypixel > 0 {
		return xpixel, ypixel
	}
	if cols == 0 {
		cols = 80
	}
	if rows == 0 {
		rows = 40
	}
	return cols * 8.0, rows * 16.0
}

// guard runs a callback under panic capture: a Go panic may not cross
// the cgo boundary, so it is stashed, the loop stops, and Run returns it.
func (app *App) guard(callback func()) {
	app.inCallback++
	defer func() {
		app.inCallback--
		if recovered := recover(); recovered != nil {
			if app.callbackError == nil {
				app.callbackError = fmt.Errorf("ygui2 callback: %v", recovered)
			}
			app.running = false
		}
	}()
	callback()
}

// drainPendingClose runs a Close() requested from inside a native
// callback. Called at the dispatch boundaries (after every native call
// that can invoke user callbacks has fully returned) — never inside the
// callback itself. A close failure lands in callbackError so Run
// surfaces it.
func (app *App) drainPendingClose() {
	if app.closePending && app.inCallback == 0 {
		app.closePending = false
		if closeError := app.Close(); closeError != nil && app.callbackError == nil {
			app.callbackError = closeError
		}
	}
}

// -- overlays -----------------------------------------------------------

func (app *App) overlay(class C.struct_yetty_yclass_ptr_result, what string) *Node {
	handle := objectValue(C.yetty_ygui2_framework_overlay_add(app.framework,
		classValue(class, what)), what)
	return nodeNew(app, handle, app.Root)
}

type DialogOpts struct {
	Title   string
	X       *float64 // nil = 100.0; Float(0) places at the left edge
	Y       *float64 // nil = 80.0; Float(0) places at the top edge
	Width   *float64 // nil = 280.0
	Height  *float64 // nil = 150.0
	OnClose func()
}

func floatOr(value *float64, fallback float64) float64 {
	if value != nil {
		return *value
	}
	return fallback
}

func (app *App) Dialog(opts DialogOpts) *Node {
	overlay := app.overlay(C.yetty_ygui2_dialog_class_get(), "dialog")
	title := cString(opts.Title)
	defer C.free(unsafe.Pointer(title))
	checkVoid(C.yetty_ygui2_dialog_set_title(overlay.handle, title), "dialog title")
	overlay.SetPosition(floatOr(opts.X, 100.0), floatOr(opts.Y, 80.0)).
		SetSize(floatOr(opts.Width, 280.0), floatOr(opts.Height, 150.0))
	applyLayout(overlay.handle, Layout{Gap: 6, PadLeft: 12, PadTop: 40, PadRight: 12})
	if opts.OnClose != nil {
		callback := opts.OnClose
		bridge, userdata := overlay.ownClick(func() { app.guard(callback) })
		checkVoid(C.yetty_ygui2_dialog_on_close_set(overlay.handle, bridge, userdata),
			"dialog on_close")
	}
	overlay.SetVisible(false)
	return overlay
}

type TooltipOpts struct {
	Text   string
	X      float64  // explicit; zero (the default) is the left edge
	Y      float64  // explicit; zero (the default) is the top edge
	Width  *float64 // nil = 190.0
	Height *float64 // nil = 24.0
}

func (app *App) Tooltip(opts TooltipOpts) *Node {
	overlay := app.overlay(C.yetty_ygui2_tooltip_class_get(), "tooltip")
	text := cString(opts.Text)
	defer C.free(unsafe.Pointer(text))
	checkVoid(C.yetty_ygui2_tooltip_set_text(overlay.handle, text), "tooltip text")
	overlay.SetPosition(opts.X, opts.Y).
		SetSize(floatOr(opts.Width, 190.0), floatOr(opts.Height, 24.0))
	overlay.SetVisible(false)
	return overlay
}

// -- lifecycle ----------------------------------------------------------

func (app *App) Quit() {
	app.running = false
}

// Alive reports whether the native framework is still owned by this App
// (false after Close has actually run; a close deferred from inside a
// callback keeps it true until the dispatch boundary drains it).
func (app *App) Alive() bool {
	return app.alive
}

// Close — idempotent teardown: clear the surface, unsubscribe pane
// input, dispose the native framework, then invalidate every wrapper.
// Safe to call from any state — INCLUDING widget/sink callbacks: native
// dispatch is still executing there, so the actual disposal is DEFERRED
// to the moment the native call returns (the dispatch helpers drain
// it); the loop also stops immediately.
func (app *App) Close() error {
	if app.inCallback > 0 && app.alive {
		app.closePending = true
		app.running = false
		return nil
	}
	if !app.alive {
		return nil
	}
	app.alive = false
	app.running = false
	app.closePending = false
	var messages []string
	if result := C.yetty_ygui2_framework_clear(app.framework); result.ok == 0 {
		messages = append(messages, "clear: "+takeError(C.yetty_bind_void_error(result),
			"yetty error"))
	}
	if result := C.yetty_ygui2_framework_detach(app.framework); result.ok == 0 {
		messages = append(messages, "detach: "+takeError(C.yetty_bind_void_error(result),
			"yetty error"))
	}
	if result := C.yetty_ygui2_framework_dispose(app.framework); result.ok == 0 {
		messages = append(messages, "dispose: "+takeError(C.yetty_bind_void_error(result),
			"yetty error"))
	}
	if app.Root != nil {
		app.Root.invalidate()
	}
	for _, id := range app.appCallbacks {
		dropCallback(id)
	}
	app.appCallbacks = nil
	app.framework = nil
	if len(messages) > 0 {
		return errors.New("ygui2 close: " + strings.Join(messages, "; "))
	}
	return nil
}

// SetSink routes emitted envelopes into a Go callback instead of an
// attached fd — the headless capture hook (binding tests).
func (app *App) SetSink(sink func([]byte)) {
	id := registerSink(func(payload []byte) { app.guard(func() { sink(payload) }) })
	app.appCallbacks = append(app.appCallbacks, id)
	checkVoid(C.yetty_ygui2_framework_set_sink(app.framework, C.yetty_bind_sink_bridge(),
		C.yetty_bind_id_to_ptr(C.uint64_t(id))), "set_sink")
}

func (app *App) Emit() {
	// The sink callback runs INSIDE this native call: a Close() it
	// requests is drained only after the native emit has returned.
	result := C.yetty_ygui2_framework_emit(app.framework)
	app.drainPendingClose()
	checkVoid(result, "emit")
}

func (app *App) SetViewport(width, height float64) {
	checkVoid(C.yetty_ygui2_framework_set_viewport(app.framework, C.float(width),
		C.float(height)), "set_viewport")
}

// SetFullscreen chooses the reservation mode (strategy.md §5): fullscreen
// (default) reserves the full supported viewport range; inline (false)
// reserves the declared viewport height and lives in the scrollback
// flow. Must be chosen BEFORE the first emit — the C side rejects the
// call once inserted.
func (app *App) SetFullscreen(fullscreen bool) {
	flag := C.int(0)
	if fullscreen {
		flag = 1
	}
	checkVoid(C.yetty_ygui2_framework_set_fullscreen(app.framework, flag), "set_fullscreen")
}

// ContentScale returns the committed HiDPI input divisor (1.0 until a
// pane-resize envelope carries a different scale).
func (app *App) ContentScale() float64 {
	var outScale C.float
	checkVoid(C.yetty_ygui2_framework_content_scale(app.framework, &outScale), "content_scale")
	return float64(outScale)
}

func (app *App) FeedMouseButton(mouseX, mouseY float64, button int, pressed bool, mods int) {
	pressedFlag := C.int(0)
	if pressed {
		pressedFlag = 1
	}
	result := C.yetty_ygui2_framework_feed_mouse_button(app.framework, C.float(mouseX),
		C.float(mouseY), C.int(button), pressedFlag, C.int(mods))
	app.drainPendingClose()
	checkVoid(result, "feed_mouse_button")
}

func (app *App) FeedMouseScroll(mouseX, mouseY, wheelDy float64) {
	result := C.yetty_ygui2_framework_feed_mouse_scroll(app.framework, C.float(mouseX),
		C.float(mouseY), C.float(wheelDy))
	app.drainPendingClose()
	checkVoid(result, "feed_mouse_scroll")
}

// -- the host loop ------------------------------------------------------

type RunOpts struct {
	Tick       func()
	TickMillis int
}

func writeStdout(text string) {
	os.Stdout.WriteString(text)
}

func (app *App) Run(opts RunOpts) error {
	if !app.alive {
		return errors.New("ygui2 app is closed")
	}
	if app.running {
		return errors.New("ygui2 app is already running")
	}
	tickMillis := opts.TickMillis
	if tickMillis <= 0 {
		tickMillis = 250 // match the C runner's clamp
	}
	var saved C.struct_termios
	if C.yetty_bind_term_raw(0, &saved) != 0 {
		return errors.New("ygui2 run: stdin is not a terminal")
	}
	loopError := app.runRaw(opts.Tick, tickMillis)
	// Terminal restoration is never skippable and each step stands alone.
	writeStdout("\x1b[?25h\x1b[?1049l")
	C.yetty_bind_term_restore(0, &saved)
	if app.callbackError != nil {
		primary := app.callbackError
		app.callbackError = nil
		if loopError != nil {
			return fmt.Errorf("%w; caused by: %v", primary, loopError)
		}
		return primary
	}
	return loopError
}

func (app *App) runRaw(tick func(), tickMillis int) (loopError error) {
	defer func() {
		if recovered := recover(); recovered != nil {
			loopError = fmt.Errorf("ygui2 run: %v", recovered)
		}
		app.running = false
		app.teardown()
	}()

	writeStdout("\x1b[?1049h\x1b[?25l\x1b[H")
	app.running = true

	quitKeyID := registerKey(func(key, mods uint32) int {
		if key == 'q' || key == 0x03 {
			app.running = false
			return 1
		}
		return 0
	})
	app.appCallbacks = append(app.appCallbacks, quitKeyID)
	checkVoid(C.yetty_ygui2_framework_set_key_cb(app.framework, C.yetty_bind_key_bridge(),
		C.yetty_bind_id_to_ptr(C.uint64_t(quitKeyID))), "set_key_cb")
	checkVoid(C.yetty_ygui2_framework_attach(app.framework, 0, 1), "attach")
	app.SetViewport(app.viewportW, app.viewportH)
	app.Emit()

	buffer := make([]byte, 4096)
	tickInterval := time.Duration(tickMillis) * time.Millisecond
	// Monotonic tick schedule: input bursts must not drive the tick
	// faster than tickMillis.
	nextTick := time.Now().Add(tickInterval)
	for app.running {
		timeout := time.Until(nextTick)
		if timeout < 0 {
			timeout = 0
		}
		ready := C.yetty_bind_poll_stdin(C.int(timeout.Milliseconds()))
		if ready > 0 {
			count, readError := os.Stdin.Read(buffer)
			if readError != nil || count == 0 {
				break
			}
			feedResult := C.yetty_ygui2_framework_feed_input(app.framework,
				(*C.uint8_t)(unsafe.Pointer(&buffer[0])), C.size_t(count))
			app.drainPendingClose()
			checkVoid(feedResult, "feed_input")
			if !app.alive {
				break
			}
		}
		if now := time.Now(); !now.Before(nextTick) {
			flushResult := C.yetty_ygui2_framework_feed_input_flush(app.framework)
			app.drainPendingClose()
			checkVoid(flushResult, "input flush")
			if !app.alive {
				break
			}
			if tick != nil {
				app.guard(tick)
				app.drainPendingClose()
				if !app.alive {
					break
				}
			}
			nextTick = now.Add(tickInterval)
		}
		if app.alive && checkInt(C.yetty_ygui2_framework_is_dirty(app.framework), "is_dirty") != 0 {
			app.Emit()
		}
	}
	return nil
}

// teardown — exit-window barrier then Close(). The barrier is
// best-effort: no failure may skip the unsubscribe/dispose in Close().
func (app *App) teardown() {
	if !app.alive {
		return
	}
	func() {
		defer func() { recover() }() // a wedged barrier must never block the real teardown
		checkVoid(C.yetty_ygui2_framework_send_hold(app.framework), "send_hold")
		deadline := time.Now().Add(500 * time.Millisecond) // wall-clock bound
		buffer := make([]byte, 4096)
		for time.Now().Before(deadline) {
			if checkInt(C.yetty_ygui2_framework_hold_ack_seen(app.framework),
				"hold_ack_seen") != 0 {
				break
			}
			if C.yetty_bind_poll_stdin(50) > 0 {
				count, readError := os.Stdin.Read(buffer)
				if readError != nil || count == 0 {
					break
				}
				feedResult := C.yetty_ygui2_framework_feed_input(app.framework,
					(*C.uint8_t)(unsafe.Pointer(&buffer[0])), C.size_t(count))
				app.drainPendingClose()
				checkVoid(feedResult, "feed_input")
				if !app.alive {
					break
				}
			}
		}
	}()
	app.Close()
}
