// ydraw client interface target sketch — yimage complex drawables (Go).
// NOT RUNNABLE YET — see python/yimage/gallery.py. Asset paths are
// repo-relative. Assets: demo/assets/yimage/.
package main

import (
	"fmt"

	"github.com/zokrezyl/yetty/bindings/go/ydraw"
)

const assets = "demo/assets/yimage/"

func show(drawables ...ydraw.Drawable) {
	dlist := ydraw.NewDrawableList()
	for _, drawable := range drawables {
		dlist.Add(drawable)
	}
	dlist.DcsEmit()
	dlist.Destroy()
}

func main() {
	fmt.Println("rose")
	show(ydraw.Image{Path: assets + "rose.png"})

	fmt.Println("hero, scaled to 480px wide")
	show(ydraw.Image{Path: assets + "hero.png", Width: 480, Height: 270})

	// One record among others: caption in the same envelope.
	fmt.Println("wordmark with caption")
	show(ydraw.Image{Path: assets + "wordmark.png", X: 0, Y: 0, Width: 320, Height: 96},
		ydraw.Text{Body: "terminal unchained", X: 0, Y: 112, FontSize: 18,
			Color: "#9FA7A8"})

	fmt.Println("thumbnail strip")
	strip := ydraw.NewDrawableList()
	names := []string{"gradient.png", "rose.png", "hero.png", "wordmark.png"}
	for index, name := range names {
		strip.Add(ydraw.Image{Path: assets + name, X: float64(index) * 170,
			Y: 0, Width: 160, Height: 100})
	}
	strip.DcsEmit()
	strip.Destroy()
}
