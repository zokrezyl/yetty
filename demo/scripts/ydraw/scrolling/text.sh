#!/bin/bash
# Demo: ypaint text rendering via YAML

# YAML document with text primitive
YAML='body:
  - text:
      position: [50, 50]
      content: "Hello World!"
      font-size: 48
      color: "#ff0000"
  - text:
      position: [50, 100]
      content: "Wellcome to this presentation about some features of the Yetty Terminal Unchained"
      font-size: 16
      color: "#00ff00"
  - text:
      position: [50, 130]
      content: "This is a rich text rendered on gpu using MSDF fonts"
      font-size: 14
      color: "#00ffff"
  - text:
      position: [0, 160]
      content: "Yetty stands for (Ye)tty(tty). You likely know what tty means"
      font-size: 14
      color: "#00ffff"
'

# Base64 encode the YAML
PAYLOAD=$(echo -n "$YAML" | base64 -w0)

# Send via OSC 666674 (ypaint scroll mode)
# Format: ESC ] 666674 ; --yaml ; <base64_payload> BEL
printf '\033]600002;;%s\007' "$PAYLOAD"
