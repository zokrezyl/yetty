-- ydraw client interface target sketch — yvideo complex drawables (Lua).
-- NOT RUNNABLE YET — see python/yvideo/playback.py. A Video carries an
-- explicit user-chosen id: frames stream to the live instance as
-- CMD_UPDATE payloads addressed by it. Assets: demo/assets/yvideo/.
local ydraw = require("yetty.ydraw")

local ASSETS = "demo/assets/yvideo/"

print("smpte bars")
local dlist = ydraw.DrawableList()
dlist:add(ydraw.Video{ASSETS .. "smpte.h264", video_w = 320, video_h = 240})
dlist:dcs_emit()
dlist:destroy()

print("testsrc at 480x270, 25fps")
dlist = ydraw.DrawableList()
dlist:add(ydraw.Video{ASSETS .. "testsrc.h264", video_w = 320, video_h = 240,
                      width = 480, height = 360, fps = 25})
dlist:dcs_emit()
dlist:destroy()
