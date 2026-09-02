"""yetty.ygui2 — pythonic wrapper over the generated ygui2 FFI bindings.

ygui2 is the drawable-contract widget toolkit: the widget tree projects
onto the terminal's wire group tree, every frame is a DCS drawable
envelope on stdout, and steady-state updates are incremental. A Python
ygui2 app is a plain PTY client — run it INSIDE a yetty pane:

    PYTHONPATH=bindings/python python3 demo/ffi/ygui2/python/counter.py

Layer map:
  - `yetty.generated.ygui2` — raw ctypes functions, one per exposed C
    symbol (GENERATED from model.yaml; import as `_g` here). Framework()
    routes to `framework_make`; widget classes are factory-owned and not
    directly constructible.
  - this module — the ergonomic layer: `App` (the terminal host loop:
    raw termios + alternate screen + select loop + HOLD/ACK teardown,
    the Python port of src/yetty/yguiapp2), `Node` builders
    (`col.button(label=..., on_click=fn)`), and callback trampolines.

Ownership and liveness: the native tree is owned by the framework. Every
`Node` is a wrapper that the App tracks; `Node.remove()` invalidates the
whole wrapper subtree (and releases its callbacks) before the native
subtree is destroyed, and `App.close()` invalidates everything. A call on
a dead node raises YettyError instead of touching freed memory. Callback
exceptions are captured at the ctypes boundary, stop the app, and re-raise
from `run()`.

Colors are packed 0xAABBGGRR (the SDF fill word) or "#RRGGBB[AA]"
strings — `color()` converts.

Quit keys: Ctrl-C always quits (the host runs with ISIG off and handles
byte 0x03 itself). `q` quits while no text input holds focus — a focused
textinput consumes printable keys first.
"""
from __future__ import annotations

import array
import ctypes
import fcntl
import inspect
import os
import select
import sys
import termios
import time
from ctypes import (CFUNCTYPE, POINTER, byref, c_float, c_int, c_size_t, c_uint8, c_uint32,
                    c_void_p, cast, create_string_buffer)
from typing import Any, Callable

from . import runtime as _rt
from .generated import _types as _t
from .generated import ygui2 as _g

if not _rt.has_symbol("yetty_ygui2_framework_make"):
    raise ImportError(
        "the loaded libyetty_ffi.so does not export the ygui2 toolkit "
        f"(yetty_ffi_version symbol {'present' if _rt.has_symbol('yetty_ffi_version') else 'absent'}) "
        "— rebuild it (USE_DISTCC=1 make build-desktop-ytrace-release) or point "
        "YETTY_FFI_LIB at a current build")

COLUMN = 0
ROW = 1

CLICK_CB = CFUNCTYPE(None, c_void_p, c_void_p)
SELECT_CB = CFUNCTYPE(None, c_void_p, c_uint32, c_void_p)
KEY_CB = CFUNCTYPE(c_int, c_uint32, c_uint32, c_void_p)
SINK_CB = CFUNCTYPE(None, POINTER(c_uint8), c_size_t, c_void_p)

# The ABI source of truth is the GENERATED struct set — aliased, not
# duplicated (a manual copy here is exactly the drift point that once
# missed a signature change).
Layout = _t.yetty_ygui2_layout
Theme = _t.yetty_ygui2_theme


def color(value) -> int:
    """Packed 0xAABBGGRR from an int (passed through) or '#RRGGBB[AA]'."""
    if isinstance(value, int):
        return value
    text = value.lstrip("#")
    red, green, blue = (int(text[i:i + 2], 16) for i in (0, 2, 4))
    alpha = int(text[6:8], 16) if len(text) >= 8 else 0xFF
    return (alpha << 24) | (blue << 16) | (green << 8) | red


def _check(result):
    if result.error is not None:
        raise _rt.YettyError(result.error.message)
    return result.value


def _class_ptr(generated_class) -> Any:
    return _check(generated_class.yclass())


_LAYOUT_KEYS = ("basis", "grow", "cross", "cross_size", "min_main", "direction", "gap",
                "pad", "pad_left", "pad_top", "pad_right", "pad_bottom")


def _apply_layout(handle, picked: dict) -> None:
    """Read-modify-write the widget's layout: partial kwargs must never
    clobber defaults the C side installed (row/column ship with their
    direction preset)."""
    spec = Layout()
    _check(_g.widget_layout_copy(handle, byref(spec)))
    if "pad" in picked:
        pad = picked.pop("pad")
        spec.pad_left = spec.pad_top = spec.pad_right = spec.pad_bottom = pad
    if "cross" in picked:
        picked["cross_size"] = picked.pop("cross")
    for key, value in picked.items():
        setattr(spec, key, value)
    _check(_g.widget_layout_set(handle, byref(spec)))


def _pick_layout(kwargs: dict, where: str) -> dict:
    """Pop the layout kwargs; anything left over is a typo — reject it
    loudly instead of silently ignoring a misspelled option."""
    picked = {key: kwargs.pop(key) for key in list(kwargs) if key in _LAYOUT_KEYS}
    if kwargs:
        raise TypeError(f"{where}() got unexpected keyword argument(s): "
                        f"{', '.join(sorted(kwargs))}")
    return picked


def _as_sample_array(samples):
    """Any number sequence (list, tuple, array('f'), generator) to the
    contiguous c_float array the native streaming calls expect."""
    values = [float(value) for value in samples]
    if not values:
        raise _rt.YettyError("ygui2 samples: empty sequence")
    return (c_float * len(values))(*values)


def _takes_arg(callback: Callable) -> bool:
    try:
        return len(inspect.signature(callback).parameters) >= 1
    except (TypeError, ValueError):
        return False


def _chain_failures(failures: list) -> BaseException:
    """Link every failure into one explicit cause chain (appending at the
    TAIL of each exception's existing chain, never overwriting a cause the
    application already set) and return the primary to raise. Nothing
    observed disappears: secondary failures ride behind the primary."""
    for earlier, later in zip(failures, failures[1:]):
        tail = earlier
        while tail.__cause__ is not None:
            tail = tail.__cause__
        tail.__cause__ = later
    return failures[0]


class Node:
    """One widget in the tree. Builder methods create children; setters
    forward to the exposed C API. Never constructed directly — start from
    `App.root`. Wrappers are LIVENESS-TRACKED: after `remove()` or
    `App.close()` every affected Node raises instead of touching freed
    native memory."""

    def __init__(self, app: "App", handle, parent: "Node | None") -> None:
        self._app = app
        self._handle = handle
        self._parent = parent
        self._children: list[Node] = []
        self._callbacks: list[Any] = []  # CFUNCTYPE refs owned by THIS node
        self._invalidate_hooks: list[Callable] = []  # e.g. RadioGroup slot clears
        self._alive = True
        if parent is not None:
            parent._children.append(self)

    @property
    def handle(self):
        return self._live()

    def _live(self):
        if not self._alive or not self._app._alive:
            raise _rt.YettyError("ygui2 node is dead (removed or app closed)")
        return self._handle

    def _invalidate(self) -> None:
        """Mark this wrapper subtree dead and drop its callback roots."""
        for child in self._children:
            child._invalidate()
        self._children.clear()
        self._callbacks.clear()
        for hook in self._invalidate_hooks:
            hook()
        self._invalidate_hooks.clear()
        self._alive = False
        self._handle = None

    # -- tree building ---------------------------------------------------
    def _rollback(self, node: "Node") -> BaseException | None:
        """A builder step failed AFTER the native child existed: remove the
        NATIVE side first (matching remove() ordering), then drop the
        wrapper. Returns the native removal failure (if any) so the caller
        can chain it behind the primary builder exception instead of
        losing it. On a native removal failure the wrapper stays LINKED
        AND LIVE: the native child still exists, and discarding the only
        tracked handle would recreate exactly the native/Python divergence
        this helper prevents — the caller can inspect, retry remove(), or
        close the app."""
        handle = node._handle
        if handle is not None:
            rollback_res = _g.widget_remove(handle)
            if rollback_res.error is not None:
                return _rt.YettyError(f"rollback: {rollback_res.error.message}")
        if node._parent is not None and node in node._parent._children:
            node._parent._children.remove(node)
        node._invalidate()
        return None

    def _configured(self, node: "Node", configure: Callable) -> "Node":
        try:
            configure(node)
            return node
        except BaseException as builder_error:
            rollback_failure = self._rollback(node)
            if rollback_failure is not None:
                raise builder_error from rollback_failure
            raise

    def _child(self, generated_class, picked: dict) -> "Node":
        node = Node(self._app, _check(_g.widget_add(self._live(),
                                                    _class_ptr(generated_class))), self)
        return self._configured(node, lambda configured: _apply_layout(
            configured._handle, picked) if picked else None)

    def row(self, **kwargs) -> "Node":
        picked = _pick_layout(kwargs, "row")
        node = Node(self._app, _check(_g.row_add(self._live())), self)
        return self._configured(node, lambda n: _apply_layout(n._handle, picked)
                                if picked else None)

    def column(self, **kwargs) -> "Node":
        picked = _pick_layout(kwargs, "column")
        node = Node(self._app, _check(_g.column_add(self._live())), self)
        return self._configured(node, lambda n: _apply_layout(n._handle, picked)
                                if picked else None)

    def panel(self, bg=None, border=None, border_width=1.0, **kwargs) -> "Node":
        def configure(node):
            if bg is not None:
                _check(_g.panel_set_bg(node._handle, color(bg)))
            if border is not None:
                _check(_g.panel_set_border(node._handle, color(border), border_width))
        return self._configured(self._child(_g.Panel, _pick_layout(kwargs, "panel")), configure)

    def label(self, text="", fg=None, **kwargs) -> "Node":
        def configure(node):
            _check(_g.label_set_text(node._handle, text))
            if fg is not None:
                _check(_g.label_set_color(node._handle, color(fg)))
        return self._configured(self._child(_g.Label, _pick_layout(kwargs, "label")), configure)

    def separator(self, **kwargs) -> "Node":
        return self._child(_g.Separator, _pick_layout(kwargs, "separator"))

    def button(self, label="", on_click: Callable | None = None, **kwargs) -> "Node":
        def configure(node):
            _check(_g.button_set_label(node._handle, label))
            _check(_g.widget_set_focusable(node._handle, 1))
            if on_click is not None:
                _check(_g.button_on_click_set(
                    node._handle, self._app._click_trampoline(node, on_click), None))
        return self._configured(self._child(_g.Button, _pick_layout(kwargs, "button")), configure)

    def checkbox(self, label="", checked=False, on_toggle: Callable | None = None,
                 **kwargs) -> "Node":
        def configure(node):
            _check(_g.checkbox_set_label(node._handle, label))
            if checked:
                _check(_g.checkbox_set_checked(node._handle, 1))
            _check(_g.widget_set_focusable(node._handle, 1))
            if on_toggle is not None:
                _check(_g.checkbox_on_toggle_set(
                    node._handle, self._app._click_trampoline(node, on_toggle), None))
        return self._configured(self._child(_g.Checkbox, _pick_layout(kwargs, "checkbox")),
                                configure)

    def toggle(self, label="", checked=False, on_toggle: Callable | None = None,
               **kwargs) -> "Node":
        def configure(node):
            _check(_g.toggle_set_label(node._handle, label))
            if checked:
                _check(_g.toggle_set_checked(node._handle, 1))
            _check(_g.widget_set_focusable(node._handle, 1))
            if on_toggle is not None:
                _check(_g.toggle_on_toggle_set(
                    node._handle, self._app._click_trampoline(node, on_toggle), None))
        return self._configured(self._child(_g.Toggle, _pick_layout(kwargs, "toggle")), configure)

    def radio(self, label="", group: "RadioGroup | None" = None, selected=False,
              on_select: Callable | None = None, **kwargs) -> "Node":
        def configure(node):
            _check(_g.radio_set_label(node._handle, label))
            _check(_g.widget_set_focusable(node._handle, 1))
            if selected:
                _check(_g.radio_set_selected(node._handle, 1))
            if group is not None:
                group._register(node, on_select)
            elif on_select is not None:
                _check(_g.radio_on_select_set(
                    node._handle, self._app._click_trampoline(node, on_select), None))
        return self._configured(self._child(_g.Radio, _pick_layout(kwargs, "radio")), configure)

    def slider(self, value=0.0, minimum=0.0, maximum=1.0, on_change: Callable | None = None,
               **kwargs) -> "Node":
        def configure(node):
            if (minimum, maximum) != (0.0, 1.0):
                _check(_g.slider_set_range(node._handle, minimum, maximum))
            _check(_g.slider_set_value(node._handle, value))
            _check(_g.widget_set_focusable(node._handle, 1))
            if on_change is not None:
                _check(_g.slider_on_change_set(
                    node._handle, self._app._click_trampoline(node, on_change), None))
        return self._configured(self._child(_g.Slider, _pick_layout(kwargs, "slider")), configure)

    def spinner(self, value=0.0, minimum=0.0, maximum=100.0, step=1.0,
                on_change: Callable | None = None, **kwargs) -> "Node":
        def configure(node):
            _check(_g.spinner_configure(node._handle, minimum, maximum, step))
            _check(_g.spinner_set_value(node._handle, value))
            _check(_g.widget_set_focusable(node._handle, 1))
            if on_change is not None:
                _check(_g.spinner_on_change_set(
                    node._handle, self._app._click_trampoline(node, on_change), None))
        return self._configured(self._child(_g.Spinner, _pick_layout(kwargs, "spinner")),
                                configure)

    def plot(self, title=None, expression=None, x_range=None, y_range=None,
             stream=None, **kwargs) -> "Node":
        """A yplot complex drawable as a widget. `expression` is a raw
        plot-DSL fragment (curves, colors, axis attributes); `stream`
        declares THE single streamed buffer as (name, capacity, color) —
        a size-only zero-filled slot fed with `append_samples()` at ~40
        bytes per sample on the wire (the receiver's ring unwrap scrolls
        the window). Capacity must be within 2..65536. Resize is ONE tiny
        addressed geometry op — the record and its samples are never
        re-sent; only a structural DSL change replaces the record, and
        that insertion replays the cached window inside its own
        envelope."""
        def configure(node):
            if title is not None:
                _check(_g.plot_set_title(node._handle, title))
            if expression is not None:
                _check(_g.plot_set_expression(node._handle, expression))
            if x_range is not None:
                _check(_g.plot_set_x_range(node._handle, float(x_range[0]),
                                           float(x_range[1])))
            if y_range is not None:
                _check(_g.plot_set_y_range(node._handle, float(y_range[0]),
                                           float(y_range[1])))
            if stream is not None:
                stream_name, stream_capacity, stream_color = stream
                capacity = int(stream_capacity)
                if capacity < 2 or capacity > 65536:
                    # Validated BEFORE the ctypes u32 conversion: a Python
                    # -1 would otherwise wrap to a 16 GiB allocation.
                    raise ValueError(
                        "plot stream capacity must be within 2..65536, got "
                        f"{stream_capacity!r}")
                _check(_g.plot_add_stream_buffer(node._handle, stream_name,
                                                 capacity, stream_color))
        return self._configured(self._child(_g.Plot, _pick_layout(kwargs, "plot")),
                                configure)

    def append_samples(self, samples) -> "Node":
        """Append to a plot's streamed window — O(new samples) on the
        wire. Accepts any sequence of numbers."""
        values = _as_sample_array(samples)
        _check(_g.plot_append_samples(self._live(), values, len(values)))
        return self

    def stream_samples(self, samples) -> "Node":
        """Bulk-overwrite a plot's streamed window from sample 0."""
        values = _as_sample_array(samples)
        _check(_g.plot_stream_samples(self._live(), values, len(values)))
        return self

    def progress(self, value=0.0, accent=None, **kwargs) -> "Node":
        def configure(node):
            _check(_g.progress_set_value(node._handle, value))
            if accent is not None:
                _check(_g.progress_set_accent(node._handle, color(accent)))
        return self._configured(self._child(_g.Progress, _pick_layout(kwargs, "progress")),
                                configure)

    def chip(self, label="", selectable=False, selected=False,
             on_toggle: Callable | None = None, **kwargs) -> "Node":
        def configure(node):
            _check(_g.chip_set_label(node._handle, label))
            if selectable:
                _check(_g.chip_set_selectable(node._handle, 1))
            if selected:
                _check(_g.chip_set_selected(node._handle, 1))
            if on_toggle is not None:
                _check(_g.chip_on_toggle_set(
                    node._handle, self._app._click_trampoline(node, on_toggle), None))
        return self._configured(self._child(_g.Chip, _pick_layout(kwargs, "chip")), configure)

    def statusbar(self, left="", right="", **kwargs) -> "Node":
        def configure(node):
            _check(_g.statusbar_set_left(node._handle, left))
            _check(_g.statusbar_set_right(node._handle, right))
        return self._configured(self._child(_g.Statusbar, _pick_layout(kwargs, "statusbar")),
                                configure)

    def stepper(self, count=3, current=0, **kwargs) -> "Node":
        def configure(node):
            _check(_g.stepper_set_count(node._handle, count))
            _check(_g.stepper_set_current(node._handle, current))
        return self._configured(self._child(_g.Stepper, _pick_layout(kwargs, "stepper")),
                                configure)

    def textinput(self, text="", placeholder="", on_submit: Callable | None = None,
                  on_change: Callable | None = None, **kwargs) -> "Node":
        def configure(node):
            if text:
                _check(_g.textinput_set_text(node._handle, text))
            if placeholder:
                _check(_g.textinput_set_placeholder(node._handle, placeholder))
            _check(_g.widget_set_focusable(node._handle, 1))
            if on_submit is not None:
                _check(_g.textinput_on_submit_set(
                    node._handle, self._app._click_trampoline(node, on_submit), None))
            if on_change is not None:
                _check(_g.textinput_on_change_set(
                    node._handle, self._app._click_trampoline(node, on_change), None))
        return self._configured(self._child(_g.Textinput, _pick_layout(kwargs, "textinput")),
                                configure)

    def dropdown(self, items=(), selected=-1, on_change: Callable | None = None,
                 **kwargs) -> "Node":
        def configure(node):
            for item in items:
                _check(_g.dropdown_item_add(node._handle, item))
            if selected >= 0:
                _check(_g.dropdown_set_selected(node._handle, selected))
            _check(_g.widget_set_focusable(node._handle, 1))
            if on_change is not None:
                _check(_g.dropdown_on_change_set(
                    node._handle, self._app._select_trampoline(node, on_change), None))
        return self._configured(self._child(_g.Dropdown, _pick_layout(kwargs, "dropdown")),
                                configure)

    def scrollarea(self, wheel_step=24.0, max_scroll=1000.0, **kwargs) -> "Node":
        def configure(node):
            _check(_g.scrollarea_configure(node._handle, wheel_step, max_scroll))
        return self._configured(self._child(_g.Scrollarea, _pick_layout(kwargs, "scrollarea")),
                                configure)

    def table(self, columns=(), widths=None, **kwargs) -> "Node":
        def configure(node):
            if columns:
                node.set_columns(columns, widths)
        return self._configured(self._child(_g.Table, _pick_layout(kwargs, "table")), configure)

    def complex_host(self, record_words, child_node_id: int, **kwargs) -> "Node":
        """T5 carrier: hosts one own-id COMPLEX record (plot/image/…). The
        creation record ships once; stream() then sends addressed runtime
        payloads without any repaint. `record_words` is EXACTLY ONE complex
        creation record ([type][payload_size][payload…] u32 words) — the C
        side rejects commands and malformed sizes."""
        def configure(node):
            words = record_words
            if not isinstance(words, ctypes.Array):
                words = (c_uint32 * len(words))(*words)
            _check(_g.complex_host_set_record(node._handle, words, len(words), child_node_id))
        return self._configured(self._child(_g.ComplexHost,
                                            _pick_layout(kwargs, "complex_host")), configure)

    def ydraw_embed(self, drawable_list=None, **kwargs) -> "Node":
        """T6 embed: hosts a drawable list as this widget's group body.
        `drawable_list` is a v2 DrawableList (yetty.ydraw) or a raw list
        handle. OWNERSHIP TRANSFERS to the widget on success — do not
        destroy or reuse the list afterwards. Leaf records only: command
        records are rejected (and the caller keeps ownership)."""
        def configure(node):
            if drawable_list is not None:
                node.set_embed(drawable_list)
        return self._configured(self._child(_g.YdrawEmbed,
                                            _pick_layout(kwargs, "ydraw_embed")), configure)

    # -- common setters --------------------------------------------------
    def layout(self, **kwargs) -> "Node":
        picked = _pick_layout(kwargs, "layout")
        if picked:
            _apply_layout(self._live(), picked)
        return self

    def set_position(self, x, y) -> "Node":
        _check(_g.widget_set_position(self._live(), x, y))
        return self

    def set_size(self, width, height) -> "Node":
        _check(_g.widget_set_size(self._live(), width, height))
        return self

    def set_visible(self, visible: bool) -> "Node":
        _check(_g.widget_set_visible(self._live(), 1 if visible else 0))
        return self

    def remove(self) -> None:
        """Remove this widget (and its whole subtree) from the live tree.
        The NATIVE removal runs first — if it rejects (e.g. a root), the
        Python tree stays fully usable. On success there is no callback
        window before the wrappers are invalidated, so nothing can call
        into freed memory."""
        handle = self._live()
        if self._parent is None:
            raise _rt.YettyError("ygui2: cannot remove a root node")
        _check(_g.widget_remove(handle))
        if self in self._parent._children:
            self._parent._children.remove(self)
        self._invalidate()

    # -- per-widget accessors (raise on a class mismatch) ----------------
    def set_text(self, text: str) -> "Node":
        _check(_g.label_set_text(self._live(), text))
        return self

    def set_value(self, value: float) -> "Node":
        _check(_g.progress_set_value(self._live(), value))
        return self

    def slider_value(self) -> float:
        return _check(_g.slider_value(self._live()))

    def spinner_value(self) -> float:
        return _check(_g.spinner_value(self._live()))

    def checkbox_checked(self) -> bool:
        return bool(_check(_g.checkbox_checked(self._live())))

    def toggle_checked(self) -> bool:
        return bool(_check(_g.toggle_checked(self._live())))

    def input_text(self) -> str:
        buffer = create_string_buffer(256)
        _check(_g.textinput_text_copy(self._live(), buffer, len(buffer)))
        return buffer.value.decode("utf-8", "replace")

    def status(self, left=None, right=None) -> "Node":
        if left is not None:
            _check(_g.statusbar_set_left(self._live(), left))
        if right is not None:
            _check(_g.statusbar_set_right(self._live(), right))
        return self

    def stepper_current(self, current: int) -> "Node":
        _check(_g.stepper_set_current(self._live(), current))
        return self

    def set_columns(self, columns, widths=None) -> "Node":
        encoded = [column.encode() for column in columns]
        array_type = ctypes.c_char_p * len(encoded)
        width_values = (c_float * len(encoded))(*(widths or [0.0] * len(encoded)))
        _check(_g.table_set_columns(self._live(), array_type(*encoded), width_values,
                                    len(encoded)))
        return self

    def clear_rows(self) -> "Node":
        _check(_g.table_clear_rows(self._live()))
        return self

    def add_row(self, cells) -> "Node":
        encoded = [cell.encode() for cell in cells]
        array_type = ctypes.c_char_p * len(encoded)
        _check(_g.table_add_row(self._live(), array_type(*encoded), len(encoded)))
        return self

    def stream(self, payload) -> "Node":
        """complex_host: ship one addressed runtime update (bytes or a
        ctypes buffer) — no repaint, geometry frozen."""
        if isinstance(payload, (bytes, bytearray)):
            payload = bytes(payload)
        _check(_g.complex_host_stream(self._live(), payload, len(payload)))
        return self

    def set_embed(self, drawable_list) -> "Node":
        """ydraw_embed: adopt `drawable_list`. A v2 `yetty.ydraw`
        DrawableList has its RAW list transferred out on SUCCESS (the v2
        object stays alive and reusable — the next add() starts a fresh
        list); on rejection (non-leaf records) the raw list is restored
        into the v2 wrapper, so the caller keeps its content — matching
        the native contract. A raw handle passed directly stays
        caller-owned on rejection and is never destroyed here."""
        target = self._live()
        v2_source = getattr(drawable_list, "__yclass_domain__", None) == "ydrawlist2"
        if v2_source:
            from .generated import ydrawlist2 as _dl2
            raw = _check(_dl2.drawable_list_release_raw(drawable_list.handle))
        else:
            raw = getattr(drawable_list, "handle", drawable_list)
        adopt_res = _g.ydraw_embed_set_buffer(target, raw)
        if adopt_res.error is not None:
            if v2_source:
                restore_res = _dl2.drawable_list_adopt_raw(drawable_list.handle, raw)
                if restore_res.error is not None:
                    # Restore impossible (should not happen: we just emptied
                    # it) — destroy rather than leak, and say so.
                    destroy = _rt.cfn("yetty_ydraw_drawable_list_destroy", None, [c_void_p])
                    destroy(raw)
                    raise _rt.YettyError(
                        f"{adopt_res.error.message}; restore also failed: "
                        f"{restore_res.error.message}")
            raise _rt.YettyError(adopt_res.error.message)
        return self


class RadioGroup:
    """App-side radio-group semantics: selecting one clears the others
    (the C widget is deliberately dumb about groups). A removed member's
    slot is TOMBSTONED (node and callback dropped, index preserved), so a
    dynamic list retains nothing for dead radios."""

    def __init__(self) -> None:
        self._members: list[tuple[Node | None, Callable | None]] = []

    def _register(self, node: Node, on_select: Callable | None) -> None:
        index = len(self._members)
        self._members.append((node, on_select))
        node._invalidate_hooks.append(lambda: self._members.__setitem__(index, (None, None)))

        def select(_node, _index=index):
            for member_index, (member, _cb) in enumerate(self._members):
                if member_index != _index and member is not None and member._alive:
                    _check(_g.radio_set_selected(member._handle, 0))
            callback = self._members[_index][1]
            if callback is not None:
                callback(_index)

        _check(_g.radio_on_select_set(node._handle,
                                      node._app._click_trampoline(node, select), None))


class App:
    """The terminal host (Python port of yguiapp2): raw termios, alternate
    screen, pane-input subscription, select loop with idle Esc flush,
    monotonic ticks, emit-on-dirty, HOLD/ACK barrier + unsubscribe on
    every exit path. Ctrl-C always quits; `q` quits while no text input
    holds focus. Use as a context manager or call `close()`.

        app = App()
        column = app.root.column(grow=1, gap=8, pad=16)
        column.label(text="hello")
        app.run(tick=..., tick_ms=250)
    """

    def __init__(self, theme: Theme | None = None, fullscreen: bool = True) -> None:
        self._app_callbacks: list[Any] = []  # app-scoped roots (key cb)
        self._callback_error: BaseException | None = None
        self._running = False
        self._alive = False
        # Python callbacks execute INSIDE the native dispatcher; disposing
        # the framework there is a native use-after-free when dispatch
        # resumes. close() defers while this depth is nonzero; the host
        # loop (and the wrapper feed helpers) drain the deferred close
        # after the native call has fully returned.
        self._in_callback = 0
        self._close_pending = False
        self.framework = _check(_g.framework_make())
        self._alive = True
        try:
            if theme is not None:
                _check(_g.framework_set_theme(self.framework, byref(theme)))
            if not fullscreen:
                self.set_fullscreen(False)
            self._viewport = self._probe_viewport()
            root_handle = _check(_g.framework_root_create(self.framework,
                                                          _class_ptr(_g.Panel)))
            _check(_g.panel_set_bg(root_handle, color("#0B1014")))
            self.root = Node(self, root_handle, None)
        except BaseException as construction_error:
            try:
                self.close()
            except BaseException:
                pass  # cleanup must not mask the construction error
            raise construction_error

    def set_fullscreen(self, fullscreen: bool) -> None:
        """Choose the reservation mode (strategy.md §5) BEFORE the first
        emit: fullscreen (default) reserves the full supported viewport
        range; inline reserves the declared viewport height and lives in
        the scrollback flow. The C side rejects the call once inserted."""
        _check(_g.framework_set_fullscreen(self.framework, 1 if fullscreen else 0))

    def content_scale(self) -> float:
        """The committed HiDPI input divisor (1.0 until a pane-resize
        envelope carries a different scale)."""
        out_scale = c_float(0.0)
        _check(_g.framework_content_scale(self.framework, byref(out_scale)))
        return out_scale.value

    @staticmethod
    def _probe_viewport() -> tuple[float, float]:
        """Pane pixels from TIOCGWINSZ — exact ws_xpixel/ws_ypixel when the
        terminal reports them, cell estimate as fallback. The forwarded
        RESIZE envelope corrects either way."""
        buffer = array.array("H", [0, 0, 0, 0])
        try:
            fcntl.ioctl(sys.stdout.fileno(), termios.TIOCGWINSZ, buffer, True)
        except OSError:
            pass
        rows, cols, xpixel, ypixel = buffer
        if xpixel and ypixel:
            return (float(xpixel), float(ypixel))
        return ((cols or 80) * 8.0, (rows or 40) * 16.0)

    # -- callback trampolines --------------------------------------------
    # CFUNCTYPE references are rooted on the NODE that owns them (released
    # with the node), except the app-level key callback. Exceptions cannot
    # cross the ctypes boundary: they are captured here, stop the app, and
    # re-raise from run().
    def _guard(self, callback: Callable, *args) -> Any:
        self._in_callback += 1
        try:
            return callback(*args)
        except BaseException as error:  # noqa: BLE001 — must not cross into C
            if self._callback_error is None:
                self._callback_error = error
            self._running = False
            return None
        finally:
            self._in_callback -= 1

    def _click_trampoline(self, node: Node, callback: Callable) -> Any:
        def bridge(_widget, _userdata):
            if _takes_arg(callback):
                self._guard(callback, node)
            else:
                self._guard(callback)
        cfn = CLICK_CB(bridge)
        node._callbacks.append(cfn)
        return cast(cfn, c_void_p)

    def _select_trampoline(self, node: Node, callback: Callable) -> Any:
        def bridge(_widget, index, _userdata):
            self._guard(callback, index)
        cfn = SELECT_CB(bridge)
        node._callbacks.append(cfn)
        return cast(cfn, c_void_p)

    # -- overlay ---------------------------------------------------------
    def _overlay(self, generated_class) -> Node:
        return Node(self, _check(_g.framework_overlay_add(self.framework,
                                                          _class_ptr(generated_class))),
                    self.root)

    def dialog(self, title="", x=100.0, y=80.0, width=280.0, height=150.0,
               on_close: Callable | None = None) -> Node:
        node = self._overlay(_g.Dialog)

        def configure(overlay):
            _check(_g.dialog_set_title(overlay._handle, title))
            overlay.set_position(x, y).set_size(width, height)
            overlay.layout(gap=6, pad_left=12, pad_top=40, pad_right=12)
            if on_close is not None:
                _check(_g.dialog_on_close_set(overlay._handle,
                                              self._click_trampoline(overlay, on_close), None))
            overlay.set_visible(False)
        return self.root._configured(node, configure)

    def tooltip(self, text="", x=0.0, y=0.0, width=190.0, height=24.0) -> Node:
        node = self._overlay(_g.Tooltip)

        def configure(overlay):
            _check(_g.tooltip_set_text(overlay._handle, text))
            overlay.set_position(x, y).set_size(width, height)
            overlay.set_visible(False)
        return self.root._configured(node, configure)

    def popup_menu(self, items=(), x=0.0, y=0.0, width=160.0,
                   on_select: Callable | None = None) -> Node:
        """An overlay item list: press an item → on_select(index) + close;
        press outside → dismiss. Show it with set_visible(True)."""
        node = self._overlay(_g.PopupMenu)

        def configure(overlay):
            for item in items:
                _check(_g.popup_menu_item_add(overlay._handle, item))
            _check(_g.widget_set_dismiss_on_outside(overlay._handle, 1))
            height = _check(_g.popup_menu_content_height(overlay._handle))
            overlay.set_position(x, y).set_size(width, height)
            if on_select is not None:
                _check(_g.popup_menu_on_select_set(
                    overlay._handle, self._select_trampoline(overlay, on_select), None))
            overlay.set_visible(False)
        return self.root._configured(node, configure)

    # -- lifecycle -------------------------------------------------------
    def _drain_pending_close(self) -> None:
        if self._close_pending and self._in_callback == 0:
            self._close_pending = False
            self.close()

    def feed_input(self, data: bytes) -> None:
        """Forward raw PTY bytes into the framework — the safe dispatch
        boundary for callers running their own event loop. A callback
        fired during dispatch may request close(); the deferred close is
        always drained, even when the native call failed. EVERY failure
        observed here is raised from here, as one chained exception with
        the callback error first, then the native dispatch failure, then
        the deferred-close failure — nothing is left stranded on an app
        that close() just made dead."""
        native_error: BaseException | None = None
        close_error: BaseException | None = None
        try:
            _check(_g.framework_feed_input(self.framework, data, len(data)))
        except BaseException as dispatch_error:  # noqa: BLE001 — chained below
            native_error = dispatch_error
        try:
            self._drain_pending_close()
        except BaseException as drain_error:  # noqa: BLE001 — chained below
            close_error = drain_error
        failures: list[BaseException] = []
        if self._callback_error is not None:
            failures.append(self._callback_error)
            self._callback_error = None
        if native_error is not None:
            failures.append(native_error)
        if close_error is not None:
            failures.append(close_error)
        if failures:
            raise _chain_failures(failures)

    def close(self) -> None:
        """Idempotent teardown: unsubscribe pane input and dispose the
        native framework, then invalidate every wrapper. Safe to call from
        any state — INCLUDING widget callbacks: native dispatch is still
        executing there, so the actual disposal is DEFERRED to the moment
        the native feed call returns (the host loop and feed_input() drain
        it); the loop also stops immediately."""
        if self._in_callback > 0 and self._alive:
            self._close_pending = True
            self._running = False
            return
        if not self._alive:
            return
        self._alive = False
        self._running = False
        self._close_pending = False
        errors: list[str] = []
        for step_name, step in (("clear", _g.framework_clear),
                                ("detach", _g.framework_detach),
                                ("dispose", _g.framework_dispose)):
            try:
                result = step(self.framework)
                if result.error is not None:
                    errors.append(f"{step_name}: {result.error.message}")
            except BaseException as error:  # keep tearing down regardless
                errors.append(f"{step_name}: {error}")
        if hasattr(self, "root"):
            self.root._invalidate()
        self._app_callbacks.clear()
        self.framework = None
        if errors:
            raise _rt.YettyError("ygui2 close: " + "; ".join(errors))

    def __enter__(self) -> "App":
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    def quit(self) -> None:
        self._running = False

    # -- the host loop ---------------------------------------------------
    def run(self, tick: Callable | None = None, tick_ms: int = 250) -> None:
        if not self._alive:
            raise _rt.YettyError("ygui2 app is closed")
        if self._running:
            raise _rt.YettyError("ygui2 app is already running")
        if tick_ms <= 0:
            tick_ms = 250  # match the C runner's clamp
        stdin_fd = sys.stdin.fileno()
        saved = termios.tcgetattr(stdin_fd)
        loop_error: BaseException | None = None
        try:
            self._run_raw(stdin_fd, tick, tick_ms)
        except BaseException as error:
            loop_error = error
        # Terminal restoration is NEVER skippable (loop_error captured
        # everything, so this line is always reached) and each step stands
        # alone: a stdout failure — ValueError on a closed stream, not
        # just OSError — must not stop the termios restore, and no
        # restoration failure may mask the application exception.
        restore_errors: list[BaseException] = []
        try:
            sys.stdout.write("\x1b[?25h\x1b[?1049l")
            sys.stdout.flush()
        except BaseException as error:  # noqa: BLE001 — collected, chained below
            restore_errors.append(error)
        try:
            termios.tcsetattr(stdin_fd, termios.TCSANOW, saved)
        except BaseException as error:  # noqa: BLE001 — collected, chained below
            restore_errors.append(error)
        # Precedence: callback > loop > restoration. The primary is
        # raised; EVERY other failure stays observable, linked behind it
        # in the cause chain.
        failures: list[BaseException] = []
        if self._callback_error is not None:
            failures.append(self._callback_error)
            self._callback_error = None
        if loop_error is not None:
            failures.append(loop_error)
        failures.extend(restore_errors)
        if failures:
            raise _chain_failures(failures)

    def _run_raw(self, stdin_fd: int, tick: Callable | None, tick_ms: int) -> None:
        try:
            # ALL state-changing terminal setup happens inside this scope:
            # a failure anywhere past this point still reaches _teardown()
            # (the outer run() restores the saved termios regardless).
            raw = termios.tcgetattr(stdin_fd)
            raw[0] &= ~(termios.ICRNL | termios.INLCR | termios.IXON | termios.IXOFF |
                        termios.BRKINT | termios.INPCK | termios.ISTRIP)
            raw[3] &= ~(termios.ICANON | termios.ECHO | termios.ISIG | termios.IEXTEN)
            termios.tcsetattr(stdin_fd, termios.TCSANOW, raw)
            sys.stdout.write("\x1b[?1049h\x1b[?25l\x1b[H")
            sys.stdout.flush()
            self._running = True

            def quit_key(key, _mods, _userdata):
                if key in (ord("q"), 0x03):
                    self._running = False
                    return 1
                return 0

            key_cfn = KEY_CB(quit_key)
            self._app_callbacks.append(key_cfn)
            _check(_g.framework_set_key_cb(self.framework, cast(key_cfn, c_void_p), None))
            _check(_g.framework_attach(self.framework, stdin_fd, sys.stdout.fileno()))
            _check(_g.framework_set_viewport(self.framework, *self._viewport))
            _check(_g.framework_emit(self.framework))
            sys.stdout.flush()
            # Monotonic tick schedule: input bursts must not drive the tick
            # faster than tick_ms.
            next_tick = time.monotonic() + tick_ms / 1000.0
            while self._running:
                timeout = max(0.0, next_tick - time.monotonic())
                ready, _, _ = select.select([stdin_fd], [], [], timeout)
                if ready:
                    data = os.read(stdin_fd, 4096)
                    if not data:
                        break
                    _check(_g.framework_feed_input(self.framework, data, len(data)))
                    # A callback inside that dispatch may have requested
                    # close(): the disposal was deferred to THIS boundary.
                    self._drain_pending_close()
                    if not self._alive:
                        break
                now = time.monotonic()
                if now >= next_tick:
                    _check(_g.framework_feed_input_flush(self.framework))
                    self._drain_pending_close()
                    if not self._alive:
                        break
                    if tick is not None:
                        self._guard(tick)
                        self._drain_pending_close()
                        if not self._alive:
                            break
                    next_tick = now + tick_ms / 1000.0
                if _check(_g.framework_is_dirty(self.framework)):
                    _check(_g.framework_emit(self.framework))
                sys.stdout.flush()
        finally:
            self._running = False
            self._teardown(stdin_fd)

    def _teardown(self, stdin_fd: int) -> None:
        """Exit-window barrier then close(). The barrier is best-effort in
        the STRONGEST sense: no exception of any kind may skip the
        unsubscribe/dispose in close() — close() runs in a finally."""
        if not self._alive:
            # A drained deferred close already ran the full teardown; the
            # barrier has nothing left to talk to.
            return
        try:
            _check(_g.framework_send_hold(self.framework))
            deadline = time.monotonic() + 0.5  # wall-clock bound, not iterations
            while time.monotonic() < deadline and \
                    not _check(_g.framework_hold_ack_seen(self.framework)):
                remaining = max(0.0, deadline - time.monotonic())
                ready, _, _ = select.select([stdin_fd], [], [], min(0.05, remaining))
                if ready:
                    data = os.read(stdin_fd, 4096)
                    if not data:
                        break
                    _check(_g.framework_feed_input(self.framework, data, len(data)))
        except BaseException:
            pass  # a wedged barrier must never block the real teardown
        finally:
            self.close()
