#!/usr/bin/env python3
"""Lazy <picture> images whose URL only lives in the <noscript> fallback.

nytimes (and many lazy-loading sites) ship article thumbnails as:

    <picture>
      <source media="...">            <!-- no srcset until JS runs -->
      <img loading="lazy">            <!-- no src -->
      <noscript><img src="real.jpg"></noscript>   <!-- SEO / no-JS fallback -->
    </picture>

The real URL is not in the accessible DOM (the img has no src, the sources carry
only media); it's computed in JS on scroll. But the page ships the real image in
a <noscript> fallback. This test pins that img_pick_url falls back to the
<noscript>'s <img src> (and to a <picture><source srcset>) so a one-shot render
shows the images instead of one lone hero. Without the fallback nytimes rendered
exactly ONE image.

    run: test/ybrowser/render/lazy-image-noscript.py
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib
import binascii

YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")


def make_png(path, w, h):
    """Write a minimal solid-colour PNG of exactly w x h."""
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", binascii.crc32(tag + data) & 0xffffffff))
    raw = b""
    for _ in range(h):
        raw += b"\x00" + (b"\x40\x80\xc0" * w)  # filter byte + RGB pixels
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit RGB
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def decoded_urls(html_path):
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    p = subprocess.run([YB, "--once", "-w", "800", html_path],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60, env=env)
    trace = p.stderr.decode("utf-8", "replace")
    return set(re.findall(r"img DECODE OK.*?url=(\S+)", trace))


def main():
    fails = 0
    total = 0
    with tempfile.TemporaryDirectory() as d:
        img_ns = os.path.join(d, "ns.png")
        img_ss = os.path.join(d, "ss.png")
        make_png(img_ns, 60, 40)
        make_png(img_ss, 50, 30)

        # Case 1: URL only in the <noscript> fallback img.
        p1 = os.path.join(d, "p1.html")
        with open(p1, "w") as f:
            f.write("<!doctype html><meta charset=utf-8><body><picture>"
                    "<source media='screen and (min-width:1px)'>"
                    "<img loading='lazy' alt='x'>"
                    "<noscript><img src='file://" + img_ns + "'></noscript>"
                    "</picture></body>")

        # Case 2: URL in a <picture><source srcset> (no img src).
        p2 = os.path.join(d, "p2.html")
        with open(p2, "w") as f:
            f.write("<!doctype html><meta charset=utf-8><body><picture>"
                    "<source srcset='file://" + img_ss + "'>"
                    "<img loading='lazy' alt='y'>"
                    "</picture></body>")

        cases = [
            (p1, "file://" + img_ns, "noscript fallback <img src> is loaded"),
            (p2, "file://" + img_ss, "picture <source srcset> is loaded"),
        ]
        for path, want_url, label in cases:
            total += 1
            urls = decoded_urls(path)
            ok = any(want_url in u for u in urls)
            print(f"{'PASS' if ok else 'FAIL'}  {label} -> decoded={sorted(urls) or 'none'}")
            if not ok:
                fails += 1

    print(f"\n=== {total - fails} passed, {fails} failed ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
