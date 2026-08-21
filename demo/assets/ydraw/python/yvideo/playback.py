#!/usr/bin/env python3
"""ydraw client interface — yvideo complex drawables. RUNNABLE.

A Video packs one yvideo complex record carrying the raw H.264 Annex-B
stream as its initial chunk (`ffmpeg -i in.mp4 -c:v copy -an -f h264
out.h264`). Dimensions come from the stream's SPS; set video_w/video_h
only to override a lying SPS. A Video carries an explicit user-chosen
id: the record is wrapped in CMD_GROUP(id) so frame streaming —
CMD_UPDATE payloads addressed by that id, still the yvideo tool's job —
can target the live instance. Assets: demo/assets/yvideo/.
"""
from pathlib import Path

from yetty.ydraw import DrawableList, Video

ASSETS = Path(__file__).resolve().parents[3] / "yvideo"

# SMPTE color bars. Dimensions come from the SPS.
print('smpte bars')
dlist = DrawableList()
dlist.add(Video(ASSETS / "smpte.h264", id=1))
dlist.dcs_emit()
dlist.destroy()

# Explicit display bounds + fps override, test source clip.
print('testsrc at 480x270, 25fps')
dlist = DrawableList()
dlist.add(Video(ASSETS / "testsrc.h264", id=2,
                width=480, height=270, fps=25))
dlist.dcs_emit()
dlist.destroy()
