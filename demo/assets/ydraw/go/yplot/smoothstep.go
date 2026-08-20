// Conceptual reproduction of demo/scripts/yplot/smoothstep.sh
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

	// (1) Ease in/out versus a linear ramp.
	fmt.Println("(1) smoothstep ease vs linear ramp")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 1}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "x", Name: "linear", Color: "#556162"},
		{Body: "smoothstep(0,1,x)", Name: "eased", Color: "#74C5A5"},
	}})

	// (2) Iterated smoothstep — the "smootherstep" trick.
	fmt.Println("(2) iterated smoothstep")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 1}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "smoothstep(0,1,x)", Name: "once", Color: "#5A8979"},
		{Body: "smoothstep(0,1,smoothstep(0,1,x))", Name: "twice", Color: "#74C5A5"},
	}})

	// (3) A soft window: ramp up across one edge, back down across another.
	fmt.Println("(3) soft window")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-4, 4}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "smoothstep(-2,-1,x)*(1 - smoothstep(1,2,x))", Name: "gate", Color: "#6BA892"},
	}})

	// (4) A soft staircase vs the hard floor() staircase.
	fmt.Println("(4) soft staircase vs hard floor")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 5}, YRange: []float64{-0.2, 5.2}, Functions: []ydraw.Function{
		{Body: "floor(x)", Name: "hard", Color: "#364A47"},
		{Body: "floor(x)+smoothstep(0,1,fract(x))", Name: "soft", Color: "#74C5A5"},
	}})

	// (5) A contrast curve: smoothstep between two interior edges remaps a
	// 0..1 signal — the tone response of a contrast slider.
	fmt.Println("(5) contrast remap")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 1}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "x", Name: "identity", Color: "#556162"},
		{Body: "smoothstep(0.3,0.7,x)", Name: "contrast", Color: "#FFE66D"},
	}})
}
