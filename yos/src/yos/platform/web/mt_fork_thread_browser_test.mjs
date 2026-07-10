// Per-process thread-memory safety in a REAL browser (issue #23). Loads
// mt_fork_thread.html (served with COOP/COEP so SharedArrayBuffer is enabled),
// which runs fork_thread_demo.wasm through mt_coordinator_browser (the
// real-thread engine in a Worker, spawning nested pool Workers).
//
// This guards the INVARIANT that survives the current browser limitation: a
// fork child's thread must NEVER silently read the ROOT memory (the original
// bug, which showed up as t100). In the browser a fork child cannot boot its
// pool synchronously (a Web Worker's module load needs an event-loop turn the
// coordinator isn't giving while blocked in Atomics.wait), so today the child
// fails LOUDLY instead of running its thread on the wrong memory — the parent
// still runs correctly (p42). When the child DOES run a thread (node already;
// the browser once pthread_create gains an asyncify-suspend), it must observe
// the child's value (t7), never the root's (t100). Both states pass here; a
// silent t100 fails. See mt_fork_thread_test.mjs for the node case that
// exercises the full t7/c7/p42 path.
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8142;
const DEBUG_PORT = 9352;
const here = new URL(".", import.meta.url);
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript" };

const server = createServer(async (req, res) => {
  const path = new URL("." + (req.url === "/" ? "/mt_fork_thread.html" : req.url.split("?")[0]), here);
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
  `--remote-debugging-port=${DEBUG_PORT}`, `--user-data-dir=${profile}`, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill("SIGTERM"); } catch {} server.close(); };
process.on("exit", cleanup);

async function cdpUrl() {
  for (let i = 0; i < 100; i++) { try { const r = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/version`); if (r.ok) return (await r.json()).webSocketDebuggerUrl; } catch {} await new Promise((r) => setTimeout(r, 100)); }
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
const target = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json());
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
await client.send("Page.navigate", { url: `http://127.0.0.1:${PORT}/mt_fork_thread.html` });

const evaluate = async (expr) => { const r = await client.send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || "eval"); return r.result.value; };

let res = null;
for (let i = 0; i < 300; i++) { res = await evaluate("window.__mtForkThread || null"); if (res) break; await new Promise((r) => setTimeout(r, 100)); }

if (!res) { console.error("FAIL: no result (timeout)"); if (errs.length) console.error(errs.join("\n")); cleanup(); process.exit(1); }
if (res.error) { console.error("PAGE/WORKER ERROR: " + res.error); cleanup(); process.exit(1); }

const lines = String(res.out || "").split("\n").map((l) => l.trim()).filter(Boolean);
console.error("output: " + JSON.stringify(lines) + "  exit=" + res.exitCode);
const has = (tag) => lines.includes(tag);
const checks = [
  ["parent kept its own memory (p42)", has("p42")],
  ["no silent root-memory leak in a child thread (no t100)", !has("t100")],
  ["if a child thread ran, it saw child memory (t7 or none)", has("t7") || !has("c7")],
  ["run completed (exit 0)", res.exitCode === 0],
];
let ok = true;
for (const [name, pass] of checks) { console.error(`${pass ? "ok  " : "FAIL"}  ${name}`); ok = ok && pass; }
console.error(has("t7")
  ? "browser fork-child threads fully working (t7)"
  : "browser fork-child threads: child fails loudly (documented gap), parent correct");
console.error(ok ? "PASS fork-child thread memory safety (browser)" : "FAIL");
client.close(); cleanup(); process.exit(ok ? 0 : 1);
