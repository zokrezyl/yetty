// ydraw client interface target sketch — yvideo complex drawables (Go).
// NOT RUNNABLE YET — see python/yvideo/playback.py. A Video carries an
// explicit user-chosen id: frames stream to the live instance as
// CMD_UPDATE payloads addressed by it. Assets: demo/assets/yvideo/.
package main

import (
	"fmt"

	"github.com/zokrezyl/yetty/bindings/go/ydraw"
)

const assets = "demo/assets/yvideo/"

func main() {
	fmt.Println("smpte bars")
	dlist := ydraw.NewDrawableList()
	dlist.Add(ydraw.Video{Path: assets + "smpte.h264", ID: 1})
	dlist.DcsEmit()
	dlist.Destroy()

	fmt.Println("testsrc at 480x270, 25fps")
	dlist = ydraw.NewDrawableList()
	dlist.Add(ydraw.Video{Path: assets + "testsrc.h264", ID: 2,
		Width: 480, Height: 270, FPS: 25})
	dlist.DcsEmit()
	dlist.Destroy()
}
