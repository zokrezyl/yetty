// Conceptual reproduction of demo/scripts/yplot/heatmaps.sh
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

	// (1) Separable interference: the standing-wave checkerboard.
	fmt.Println("(1) standing-wave checkerboard")
	show(ydraw.Plot{Width: 360, Height: 360, XRange: []float64{-6.28, 6.28}, YRange: []float64{-6.28, 6.28}, Functions: []ydraw.Function{
		{Body: "sin(x)*cos(y)", Name: "field"},
	}})

	// (2) Concentric ripples from a point source.
	fmt.Println("(2) radial ripples")
	show(ydraw.Plot{Width: 360, Height: 360, XRange: []float64{-6, 6}, YRange: []float64{-6, 6}, Functions: []ydraw.Function{
		{Body: "sin(3*sqrt(x*x+y*y))", Name: "field"},
	}})

	// (3) Two-source interference — the double-slit pattern.
	fmt.Println("(3) two-source interference")
	show(ydraw.Plot{Width: 360, Height: 360, XRange: []float64{-7, 7}, YRange: []float64{-7, 7}, Functions: []ydraw.Function{
		{Body: "0.5*(sin(4*sqrt((x-2)*(x-2)+y*y))+sin(4*sqrt((x+2)*(x+2)+y*y)))", Name: "field"},
	}})

	// (4) A hyperbolic saddle.
	fmt.Println("(4) hyperbolic saddle")
	show(ydraw.Plot{Width: 360, Height: 360, XRange: []float64{-4, 4}, YRange: []float64{-4, 4}, Functions: []ydraw.Function{
		{Body: "tanh(x*y)", Name: "field"},
	}})

	// (5) Procedural terrain via fBm: three octaves of noise2 at decorrelated
	// frequencies.
	fmt.Println("(5) procedural terrain")
	show(ydraw.Plot{Width: 360, Height: 360, XRange: []float64{0, 4}, YRange: []float64{0, 4}, Functions: []ydraw.Function{
		{Body: "(0.55*noise2(x,y)+0.3*noise2(x*2.13,y*2.13)+0.15*noise2(x*4.27,y*4.27))*2-1", Name: "field"},
	}})
}
