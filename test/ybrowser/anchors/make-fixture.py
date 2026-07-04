#!/usr/bin/env python3
"""
Build an anchor fixture: stamp anchor IDs into an HTML page and record the
Chrome-rendered geometry of every anchor as the committed reference.

One headless-Chrome run does both, so the fixture and its reference can never
drift apart:

  1. The page is loaded with an injected epilogue script. After the page
     settles (load + a virtual-time delay, so timer-driven JS has run), the
     script strips any pre-existing data-test/data-expect attributes, stamps
     `data-test="aNNNN"` on every layout-significant element in document
     order, and appends a JSON dump of {anchor: tag, rect, parent, display}.
  2. Chrome's --dump-dom serialization of that DOM *is* the stamped fixture;
     the JSON block is extracted into ref.json and removed from the fixture.

`data-test` is used as the anchor attribute (rather than a new name) because
`ybrowser --dump-boxes` already emits it per box — the comparison needs no
engine change.

Layout-significant = every element that owns a box worth pinning: block-level
containers, flex/grid containers and their items, images. Plain inline spans
are skipped (their rects span line fragments) unless they are flex/grid items.

Usage:
    make-fixture.py <input.html> [-o <output-dir>] [-w <width>] [--budget <ms>]

Defaults: output dir = the input's directory (writes fixture.html + ref.json
there), width 1200, virtual-time budget 4000 ms (the harvest fires at 60% of
the budget). Relative subresources are resolved against the INPUT's directory,
so keep a fixture's assets in its own directory.

Re-running on an already-stamped fixture is idempotent: old anchors are
stripped before stamping.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

CHROME_CANDIDATES = (
    "google-chrome",
    "google-chrome-stable",
    "chromium-browser",
    "chromium",
)

# Runs inside the page. Kept ES5-ish so it also behaves under older engines.
EPILOGUE_JS = r"""
(function () {
  var SKIP_TAGS = { SCRIPT: 1, STYLE: 1, LINK: 1, META: 1, TITLE: 1, HEAD: 1,
                    BR: 1, WBR: 1, NOSCRIPT: 1, TEMPLATE: 1, SOURCE: 1, TRACK: 1 };

  function isSignificant(element) {
    if (SKIP_TAGS[element.tagName]) return false;
    var style = getComputedStyle(element);
    if (style.display === "none") return false;
    if (element.tagName === "IMG") return true;
    if (style.display.indexOf("inline") === 0 &&
        style.display !== "inline-block" &&
        style.display !== "inline-flex" &&
        style.display !== "inline-grid" &&
        style.display !== "inline-table") {
      /* Plain inline (span/a/em): rects span line fragments, skip — unless
       * it is a flex/grid item, where it owns a real box. */
      var parent = element.parentElement;
      if (!parent) return false;
      var parentDisplay = getComputedStyle(parent).display;
      if (parentDisplay.indexOf("flex") < 0 && parentDisplay.indexOf("grid") < 0) {
        return false;
      }
    }
    return true;
  }

  function harvest() {
    var stale = document.querySelectorAll("[data-test],[data-expect]");
    for (var i = 0; i < stale.length; i++) {
      stale[i].removeAttribute("data-test");
      stale[i].removeAttribute("data-expect");
    }

    function pad(number) {
      var text = String(number);
      while (text.length < 4) text = "0" + text;
      return text;
    }

    var body = document.body;
    var all = [body].concat(Array.prototype.slice.call(body.querySelectorAll("*")));
    var stamped = [];
    for (var j = 0; j < all.length; j++) {
      var element = all[j];
      if (element !== body && !isSignificant(element)) continue;
      element.setAttribute("data-test", "a" + pad(stamped.length));
      stamped.push(element);
    }

    var scrollLeft = window.scrollX, scrollTop = window.scrollY;
    var anchors = {};
    for (var k = 0; k < stamped.length; k++) {
      var el = stamped[k];
      var rect = el.getBoundingClientRect();
      var parentAnchor = el.parentElement
          ? el.parentElement.closest("[data-test]") : null;
      anchors[el.getAttribute("data-test")] = {
        tag: el.tagName.toLowerCase(),
        rect: [+(rect.left + scrollLeft).toFixed(1),
               +(rect.top + scrollTop).toFixed(1),
               +rect.width.toFixed(1),
               +rect.height.toFixed(1)],
        parent: parentAnchor ? parentAnchor.getAttribute("data-test") : null,
        display: getComputedStyle(el).display
      };
    }

    var payload = {
      meta: {
        width: window.innerWidth,
        doc_height: +document.documentElement.scrollHeight.toFixed(1)
      },
      anchors: anchors
    };
    var dumpNode = document.createElement("script");
    dumpNode.type = "application/json";
    dumpNode.id = "anchor-dump";
    dumpNode.textContent = JSON.stringify(payload);
    document.body.appendChild(dumpNode);

    var selfNode = document.getElementById("anchor-tool");
    if (selfNode) selfNode.remove();
  }

  if (document.readyState === "complete") {
    setTimeout(harvest, __HARVEST_DELAY_MS__);
  } else {
    window.addEventListener("load", function () {
      setTimeout(harvest, __HARVEST_DELAY_MS__);
    });
  }
})();
"""

DUMP_BLOCK_RE = re.compile(
    r'<script[^>]*id="anchor-dump"[^>]*>(.*?)</script>\s*', re.DOTALL)


def find_chrome():
    override = os.environ.get("CHROME")
    if override:
        return override
    for candidate in CHROME_CANDIDATES:
        path = shutil.which(candidate)
        if path:
            return path
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("input", help="HTML page to turn into a fixture")
    parser.add_argument("-o", "--output-dir",
                        help="directory for fixture.html + ref.json "
                             "(default: the input's directory)")
    parser.add_argument("-w", "--width", type=int, default=1200,
                        help="viewport width in px (default 1200)")
    parser.add_argument("--budget", type=int, default=4000,
                        help="Chrome virtual-time budget in ms (default 4000); "
                             "the anchor harvest fires at 60%% of it")
    args = parser.parse_args()

    chrome = find_chrome()
    if chrome is None:
        print("error: no Chrome/Chromium binary found (set CHROME=<path>)",
              file=sys.stderr)
        return 1

    input_path = os.path.abspath(args.input)
    input_dir = os.path.dirname(input_path)
    output_dir = os.path.abspath(args.output_dir or input_dir)
    os.makedirs(output_dir, exist_ok=True)

    harvest_delay = max(200, int(args.budget * 0.6))
    epilogue = EPILOGUE_JS.replace("__HARVEST_DELAY_MS__", str(harvest_delay))
    source = open(input_path, encoding="utf-8").read()
    # Strip a stale dump block from a previous run before re-injecting.
    source = DUMP_BLOCK_RE.sub("", source)
    injected = source + '\n<script id="anchor-tool">' + epilogue + "</script>\n"

    # The injected copy sits NEXT TO the input so relative subresources
    # (stylesheets, images, fonts) resolve exactly as they do for the fixture.
    injected_path = os.path.join(
        input_dir, ".anchor-inject.%d.html" % os.getpid())
    profile_dir = tempfile.mkdtemp(prefix="ybrowser-anchor-chrome-")
    try:
        with open(injected_path, "w", encoding="utf-8") as handle:
            handle.write(injected)
        command = [
            chrome, "--headless=new", "--disable-gpu", "--no-sandbox",
            "--hide-scrollbars", "--force-device-scale-factor=1",
            "--window-size=%d,900" % args.width,
            "--virtual-time-budget=%d" % args.budget,
            "--user-data-dir=" + profile_dir,
            "--dump-dom", "file://" + injected_path,
        ]
        result = subprocess.run(command, capture_output=True, text=True,
                                timeout=120)
        dom = result.stdout
    finally:
        if os.path.exists(injected_path):
            os.unlink(injected_path)
        shutil.rmtree(profile_dir, ignore_errors=True)

    match = DUMP_BLOCK_RE.search(dom)
    if not match:
        print("error: no anchor dump in Chrome output (page crashed or the "
              "harvest never ran)", file=sys.stderr)
        sys.stderr.write(result.stderr[-2000:])
        return 1
    payload = json.loads(match.group(1))

    fixture_html = DUMP_BLOCK_RE.sub("", dom)
    if not fixture_html.lstrip().lower().startswith("<!doctype"):
        fixture_html = "<!DOCTYPE html>\n" + fixture_html

    payload["meta"]["fixture"] = "fixture.html"
    payload["meta"]["chrome"] = subprocess.run(
        [chrome, "--version"], capture_output=True, text=True
    ).stdout.strip()
    payload["meta"]["budget_ms"] = args.budget

    fixture_path = os.path.join(output_dir, "fixture.html")
    ref_path = os.path.join(output_dir, "ref.json")
    with open(fixture_path, "w", encoding="utf-8") as handle:
        handle.write(fixture_html)
    with open(ref_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=1, sort_keys=True)
        handle.write("\n")

    anchor_count = len(payload["anchors"])
    print("wrote %s (%d anchors, width %d) + %s" %
          (os.path.relpath(fixture_path), anchor_count,
           payload["meta"]["width"], os.path.relpath(ref_path)))
    if payload["meta"]["width"] != args.width:
        print("warning: Chrome viewport was %d, requested %d" %
              (payload["meta"]["width"], args.width), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
