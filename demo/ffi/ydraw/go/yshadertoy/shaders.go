// ydraw client interface target sketch — yshadertoy complex drawables (Go).
// NOT RUNNABLE YET — see python/yshadertoy/shaders.py. The payload IS the
// WGSL shader (mainImage contract). Assets: demo/assets/yshadertoy/.
package main

import (
	"fmt"
	"os"

	"github.com/zokrezyl/yetty/bindings/go/ydraw"
)

const assets = "demo/assets/yshadertoy/"

func show(shader ydraw.Shadertoy) {
	dlist := ydraw.NewDrawableList()
	dlist.Add(shader)
	dlist.DcsEmit()
	dlist.Destroy()
}

func main() {
	// Animated plasma — iTime drives it, no client-side ticking needed.
	fmt.Println("plasma")
	show(ydraw.Shadertoy{Path: assets + "plasma.wgsl", Width: 560, Height: 240})

	// Swirl, taller rect.
	fmt.Println("swirl")
	show(ydraw.Shadertoy{Path: assets + "swirl.wgsl", Width: 560, Height: 320})

	// Source can come from anywhere — a string works as well as a file.
	fmt.Println("palette, inline source")
	source, _ := os.ReadFile(assets + "palette.wgsl")
	show(ydraw.Shadertoy{Source: string(source), Width: 560, Height: 160})
}
