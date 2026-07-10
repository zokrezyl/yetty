// Run zsh.html in real headless Chrome: type real zsh commands through
// the terminal's own keystroke path, read back the rendered output,
// screenshot. Drives Chrome over the DevTools Protocol (no npm deps).
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8137;
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

const profile = mkdtempSync(join(tmpdir(), "yos-chrome-"));
const chrome = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
  "--remote-debugging-port=9344", `--user-data-dir=${profile}`, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill("SIGTERM"); } catch {} server.close(); };
process.on("exit", cleanup);

async function cdpUrl() {
  for (let i = 0; i < 100; i++) {
    try { const r = await fetch("http://127.0.0.1:9344/json/version"); if (r.ok) return (await r.json()).webSocketDebuggerUrl; } catch {}
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
const target = await fetch("http://127.0.0.1:9344/json/new?about:blank", { method: "PUT" }).then((r) => r.json());
const client = cdp(target.webSocketDebuggerUrl);
await client.ready;
const pageErrors = [];
client.onEvent = (m, p) => {
  if (m === "Runtime.exceptionThrown") pageErrors.push("exc: " + (p.exceptionDetails.exception?.description || p.exceptionDetails.text + " @line " + p.exceptionDetails.lineNumber));
  if (m === "Runtime.consoleAPICalled" && p.type === "error") pageErrors.push("console: " + p.args.map((a) => a.value ?? a.description).join(" "));
  if (m === "Log.entryAdded" && (p.entry.level === "error" || p.entry.level === "warning")) pageErrors.push(`log[${p.entry.level}]: ${p.entry.text}` + (p.entry.url ? ` (${p.entry.url}:${p.entry.lineNumber})` : ""));
};
await client.send("Runtime.enable");
await client.send("Log.enable");
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
  const perr = await evaluate("window.__err || ''").catch(() => "");
  if (perr) console.error("page error: " + perr);
  if (pageErrors.length) console.error("cdp errors:\n" + pageErrors.join("\n"));
  const probe = await evaluate("({Terminal: typeof Terminal, zsh: typeof window.__zsh, stage: window.__stage, err: window.__err})").catch((e) => e.message);
  console.error("probe: " + JSON.stringify(probe));
  const dyn = await evaluate("import('./zsh_host.mjs').then(m=>'ok runZsh='+typeof m.runZsh).catch(e=>'IMPORT ERR: '+(e&&e.stack||e))").catch((e) => "eval err " + e.message);
  console.error("dynamic import zsh_host.mjs: " + dyn);
  const dom = await evaluate("({ready: document.readyState, modScripts: [...document.scripts].filter(s=>s.type==='module').length, hasMarker: document.documentElement.outerHTML.includes('enter-module'), bodyLen: document.body.innerHTML.length})").catch((e) => e.message);
  console.error("dom: " + JSON.stringify(dom));
  for (const r of ["./zsh_host.mjs", "./zsh.wasm"]) {
    const info = await evaluate(`fetch(${JSON.stringify(r)}).then(x=>x.status+" "+x.headers.get("content-type")+" "+x.headers.get("content-length")).catch(e=>"ERR "+e)`).catch((e) => e.message);
    console.error(`  resource ${r}: ${info}`);
  }
  cleanup(); process.exit(1);
}

// Type real commands — builtins, external tools, AND fork demos that
// create real processes via asyncify fork in the browser.
// Smaller fork tree (-d 4,4 = 4+16 = 20 procs) so the whole tree fits the
// browser's single-turn wasm budget. The default 111-proc tree runs in
// node/native; here we prove the same phases pass at browser-fittable size.
const script = "perfstress -d 4,4\r";
await evaluate(`window.__zsh.type(${JSON.stringify(script)})`);
await new Promise((r) => setTimeout(r, 4000));
const text = await evaluate("window.__zsh.raw()");

const checks = [
  ["page up", up],
  ["fork+wait phase", /fork\+wait/.test(text)],
  ["recursive fork tree: 0 hash dups", /fork-tree.*log lines, 0 hash dups/.test(text)],
  ["/proc walk: PIDs visible", /proc list.*\d+\/\d+ PIDs visible/.test(text)],
  ["chaos: 0 zombies-left, 0 fork-fails", /chaos proc.*0 zombies-left, 0 fork-fails/.test(text)],
];
let ok = true;
for (const [name, pass] of checks) { console.error(`${pass ? "ok  " : "FAIL"}  ${name}`); ok = ok && pass; }

if (process.env.YOS_SHOT) {
  const shot = await client.send("Page.captureScreenshot", { format: "png" });
  await writeFile(process.env.YOS_SHOT, Buffer.from(shot.data, "base64"));
  console.error("screenshot: " + process.env.YOS_SHOT);
}
console.error("\n--- terminal (ansi-stripped) ---\n" + text.replace(/\x1b\[[0-9;]*[A-Za-z]/g, "").trimEnd());

client.close(); cleanup(); process.exit(ok ? 0 : 1);
