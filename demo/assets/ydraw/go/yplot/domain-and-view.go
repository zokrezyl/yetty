// Conceptual reproduction of demo/scripts/yplot/domain-and-view.sh
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

	// (1) Inline domain -> x_range.
	fmt.Println("(1) x in -pi..pi")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{-math.Pi, math.Pi}, Functions: []ydraw.Function{
		{Body: "sin(x)", Name: "sine", Color: "#FF6B6B"},
		{Body: "cos(x)", Name: "cosine", Color: "#4ECDC4"},
	}})

	// (2) Inline x and y ranges together.
	fmt.Println("(2) x and y ranges")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 2 * math.Pi}, YRange: []float64{-1.2, 1.2}, Functions: []ydraw.Function{
		{Body: "sin(x)+0.3*sin(5*x)", Name: "ripple", Color: "#FCBF49"},
	}})

	// (3) view= overrides the framing without changing the domain — zoom into
	// a region without resampling the expression.
	fmt.Println("(3) view zoom-in")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{-10, 10}, View: [][]float64{{-math.Pi, math.Pi}, {-0.5, 1.5}}, Functions: []ydraw.Function{
		{Body: "sin(x)/x", Name: "signal", Color: "#74C5A5"},
	}})

	// (4) Wide domain, deliberately tighter viewport: evaluated across the
	// full domain, only the viewport is rendered.
	fmt.Println("(4) wide eval, narrow view")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{-10, 10}, View: [][]float64{{-2, 2}, {-1, 1}}, Functions: []ydraw.Function{
		{Body: "sin(x)*exp(-abs(x)/3)", Name: "damped", Color: "#AA96DA"},
	}})
}
