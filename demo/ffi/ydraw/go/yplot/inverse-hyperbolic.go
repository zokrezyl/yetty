// Conceptual reproduction of demo/scripts/yplot/inverse-hyperbolic.sh
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

	// (1) asinh as a signed-log compressor: linear core, log tails.
	fmt.Println("(1) asinh — signed-log compression")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-10, 10}, YRange: []float64{-3.5, 3.5}, Functions: []ydraw.Function{
		{Body: "x", Name: "identity", Color: "#364A47"},
		{Body: "sign(x)*log(1+abs(x))", Name: "signed_log", Color: "#556162"},
		{Body: "asinh(x)", Name: "arcsinh", Color: "#74C5A5"},
	}})

	// (2) atanh, the Fisher z-transform — blows up at +/-1.
	fmt.Println("(2) atanh — Fisher transform")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-0.99, 0.99}, YRange: []float64{-3, 3}, Functions: []ydraw.Function{
		{Body: "atanh(x)", Name: "fisher", Color: "#6BA892"},
	}})

	// (3) acosh, defined for x >= 1 — rapidity / catenary arc length.
	fmt.Println("(3) acosh")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{1, 10}, YRange: []float64{-0.2, 3.2}, Functions: []ydraw.Function{
		{Body: "acosh(x)", Name: "arccosh", Color: "#FFE66D"},
	}})

	// (4) The three together, each real on its own part of the domain.
	fmt.Println("(4) asinh vs atanh vs acosh")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-3, 3}, Functions: []ydraw.Function{
		{Body: "asinh(x)", Name: "arcsinh", Color: "#74C5A5"},
		{Body: "atanh(x)", Name: "fisher", Color: "#FF6B6B"},
		{Body: "acosh(x)", Name: "arccosh", Color: "#FFE66D"},
	}})
}
