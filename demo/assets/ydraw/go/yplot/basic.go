// Conceptual reproduction of demo/scripts/yplot/basic.sh
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

	// Single function — defaults for everything else.
	fmt.Println("basic sin(x)")
	show(ydraw.Plot{Functions: []ydraw.Function{
		{Body: "sin(x)"},
	}})

	// Multi-function with explicit names + per-curve colors. The names double
	// as the legend labels, so pick descriptive ones.
	fmt.Println("named curves with colors")
	show(ydraw.Plot{Functions: []ydraw.Function{
		{Body: "sin(x)", Name: "sine", Color: "#FF6B6B"},
		{Body: "cos(x)", Name: "cosine", Color: "#4ECDC4"},
	}})

	// Custom dimensions and axis range.
	fmt.Println("custom size and ranges")
	show(ydraw.Plot{Width: 480, Height: 240, XRange: []float64{-3, 3}, YRange: []float64{-2, 10}, Functions: []ydraw.Function{
		{Body: "x*x", Name: "parabola", Color: "#FFE66D"},
		{Body: "2*x+1", Name: "line", Color: "#AA96DA"},
	}})

	// Minimal: no grid, no axes, no labels.
	fmt.Println("minimal chrome")
	show(ydraw.Plot{NoGrid: true, NoAxes: true, NoLabels: true, Functions: []ydraw.Function{
		{Body: "sin(x)*cos(3*x)"},
	}})

	// Chained spectrum-like plot.
	fmt.Println("audio harmonics")
	show(ydraw.Plot{Width: 520, Height: 200, XRange: []float64{0, 6.28}, YRange: []float64{-1, 1}, Functions: []ydraw.Function{
		{Body: "sin(x)", Name: "first", Color: "#FF6B6B"},
		{Body: "sin(2*x)/2", Name: "second", Color: "#4ECDC4"},
		{Body: "sin(3*x)/3", Name: "third", Color: "#AA96DA"},
		{Body: "sin(x)+sin(2*x)/2+sin(3*x)/3", Name: "sum", Color: "#FCBF49"},
	}})
}
