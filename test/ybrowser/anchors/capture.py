#!/usr/bin/env python3
"""
Capture a live URL into a pack-1 (script-free) anchor fixture directory.

Fetches the page HTML once, strips every <script> element (pack 1 compares
the server-rendered layout; neither engine runs page JS), rewrites relative
resource URLs to absolute against the final fetch URL (so stylesheets,
images and fonts load identically from the fixture file in both engines),
then hands the result to make-fixture.py, which stamps the anchors and
records the Chrome geometry reference.

    capture.py <url> <fixture-dir> [-w <width>] [--budget <ms>]

The captured page needs network access at COMPARE time too (linked CSS /
images stay remote) — live captures are for investigation and belong under
tmp/, not in fixtures/ (committed fixtures must be synthetic, obfuscated or
reduced; see README.md).

A Google consent wall is dodged with the SOCS cookie; pages are fetched
with a desktop-Chrome user agent so the desktop variant is served.
"""
import argparse
import gzip
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
USER_AGENT = ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
              "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36")

SCRIPT_RE = re.compile(r"<script\b[^>]*>.*?</script\s*>",
                       re.DOTALL | re.IGNORECASE)
URL_ATTRIBUTE_RE = re.compile(
    r'\b(src|href|poster|data-src)=(["\'])(.*?)\2', re.IGNORECASE)
SRCSET_RE = re.compile(r'\b(srcset)=(["\'])(.*?)\2', re.IGNORECASE)
CSS_URL_RE = re.compile(r'url\(\s*(["\']?)([^"\')]+)\1\s*\)')


def fetch(url):
    request = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "text/html,application/xhtml+xml",
        "Accept-Encoding": "gzip",
        "Accept-Language": "en-US,en;q=0.9",
        "Cookie": "SOCS=CAI",
    })
    with urllib.request.urlopen(request, timeout=60) as response:
        payload = response.read()
        if response.headers.get("Content-Encoding") == "gzip":
            payload = gzip.decompress(payload)
        return payload.decode("utf-8", errors="replace"), response.geturl()


def absolutize(html, base_url):
    def join(candidate):
        candidate = candidate.strip()
        if (not candidate or candidate.startswith("#")
                or candidate.startswith("data:")
                or "://" in candidate.split("?")[0].split("#")[0][:12]):
            return candidate
        return urllib.parse.urljoin(base_url, candidate)

    def rewrite_attribute(match):
        return "%s=%s%s%s" % (match.group(1), match.group(2),
                              join(match.group(3)), match.group(2))

    def rewrite_srcset(match):
        candidates = []
        for entry in match.group(3).split(","):
            parts = entry.strip().split(None, 1)
            if not parts:
                continue
            parts[0] = join(parts[0])
            candidates.append(" ".join(parts))
        return "%s=%s%s%s" % (match.group(1), match.group(2),
                              ", ".join(candidates), match.group(2))

    def rewrite_css_url(match):
        return "url(%s%s%s)" % (match.group(1), join(match.group(2)),
                                match.group(1))

    html = URL_ATTRIBUTE_RE.sub(rewrite_attribute, html)
    html = SRCSET_RE.sub(rewrite_srcset, html)
    html = CSS_URL_RE.sub(rewrite_css_url, html)
    return html


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("url")
    parser.add_argument("fixture_dir")
    parser.add_argument("-w", "--width", type=int, default=1200)
    parser.add_argument("--budget", type=int, default=10000,
                        help="Chrome virtual-time budget in ms (default "
                             "10000 — live subresources need real time)")
    args = parser.parse_args()

    html, final_url = fetch(args.url)
    print("fetched %s (%d bytes, final URL %s)"
          % (args.url, len(html), final_url))

    stripped = SCRIPT_RE.sub("", html)
    script_count = len(SCRIPT_RE.findall(html))
    stripped = absolutize(stripped, final_url)
    print("stripped %d script element(s), absolutized resource URLs"
          % script_count)

    os.makedirs(args.fixture_dir, exist_ok=True)
    fixture_path = os.path.join(args.fixture_dir, "fixture.html")
    with open(fixture_path, "w", encoding="utf-8") as handle:
        handle.write(stripped)

    result = subprocess.run(
        [sys.executable, os.path.join(HERE, "make-fixture.py"),
         fixture_path, "-w", str(args.width), "--budget", str(args.budget)])
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
