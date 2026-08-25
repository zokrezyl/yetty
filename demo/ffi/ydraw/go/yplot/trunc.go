// Conceptual reproduction of demo/scripts/yplot/trunc.sh
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

	// (1) The rounding family side by side — they split left of the origin.
	fmt.Println("(1) trunc vs floor vs round")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-3.5, 3.5}, Functions: []ydraw.Function{
		{Body: "trunc(x)", Name: "truncated", Color: "#74C5A5"},
		{Body: "floor(x)", Name: "floored", Color: "#FF6B6B"},
		{Body: "round(x)", Name: "rounded", Color: "#FFE66D"},
	}})

	// (2) A symmetric sawtooth: x - trunc(x) keeps the sign of x.
	fmt.Println("(2) signed sawtooth vs fract")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-1.1, 1.1}, Functions: []ydraw.Function{
		{Body: "x-trunc(x)", Name: "signed_saw", Color: "#6BA892"},
		{Body: "fract(x)", Name: "fract_saw", Color: "#556162"},
	}})

	// (3) A quantizer / ADC: a sine snapped onto discrete steps.
	fmt.Println("(3) quantizer")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 6.28}, YRange: []float64{-1.1, 1.1}, Functions: []ydraw.Function{
		{Body: "sin(x)", Name: "signal", Color: "#364A47"},
		{Body: "trunc(sin(x)*4)/4", Name: "quantized", Color: "#74C5A5"},
	}})

	// (4) A bit-crushed ramp: sample-and-hold staircase.
	fmt.Println("(4) bit-crushed ramp")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 4}, YRange: []float64{-0.2, 4.2}, Functions: []ydraw.Function{
		{Body: "x", Name: "ramp", Color: "#556162"},
		{Body: "trunc(x*3)/3", Name: "crushed", Color: "#6BA892"},
	}})
}
