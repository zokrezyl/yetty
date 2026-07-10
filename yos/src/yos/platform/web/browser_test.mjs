// Actually run terminal.html in a real headless browser: start a static
// server, launch Chrome, load the page (so the BROWSER's wasm engine
// instantiates guest+bridge and xterm renders), type via the test hook,
// read back what the guest emitted, assert. Drives Chrome over the
// DevTools Protocol using node's built-in WebSocket — no npm deps.
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { extname } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8131;
const here = new URL(".", import.meta.url);

// --- static server (same headers as serve.sh) ---
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript" };
const server = createServer(async (req, res) => {
  const path = new URL("." + (req.url === "/" ? "/index.html" : req.url.split("?")[0]), here);
  try {
    const body = await readFile(path);
    res.setHeader("Content-Type", types[extname(path.pathname)] || "application/octet-stream");
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    res.end(body);
  } catch {
    res.statusCode = 404;
    res.end("not found");
  }
});
await new Promise((r) => server.listen(PORT, "127.0.0.1", r));

// --- launch headless Chrome ---
const profile = mkdtempSync(join(tmpdir(), "yos-chrome-"));
const chrome = spawn(CHROME, [
  "--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
  "--remote-debugging-port=9333", `--user-data-dir=${profile}`,
  "about:blank",
], { stdio: ["ignore", "ignore", "pipe"] });

const cleanup = () => { try { chrome.kill("SIGTERM"); } catch {} server.close(); };
process.on("exit", cleanup);

// CDP endpoint comes up async; poll /json/version.
async function waitForCdp() {
  for (let i = 0; i < 100; i++) {
    try {
      const res = await fetch("http://127.0.0.1:9333/json/version");
      if (res.ok) return (await res.json()).webSocketDebuggerUrl;
    } catch {}
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error("Chrome CDP did not come up");
}

// Minimal CDP client over the built-in WebSocket.
function cdp(url) {
  const ws = new WebSocket(url);
  let id = 0;
  const pending = new Map();
  const ready = new Promise((res) => (ws.onopen = res));
  const api = { ready: null, send: null, close: () => ws.close(), onEvent: null };
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      msg.error ? reject(new Error(JSON.stringify(msg.error))) : resolve(msg.result);
    } else if (msg.method && api.onEvent) {
      api.onEvent(msg.method, msg.params);
    }
  };
  api.send = (method, params = {}) =>
    new Promise((resolve, reject) => {
      const mid = ++id;
      pending.set(mid, { resolve, reject });
      ws.send(JSON.stringify({ id: mid, method, params }));
    });
  api.ready = ready;
  return api;
}

const browserWs = await waitForCdp();

// Open a blank tab and attach to it.
const target = await fetch("http://127.0.0.1:9333/json/new?about:blank", { method: "PUT" })
  .then((r) => r.json());

const client = cdp(target.webSocketDebuggerUrl);
await client.ready;

// Surface page errors so a failed instantiation is diagnosable.
const pageErrors = [];
client.onEvent = (method, params) => {
  if (method === "Runtime.exceptionThrown")
    pageErrors.push("exception: " + (params.exceptionDetails.exception?.description || params.exceptionDetails.text));
  if (method === "Runtime.consoleAPICalled" && params.type === "error")
    pageErrors.push("console.error: " + params.args.map((a) => a.value ?? a.description).join(" "));
  if (method === "Log.entryAdded" && params.entry.level === "error")
    pageErrors.push("log: " + params.entry.text);
};
await client.send("Runtime.enable");
await client.send("Log.enable");
await client.send("Page.enable");
await client.send("Page.navigate", { url: `http://127.0.0.1:${PORT}/terminal.html` });

async function evaluate(expression) {
  const r = await client.send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || "eval error");
  return r.result.value;
}

// Wait until the page wired up window.__yos (wasm instantiated).
let up = false;
for (let i = 0; i < 100; i++) {
  if (await evaluate("typeof window.__yos === 'object' && !!window.__yos")) { up = true; break; }
  await new Promise((r) => setTimeout(r, 100));
}
if (!up) {
  console.error("FAIL: page never instantiated wasm");
  if (pageErrors.length) console.error("page errors:\n  " + pageErrors.join("\n  "));
  else console.error("(no page errors captured)");
  cleanup();
  process.exit(1);
}

// Type into the real browser-hosted guest.
await evaluate("window.__yos.feed('help\\recho hello from chrome\\rbogus\\r')");
await new Promise((r) => setTimeout(r, 150));
const text = await evaluate("window.__yos.stripped()");
// Also confirm xterm actually rendered into the DOM.
const domText = await evaluate(
  "document.querySelector('.xterm-screen')?.textContent?.length || document.querySelector('.xterm-rows')?.textContent?.length || 0",
);

// Required: the wasm path ran in a real browser. xterm DOM rendering is
// informational — the CDN may be unreachable in a headless sandbox, and
// the guest+bridge do not depend on it.
const checks = [
  ["wasm instantiated in browser", up],
  ["banner painted", text.includes("in the browser")],
  ["help builtin", text.includes("builtins: help")],
  ["echo executed", text.includes("hello from chrome")],
  ["command not found", text.includes("command not found: bogus")],
];

let ok = true;
for (const [name, pass] of checks) {
  console.error(`${pass ? "ok  " : "FAIL"}  ${name}`);
  ok = ok && pass;
}
console.error(`info  xterm rendered to DOM: ${domText > 0 ? "yes (" + domText + " chars)" : "no (CDN blocked/offline — guest unaffected)"}`);
console.error("\n--- terminal contents (ansi-stripped) ---");
console.error(text.trimEnd());

// Save a screenshot of the rendered terminal so the result is visible.
if (process.env.YOS_SHOT) {
  try {
    const shot = await client.send("Page.captureScreenshot", { format: "png" });
    const { writeFileSync } = await import("node:fs");
    writeFileSync(process.env.YOS_SHOT, Buffer.from(shot.data, "base64"));
    console.error("screenshot: " + process.env.YOS_SHOT);
  } catch (e) {
    console.error("screenshot failed: " + e.message);
  }
}

client.close();
cleanup();
process.exit(ok ? 0 : 1);
