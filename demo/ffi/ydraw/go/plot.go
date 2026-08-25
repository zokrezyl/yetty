// ydraw client interface target sketch — plots as complex drawables (Go).
// NOT RUNNABLE YET — see python/plot.py. A Plot is a drawable like any
// shape; Add(plot) packs one binary yplot complex record. Subplots are
// rect arithmetic: several records at computed bounds.
package main

import "github.com/zokrezyl/yetty/bindings/go/ydraw"

func main() {
	dlist := ydraw.NewDrawableList()

	// One plot, two symbolic curves.
	dlist.Add(ydraw.Plot{X: 0, Y: 0, Width: 800, Height: 240,
		Title: "harmonics", XRange: []float64{-6.28, 6.28},
		Functions: []ydraw.Function{
			{Body: "sin(x)", Name: "first", Color: "#6BA892"},
			{Body: "sin(3*x)/3", Name: "third", Color: "#74C5A5"},
		}})

	// Data-driven: samples travel as a named buffer in the record.
	dlist.Add(ydraw.Plot{X: 0, Y: 260, Width: 800, Height: 180,
		Title: "measured", NoGrid: true,
		Buffers: []ydraw.Buffer{
			{Name: "load", Values: []float64{1.0, 1.4, 1.2, 2.1, 1.9, 2.8},
				Color: "#E0E5E4"},
		}})

	// A 2x2 subplot grid is rect arithmetic, nothing more.
	bodies := []string{"sin(x)", "cos(x)", "sin(x)*x", "1/x"}
	for index, body := range bodies {
		column := float64(index % 2)
		row := float64(index / 2)
		dlist.Add(ydraw.Plot{X: column * 400, Y: 460 + row*90,
			Width: 390, Height: 80, NoAxes: true,
			Functions: []ydraw.Function{{Body: body, Color: "#5A8979"}}})
	}

	dlist.DcsEmit()
	dlist.Destroy()
}
