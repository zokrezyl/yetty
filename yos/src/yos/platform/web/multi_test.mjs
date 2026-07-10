import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile, writeFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
const here = new URL(".", import.meta.url);
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript" };
const server = createServer(async (req, res) => { const p = new URL("." + (req.url === "/" ? "/multi.html" : req.url.split("?")[0]), here); try { const b = await readFile(p); res.setHeader("Content-Type", types[extname(p.pathname)] || "application/octet-stream"); res.setHeader("Cross-Origin-Opener-Policy", "same-origin"); res.setHeader("Cross-Origin-Embedder-Policy", "require-corp"); res.end(b); } catch { res.statusCode = 404; res.end("nf"); } });
await new Promise((r) => server.listen(8149, "127.0.0.1", r));
const profile = mkdtempSync(join(tmpdir(), "c-"));
const chrome = spawn(process.env.YOS_CHROME || "google-chrome-stable", ["--headless=new", "--no-sandbox", "--disable-gpu", "--remote-debugging-port=9359", `--user-data-dir=${profile}`, "--window-size=1100,760", "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill(); } catch {} server.close(); };
process.on("exit", cleanup);
for (let i = 0; i < 100; i++) { try { if ((await fetch("http://127.0.0.1:9359/json/version")).ok) break; } catch {} await new Promise((r) => setTimeout(r, 100)); }
const t = await fetch("http://127.0.0.1:9359/json/new?about:blank", { method: "PUT" }).then((r) => r.json());
const ws = new WebSocket(t.webSocketDebuggerUrl); let id = 0; const pend = new Map();
ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m.result); pend.delete(m.id); } };
await new Promise((r) => (ws.onopen = r));
const send = (m, p = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method: m, params: p })); });
const evl = async (e) => (await send("Runtime.evaluate", { expression: e, awaitPromise: true, returnByValue: true })).result.value;
await send("Runtime.enable"); await send("Page.enable");
await send("Page.navigate", { url: "http://127.0.0.1:8149/multi.html" });
for (let i = 0; i < 150; i++) { if (await evl("!!(window.__multi && window.__multi.length === 4)")) break; await new Promise((r) => setTimeout(r, 100)); }
await evl(`window.__multi[0].type("perfstress -d 3,3 -r 0 -R 0 -T 4\\r")`);
await evl(`window.__multi[1].type("ps\\r")`);
await evl(`window.__multi[2].type("ls /\\r")`);
await evl(`window.__multi[3].type("for i in 1 2 3; do echo pane3 $i; done\\r")`);
await new Promise((r) => setTimeout(r, 6000));
if (process.env.YOS_SHOT) { const shot = await send("Page.captureScreenshot", { format: "png" }); await writeFile(process.env.YOS_SHOT, Buffer.from(shot.data, "base64")); console.error("screenshot: " + process.env.YOS_SHOT); }
const err = await evl("window.__err || ''");
console.error("panes:", await evl("window.__multi ? window.__multi.length : 0"), err ? "ERR " + err : "");
cleanup(); process.exit(0);
