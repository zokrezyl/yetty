# yetty rich-content demos

Everything in this directory renders **inline in the terminal** — no
window, no viewer, just `ycat`. The same wasm binaries run on the web,
on iOS/tvOS, and on the desktop.

## Try these

```sh
ycat /usr/share/yetty/demos/README.md      # this file, rendered
ycat /usr/share/yetty/demos/sample.md      # markdown feature tour
ycat /usr/share/yetty/demos/shapes.svg     # vector graphics
ycat /usr/share/yetty/demos/logo.jpeg      # raster image
ycat /usr/share/yetty/demos/sample.pdf     # a real PDF, page by page
```

`yecho` echoes text with inline glyphs and styled spans:

```sh
yecho '@spinner loading {color=#6BA892: yetty} @heart'
yecho --list                               # all 47 shader glyphs
yecho '{style=bold|underline: unchained} {bg=#1E262C: terminal}'
```

Plain pipes still work — outside a yetty terminal both tools degrade
to ordinary `cat` / `echo` behaviour.
