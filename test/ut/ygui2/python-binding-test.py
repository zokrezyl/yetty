#!/usr/bin/env python3
"""ygui2 Python binding test — headless, against the real libyetty_ffi.so.

Covers the FFI-layer contract the C wire test cannot see: generated
constructor routing and factory ownership, writable out-buffer arguments,
builder/generated signature parity (every builder actually calls its C
function), callback trampolines including exception capture and release
on removal, recursive wrapper invalidation, idempotent close, and the
embed containment path.

Run (ctest wires this up with the right environment):

    PYTHONPATH=bindings/python YETTY_FFI_LIB=<build>/src/yetty/yffi/libyetty_ffi.so \
        python3 test/ut/ygui2/python-binding-test.py
"""
import ctypes
import sys
from ctypes import byref, c_float, c_uint32, c_void_p, cast

FAILURES = []


def check(name, condition, detail=""):
    status = "ok" if condition else "FAIL"
    print(f"  {status}: {name}{(' — ' + str(detail)) if detail and not condition else ''}")
    if not condition:
        FAILURES.append(name)


def expect_raises(name, exception_type, callback):
    try:
        callback()
    except exception_type:
        check(name, True)
    except BaseException as error:
        check(name, False, f"wrong exception {type(error).__name__}: {error}")
    else:
        check(name, False, "no exception raised")


from yetty import ygui2  # noqa: E402
from yetty import runtime as _rt  # noqa: E402
from yetty.generated import ygui2 as g  # noqa: E402


def make_sink_app():
    captured = {"envelopes": 0, "bytes": 0}

    def sink(_data, count, _user):
        captured["envelopes"] += 1
        captured["bytes"] += count

    sink_cfn = ygui2.SINK_CB(sink)
    app = ygui2.App()
    app._sink_ref = sink_cfn  # rooted for the app's life
    ygui2._check(g.framework_set_sink(app.framework, cast(sink_cfn, c_void_p), None))
    ygui2._check(g.framework_set_viewport(app.framework, 640.0, 400.0))
    return app, captured


print("1. generated factory routing + first child ids")
framework = g.Framework()  # must route to framework_make
root = ygui2._check(g.framework_root_create(framework.handle, g.Panel.yclass().value))
child = ygui2._check(g.widget_add(root, g.Label.yclass().value))
check("root id 1", ygui2._check(g.widget_node_id(root)) == 1)
check("first child id 3 (2 = overlay root)", ygui2._check(g.widget_node_id(child)) == 3)
expect_raises("widget classes are factory-owned", _rt.YettyError, lambda: g.Button())
ygui2._check(g.framework_dispose(framework))  # wrapper form: invalidates too
check("free-function dispose invalidated the wrapper", framework._handle is None)

print("2. writable text-copy out buffer")
app, captured = make_sink_app()
field = app.root.textinput(text="abc", basis=24)
check("input_text round-trip", field.input_text() == "abc")

print("2b. plot builder + streaming through the public wrapper")
plot_app, plot_captured = make_sink_app()
plot_node = plot_app.root.plot(title="wrapper plot", y_range=(0, 100),
                               expression="sin(3*x) * 40 + 50",
                               stream=("live", 16, "#6BA892"), basis=60)
ygui2._check(g.framework_emit(plot_app.framework))
plot_envelopes = dict(plot_captured)
plot_node.append_samples([12.5, 50.0, 87.5])
check("wrapper append shipped one envelope",
      plot_captured["envelopes"] == plot_envelopes["envelopes"] + 1)
plot_node.stream_samples(range(16))  # any number sequence converts
check("wrapper bulk stream shipped one envelope",
      plot_captured["envelopes"] == plot_envelopes["envelopes"] + 2)
expect_raises("over-capacity append rejected", _rt.YettyError,
              lambda: plot_node.append_samples([0.0] * 17))
expect_raises("empty samples rejected", _rt.YettyError,
              lambda: plot_node.append_samples([]))
second_buffer_res = g.plot_add_stream_buffer(plot_node._handle, "extra", 8, "#74C5A5")
check("second stream buffer rejected", second_buffer_res.error is not None)
plot_node.append_samples([33.0])  # first buffer still fully usable
check("first buffer usable after the rejected declaration",
      plot_captured["envelopes"] == plot_envelopes["envelopes"] + 3)
plot_app.close()

print("2c. plot stream capacity bounds (validated before ctypes conversion)")
cap_app, cap_captured = make_sink_app()
expect_raises("negative capacity rejected in the wrapper", ValueError,
              lambda: cap_app.root.plot(stream=("bad", -1, "#6BA892"), basis=40))
expect_raises("capacity 1 rejected in the wrapper", ValueError,
              lambda: cap_app.root.plot(stream=("bad", 1, "#6BA892"), basis=40))
expect_raises("capacity 65537 rejected in the wrapper", ValueError,
              lambda: cap_app.root.plot(stream=("bad", 65537, "#6BA892"), basis=40))
cap_over_res = g.plot_add_stream_buffer(
    cap_app.root.plot(basis=40)._handle, "raw", 0xFFFFFFFF, "#6BA892")
check("wrapped capacity rejected by the C layer too", cap_over_res.error is not None)
cap_edge = cap_app.root.plot(stream=("edge", 65536, "#6BA892"), basis=40)
cap_edge2 = cap_app.root.plot(stream=("edge2", 2, "#6BA892"), basis=40)
check("boundary capacities accepted", cap_edge is not None and cap_edge2 is not None)
cap_app.close()

print("3. builder / generated signature parity (every builder calls C)")
column = app.root.column(grow=1, gap=4, pad=8)
column.panel(bg="#141A1F", border="#364A47", border_width=2.0, basis=10)
column.label(text="l", fg="#9FA7A8", basis=10)
column.separator(basis=4)
column.button(label="b", on_click=lambda: None, basis=10)
column.checkbox(label="c", checked=True, on_toggle=lambda: None, basis=10)
column.toggle(label="t", checked=True, on_toggle=lambda: None, basis=10)
group = ygui2.RadioGroup()
radio_a = column.radio(label="r0", group=group, selected=True, basis=10)
radio_b = column.radio(label="r1", group=group, basis=10)
column.slider(value=0.5, minimum=0.0, maximum=2.0, on_change=lambda node: None, basis=10)
column.spinner(value=1, minimum=0, maximum=5, step=1, on_change=lambda node: None, basis=10)
column.progress(value=0.5, accent="#6BA892", basis=10)
column.chip(label="ch", selectable=True, selected=True, on_toggle=lambda: None, basis=10)
column.statusbar(left="l", right="r", basis=10)
column.stepper(count=4, current=1, basis=10)
column.dropdown(items=("a", "b"), selected=0, on_change=lambda index: None, basis=10)
scroll = column.scrollarea(wheel_step=10.0, max_scroll=100.0, basis=20)
scroll.label(text="in-scroll", basis=10)
table = column.table(columns=("x", "y"), widths=(40.0, 0.0), basis=20)
table.add_row(("1", "2")).clear_rows()
expect_raises("command-typed record rejected (CMD_DELETE is not a complex)",
              _rt.YettyError,
              lambda: column.complex_host((c_uint32 * 6)(0x80000001, 16, 1, 2, 3, 4), 7,
                                          basis=10))
record = (c_uint32 * 6)(0x80001000, 16, 1, 2, 3, 4)
host = column.complex_host(record, 7, basis=10)
app.tooltip(text="tip", x=1, y=1)
app.dialog(title="d", on_close=lambda: None)
popup = app.popup_menu(items=("one", "two"), x=5, y=5, on_select=lambda index: None)
check("builders all executed", True)
expect_raises("unknown kwarg rejected", TypeError,
              lambda: column.label(text="x", grw=1))

print("4. first frame + clean frame + stream")
ygui2._check(g.framework_emit(app.framework))
first = dict(captured)
check("one first-frame envelope", first["envelopes"] == 1)
check("clean after first frame", ygui2._check(g.framework_is_dirty(app.framework)) == 0)
host.stream(bytes(16))
check("stream shipped one envelope", captured["envelopes"] == first["envelopes"] + 1)

print("5. callback invocation + exception capture")
clicks = {"count": 0}
button = column.button(label="live", basis=10,
                       on_click=lambda: clicks.__setitem__("count", clicks["count"] + 1))
ygui2._check(g.framework_emit(app.framework))
x, y = c_float(), c_float()
ygui2._check(g.widget_rect(button._handle, byref(x), byref(y), None, None))
ygui2._check(g.framework_feed_mouse_button(app.framework, x.value + 4, y.value + 4, 0, 1, 0))
ygui2._check(g.framework_feed_mouse_button(app.framework, x.value + 4, y.value + 4, 0, 0, 0))
check("click delivered", clicks["count"] == 1)

boom = app.root.button(label="boom", basis=10, on_click=lambda: 1 / 0)
ygui2._check(g.framework_emit(app.framework))
ygui2._check(g.widget_rect(boom._handle, byref(x), byref(y), None, None))
app._running = True
ygui2._check(g.framework_feed_mouse_button(app.framework, x.value + 4, y.value + 4, 0, 1, 0))
ygui2._check(g.framework_feed_mouse_button(app.framework, x.value + 4, y.value + 4, 0, 0, 0))
check("exception captured, app stopped",
      isinstance(app._callback_error, ZeroDivisionError) and not app._running)
app._callback_error = None

print("6. radio-group semantics + removal releases wrappers/callbacks")
ygui2._check(g.widget_rect(radio_b._handle, byref(x), byref(y), None, None))
ygui2._check(g.framework_feed_mouse_button(app.framework, x.value + 4, y.value + 4, 0, 1, 0))
ygui2._check(g.framework_feed_mouse_button(app.framework, x.value + 4, y.value + 4, 0, 0, 0))
check("group cleared the other radio",
      ygui2._check(g.radio_selected(radio_a._handle)) == 0 and
      ygui2._check(g.radio_selected(radio_b._handle)) == 1)

scroll_button = scroll.button(label="cb-holder", basis=10, on_click=lambda: None)
check("scroll child holds a callback root", len(scroll_button._callbacks) == 1)
inner = scroll._children[0]
scroll.remove()
check("removed subtree wrappers invalidated",
      not scroll._alive and not inner._alive and not scroll_button._alive)
expect_raises("dead node raises, never touches C", _rt.YettyError,
              lambda: inner.set_text("nope"))
check("removed subtree callbacks released", len(scroll_button._callbacks) == 0)
check("tree dirty after removal", ygui2._check(g.framework_is_dirty(app.framework)) == 1)
ygui2._check(g.framework_emit(app.framework))

print("6b. failed ROOT removal leaves the Python tree intact")
expect_raises("root removal rejected", _rt.YettyError, app.root.remove)
check("root and app still usable after rejected removal",
      app.root._alive and app._alive)
probe = app.root.label(text="still building", basis=10)
check("tree still buildable", probe._alive)

print("6c. builder rollback on configure failure")
children_before = len(app.root._children)
expect_raises("builder raises on a bad value", AttributeError,
              lambda: app.root.label(text=object(), basis=10))
check("no half-configured child left behind",
      len(app.root._children) == children_before)

print("6e. row/column/overlay builders roll back on configure failure")
children_before = len(app.root._children)
expect_raises("row rolls back on a bad layout value", TypeError,
              lambda: app.root.row(basis=object()))
expect_raises("column rolls back on a bad layout value", TypeError,
              lambda: app.root.column(basis=object()))
expect_raises("dialog rolls back on a bad title", (TypeError, AttributeError),
              lambda: app.dialog(title=object()))
expect_raises("tooltip rolls back on a bad text", (TypeError, AttributeError),
              lambda: app.tooltip(text=object()))
expect_raises("popup_menu rolls back on a bad item", (TypeError, AttributeError),
              lambda: app.popup_menu(items=(object(),)))
check("no child leaked by any failed builder",
      len(app.root._children) == children_before)

print("6d. RadioGroup tombstones removed members")
tomb_group = ygui2.RadioGroup()
tomb_row = app.root.row(basis=12)
tomb_a = tomb_row.radio(label="ta", group=tomb_group, selected=True, basis=10)
tomb_b = tomb_row.radio(label="tb", group=tomb_group, basis=10)
tomb_b.remove()
check("tombstoned slot dropped node and callback",
      tomb_group._members[1] == (None, None))

print("7. idempotent close + dead-app rejection")
app.close()
app.close()  # second close must be a no-op
check("app closed", not app._alive and app.framework is None)
expect_raises("root dead after close", _rt.YettyError, lambda: app.root.label(text="x"))
expect_raises("run() after close rejected", _rt.YettyError, app.run)

print("8. context manager + init rollback path")
with ygui2.App() as scoped:
    ygui2._check(g.framework_set_viewport(scoped.framework, 100.0, 100.0))
check("context manager closed", not scoped._alive)
expect_raises("init failure raises the ORIGINAL error", TypeError,
              lambda: ygui2.App(theme=42))
after_rollback = ygui2.App()
check("apps still constructible after init rollback", after_rollback._alive)
after_rollback.close()

print("8b. teardown close() survives unexpected exceptions")
victim = ygui2.App()
original_send_hold = g.framework_send_hold
g.framework_send_hold = lambda _obj: (_ for _ in ()).throw(RuntimeError("synthetic"))
try:
    import os
    pipe_read, pipe_write = os.pipe()
    try:
        victim._teardown(pipe_read)
    finally:
        os.close(pipe_read)
        os.close(pipe_write)
finally:
    g.framework_send_hold = original_send_hold
check("close ran despite the synthetic teardown failure",
      not victim._alive and victim.framework is None)

print("8c. transport writes report failures (broken pipe)")
import os
broken = ygui2._check(g.framework_make())
pipe_read, pipe_write = os.pipe()
os.close(pipe_read)  # every write now fails with EPIPE
attach_res = g.framework_attach(broken, 0, pipe_write)
check("attach on a broken pipe reports the error", attach_res.error is not None)
hold_res = g.framework_send_hold(broken)
check("hold without a live transport reports the error", hold_res.error is not None)
os.close(pipe_write)
ygui2._check(g.framework_dispose(broken))

print("8d. owned char* results: layout + adopt/release converter")
from yetty.generated import _types as _tt
value_field_types = dict(_tt.yetty_ycore_char_ptr_result_u1._fields_)
check("owned char* value field keeps the ADDRESS (c_void_p)",
      value_field_types["value"] == ctypes.c_void_p)
libc = ctypes.CDLL(None)
libc.strdup.restype = ctypes.c_void_p
libc.strdup.argtypes = [ctypes.c_char_p]
owned_address = libc.strdup(b"owned-string")
check("take_owned_cstr copies then releases",
      _rt.take_owned_cstr(owned_address) == "owned-string")

print("8e. generated Framework lifecycle routes through dispose")
gen_framework = g.Framework()
gen_framework.destroy()
check("generated destroy() disposed and invalidated", gen_framework._handle is None)
gen_framework.destroy()  # idempotent
expect_raises("factory-owned destroy raises", _rt.YettyError,
              lambda: g.Button(_handle=1234).destroy())

print("8f. generated Framework ownership: GC finalizes, borrowed stays put")
import gc
owned_framework = g.Framework()
check("directly-created framework is owned", owned_framework._owned is True)
borrowed_view = g.Framework(_handle=owned_framework._handle)
check("handle-wrapped framework stays borrowed", not borrowed_view._owned)
del borrowed_view  # borrowed: GC must NOT dispose the live native object
gc.collect()
del owned_framework  # owned: GC disposes via the overridden destroy()
gc.collect()
check("gc finalization of an owned framework survived", True)

print("9. ydraw_embed adoption of a real DrawableList")
app2, _ = make_sink_app()
embed_host = app2.root.ydraw_embed(basis=20)
check("embed builder works without a buffer", embed_host._alive)
try:
    from yetty.ydraw import Circle, DrawableList
    from yetty.generated import ydrawlist2 as dl2
    dlist = DrawableList()
    dlist.add(Circle(center_x=10, center_y=10, radius=5, fill="#6BA892"))
    embed_host.set_embed(dlist)
    check("embed adopted the raw list (v2 object emptied)",
          dl2.drawable_list_release_raw(dlist.handle).error is not None)
    dlist.add(Circle(center_x=5, center_y=5, radius=2, fill="#6BA892"))
    reuse_res = dl2.drawable_list_release_raw(dlist.handle)
    check("v2 object reusable after adoption", reuse_res.error is None)
    if reuse_res.error is None:  # adopt back: the wrapper owns + frees it
        ygui2._check(dl2.drawable_list_adopt_raw(dlist.handle, reuse_res.value))
    dlist.destroy()
except ImportError:
    print("  skip: yetty.ydraw v2 client not importable here")

print("9b. embed: addressed leaves accepted, rejection preserves content")
try:
    from yetty.ydraw import Circle, DrawableList
    from yetty.generated import ydrawlist2 as dl2
    embed_host2 = app2.root.ydraw_embed(basis=20)
    addressed = DrawableList()
    addressed.add(Circle(id=7, center_x=10, center_y=10, radius=5, fill="#6BA892"))
    embed_host2.set_embed(addressed)
    check("addressed SDF leaf accepted (HAS_ID sizing)", True)

    bad = DrawableList()
    bad.begin_group(9)
    bad.add(Circle(center_x=1, center_y=1, radius=1, fill="#6BA892"))
    bad.end_group()
    expect_raises("wrapper set_embed rejects non-leaf content", _rt.YettyError,
                  lambda: embed_host2.set_embed(bad))
    survived_res = dl2.drawable_list_release_raw(bad.handle)
    check("caller's v2 list survived the rejection (content restored)",
          survived_res.error is None)
    if survived_res.error is None:  # adopt back so destroy() frees it
        ygui2._check(dl2.drawable_list_adopt_raw(bad.handle, survived_res.value))
    bad.destroy()
    # `bad` verified + freed; a fresh fixture feeds the raw-path test.
    raw_fixture = DrawableList()
    raw_fixture.begin_group(9)
    raw_fixture.add(Circle(center_x=1, center_y=1, radius=1, fill="#6BA892"))
    raw_fixture.end_group()
    raw_pointer = ygui2._check(dl2.drawable_list_release_raw(raw_fixture.handle))
    expect_raises("wrapper rejects a RAW handle without destroying it",
                  _rt.YettyError, lambda: embed_host2.set_embed(raw_pointer))
    # Rejection left the raw list caller-owned: adopting it back into the
    # emptied v2 wrapper both proves it is still alive and hands cleanup
    # to the wrapper's own destroy. A destroyed raw would double-free here.
    ygui2._check(dl2.drawable_list_adopt_raw(raw_fixture.handle, raw_pointer))
    raw_fixture.destroy()
    check("raw handle survived rejection (adopt-back + destroy clean)", True)
except ImportError:
    print("  skip: yetty.ydraw v2 client not importable here")
app2.close()

print("10. close() from a widget callback is deferred past native dispatch")
app3, _ = make_sink_app()
close_button = app3.root.button(label="close-me", basis=10, on_click=app3.close)
ygui2._check(g.framework_emit(app3.framework))
ygui2._check(g.widget_rect(close_button._handle, byref(x), byref(y), None, None))
ygui2._check(g.framework_feed_mouse_button(app3.framework, x.value + 4, y.value + 4, 0, 1, 0))
ygui2._check(g.framework_feed_mouse_button(app3.framework, x.value + 4, y.value + 4, 0, 0, 0))
check("dispose deferred while native dispatch was on the stack",
      app3._alive and app3._close_pending and not app3._running)
app3._drain_pending_close()
check("deferred close drained at the host boundary",
      not app3._alive and app3.framework is None and not app3._close_pending)

print("10b. close() from a tick-style guarded callback + feed_input drain")
app4, _ = make_sink_app()
app4._guard(app4.close)  # exactly how run() invokes the tick callback
check("guarded close deferred", app4._alive and app4._close_pending)
app4.feed_input(b" ")  # the wrapper feed boundary drains the deferred close
check("feed_input drained the deferred close", not app4._alive)

print("10c. run(): tick close exits cleanly; callback errors re-raise")
import os as _os


def run_pty_app(build_tick):
    """Drive App.run() for real on a private pty (stdin) + pipe (stdout)."""
    app_under_test = ygui2.App()
    app_under_test.root.label(text="run-under-test", basis=10)
    tick = build_tick(app_under_test)
    master_fd, slave_fd = _os.openpty()
    out_read, out_write = _os.pipe()
    saved_stdin, saved_stdout = sys.stdin, sys.stdout
    sys.stdin = _os.fdopen(slave_fd, "rb", buffering=0, closefd=False)
    sys.stdout = _os.fdopen(out_write, "w", closefd=False)
    caught = None
    try:
        app_under_test.run(tick=tick, tick_ms=10)
    except BaseException as run_error:  # noqa: BLE001 — the assertion target
        caught = run_error
    finally:
        sys.stdin, sys.stdout = saved_stdin, saved_stdout
        for descriptor in (master_fd, slave_fd, out_read, out_write):
            try:
                _os.close(descriptor)
            except OSError:
                pass
    return app_under_test, caught


closed_app, close_error = run_pty_app(lambda app_ref: app_ref.close)
check("tick close(): loop exited cleanly, app closed",
      close_error is None and not closed_app._alive and closed_app.framework is None)

raise_app, raised = run_pty_app(lambda _app_ref: (lambda: 1 / 0))
check("tick exception re-raised by run() after teardown",
      isinstance(raised, ZeroDivisionError))
check("run() tore the app down before re-raising",
      not raise_app._alive and raise_app.framework is None)

print("11. feed_input(): the public boundary re-raises callback errors")
import types
app5, _ = make_sink_app()
app5._guard(lambda: 1 / 0)  # a callback failed during dispatch
expect_raises("feed_input re-raises the captured callback error",
              ZeroDivisionError, lambda: app5.feed_input(b" "))
check("error slot consumed, app still alive",
      app5._callback_error is None and app5._alive)
app5.close()

print("11b. feed_input(): callback + native failure + deferred close together")
app6, _ = make_sink_app()
app6._guard(app6.close)   # close deferred from inside a callback...
app6._guard(lambda: 1 / 0)  # ...which also failed
original_feed = g.framework_feed_input
g.framework_feed_input = lambda *feed_args: types.SimpleNamespace(
    error=types.SimpleNamespace(message="synthetic feed failure"), value=None)
try:
    try:
        app6.feed_input(b" ")
        check("feed_input raised", False, "no exception")
    except ZeroDivisionError as combined_error:
        combined_chain = []
        walk_cause = combined_error.__cause__
        while walk_cause is not None:
            combined_chain.append(type(walk_cause))
            walk_cause = walk_cause.__cause__
        check("callback primary, native failure in the chain",
              _rt.YettyError in combined_chain)
    except BaseException as wrong_error:  # noqa: BLE001 — assertion reporting
        check("feed_input primary exception", False, f"wrong: {wrong_error!r}")
finally:
    g.framework_feed_input = original_feed
check("deferred close drained despite the failed native call",
      not app6._alive and not app6._close_pending and app6._callback_error is None)

print("11f. feed_input(): real callback + failing deferred close — both surface")
app8, _ = make_sink_app()


def close_and_boom(node):
    app8.close()
    raise RuntimeError("callback after close")


field8 = app8.root.textinput(text="", basis=24, on_change=close_and_boom)
ygui2._check(g.framework_emit(app8.framework))
x8, y8 = c_float(), c_float()
ygui2._check(g.widget_rect(field8._handle, byref(x8), byref(y8), None, None))
ygui2._check(g.framework_feed_mouse_button(app8.framework, x8.value + 4, y8.value + 4, 0, 1, 0))
ygui2._check(g.framework_feed_mouse_button(app8.framework, x8.value + 4, y8.value + 4, 0, 0, 0))
original_clear = g.framework_clear
g.framework_clear = lambda *clear_args: types.SimpleNamespace(
    error=types.SimpleNamespace(message="synthetic clear failure"), value=None)
try:
    try:
        app8.feed_input(b"a")
        check("feed_input raised", False, "no exception")
    except RuntimeError as boundary_error:
        boundary_chain = []
        walk_cause = boundary_error.__cause__
        while walk_cause is not None:
            boundary_chain.append(type(walk_cause))
            walk_cause = walk_cause.__cause__
        check("callback primary, deferred-close failure in the chain",
              _rt.YettyError in boundary_chain)
    except BaseException as wrong_error:  # noqa: BLE001 — assertion reporting
        check("feed_input primary exception", False, f"wrong: {wrong_error!r}")
finally:
    g.framework_clear = original_clear
check("app closed, nothing stranded on the dead app",
      not app8._alive and app8._callback_error is None and not app8._close_pending)

print("11c. failed native rollback keeps the wrapper linked and live")
app7, _ = make_sink_app()
children_before = len(app7.root._children)
original_remove = g.widget_remove
g.widget_remove = lambda handle: types.SimpleNamespace(
    error=types.SimpleNamespace(message="synthetic remove failure"), value=None)
try:
    try:
        app7.root.label(text=object(), basis=10)
        check("builder raised on the bad value", False)
    except (TypeError, AttributeError) as builder_error:
        cause_types = []
        walk_cause = builder_error.__cause__
        while walk_cause is not None:
            cause_types.append(type(walk_cause))
            walk_cause = walk_cause.__cause__
        check("rollback failure chained behind the builder error",
              _rt.YettyError in cause_types)
finally:
    g.widget_remove = original_remove
check("wrapper stays linked and LIVE after the failed native rollback",
      len(app7.root._children) == children_before + 1 and
      app7.root._children[-1]._alive)
app7.root._children[-1].remove()  # actionable: retry works unpatched
check("kept wrapper retried removal successfully",
      len(app7.root._children) == children_before)
app7.close()

print("11d. run(): broken stdout (ValueError) still restores termios")
import termios as _termios
real_tcsetattr = _termios.tcsetattr
tcsetattr_calls = []


def recording_tcsetattr(fd, when, attrs):
    tcsetattr_calls.append(fd)
    return real_tcsetattr(fd, when, attrs)


_termios.tcsetattr = recording_tcsetattr
try:
    broken_stdout_app, restore_caught = run_pty_app(
        lambda app_ref: (lambda: (sys.stdout.close(), app_ref.close())))
finally:
    _termios.tcsetattr = real_tcsetattr
check("stdout ValueError surfaced after restoration",
      isinstance(restore_caught, ValueError))
check("tcsetattr still ran for setup AND restore", len(tcsetattr_calls) >= 2)
check("app torn down despite the broken stdout", not broken_stdout_app._alive)

print("11e. run(): callback error + tcsetattr failure — BOTH observable")
setattr_state = {"calls": 0}


def flaky_tcsetattr(fd, when, attrs):
    setattr_state["calls"] += 1
    if setattr_state["calls"] >= 2:  # setup passes, restoration fails
        raise OSError("synthetic tcsetattr restore failure")
    return real_tcsetattr(fd, when, attrs)


_termios.tcsetattr = flaky_tcsetattr
try:
    flaky_app, flaky_caught = run_pty_app(lambda _app_ref: (lambda: 1 / 0))
finally:
    _termios.tcsetattr = real_tcsetattr
flaky_chain = []
walk_cause = flaky_caught
while walk_cause is not None:
    flaky_chain.append(type(walk_cause))
    walk_cause = walk_cause.__cause__
check("primary is the callback error", isinstance(flaky_caught, ZeroDivisionError))
check("tcsetattr failure stays in the cause chain", OSError in flaky_chain)
check("app torn down", not flaky_app._alive)

print("12. reservation-mode surface parity — set_fullscreen/content_scale exposed")
# The C model exposes these; every high-level frontend must too (a stale
# generated module silently dropped them once — this pins the surface).
check("generated framework_set_fullscreen present", hasattr(g, "framework_set_fullscreen"))
check("generated framework_content_scale present", hasattr(g, "framework_content_scale"))
mode_app, _mode_captured = make_sink_app()
check("wrapper content_scale() = 1.0", abs(mode_app.content_scale() - 1.0) < 1e-6)
inline_app, _inline_captured = make_sink_app()
inline_app.set_fullscreen(False)  # accepted: nothing inserted yet
ygui2._check(g.framework_set_viewport(inline_app.framework, 640.0, 300.0))
inline_app.root.column(grow=1).label(text="inline", basis=20)
ygui2._check(g.framework_emit(inline_app.framework))
flip_rejected = False
try:
    inline_app.set_fullscreen(True)
except Exception:
    flip_rejected = True
check("set_fullscreen rejected after insertion", flip_rejected)
inline_app.close()
mode_app.close()

print()
if FAILURES:
    print(f"FAILED: {len(FAILURES)}: {', '.join(FAILURES)}")
    sys.exit(1)
print("python-binding-test: all checks passed")
