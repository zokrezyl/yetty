// Conceptual reproduction of demo/scripts/yplot/stochastic.sh
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

	// (1) White noise vs smooth value noise at the same frequency.
	fmt.Println("(1) rand() vs noise()")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 10}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "rand(x*8)", Name: "white", Color: "#556162"},
		{Body: "noise(x*8)", Name: "smooth", Color: "#74C5A5"},
	}})

	// (2) Fractal (fBm) noise: summed octaves.
	fmt.Println("(2) fractal noise")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 6}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "0.5*noise(x*2)+0.25*noise(x*4)+0.125*noise(x*8)+0.0625*noise(x*16)", Name: "fbm", Color: "#6BA892"},
	}})

	// (3) A clean signal corrupted by additive noise.
	fmt.Println("(3) signal + additive noise")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 6.28}, YRange: []float64{-1.3, 1.3}, Functions: []ydraw.Function{
		{Body: "sin(x)", Name: "clean", Color: "#364A47"},
		{Body: "sin(x)+0.2*(rand(x*97)*2-1)", Name: "noisy", Color: "#FF6B6B"},
	}})
}
