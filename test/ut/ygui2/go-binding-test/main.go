// ygui2 Go binding test — headless, against the real libyetty_ffi.so
// through cgo.
//
// Covers the wrapper contracts the wire test cannot see: builder wiring
// and value getters, callback dispatch through synthetic mouse input,
// the DEFERRED Close boundary (Close() from a widget callback and from
// the sink callback must not dispose the framework while native
// dispatch is still on the stack), reservation-mode exposure
// (SetFullscreen accepted before the first emit, rejected after),
// ContentScale, explicit-zero layout/option values (zero padding via a
// passed Layout, a dialog at the (0,0) corner), and wrapper liveness.
//
// Run (ctest wires this up with the right environment):
//
//	cd test/ut/ygui2/go-binding-test
//	CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi" \
//	LD_LIBRARY_PATH=<build>/src/yetty/yffi go run .
package main

import (
	"fmt"
	"math"
	"os"

	"github.com/zokrezyl/yetty/bindings/go/ygui2"
)

var failures int

func expect(condition bool, label string) {
	if !condition {
		failures++
		fmt.Println("FAIL:", label)
	}
}

func expectPanic(label string, body func()) {
	panicked := func() (caught bool) {
		defer func() { caught = recover() != nil }()
		body()
		return false
	}()
	expect(panicked, label)
}

func sinkCloseDeferred() {
	app := ygui2.NewApp()
	sinkCalls := 0
	app.SetSink(func(payload []byte) {
		sinkCalls++
		if sinkCalls == 1 {
			app.Close() // requested mid-emit; drained AFTER emit returns
			// (the drained close later ships its own clear envelope,
			// invoking this sink again with the app already closed —
			// assert only here)
			expect(app.Alive(), "sink close deferred (app still alive inside callback)")
		}
	})
	app.SetViewport(640, 480)
	app.Root.Column(ygui2.Layout{Grow: 1}).Label(ygui2.LabelOpts{Text: "sink close"},
		ygui2.Layout{Basis: 20})
	app.Emit()
	expect(sinkCalls >= 1, "sink ran")
	expect(!app.Alive(), "sink close drained after emit")
}

func clickCloseDeferred() {
	app := ygui2.NewApp()
	app.SetSink(func(payload []byte) {})
	app.SetViewport(640, 480)
	column := app.Root.Column(ygui2.Layout{Grow: 1, Pad: 8})
	clicked := 0
	button := column.Button(ygui2.ButtonOpts{Label: "close me", OnClick: func() {
		clicked++
		app.Close()
		expect(app.Alive(), "click close deferred inside callback")
	}}, ygui2.Layout{Basis: 24, Cross: 200})
	app.Emit()
	buttonX, buttonY, buttonW, buttonH := button.Rect()
	expect(buttonW > 0 && buttonH > 0, "button has a rect")
	app.FeedMouseButton(buttonX+4, buttonY+4, 0, true, 0)
	if app.Alive() {
		app.FeedMouseButton(buttonX+4, buttonY+4, 0, false, 0)
	}
	expect(clicked == 1, fmt.Sprintf("click fired once (clicked=%d)", clicked))
	expect(!app.Alive(), "click close drained after dispatch")
}

func reservationMode() {
	app := ygui2.NewApp()
	app.SetFullscreen(false) // before the first emit — accepted
	app.SetSink(func(payload []byte) {})
	app.SetViewport(640, 300)
	expect(math.Abs(app.ContentScale()-1.0) < 1e-6, "ContentScale starts at 1.0")
	app.Root.Column(ygui2.Layout{Grow: 1}).Label(ygui2.LabelOpts{Text: "inline"},
		ygui2.Layout{Basis: 20})
	app.Emit()
	expectPanic("SetFullscreen rejected after insertion", func() {
		app.SetFullscreen(true)
	})
	app.Close()
}

func explicitZeroValues() {
	app := ygui2.NewApp()
	app.SetSink(func(payload []byte) {})
	app.SetViewport(640, 480)
	column := app.Root.Column(ygui2.Layout{Grow: 1, Pad: 16})
	child := column.Label(ygui2.LabelOpts{Text: "padded"}, ygui2.Layout{Basis: 20})
	app.Emit()
	paddedX, _, _, _ := child.Rect()
	expect(paddedX >= 16, fmt.Sprintf("pad 16 took effect (x=%g)", paddedX))
	// A passed Layout is explicit in EVERY field: zero pad resets it.
	column.Layout(ygui2.Layout{Grow: 1})
	app.Emit()
	zeroX, _, _, _ := child.Rect()
	expect(zeroX < 16, fmt.Sprintf("explicit zero pad restored (x=%g)", zeroX))

	// A dialog can sit at the top-left corner: Float(0) is explicit.
	dialog := app.Dialog(ygui2.DialogOpts{Title: "corner",
		X: ygui2.Float(0), Y: ygui2.Float(0)})
	dialogX, dialogY, _, _ := dialog.Rect()
	expect(dialogX == 0 && dialogY == 0,
		fmt.Sprintf("dialog placed at (0,0) (got %g,%g)", dialogX, dialogY))
	app.Close()
	expectPanic("dead node rejected after close", func() {
		child.SetText("after close")
	})
}

func main() {
	sinkCloseDeferred()
	clickCloseDeferred()
	reservationMode()
	explicitZeroValues()
	if failures > 0 {
		os.Exit(1)
	}
	fmt.Println("go binding test OK")
}
