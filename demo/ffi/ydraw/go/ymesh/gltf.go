// ydraw client interface target sketch — ymesh complex drawables (Go).
// NOT RUNNABLE YET — see python/ymesh/gltf.py. Assets: demo/assets/ymesh/.
package main

import (
	"fmt"

	"github.com/zokrezyl/yetty/bindings/go/ydraw"
)

const assets = "demo/assets/ymesh/"

func show(mesh ydraw.Mesh) {
	dlist := ydraw.NewDrawableList()
	dlist.Add(mesh)
	dlist.DcsEmit()
	dlist.Destroy()
}

func main() {
	// Default camera (frame-all), solid shading.
	fmt.Println("duck")
	show(ydraw.Mesh{Path: assets + "Duck.glb", Width: 480, Height: 360})

	// Camera posed via the same parameters the tool's orbit drag mutates.
	fmt.Println("avocado, posed camera")
	show(ydraw.Mesh{Path: assets + "Avocado.glb", Width: 480, Height: 360,
		Azimuth: 0.6, Elevation: 0.3, Zoom: 1.4})

	// Wireframe toggle (the tool's W key).
	fmt.Println("box, wireframe")
	show(ydraw.Mesh{Path: assets + "Box.glb", Width: 320, Height: 240,
		Wireframe: true})
}
