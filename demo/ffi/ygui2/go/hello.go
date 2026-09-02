// ygui2 from Go — the static hello. RUNNABLE inside a yetty pane:
//
//	cd demo/ffi/ygui2/go
//	CGO_LDFLAGS="-L<build>/src/yetty/yffi -lyetty_ffi" \
//	LD_LIBRARY_PATH=<build>/src/yetty/yffi go run hello.go
//
// One fullscreen frame over the drawable contract: a panel root, a
// column of labels, a separator, a progress bar, and a statusbar. After
// the first envelope the app sits idle — a clean frame ships ZERO
// bytes. Resize the pane: the toolkit relays out and ships only the
// widgets whose geometry actually changed. Ctrl-C quits (also `q` while
// no text input holds focus).
package main

import (
	"fmt"
	"os"

	"github.com/zokrezyl/yetty/bindings/go/ygui2"
)

func main() {
	app := ygui2.NewApp()

	column := app.Root.Column(ygui2.Layout{Grow: 1, Gap: 10, Pad: 16})
	column.Label(ygui2.LabelOpts{Text: "ygui2 — hello from Go", Fg: "#74C5A5"},
		ygui2.Layout{Basis: 24})
	column.Separator(ygui2.Layout{Basis: 8})
	column.Label(ygui2.LabelOpts{
		Text: "every widget is a wire group; this whole page was ONE envelope"},
		ygui2.Layout{Basis: 20})
	column.Label(ygui2.LabelOpts{
		Text: "clean frames ship zero bytes — watch the pane stay silent", Fg: "#9FA7A8"},
		ygui2.Layout{Basis: 20})
	row := column.Row(ygui2.Layout{Basis: 24, Gap: 10})
	row.Label(ygui2.LabelOpts{Text: "progress", Fg: "#9FA7A8"}, ygui2.Layout{Basis: 90})
	row.Progress(ygui2.ProgressOpts{Value: 0.42}, ygui2.Layout{Basis: 220, Cross: 12})
	column.Column(ygui2.Layout{Grow: 1}) // spacer pushes the statusbar to the bottom
	column.Statusbar(ygui2.StatusbarOpts{Left: "hello.go — static frame",
		Right: "Ctrl-C: quit"}, ygui2.Layout{Basis: 24})

	if runError := app.Run(ygui2.RunOpts{}); runError != nil {
		fmt.Fprintln(os.Stderr, "hello:", runError)
		os.Exit(1)
	}
}
