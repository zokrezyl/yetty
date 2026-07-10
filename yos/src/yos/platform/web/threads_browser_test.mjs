// Prove real threads in a real browser: load threads.html (served with
// COOP/COEP so SharedArrayBuffer is enabled), let the coordinator Worker
// spawn 4 thread Workers over shared memory, read back the result.
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8141;
const here = new URL(".", import.meta.url);
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript" };

const server = createServer(async (req, res) => {
  const path = new URL("." + (req.url === "/" ? "/threads.html" : req.url.split("?")[0]), here);
  try {
    const body = await readFile(path);
    res.setHeader("Content-Type", types[extname(path.pathname)] || "application/octet-stream");
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");      // required for
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");   // SharedArrayBuffer
    res.end(body);
  } catch { res.statusCode = 404; res.end("nf"); }
});
await new Promise((r) => server.listen(PORT, "127.0.0.1", r));

const profile = mkdtempSync(join(tmpdir(), "yos-chrome-"));
const chrome = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
  "--remote-debugging-port=9351", `--user-data-dir=${profile}`, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill("SIGTERM"); } catch {} server.close(); };
process.on("exit", cleanup);

async function cdpUrl() {
  for (let i = 0; i < 100; i++) { try { const r = await fetch("http://127.0.0.1:9351/json/version"); if (r.ok) return (await r.json()).webSocketDebuggerUrl; } catch {} await new Promise((r) => setTimeout(r, 100)); }
  throw new Error("no CDP");
}
function cdp(url) {
  const ws = new WebSocket(url); let id = 0; const pending = new Map();
  const api = { ready: new Promise((r) => (ws.onopen = r)), onEvent: null };
  ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pending.has(m.id)) { const { resolve, reject } = pending.get(m.id); pending.delete(m.id); m.error ? reject(new Error(JSON.stringify(m.error))) : resolve(m.result); } else if (m.method && api.onEvent) api.onEvent(m.method, m.params); };
  api.send = (method, params = {}) => new Promise((resolve, reject) => { const mid = ++id; pending.set(mid, { resolve, reject }); ws.send(JSON.stringify({ id: mid, method, params })); });
  api.close = () => ws.close();
  return api;
}

await cdpUrl();
const target = await fetch("http://127.0.0.1:9351/json/new?about:blank", { method: "PUT" }).then((r) => r.json());
const client = cdp(target.webSocketDebuggerUrl);
await client.ready;
const errs = [];
client.onEvent = (m, p) => {
  if (m === "Runtime.exceptionThrown") errs.push(p.exceptionDetails.exception?.description || p.exceptionDetails.text);
  if (m === "Log.entryAdded" && p.entry.level === "error") errs.push("log: " + p.entry.text + " " + (p.entry.url || ""));
};
await client.send("Runtime.enable");
await client.send("Log.enable");
await client.send("Page.enable");
await client.send("Page.navigate", { url: `http://127.0.0.1:${PORT}/threads.html` });

const evaluate = async (expr) => { const r = await client.send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || "eval"); return r.result.value; };

let res = null;
for (let i = 0; i < 200; i++) { res = await evaluate("window.__threads || null"); if (res) break; await new Promise((r) => setTimeout(r, 100)); }

if (process.env.YOS_SHOT) { const shot = await client.send("Page.captureScreenshot", { format: "png" }); await writeFile(process.env.YOS_SHOT, Buffer.from(shot.data, "base64")); console.error("screenshot: " + process.env.YOS_SHOT); }

if (!res) { console.error("FAIL: no result (timeout)"); if (errs.length) console.error(errs.join("\n")); cleanup(); process.exit(1); }
console.error("RES: " + JSON.stringify(res)); if (res.error) { console.error("PAGE/WORKER ERROR: " + res.error); cleanup(); process.exit(1); }

console.error(`racy=${res.racy} (lost ${res.expected - res.racy}), locked=${res.locked}, expected=${res.expected}`);
const checks = [
  ["SharedArrayBuffer threads ran", typeof res.racy === "number"],
  ["real parallelism (racy lost updates)", res.raced],
  ["mutex correctness (locked exact)", res.exact],
];
let ok = true;
for (const [name, pass] of checks) { console.error(`${pass ? "ok  " : "FAIL"}  ${name}`); ok = ok && pass; }
client.close(); cleanup(); process.exit(ok ? 0 : 1);
