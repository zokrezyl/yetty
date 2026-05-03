#!/usr/bin/env python3
"""
Connect to a headless Chrome via Chrome DevTools Protocol and dump every
page-side console.* message to stdout.

Usage:
    python3 capture-chrome-console.py <DEBUG_PORT> [TIMEOUT_SECONDS]

Why: --headless=new (and even --headless=old in modern Chrome) does not
reliably pipe console.* output to stderr — the output we used to grep
for from `chrome ... 2>&1 | tee log` is just not there. CDP is the only
robust way. Each message is printed as one line:

    [CONSOLE log] <text>
    [PAGE-EXCEPTION] <text>

so the surrounding shell script can grep for checkpoints normally.
"""
import json
import sys
import time
import urllib.request

import websocket  # websocket-client (sync) — already available in nix shell


def find_page_target(port: int, retries: int = 30) -> str:
    """Return the WebSocket URL of the first 'page' target."""
    for _ in range(retries):
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list") as r:
                targets = json.load(r)
            for t in targets:
                if t.get("type") == "page" and "webSocketDebuggerUrl" in t:
                    return t["webSocketDebuggerUrl"]
        except Exception:
            pass
        time.sleep(0.2)
    raise SystemExit(f"no page target on debug port {port}")


def fmt_arg(arg: dict) -> str:
    """Render one Runtime.RemoteObject for stdout."""
    if "value" in arg:
        return str(arg["value"])
    if arg.get("type") == "object":
        return arg.get("description", "{object}")
    return arg.get("description", "")


def main() -> None:
    if len(sys.argv) < 2:
        print("usage: capture-chrome-console.py <debug_port> [timeout] [navigate_url]",
              file=sys.stderr)
        sys.exit(2)
    port = int(sys.argv[1])
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
    navigate_url = sys.argv[3] if len(sys.argv) > 3 else None

    ws_url = find_page_target(port)
    ws = websocket.create_connection(ws_url)
    msg_id = 0

    def send(method: str, params: dict | None = None) -> None:
        nonlocal msg_id
        msg_id += 1
        ws.send(json.dumps({"id": msg_id, "method": method, "params": params or {}}))

    # Subscribe BEFORE navigating so we don't miss the very first
    # console.log calls (e.g. yetty's index.html boot logs).
    send("Runtime.enable")
    send("Log.enable")
    send("Page.enable")

    # Mirror postMessage to console.log. vm-bridge.html dispatches VM
    # console output as `window.parent.postMessage({type:'term-output',
    # data:...}, '*')`. When the iframe is opened directly (no parent
    # listener), or even when nested under index.html, those messages are
    # invisible to CDP — they don't go through console.* and CDP can't
    # observe postMessage traffic. Solution: inject a `message` listener
    # on `window` (since `parent === window` for top-level navigation, the
    # postMessage routes to self) that re-emits each event as a
    # console.log line, prefixed so the surrounding test script can grep
    # for it (e.g. `[VM-OUT]`, `[VM-MSG]`).
    #
    # Page.addScriptToEvaluateOnNewDocument registers the script for
    # *every* document created from now on, and runs before the page's
    # own JS — so we capture the very first byte the VM writes.
    POSTMSG_MIRROR_JS = """
(function() {
    function fmt(d) {
        if (d == null) return String(d);
        if (typeof d === 'string') return d;
        try { return JSON.stringify(d); } catch (e) { return String(d); }
    }
    window.addEventListener('message', function(e) {
        var d = e.data;
        if (d && d.type === 'term-output') {
            // Pump VM bytes verbatim — grep-friendly. \\r/\\n in the data
            // already form natural log breaks once console.log runs.
            console.log('[VM-OUT] ' + (d.data == null ? '' : d.data));
        } else if (d && d.type) {
            console.log('[VM-MSG] ' + d.type + ' ' + fmt(d));
        }
    }, false);
})();
"""
    send("Page.addScriptToEvaluateOnNewDocument", {"source": POSTMSG_MIRROR_JS})

    if navigate_url:
        send("Page.navigate", {"url": navigate_url})

    deadline = time.time() + timeout
    ws.settimeout(0.5)
    while time.time() < deadline:
        try:
            raw = ws.recv()
        except websocket.WebSocketTimeoutException:
            continue
        except Exception:
            break
        if not raw:
            continue
        msg = json.loads(raw)
        method = msg.get("method")
        params = msg.get("params") or {}

        # console.log / console.error / etc.
        if method == "Runtime.consoleAPICalled":
            level = params.get("type", "log")
            args = " ".join(fmt_arg(a) for a in params.get("args", []))
            print(f"[CONSOLE {level}] {args}", flush=True)

        # uncaught exceptions
        elif method == "Runtime.exceptionThrown":
            details = params.get("exceptionDetails", {})
            text = details.get("text", "")
            exc = details.get("exception") or {}
            desc = exc.get("description") or details.get("stackTrace", {}).get("description", "")
            print(f"[PAGE-EXCEPTION] {text}: {desc}", flush=True)

        # browser-emitted log (e.g. CSP, deprecations, network failures)
        elif method == "Log.entryAdded":
            entry = params.get("entry", {})
            print(
                f"[BROWSER {entry.get('level','info')} {entry.get('source','')}] "
                f"{entry.get('text','')}",
                flush=True,
            )

        # page lifecycle (useful to confirm navigation succeeded)
        elif method == "Page.frameNavigated":
            f = params.get("frame", {})
            print(f"[PAGE navigated] {f.get('url','')}", flush=True)
        elif method == "Page.loadEventFired":
            print("[PAGE load]", flush=True)

        # New execution context — Runtime.enable was issued before
        # Page.navigate, but emit a marker so we know the page actually
        # has a JS context (vs. a load failure).
        elif method == "Runtime.executionContextCreated":
            ctx = params.get("context", {})
            print(f"[CTX created] id={ctx.get('id')} origin={ctx.get('origin','')} "
                  f"name={ctx.get('name','')}", flush=True)

    # End of window — try to dump the document title and the innerText of
    # an element with id=status or similar so the surrounding shell can
    # see what state the page reached even if console.log was silent.
    try:
        ws.settimeout(2.0)
        send("Runtime.evaluate", {
            "expression": "JSON.stringify({title:document.title,"
                          "status:(document.getElementById('status')||{}).textContent,"
                          "loadingHtml:(document.getElementById('loading')||{}).innerHTML,"
                          "haveYettyJs:typeof Module!=='undefined',"
                          "selectedMode:(typeof selectedMode!=='undefined'?selectedMode:null)})",
            "returnByValue": True,
        })
        for _ in range(40):
            try:
                raw = ws.recv()
            except Exception:
                break
            if not raw:
                break
            msg = json.loads(raw)
            if msg.get("id") and "result" in msg:
                v = msg.get("result", {}).get("result", {}).get("value")
                if v:
                    print(f"[PAGE state] {v}", flush=True)
                break
    except Exception as e:
        print(f"[CAPTURE end-of-window error] {e}", flush=True)


if __name__ == "__main__":
    main()
