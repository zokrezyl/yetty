// Conceptual reproduction of demo/scripts/yplot/sinc.sh
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

	// (1) The singularity, fixed: sinc(x) rides through a peak of 1 at the
	// origin; bare sin(x)/x is 0/0 there.
	fmt.Println("(1) sinc(x) vs raw sin(x)/x")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-15.7, 15.7}, YRange: []float64{-0.3, 1.1}, Functions: []ydraw.Function{
		{Body: "sin(x)/x", Name: "raw", Color: "#FF6B6B"},
		{Body: "sinc(x)", Name: "fixed", Color: "#74C5A5"},
	}})

	// (2) Single-slit diffraction intensity: sinc squared.
	fmt.Println("(2) single-slit diffraction")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-12.56, 12.56}, YRange: []float64{-0.05, 1.05}, Functions: []ydraw.Function{
		{Body: "sinc(x)", Name: "amplitude", Color: "#364A47"},
		{Body: "sinc(x)*sinc(x)", Name: "intensity", Color: "#6BA892"},
	}})

	// (3) Fourier duality: wider aperture, narrower sinc.
	fmt.Println("(3) aperture width vs lobe width")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-9.42, 9.42}, YRange: []float64{-0.3, 1.1}, Functions: []ydraw.Function{
		{Body: "sinc(x)", Name: "narrow", Color: "#5A8979"},
		{Body: "sinc(x*2)", Name: "wider", Color: "#6BA892"},
		{Body: "sinc(x*4)", Name: "widest", Color: "#74C5A5"},
	}})

	// (4) A Lanczos-3 resampling kernel: a sinc under a wider sinc window.
	fmt.Println("(4) Lanczos-3 kernel")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-9.42, 9.42}, YRange: []float64{-0.3, 1.1}, Functions: []ydraw.Function{
		{Body: "sinc(x/3)", Name: "window", Color: "#364A47"},
		{Body: "sinc(x)*sinc(x/3)", Name: "lanczos", Color: "#74C5A5"},
	}})
}
