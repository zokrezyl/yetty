// ydraw client interface target sketch — drawing from Go.
// NOT RUNNABLE YET — same draw-list semantics as python/hello.py: one
// drawable list, immediate appends in call order; Add() manages nothing
// and returns nothing; the user picks font ids (a field of the FONT
// record); the receiver measures content from record AABBs.
package main

import "github.com/zokrezyl/yetty/bindings/go/ydraw"

func main() {
	dlist := ydraw.NewDrawableList()

	// Fonts: FontID is a field OF the record — the user picks it and
	// references it from Text spans.
	const scoreFont = 7
	dlist.Add(ydraw.Font{FontID: scoreFont, Name: "Emmentaler"})

	// Shapes: paint prefix (Z, Fill, Stroke, StrokeWidth) + the schema's
	// flattened geometry fields, exact names.
	dlist.Add(ydraw.Circle{CenterX: 96, CenterY: 96, Radius: 64,
		Fill: "#6BA892", Stroke: "#364A47", StrokeWidth: 2})
	dlist.Add(ydraw.Box{CenterX: 280, CenterY: 96, HalfWidth: 72,
		HalfHeight: 48, CornerRadius: 8, Fill: "#1E262C", Z: 1})
	dlist.Add(ydraw.Star{CenterX: 460, CenterY: 96, Radius: 56,
		NumPoints: 5, InnerRatio: 0.45, Fill: "#74C5A5"})
	dlist.Add(ydraw.Segment{StartX: 40, StartY: 180, EndX: 600, EndY: 180,
		Stroke: "#9FA7A8", StrokeWidth: 3})

	// Text runs: FontID -1 (default) is the terminal's default face.
	dlist.Add(ydraw.Text{Body: "hello ydraw", X: 40, Y: 240, FontSize: 24,
		Color: "#E0E5E4"})
	dlist.Add(ydraw.Text{Body: "\U0001D11E\U0001D122", X: 40, Y: 290,
		FontSize: 32, FontID: scoreFont, Color: "#6BA892"})

	dlist.DcsEmit() // envelope on stdout, scrolls with the text
	// dlist.ToBytes() // same payload, for yscene NodeSetContent over RPC
	dlist.Destroy()
}
