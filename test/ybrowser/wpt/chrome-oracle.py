#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["websocket-client"]
# ///
"""
Chrome geometry oracle: render a URL in headless Chrome and emit, for every
element, `<dom-path>\\t<x>\\t<y>\\t<w>\\t<h>` (document coords). The DOM path is
an nth-of-type chain identical to ybrowser's `--dump-geo`
(yetty_ylexbor_test_box_path_at), so the two can be diffed element-for-element.

    chrome-oracle.py <url> [width]

Dismisses a cookie-consent wall if present (Google sites).
"""
import base64
import json
import os
import signal
import subprocess
import sys
import time
import urllib.request

from websocket import create_connection

URL = sys.argv[1] if len(sys.argv) > 1 else "https://example.com/"
WIDTH = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
PORT = int(os.environ.get("CDP_PORT", "9242"))
PROF = "/home/misi/work/my/yetty-ybrowser/tmp/cdp-oracle-prof"
os.makedirs(PROF, exist_ok=True)

JS = r"""
(function(){
  function pathOf(el){
    var segs=[];
    while(el && el.nodeType===1){
      var tag=el.tagName.toLowerCase();
      var n=1, s=el.previousElementSibling;
      while(s){ if(s.tagName.toLowerCase()===tag) n++; s=s.previousElementSibling; }
      segs.unshift(tag+":"+n);
      el=el.parentElement;
    }
    return segs.join(">");
  }
  var sx=window.scrollX, sy=window.scrollY;
  var out=[], all=document.querySelectorAll("*");
  for(var i=0;i<all.length;i++){
    var el=all[i], r=el.getBoundingClientRect();
    out.push(pathOf(el)+"\t"+(r.left+sx).toFixed(1)+"\t"+(r.top+sy).toFixed(1)+
             "\t"+r.width.toFixed(1)+"\t"+r.height.toFixed(1));
  }
  return out.join("\n");
})()
"""

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
            for t in data:
                if t.get("type") == "page":
                    ws_url = t["webSocketDebuggerUrl"]
                    break
            if ws_url:
                break
        except Exception:
            time.sleep(0.3)
    if not ws_url:
        print("oracle: no devtools target", file=sys.stderr)
        sys.exit(2)
    ws = create_connection(ws_url, max_size=None)
    mid = [0]

    def cmd(method, **params):
        mid[0] += 1
        ws.send(json.dumps({"id": mid[0], "method": method, "params": params}))
        while True:
            msg = json.loads(ws.recv())
            if msg.get("id") == mid[0]:
                return msg.get("result", {})

    cmd("Page.enable")
    cmd("Runtime.enable")
    cmd("Emulation.setDeviceMetricsOverride", width=WIDTH, height=2000,
        deviceScaleFactor=1, mobile=False)
    cmd("Page.navigate", url=URL)
    time.sleep(5)
    cmd("Runtime.evaluate", expression="""
      (function(){var b=[...document.querySelectorAll('button,[role=button],a')]
        .find(e=>/accept all|reject all|i agree|accept/i.test((e.textContent||'').trim()));
        if(b){b.click();return 1;}return 0;})()
    """)
    time.sleep(4)
    res = cmd("Runtime.evaluate", expression=JS, returnByValue=True)
    val = res.get("result", {}).get("value", "")
    sys.stdout.write(val or "")
    ws.close()
finally:
    chrome.send_signal(signal.SIGTERM)
    try:
        chrome.wait(timeout=5)
    except Exception:
        chrome.kill()
