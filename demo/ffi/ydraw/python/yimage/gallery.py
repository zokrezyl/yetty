#!/usr/bin/env python3
"""ydraw client interface target sketch — yimage complex drawables.

NOT RUNNABLE YET. Conceptual: an Image packs one yimage complex record
(bounds + image_w/image_h + the pixel buffer feeding the atlas texture;
decode from PNG happens client-side, exactly where ycat does it today).
Assets: demo/assets/yimage/.
"""
from pathlib import Path

from yetty.ydraw import DrawableList, Image, Text

ASSETS = Path(__file__).resolve().parents[4] / "assets" / "yimage"


def show(*drawables):
    dlist = DrawableList()
    for drawable in drawables:
        dlist.add(drawable)
    dlist.dcs_emit()
    dlist.destroy()


# One image, displayed at its natural size.
print('rose')
show(Image(ASSETS / "rose.png"))

# Scaled display bounds: the record's bounds are independent of the
# pixel dimensions.
print('hero, scaled to 480px wide')
show(Image(ASSETS / "hero.png", width=480, height=270))

# The FFI advantage over the CLI: an image is just one record among
# others — caption it in the same envelope, in the same coordinate
# space.
print('wordmark with caption')
show(Image(ASSETS / "wordmark.png", x=0, y=0, width=320, height=96),
     Text("terminal unchained", x=0, y=112, font_size=18,
          color="#9FA7A8"))

# A row of thumbnails — layout is plain arithmetic, like everywhere else.
print('thumbnail strip')
strip = DrawableList()
for index, name in enumerate(["gradient.png", "rose.png", "hero.png",
                              "wordmark.png"]):
    strip.add(Image(ASSETS / name, x=index * 170, y=0,
                    width=160, height=100))
strip.dcs_emit()
strip.destroy()
