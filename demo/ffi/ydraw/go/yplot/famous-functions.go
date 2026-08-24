// Conceptual reproduction of demo/scripts/yplot/famous-functions.sh
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

	// (1) Fourier synthesis of a square wave — the Gibbs phenomenon.
	fmt.Println("(1) Fourier square wave")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-6.28, 6.28}, YRange: []float64{-1.4, 1.4}, Functions: []ydraw.Function{
		{Body: "sign(sin(x))", Name: "target", Color: "#556162"},
		{Body: "4/pi*sin(x)", Name: "first", Color: "#74C5A5"},
		{Body: "4/pi*(sin(x)+sin(3*x)/3+sin(5*x)/5+sin(7*x)/7+sin(9*x)/9+sin(11*x)/11)", Name: "sum", Color: "#FF6B6B"},
	}})

	// (2) Damped harmonic oscillator — ringdown.
	fmt.Println("(2) damped harmonic oscillator")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 12.56}, YRange: []float64{-1.1, 1.1}, Functions: []ydraw.Function{
		{Body: "exp(-x/4)", Name: "envelope", Color: "#364A47"},
		{Body: "sin(x*3)*exp(-x/4)", Name: "ring", Color: "#6BA892"},
	}})

	// (3) Gaussian vs Lorentzian bell curves.
	fmt.Println("(3) Gaussian vs Lorentzian")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-6, 6}, YRange: []float64{-0.1, 1.15}, Functions: []ydraw.Function{
		{Body: "exp(-x*x/2)", Name: "gaussian", Color: "#6BA892"},
		{Body: "1/(1+x*x)", Name: "lorentzian", Color: "#FFE66D"},
	}})

	// (4) Logistic sigmoid vs tanh — S-curves.
	fmt.Println("(4) logistic vs tanh")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-6, 6}, YRange: []float64{-1.1, 1.1}, Functions: []ydraw.Function{
		{Body: "1/(1+exp(-2*x))", Name: "logistic", Color: "#74C5A5"},
		{Body: "tanh(x)", Name: "hyperbolic", Color: "#F38181"},
	}})

	// (5) The catenary — a hanging chain (cosh, not a parabola).
	fmt.Println("(5) catenary")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-2.5, 2.5}, YRange: []float64{0, 6.5}, Functions: []ydraw.Function{
		{Body: "cosh(x)", Name: "chain", Color: "#6BA892"},
	}})

	// (6) The cardinal sine — the sinc opcode patches the 0/0 at the origin.
	fmt.Println("(6) sinc")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-15.7, 15.7}, YRange: []float64{-0.3, 1.1}, Functions: []ydraw.Function{
		{Body: "sinc(x)", Name: "cardinal", Color: "#74C5A5"},
	}})

	// (7) A wave packet: carrier under a Gaussian envelope.
	fmt.Println("(7) wave packet")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{-6, 6}, YRange: []float64{-1.1, 1.1}, Functions: []ydraw.Function{
		{Body: "exp(-x*x/4)", Name: "envelope", Color: "#364A47"},
		{Body: "exp(-x*x/4)*cos(x*6)", Name: "packet", Color: "#6BA892"},
	}})

	// (8) smoothstep easing vs a linear ramp.
	fmt.Println("(8) smoothstep easing")
	show(ydraw.Plot{Width: 600, Height: 240, XRange: []float64{0, 1}, YRange: []float64{-0.1, 1.1}, Functions: []ydraw.Function{
		{Body: "x", Name: "linear", Color: "#556162"},
		{Body: "smoothstep(0,1,x)", Name: "eased", Color: "#74C5A5"},
	}})
}
