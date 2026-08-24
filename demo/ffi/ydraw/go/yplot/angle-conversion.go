// Conceptual reproduction of demo/scripts/yplot/angle-conversion.sh
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

	// (1) Trig on a degree axis: the x-axis runs 0..360 in degrees, the
	// domain is wrapped in radians() so the curves stay correct.
	fmt.Println("(1) sin & cos over a 0..360 degree axis")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 360}, YRange: []float64{-1.2, 1.2}, Functions: []ydraw.Function{
		{Body: "sin(radians(x))", Name: "sine", Color: "#6BA892"},
		{Body: "cos(radians(x))", Name: "cosine", Color: "#74C5A5"},
	}})

	// (2) Slope to angle: degrees(atan(slope)) reads out a line's inclination
	// directly in degrees.
	fmt.Println("(2) slope to angle")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-10, 10}, YRange: []float64{-90, 90}, Functions: []ydraw.Function{
		{Body: "degrees(atan(x))", Name: "angle", Color: "#FFE66D"},
	}})

	// (3) A 90-degree phase shift written in the same units as the axis.
	fmt.Println("(3) 90-degree phase shift on a degree axis")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 360}, YRange: []float64{-1.2, 1.2}, Functions: []ydraw.Function{
		{Body: "sin(radians(x))", Name: "reference", Color: "#556162"},
		{Body: "sin(radians(x - 90))", Name: "shifted", Color: "#74C5A5"},
	}})
}
