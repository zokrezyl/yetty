// Conceptual reproduction of demo/scripts/yplot/conditionals.sh
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

	// (1) Piecewise: a parabola for x<0, a sine for x>=0.
	fmt.Println("(1) piecewise  x<0 ? x^2 : sin(4x)")
	show(ydraw.Plot{Width: 560, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-1.2, 2.2}, Functions: []ydraw.Function{
		{Body: "select(x*x, sin(4*x), ge(x,0))", Name: "piecewise", Color: "#74C5A5"},
	}})

	// (2) A rectangular pulse: 1 where |x|<1, else 0 — the boxcar window.
	fmt.Println("(2) boxcar pulse [|x| < 1]")
	show(ydraw.Plot{Width: 560, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-0.2, 1.2}, Functions: []ydraw.Function{
		{Body: "lt(abs(x),1)", Name: "boxcar", Color: "#FFE66D"},
	}})

	// (3) ReLU and the Heaviside step.
	fmt.Println("(3) ReLU max(x,0) and Heaviside step")
	show(ydraw.Plot{Width: 560, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-0.5, 3}, Functions: []ydraw.Function{
		{Body: "max(x,0)", Name: "relu", Color: "#6BA892"},
		{Body: "gt(x,0)", Name: "heaviside", Color: "#F38181"},
	}})

	// (4) A staircase from summed steps: comparisons compose into quantizers.
	fmt.Println("(4) threshold staircase")
	show(ydraw.Plot{Width: 560, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-0.2, 4.2}, Functions: []ydraw.Function{
		{Body: "ge(x,-2)+ge(x,-1)+ge(x,0)+ge(x,1)+ge(x,2)", Name: "stairs", Color: "#74C5A5"},
	}})
}
