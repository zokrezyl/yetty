#!/usr/bin/env python3
"""ydraw client interface — yvideo complex drawables. RUNNABLE.

A Video packs one yvideo complex record carrying the raw H.264 Annex-B
stream as its initial chunk (`ffmpeg -i in.mp4 -c:v copy -an -f h264
out.h264`). The class does no SPS parsing — video_w/video_h (the SPS
dimensions) are required; width/height are the display bounds (0 =
video size). Frame STREAMING (CMD_UPDATE deltas to the live instance)
stays with the yvideo tool for now. Assets: demo/assets/yvideo/.
"""
from pathlib import Path

from yetty.ydraw import DrawableList, Video

ASSETS = Path(__file__).resolve().parents[3] / "yvideo"

# SMPTE color bars, natural size (320x240 per the SPS).
print('smpte bars')
dlist = DrawableList()
dlist.add(Video(ASSETS / "smpte.h264", video_w=320, video_h=240))
dlist.dcs_emit()
dlist.destroy()

# Test source, scaled display bounds and an fps override.
print('testsrc at 480x360, 25fps')
dlist = DrawableList()
dlist.add(Video(ASSETS / "testsrc.h264", video_w=320, video_h=240,
                width=480, height=360, fps=25))
dlist.dcs_emit()
dlist.destroy()
