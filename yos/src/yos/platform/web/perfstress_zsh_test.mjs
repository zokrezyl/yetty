// Type `perfstress` into the browser zsh and verify the FULL stress test
// runs (real fork + real threads): mutex/condvar/rwlock must pass.
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8147;
const here = new URL(".", import.meta.url);
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript" };
const server = createServer(async (req, res) => {
  const path = new URL("." + (req.url === "/" ? "/zsh.html" : req.url.split("?")[0]), here);
  try { const b = await readFile(path); res.setHeader("Content-Type", types[extname(path.pathname)] || "application/octet-stream"); res.setHeader("Cross-Origin-Opener-Policy", "same-origin"); res.setHeader("Cross-Origin-Embedder-Policy", "require-corp"); res.end(b); }
  catch { res.statusCode = 404; res.end("nf"); }
});
await new Promise((r) => server.listen(PORT, "127.0.0.1", r));
const profile = mkdtempSync(join(tmpdir(), "c-"));
const chrome = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu", "--remote-debugging-port=9357", `--user-data-dir=${profile}`, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill(); } catch {} server.close(); };
process.on("exit", cleanup);
async function cdpUrl() { for (let i = 0; i < 100; i++) { try { const r = await fetch("http://127.0.0.1:9357/json/version"); if (r.ok) return (await r.json()).webSocketDebuggerUrl; } catch {} await new Promise((r) => setTimeout(r, 100)); } }
await cdpUrl();
const t = await fetch("http://127.0.0.1:9357/json/new?about:blank", { method: "PUT" }).then((r) => r.json());
const ws = new WebSocket(t.webSocketDebuggerUrl); let id = 0; const pend = new Map();
ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m.result); pend.delete(m.id); } };
await new Promise((r) => (ws.onopen = r));
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const evl = async (e) => (await send("Runtime.evaluate", { expression: e, awaitPromise: true, returnByValue: true })).result.value;
await send("Runtime.enable"); await send("Page.enable");
await send("Page.navigate", { url: `http://127.0.0.1:${PORT}/zsh.html` });
for (let i = 0; i < 150; i++) { if (await evl("!!(window.__zsh && window.__zsh.type)")) break; await new Promise((r) => setTimeout(r, 100)); }

await evl(`window.__zsh.type("perfstress\\r")`);
// wait until the run finishes (perf-stress summary line appears) or timeout
let text = "";
for (let i = 0; i < 200; i++) { text = await evl("window.__zsh.raw()"); if (/perf-stress (ok|FAILED)/.test(text)) break; await new Promise((r) => setTimeout(r, 500)); }

if (process.env.YOS_SHOT) { const shot = await send("Page.captureScreenshot", { format: "png" }); await writeFile(process.env.YOS_SHOT, Buffer.from(shot.data, "base64")); console.error("screenshot: " + process.env.YOS_SHOT); }

const checks = [
  ["111-proc tree", /fork-tree.*110 log lines, 0 hash dups/.test(text)],
  ["/proc 8/8", /proc list.*8\/8 PIDs visible/.test(text)],
  ["mutex ok", /mutex churn.*ok=yes/.test(text)],
  ["condvar ok", /condvar.*ok=yes/.test(text)],
  ["rwlock viol=0", /rwlock.*viol=0/.test(text)],
  ["completed", /perf-stress (ok|FAILED)/.test(text)],
];
let ok = true;
for (const [n, p] of checks) { console.error(`${p ? "ok  " : "FAIL"}  ${n}`); ok = ok && p; }
console.error("\n--- key lines ---");
console.error(text.replace(/\x1b\[[0-9;]*[A-Za-z]/g, "").split("\n").filter((l) => /fork-tree|proc list|chaos proc|pthread\+join|mutex churn|condvar|rwlock|perf-stress/.test(l)).join("\n"));
cleanup(); process.exit(ok ? 0 : 1);
