#!/usr/bin/env python3
"""Encode a file to base94 and output the OSC sequence for yetty plugins.

OSC Format: ESC ] 99999;<plugin>;<mode>;<x>;<y>;<w>;<h>;<base94_payload> BEL

Where:
- 99999 is YETTY_OSC_VENDOR_ID
- plugin: shader, image, etc.
- mode: A (absolute), R (relative to cursor)
- x, y: position in cells
- w, h: size in cells
- base94_payload: file content encoded in base94
"""

import sys
import os

def base94_encode(data: bytes) -> str:
    """Encode bytes to base94 (2 chars per byte)."""
    result = []
    for byte in data:
        result.append(chr(33 + (byte // 94)))
        result.append(chr(33 + (byte % 94)))
    return ''.join(result)

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file> [options]", file=sys.stderr)
        print("Options:", file=sys.stderr)
        print("  --plugin=X  Plugin name: shader, image. Default: auto-detect", file=sys.stderr)
        print("  --raw       Output only the base94 encoded string (no OSC wrapper)", file=sys.stderr)
        print("  --mode=X    Position mode: A (absolute) or R (relative). Default: R", file=sys.stderr)
        print("  --x=N       X position in cells. Default: 0", file=sys.stderr)
        print("  --y=N       Y position in cells. Default: 0", file=sys.stderr)
        print("  --w=N       Width in cells. Default: 40", file=sys.stderr)
        print("  --h=N       Height in cells. Default: 20", file=sys.stderr)
        sys.exit(1)

    input_file = sys.argv[1]
    raw_mode = '--raw' in sys.argv

    # Parse options
    plugin = None
    mode = 'R'
    x, y = 0, 0
    w, h = 40, 20

    for arg in sys.argv[2:]:
        if arg.startswith('--plugin='):
            plugin = arg.split('=')[1]
        elif arg.startswith('--mode='):
            mode = arg.split('=')[1].upper()
        elif arg.startswith('--x='):
            x = int(arg.split('=')[1])
        elif arg.startswith('--y='):
            y = int(arg.split('=')[1])
        elif arg.startswith('--w='):
            w = int(arg.split('=')[1])
        elif arg.startswith('--h='):
            h = int(arg.split('=')[1])

    # Auto-detect plugin from file extension
    if plugin is None:
        ext = os.path.splitext(input_file)[1].lower()
        if ext in ('.wgsl',):
            plugin = 'shader'
        elif ext in ('.png', '.jpg', '.jpeg', '.gif', '.bmp'):
            plugin = 'image'
        else:
            plugin = 'shader'  # default

    with open(input_file, 'rb') as f:
        data = f.read()

    encoded = base94_encode(data)

    if raw_mode:
        print(encoded)
    else:
        # Output OSC sequence: ESC ] 99999;<plugin>;<mode>;<x>;<y>;<w>;<h>;<payload> ST
        # Using ST (ESC \) instead of BEL (0x07) as terminator - more reliable through PTY
        osc = f"\033]99999;{plugin};{mode};{x};{y};{w};{h};{encoded}\033\\"
        print(osc, end='')

if __name__ == '__main__':
    main()
