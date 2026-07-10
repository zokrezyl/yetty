#!/usr/bin/env python3
"""Paint-order (CSS stacking) regression test — headless, no GPU.

There is no depth buffer in the renderer: occlusion is painter's order (draw
sequence). The paint pass computes a COMPOSITE stacking key per box and sorts by
it, reproducing CSS painting order (canvas -> negative-z -> normal flow ->
positioned by z, nesting-aware). This asserts the emission order for the cases
the WPT geometry harness cannot see (paint order is not geometry), including the
escape and containment cases that a single scalar stacking level cannot get
right at the same time.

Env:
    YBROWSER   path to the ybrowser binary
Exit 0 pass, 1 fail, 77 skip (binary missing).
"""
import os
import re
import subprocess
import sys

YB = os.environ.get("YBROWSER",
                    "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")


def paint_order(html, width=800, height=400):
    """Return the list of painted 'block'/'text' trace lines in emission (draw)
    order. Emission order == draw order == occlusion order."""
    env = dict(os.environ)
    env["YTRACE_DEFAULT_ON"] = "yes"
    env.pop("DISPLAY", None)
    proc = subprocess.run(
        [YB, "--once", "-w", str(width), "-H", str(height), "-"],
        input=html.encode("utf-8"), stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE, timeout=60, env=env)
    trace = proc.stderr.decode("utf-8", errors="replace")
    return [m.group(0) for m in
            re.finditer(r"paint (block|text).*$", trace, re.MULTILINE)]


def index_of(order, needle):
    for i, line in enumerate(order):
        if needle in line:
            return i
    return None


def last_index_of(order, needle):
    hit = None
    for i, line in enumerate(order):
        if needle in line:
            hit = i
    return hit


# Each case: (name, html, assertion(order) -> (ok, detail)). "above" means the
# element painted LATER (higher emission index) occludes the earlier one.
CASES = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn
    return deco


@case("early-DOM overlay covers later content")
def _early(order):
    # Overlay declared BEFORE the page content; must paint after it.
    ov = last_index_of(order, "OVERLAY_MARKER")
    pg = last_index_of(order, "PAGE_MARKER")
    return (ov is not None and pg is not None and ov > pg,
            f"overlay#{ov} vs last-page#{pg}")


_early.html = """<!doctype html><meta charset=utf-8>
<style>.o{position:fixed;left:0;top:40px;width:800px;height:100px;
background:#fff;z-index:1000;border:2px solid #333}
.p p{margin:0 0 12px;font-size:18px;line-height:24px}</style>
<div class="o"><div>OVERLAY_MARKER agree?</div></div>
<div class="p"><p>PAGE_MARKER one</p><p>PAGE_MARKER two</p>
<p>PAGE_MARKER three</p><p>PAGE_MARKER four</p></div>"""


@case("root canvas paints before negative-z, normal flow above it")
def _negz(order):
    # canvas is the first painted block (i=0); the z-index:-1 red box paints
    # after the canvas (not covered) but before normal-flow content.
    canvas = index_of(order, "bg=ffffffff")
    red = index_of(order, "bg=ff0000")
    content = last_index_of(order, "CONTENT_MARKER")
    ok = (canvas is not None and red is not None and content is not None
          and canvas < red < content)
    return (ok, f"canvas#{canvas} < red#{red} < content#{content}")


_negz.html = """<!doctype html><meta charset=utf-8>
<style>body{margin:0;background:#fff}
.b{position:absolute;left:0;top:20px;width:400px;height:60px;
background:#ff0000;z-index:-1}.c{font-size:18px;line-height:24px}</style>
<div class="b">BEHIND</div><div class="c"><p>CONTENT_MARKER</p></div>"""


@case("z-index:100 child of z-index:auto wrapper escapes above z-index:50 sibling")
def _escape(order):
    child = index_of(order, "bg=00ff00")   # z:100 inside auto wrapper
    sib = index_of(order, "bg=0000ff")     # z:50 sibling
    return (child is not None and sib is not None and child > sib,
            f"child(z100)#{child} vs sibling(z50)#{sib}")


_escape.html = """<!doctype html><meta charset=utf-8>
<style>.w{position:absolute;left:0;top:0;width:400px;height:200px}
.ch{position:absolute;left:0;top:40px;width:400px;height:40px;
background:#00ff00;z-index:100}
.s{position:absolute;left:0;top:40px;width:400px;height:40px;
background:#0000ff;z-index:50}</style>
<div class="w"><div class="ch">CHILD</div></div><div class="s">SIB</div>"""


@case("z-index:50 sibling paints above the whole z-index:5 wrapper context")
def _contain(order):
    child = index_of(order, "bg=00ff00")   # z:100 inside z:5 wrapper
    sib = index_of(order, "bg=0000ff")     # z:50 sibling of the wrapper
    return (child is not None and sib is not None and sib > child,
            f"sibling(z50)#{sib} vs child(z100 in z5)#{child}")


_contain.html = """<!doctype html><meta charset=utf-8>
<style>.w{position:absolute;left:0;top:0;width:400px;height:200px;z-index:5}
.ch{position:absolute;left:0;top:40px;width:400px;height:40px;
background:#00ff00;z-index:100}
.s{position:absolute;left:0;top:40px;width:400px;height:40px;
background:#0000ff;z-index:50}</style>
<div class="w"><div class="ch">CHILD</div></div><div class="s">SIB</div>"""


def main():
    if not os.path.exists(YB):
        print(f"SKIP: ybrowser binary not found at {YB}")
        return 77
    failed = 0
    for name, fn in CASES:
        order = paint_order(fn.html)
        ok, detail = fn(order)
        print(f"{'PASS' if ok else 'FAIL'}  {name}  [{detail}]")
        if not ok:
            failed += 1
    print(f"\n=== {len(CASES) - failed}/{len(CASES)} stacking cases passed ===")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
