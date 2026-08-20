// Conceptual reproduction of demo/scripts/yplot/error-function.sh
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

	// (1) erf and its complement erfc.
	fmt.Println("(1) erf(x) and erfc(x)")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-1.2, 2.2}, Functions: []ydraw.Function{
		{Body: "erf(x)", Name: "erf_x", Color: "#74C5A5"},
		{Body: "erfc(x)", Name: "erfc_x", Color: "#F38181"},
	}})

	// (2) The normal CDF beside its bell-curve pdf.
	fmt.Println("(2) normal CDF with its pdf")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-4, 4}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "0.5*(1+erf(x/sqrt(2)))", Name: "cdf", Color: "#6BA892"},
		{Body: "exp(-x*x/2)/sqrt(2*pi)", Name: "pdf", Color: "#FFE66D"},
	}})

	// (3) erf next to tanh — two look-alike sigmoids.
	fmt.Println("(3) erf vs tanh")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-1.2, 1.2}, Functions: []ydraw.Function{
		{Body: "erf(x)", Name: "erf_x", Color: "#74C5A5"},
		{Body: "tanh(x)", Name: "tanh_x", Color: "#556162"},
	}})
}
