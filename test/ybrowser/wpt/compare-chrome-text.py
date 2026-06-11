#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["websocket-client"]
# ///
"""Text-keyed ybrowser-vs-Chrome geometry diff.

Matching by nth-of-type DOM path breaks the moment the two engines build a
slightly different element tree (github: a couple of wrapper divs shift every
descendant's path, so ~nothing matches and the diff is useless). This variant
keys on the VISIBLE TEXT of each element instead — robust to structural drift —
so it reports where real, identifiable content actually lands in each engine.

    compare-chrome-text.py <url> [width]

For each element carrying direct text, both engines report (text, x, y, w, h);
we match on a normalized text prefix and print the largest geometry deltas.
Env: YBROWSER=<path>, CDP_PORT=<port>.
"""
import json
import os
import re
import signal
import subprocess
import sys
import time
import urllib.request

from websocket import create_connection

URL = sys.argv[1] if len(sys.argv) > 1 else "https://github.com/"
WIDTH = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
PORT = int(os.environ.get("CDP_PORT", "9243"))
YB = os.environ.get("YBROWSER", "./build-desktop-ytrace-release/tools/ybrowser/ybrowser")
PROF = "/home/misi/work/my/yetty-ybrowser/tmp/cdp-text-prof"
os.makedirs(PROF, exist_ok=True)


def norm(s):
    s = re.sub(r"\s+", " ", (s or "")).strip().lower()
    return s[:30]


# Chrome: each element's DIRECT text (immediate text-node children only) + rect.
JS = r"""
(function(){
  var sx=window.scrollX, sy=window.scrollY, out=[], all=document.querySelectorAll("*");
  for(var i=0;i<all.length;i++){
    var el=all[i], t="";
    for(var n=el.firstChild;n;n=n.nextSibling){ if(n.nodeType===3) t+=n.nodeValue; }
    t=t.replace(/\s+/g," ").trim();
    if(t.length<4) continue;
    var r=el.getBoundingClientRect();
    if(r.width<1&&r.height<1) continue;
    out.push(t.slice(0,60)+"\t"+(r.left+sx).toFixed(1)+"\t"+(r.top+sy).toFixed(1)+
             "\t"+r.width.toFixed(1)+"\t"+r.height.toFixed(1));
  }
  return out.join("\n");
})()
"""


def chrome_geo():
    chrome = subprocess.Popen(
        ["google-chrome", "--headless=new", "--disable-gpu", "--no-sandbox",
         f"--remote-debugging-port={PORT}", "--remote-allow-origins=*",
         f"--user-data-dir={PROF}", f"--window-size={WIDTH},2000", "--hide-scrollbars",
         "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        ws_url = None
        for _ in range(80):
            try:
                data = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json"))
                ws_url = next((t["webSocketDebuggerUrl"] for t in data if t.get("type") == "page"), None)
                if ws_url:
                    break
            except Exception:
                time.sleep(0.3)
        ws = create_connection(ws_url, max_size=None)
        mid = [0]

        def cmd(method, **params):
            mid[0] += 1
            ws.send(json.dumps({"id": mid[0], "method": method, "params": params}))
            while True:
                msg = json.loads(ws.recv())
                if msg.get("id") == mid[0]:
                    return msg.get("result", {})

        cmd("Page.enable"); cmd("Runtime.enable")
        cmd("Emulation.setDeviceMetricsOverride", width=WIDTH, height=2000,
            deviceScaleFactor=1, mobile=False)
        cmd("Page.navigate", url=URL)
        time.sleep(6)
        res = cmd("Runtime.evaluate", expression=JS, returnByValue=True)
        ws.close()
        out = {}
        for line in (res.get("result", {}).get("value", "") or "").splitlines():
            p = line.split("\t")
            if len(p) == 5:
                out.setdefault(norm(p[0]), (float(p[1]), float(p[2]), float(p[3]), float(p[4])))
        return out
    finally:
        chrome.send_signal(signal.SIGTERM)
        try:
            chrome.wait(timeout=5)
        except Exception:
            chrome.kill()


def ybrowser_geo():
    env = dict(os.environ)
    env.pop("DISPLAY", None)
    env.setdefault("YLEXBOR_BOOT_BUDGET_MS", "2000")
    p = subprocess.run([YB, "--once", "--dump-boxes", "-w", str(WIDTH), URL],
                       capture_output=True, env=env, timeout=30)
    out = {}
    for line in p.stdout.decode("utf-8", "replace").splitlines():
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if len(f) < 7:
            continue
        # kind tag dt x y w h  w=.. i=.. u=.. snippet...
        try:
            x, y, w, h = float(f[3]), float(f[4]), float(f[5]), float(f[6])
        except ValueError:
            continue
        snippet = f[-1] if len(f) > 10 else ""
        k = norm(snippet)
        if len(k) >= 4 and w >= 1:
            out.setdefault(k, (x, y, w, h))
    return out


def main():
    chrome = chrome_geo()
    yb = ybrowser_geo()
    keys = [k for k in chrome if k in yb]
    print(f"chrome text-elements: {len(chrome)}  ybrowser: {len(yb)}  matched by text: {len(keys)}")
    rows = []
    for k in keys:
        cx, cy, cw, ch = chrome[k]
        yx, yy, yw, yh = yb[k]
        d = max(abs(cx - yx), abs(cy - yy), abs(cw - yw), abs(ch - yh))
        rows.append((d, k, (cx, cy, cw, ch), (yx, yy, yw, yh)))
    rows.sort(reverse=True)
    ds = sorted(r[0] for r in rows)
    median = ds[len(ds) // 2] if ds else 0
    total = sum(ds)

    def bucket(limit):
        return sum(1 for d in ds if d <= limit)

    print(f"DIFF METRIC  matched={len(rows)}  median_dmax={median:.0f}  total_dmax={total:.0f}  "
          f"within: 8px={bucket(8)} 25px={bucket(25)} 100px={bucket(100)} 400px={bucket(400)}")
    # dump matched pairs sorted by chrome-y for drift analysis
    with open("tmp/match-pairs.tsv", "w") as fh:
        fh.write("chrome_y\tcx\tcy\tcw\tch\tyx\tyy\tyw\tyh\tdmax\ttext\n")
        for d, k, c, y in sorted(rows, key=lambda r: r[2][1]):
            fh.write("\t".join(str(round(v)) for v in (c[1],) + c + y) + f"\t{round(d)}\t{k}\n")
    print(f"\nworst matched-by-text deltas (chrome -> ybrowser):")
    print(f"{'dmax':>6}  {'chrome (x,y,w,h)':>26}  {'ybrowser (x,y,w,h)':>26}  text")
    for d, k, c, y in rows[:30]:
        print(f"{d:6.0f}  {str(tuple(round(v) for v in c)):>26}  "
              f"{str(tuple(round(v) for v in y)):>26}  {k}")


if __name__ == "__main__":
    main()
