// Run zsh.html in real headless Chrome and exercise the PASTE path through the
// page's own keyboard/paste handlers (issue #21).
//
// Regression: the keydown handler mapped every Ctrl-letter to a control byte and
// called preventDefault() — including Ctrl+V, which became ^V (0x16). That ate
// the browser's default paste action, so the native `paste` event never fired
// and the paste handler that forwards clipboard text to the shell was dead.
//
// This drives the real handlers in the page:
//   - a synthetic Ctrl+V keydown MUST NOT be preventDefault()'d (so the browser
//     would proceed to emit its native paste), while a normal Ctrl-letter (^A)
//     still IS consumed — proving the mapping wasn't broadened away;
//   - a synthetic `paste` event carrying clipboard text MUST reach the shell and
//     run the pasted command.
//
// Drives Chrome over the DevTools Protocol (no npm deps). Run:
//   node zsh_paste_test.mjs
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8138;
const DEBUG_PORT = 9345;
const here = new URL(".", import.meta.url);
// .css must be text/css or the browser refuses xterm's stylesheet (leaving its
// hidden helper textarea visible as a stray box at the cursor).
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript", ".css": "text/css", ".js": "text/javascript", ".json": "application/json", ".svg": "image/svg+xml" };

const server = createServer(async (req, res) => {
  const path = new URL("." + (req.url === "/" ? "/zsh.html" : req.url.split("?")[0]), here);
  try {
    const body = await readFile(path);
    res.setHeader("Content-Type", types[extname(path.pathname)] || "application/octet-stream");
    if (!process.env.YOS_NO_COEP) {
      res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
      res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    }
    res.end(body);
  } catch { res.statusCode = 404; res.end("not found"); }
});
await new Promise((r) => server.listen(PORT, "127.0.0.1", r));

const profile = mkdtempSync(join(tmpdir(), "yos-chrome-paste-"));
const chrome = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
  `--remote-debugging-port=${DEBUG_PORT}`, `--user-data-dir=${profile}`, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill("SIGTERM"); } catch {} server.close(); };
process.on("exit", cleanup);

async function cdpUrl() {
  for (let i = 0; i < 100; i++) {
    try { const r = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/version`); if (r.ok) return (await r.json()).webSocketDebuggerUrl; } catch {}
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error("no CDP");
}
function cdp(url) {
  const ws = new WebSocket(url); let id = 0; const pending = new Map();
  const api = { ready: new Promise((r) => (ws.onopen = r)), onEvent: null };
  ws.onmessage = (ev) => { const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { const { resolve, reject } = pending.get(m.id); pending.delete(m.id); m.error ? reject(new Error(JSON.stringify(m.error))) : resolve(m.result); }
    else if (m.method && api.onEvent) api.onEvent(m.method, m.params); };
  api.send = (method, params = {}) => new Promise((resolve, reject) => { const mid = ++id; pending.set(mid, { resolve, reject }); ws.send(JSON.stringify({ id: mid, method, params })); });
  api.close = () => ws.close();
  return api;
}

await cdpUrl();
const target = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json());
const client = cdp(target.webSocketDebuggerUrl);
await client.ready;
const pageErrors = [];
client.onEvent = (m, p) => {
  if (m === "Runtime.exceptionThrown") pageErrors.push("exc: " + (p.exceptionDetails.exception?.description || p.exceptionDetails.text));
  if (m === "Runtime.consoleAPICalled" && p.type === "error") pageErrors.push("console: " + p.args.map((a) => a.value ?? a.description).join(" "));
};
await client.send("Runtime.enable");
await client.send("Page.enable");
await client.send("Page.navigate", { url: `http://127.0.0.1:${PORT}/zsh.html` });

async function evaluate(expr) {
  const r = await client.send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || "eval error");
  return r.result.value;
}

let up = false;
for (let i = 0; i < 150; i++) { if (await evaluate("!!(window.__zsh && window.__zsh.type)")) { up = true; break; } await new Promise((r) => setTimeout(r, 100)); }
if (!up) {
  console.error("FAIL: zsh.html never came up");
  if (pageErrors.length) console.error("cdp errors:\n" + pageErrors.join("\n"));
  cleanup(); process.exit(1);
}
// Let the shell reach its first prompt.
await new Promise((r) => setTimeout(r, 800));

let failed = 0;
const check = (name, ok, detail) => { console.error(`${ok ? "ok  " : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

// 1) Ctrl+V keydown must NOT be consumed: keyToBytes returns null, so the
//    handler skips preventDefault() and the browser's native paste proceeds.
const ctrlVPrevented = await evaluate(`(() => {
  const e = new KeyboardEvent('keydown', { key: 'v', ctrlKey: true, bubbles: true, cancelable: true });
  window.dispatchEvent(e);
  return e.defaultPrevented;
})()`);
check("Ctrl+V keydown is not preventDefault()'d (paste can fire)", ctrlVPrevented === false, `defaultPrevented=${ctrlVPrevented}`);

// 2) Cmd+V (metaKey) likewise reaches the native paste.
const cmdVPrevented = await evaluate(`(() => {
  const e = new KeyboardEvent('keydown', { key: 'v', metaKey: true, bubbles: true, cancelable: true });
  window.dispatchEvent(e);
  return e.defaultPrevented;
})()`);
check("Cmd+V keydown is not preventDefault()'d", cmdVPrevented === false, `defaultPrevented=${cmdVPrevented}`);

// 3) A normal Ctrl-letter (^A) is STILL consumed — the fix didn't broaden the
//    paste special-case into swallowing other control shortcuts.
const ctrlAPrevented = await evaluate(`(() => {
  const e = new KeyboardEvent('keydown', { key: 'a', ctrlKey: true, bubbles: true, cancelable: true });
  window.dispatchEvent(e);
  return e.defaultPrevented;
})()`);
check("Ctrl+A keydown is still consumed (mapping intact)", ctrlAPrevented === true, `defaultPrevented=${ctrlAPrevented}`);

// 4) End-to-end: a real paste event carrying clipboard text runs the command.
await evaluate(`(() => {
  const e = new Event('paste', { bubbles: true, cancelable: true });
  Object.defineProperty(e, 'clipboardData', { configurable: true, value: { getData: () => 'echo PASTE_MARKER_OK\\r' } });
  window.dispatchEvent(e);
})()`);
await new Promise((r) => setTimeout(r, 1500));
const text = await evaluate("window.__zsh.raw()");
check("pasted command reached the shell and ran", /PASTE_MARKER_OK/.test(text), JSON.stringify(text.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "").slice(-120)));

// 5) xterm's hidden helper textarea must be INVISIBLE. If the page serves
//    xterm.css with the wrong MIME type the browser refuses the stylesheet, the
//    opacity:0 rule never applies, and the textarea renders as a stray box at
//    the cursor. Assert the computed style actually hides it.
const helperVis = await evaluate(`(() => {
  const ta = document.querySelector('.xterm-helper-textarea');
  if (!ta) return { missing: true };
  const cs = getComputedStyle(ta);
  return { opacity: cs.opacity, w: ta.getBoundingClientRect().width, h: ta.getBoundingClientRect().height };
})()`);
check("xterm helper textarea is hidden (stylesheet applied)",
  !!helperVis && !helperVis.missing && (helperVis.opacity === "0" || (helperVis.w === 0 && helperVis.h === 0)),
  JSON.stringify(helperVis));

if (pageErrors.length) console.error("page errors:\n" + pageErrors.join("\n"));
console.error(failed ? `\n${failed} FAILED` : "\nALL PASSED");
client.close(); cleanup(); process.exit(failed ? 1 : 0);
