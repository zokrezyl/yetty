// Conceptual reproduction of demo/scripts/yplot/buffer-language.sh
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

	// (1) Single inline buffer rendered as a curve; its samples are spread
	// across the X domain by the shader's linear-interpolation walk.
	fmt.Println("(1) inline buffer values")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 1}, YRange: []float64{-1, 1}, Buffers: []ydraw.Buffer{
		{Name: "data", Size: 8, Values: []float64{0, 0.3, 0.6, 0.9, 0.6, 0, -0.4, -0.2}},
	}})

	// (2) Buffer * expression: envelope(x) samples the buffer, multiplied by
	// a high-frequency carrier.
	fmt.Println("(2) buffer * sinusoidal carrier")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 1}, YRange: []float64{-1, 1}, Buffers: []ydraw.Buffer{
		{Name: "envelope", Size: 8, Values: []float64{0, 0.3, 0.6, 0.9, 0.6, 0, -0.4, -0.2}},
	}, Functions: []ydraw.Function{
		{Body: "envelope(x)*sin(x*60)", Name: "pulse", Color: "#74C5A5"},
	}})

	// (3) Two buffers acting as inputs to one expression: y-over-x without a
	// scatter primitive.
	fmt.Println("(3) y/x ratio of two buffers")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 1}, YRange: []float64{0, 6}, Buffers: []ydraw.Buffer{
		{Name: "bx", Size: 6, Values: []float64{1, 1.5, 2, 2.5, 3, 3.5}},
		{Name: "by", Size: 6, Values: []float64{1, 2.25, 4, 6.25, 9, 12.25}},
	}, Functions: []ydraw.Function{
		{Body: "by(x)/bx(x)", Name: "ratio", Color: "#FFE66D"},
	}})

	// (4) Animated buffer: amplitude-modulated by `time`. Referencing `time`
	// auto-subscribes the plot to the animation timer, exactly as in the DSL.
	fmt.Println("(4) time-modulated buffer (animated)")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 1}, YRange: []float64{-1.2, 1.2}, Buffers: []ydraw.Buffer{
		{Name: "wave", Size: 16, Values: []float64{0, 0.4, 0.7, 0.95, 1, 0.95, 0.7, 0.4, 0, -0.4, -0.7, -0.95, -1, -0.95, -0.7, -0.4}},
	}, Functions: []ydraw.Function{
		{Body: "wave(x)*(0.5+0.5*sin(time*2))", Name: "live", Color: "#6BA892"},
	}})
}
