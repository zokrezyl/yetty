// Conceptual reproduction of demo/scripts/yplot/signal-shaping.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
package main

import (
	"fmt"

	"github.com/zokrezyl/yetty/bindings/go/ydraw"
)

func show(plot ydraw.Plot) {
	dlist := ydraw.NewDrawableList()
	dlist.Add(plot)
	dlist.DcsEmit()
	dlist.Destroy()
}

func main() {

	// (1) ADSR-shaped envelope drives three differently-pitched carriers; the
	// same buffer is sampled by all three curves. A colored Buffer is also
	// rendered as a reference curve.
	fmt.Println("(1) ADSR envelope x three harmonics")
	show(ydraw.Plot{Width: 560, Height: 200, XRange: []float64{0, 1}, YRange: []float64{-1.1, 1.1}, Buffers: []ydraw.Buffer{
		{Name: "env", Size: 16, Color: "#364A47", Values: []float64{0, 0.4, 0.8, 1, 0.95, 0.85, 0.75, 0.65, 0.55, 0.45, 0.35, 0.25, 0.18, 0.12, 0.06, 0}},
	}, Functions: []ydraw.Function{
		{Body: "env(x)*sin(x*6)", Name: "h1", Color: "#FF6B6B"},
		{Body: "env(x)*sin(x*12)", Name: "h2", Color: "#FFE66D"},
		{Body: "env(x)*sin(x*24)", Name: "h3", Color: "#74C5A5"},
	}})

	// (2) Two control buffers driving a parametric expression; both rendered
	// as faint reference curves.
	fmt.Println("(2) gain + bias driven carrier")
	show(ydraw.Plot{Width: 560, Height: 200, XRange: []float64{0, 1}, YRange: []float64{-1.5, 1.5}, Buffers: []ydraw.Buffer{
		{Name: "gain", Size: 8, Color: "#556162", Values: []float64{0.1, 0.4, 0.7, 1, 1, 0.7, 0.4, 0.1}},
		{Name: "bias", Size: 8, Color: "#9FA7A8", Values: []float64{0, 0.05, 0.1, 0.15, 0.1, 0, -0.1, -0.05}},
	}, Functions: []ydraw.Function{
		{Body: "sin(x*60)*gain(x)+bias(x)", Name: "out", Color: "#6BA892"},
	}})

	// (3) Time-animated mix: the envelope stays put, the modulator's phase is
	// driven by `time` so the waveform travels across the window.
	fmt.Println("(3) static envelope, travelling phase")
	show(ydraw.Plot{Width: 560, Height: 200, XRange: []float64{0, 1}, YRange: []float64{-1.1, 1.1}, Buffers: []ydraw.Buffer{
		{Name: "env", Size: 12, Color: "#5A8979", Values: []float64{0, 0.3, 0.7, 1, 0.95, 0.85, 0.7, 0.5, 0.3, 0.15, 0.05, 0}},
	}, Functions: []ydraw.Function{
		{Body: "env(x)*sin(x*40 - time*4)", Name: "travel", Color: "#74C5A5"},
	}})
}
