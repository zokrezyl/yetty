// Headless-Chrome test for tmux.html: boots the real tmux 3.4 wasm session
// (server + client + zsh-in-pane, all on the browser's own wasm engine),
// asserts the status bar renders and that typing a command into the session
// runs it in the pane and draws the output back. Drives Chrome over the
// DevTools Protocol — no npm deps.
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8155;
const here = new URL(".", import.meta.url);
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript", ".css": "text/css", ".js": "text/javascript" };
const server = createServer(async (req, res) => {
  const path = new URL("." + (req.url === "/" ? "/tmux.html" : req.url.split("?")[0]), here);
  try {
    const body = await readFile(path);
    res.setHeader("Content-Type", types[extname(path.pathname)] || "application/octet-stream");
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    res.end(body);
  } catch { res.statusCode = 404; res.end("not found"); }
});
await new Promise((r) => server.listen(PORT, "127.0.0.1", r));

const profile = mkdtempSync(join(tmpdir(), "yos-tmux-"));
const chrome = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
  "--remote-debugging-port=9362", `--user-data-dir=${profile}`, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill("SIGTERM"); } catch {} server.close(); };
process.on("exit", cleanup);

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function cdpUrl() { for (let i = 0; i < 100; i++) { try { const r = await fetch("http://127.0.0.1:9362/json/version"); if (r.ok) return; } catch {} await sleep(100); } throw new Error("no CDP"); }
await cdpUrl();
const target = await fetch("http://127.0.0.1:9362/json/new?about:blank", { method: "PUT" }).then((r) => r.json());
const ws = new WebSocket(target.webSocketDebuggerUrl); let id = 0; const pend = new Map();
ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m.result); pend.delete(m.id); } };
await new Promise((r) => (ws.onopen = r));
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const evl = async (e) => (await send("Runtime.evaluate", { expression: e, awaitPromise: true, returnByValue: true })).result?.value;
await send("Runtime.enable"); await send("Page.enable");
await send("Page.navigate", { url: `http://127.0.0.1:${PORT}/tmux.html` });

let failed = 0;
const check = (n, ok, d) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${n}${ok ? "" : "  " + (d || "")}`); if (!ok) failed++; };

// tmux 3.4 + zsh = ~3.7MB of wasm to compile, then the session has to boot.
let up = false;
for (let i = 0; i < 200; i++) { if (await evl("!!(window.__tmux && window.__tmux.type)")) { up = true; break; } await sleep(150); }
check("tmux.html booted", up, await evl("window.__err || ''"));
await sleep(1500); // let the session render its first frame

const screen1 = (await evl("window.__tmux.screen()")) || "";
check("tmux status bar rendered (session/window list)", /\[0\]\s+0:/.test(screen1), JSON.stringify(screen1.split("\n").filter((l) => l.trim()).slice(-2)));
check("tmux session alive (client+server+pane)", await evl("window.__tmux.running()"));
const procs = (await evl("window.__tmux.procs()")) || [];
check("a shell is running in the pane", procs.some((p) => /sh|zsh/.test(p)), JSON.stringify(procs));

// Type a command into the session and confirm it runs in the pane. Poll for
// the output rather than a single fixed wait: under load (e.g. several headless
// Chrome instances back-to-back in the suite runner) the render can land a bit
// later, and a one-shot check after a fixed sleep flakes.
await evl(`window.__tmux.type('echo TMUX_BROWSER_OK\\r')`);
let raw = "";
for (let i = 0; i < 24; i++) { raw = (await evl("window.__tmux.raw()")) || ""; if (raw.includes("TMUX_BROWSER_OK")) break; await sleep(250); }
check("typed command ran in the pane (output drawn back)", raw.includes("TMUX_BROWSER_OK"), JSON.stringify(raw.slice(-80)));

if (process.env.YOS_SHOT) { const shot = await send("Page.captureScreenshot", { format: "png" }); const { writeFile } = await import("node:fs/promises"); await writeFile(process.env.YOS_SHOT, Buffer.from(shot.data, "base64")); console.log("screenshot: " + process.env.YOS_SHOT); }

ws.close(); cleanup();
console.log(failed ? `\n${failed} checks FAILED` : "\nALL CHECKS PASSED");
process.exit(failed ? 1 : 0);
