// ygui2 from Go — interactive counter. RUNNABLE inside a yetty pane:
//
//	cd demo/ffi/ygui2/go
//	CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi" \
//	LD_LIBRARY_PATH=<build>/src/yetty/yffi go run counter.go
//
// The full input round-trip in Go: the pane forwards mouse envelopes,
// the toolkit hit-tests and dispatches, the Go callback mutates a label,
// and ONE addressed reopen ships back. A slider drives a progress bar
// the same way. Wheel over the scrollarea to see clipped offset-only
// scrolling. Ctrl-C quits (also `q` while no text input holds focus).
package main

import (
	"fmt"
	"os"

	"github.com/zokrezyl/yetty/bindings/go/ygui2"
)

func main() {
	app := ygui2.NewApp()
	clicks := 0

	column := app.Root.Column(ygui2.Layout{Grow: 1, Gap: 8, Pad: 16})
	column.Label(ygui2.LabelOpts{Text: "ygui2 counter — Go callbacks over the wire",
		Fg: "#74C5A5"}, ygui2.Layout{Basis: 24})

	counterLabel := column.Label(ygui2.LabelOpts{Text: "clicks: 0"}, ygui2.Layout{Basis: 20})

	column.Button(ygui2.ButtonOpts{Label: "click me", OnClick: func() {
		clicks++
		counterLabel.SetText(fmt.Sprintf("clicks: %d", clicks))
	}}, ygui2.Layout{Basis: 24, Cross: 220})

	mirrorRow := column.Row(ygui2.Layout{Basis: 24, Gap: 10})
	mirrorRow.Label(ygui2.LabelOpts{Text: "slider", Fg: "#9FA7A8"}, ygui2.Layout{Basis: 90})
	bar := mirrorRow.Progress(ygui2.ProgressOpts{Value: 0.35},
		ygui2.Layout{Basis: 160, Cross: 12})
	mirrorRow.Slider(ygui2.SliderOpts{Value: 0.35, OnChange: func(node *ygui2.Node) {
		bar.SetValue(node.SliderValue())
	}}, ygui2.Layout{Basis: 160})

	column.Checkbox(ygui2.CheckboxOpts{Label: "wheel scroll below"},
		ygui2.Layout{Basis: 24, Cross: 220})
	scroll := column.Scrollarea(ygui2.ScrollareaOpts{WheelStep: ygui2.Float(24),
		MaxScroll: ygui2.Float(500)},
		ygui2.Layout{Basis: 150, Cross: 360, Gap: 4})
	for line := 0; line < 12; line++ {
		lineColor := "#9FA7A8"
		if line%2 == 0 {
			lineColor = "#6BA892"
		}
		scroll.Label(ygui2.LabelOpts{
			Text: fmt.Sprintf("scrollable row %02d — offsets only, no repaint", line),
			Fg:   lineColor}, ygui2.Layout{Basis: 48})
	}

	column.Column(ygui2.Layout{Grow: 1})
	column.Statusbar(ygui2.StatusbarOpts{Left: "counter.go — click, drag, wheel",
		Right: "Ctrl-C: quit"}, ygui2.Layout{Basis: 24})

	if runError := app.Run(ygui2.RunOpts{}); runError != nil {
		fmt.Fprintln(os.Stderr, "counter:", runError)
		os.Exit(1)
	}
}
