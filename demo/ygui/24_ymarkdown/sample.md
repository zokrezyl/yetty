# Hello from sample.md

This file lives next to the demo's `main.c`. Run the demo with this
path to render it instead of the inline default:

```
demo-ygui-24-ymarkdown demo/ygui/24_ymarkdown/sample.md
```

## Features exercised here

- Multiple heading levels
- **bold** and *italic*
- ***bold-italic***
- Inline `code` runs
- Bulleted list

## Why it works

The widget calls `yetty_ygui_ymarkdown_set_file`, which slurps
the file and forwards to `ymarkdown`. The returned `ydraw-core` buffer
is handed to the RICH widget, which translates each primitive by the
widget's resolved layout origin.
