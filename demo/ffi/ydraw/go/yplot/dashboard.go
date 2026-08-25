// Conceptual reproduction of demo/scripts/yplot/dashboard.sh
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
	fmt.Println("CPU usage (last 60s, normalized)")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 6.28}, YRange: []float64{0, 1}, Functions: []ydraw.Function{
		{Body: "0.5+0.3*sin(x)+0.1*sin(3*x)", Name: "cpu", Color: "#FF6B6B"},
	}})
	fmt.Println("memory and swap")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 6.28}, YRange: []float64{0, 1}, Functions: []ydraw.Function{
		{Body: "0.6+0.2*sin(x/2)", Name: "mem", Color: "#4ECDC4"},
		{Body: "0.1+0.05*sin(x*4)", Name: "swap", Color: "#AA96DA"},
	}})
	fmt.Println("network traffic (rx / tx)")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{0, 6.28}, YRange: []float64{-1, 1}, Functions: []ydraw.Function{
		{Body: "sin(x)*cos(x/3)", Name: "rx", Color: "#95E1D3"},
		{Body: "cos(x)*sin(x/2)", Name: "tx", Color: "#FCBF49"},
	}})
	fmt.Println("latency model (cubic vs linear)")
	show(ydraw.Plot{Width: 520, Height: 160, XRange: []float64{-2, 2}, YRange: []float64{-4, 4}, Functions: []ydraw.Function{
		{Body: "x*x*x", Name: "cubic", Color: "#F38181"},
		{Body: "2*x", Name: "linear", Color: "#72D6C9"},
	}})
}
