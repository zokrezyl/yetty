// ydraw client interface target sketch — yvideo complex drawables (TS).
// NOT RUNNABLE YET — see python/yvideo/playback.py. A Video carries an
// explicit user-chosen id: frames stream to the live instance as
// CMD_UPDATE payloads addressed by it. Assets: demo/assets/yvideo/.
import { DrawableList, Video } from "@yetty/ydraw";

const ASSETS = "demo/assets/yvideo/";

console.log("smpte bars");
let dlist = new DrawableList();
dlist.add(new Video(ASSETS + "smpte.h264", { id: 1 }));
dlist.dcsEmit();
dlist.destroy();

console.log("testsrc at 480x270, 25fps");
dlist = new DrawableList();
dlist.add(new Video(ASSETS + "testsrc.h264", { id: 2, width: 480,
                                               height: 270, fps: 25 }));
dlist.dcsEmit();
dlist.destroy();
