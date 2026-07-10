// Headless-Chrome test (issue #21): assess how tmux ACTUALLY renders in the
// xterm.js terminal — the real browser path, not just the byte stream.
//
// tmux_render_test.mjs renders the byte stream through libvterm and (at a
// matched 80x24) shows the layout is correct. THIS test boots zsh.html in real
// Chrome at a NON-24 height, launches tmux, and reads back the xterm.js screen
// buffer to check WHERE the status bar actually lands.
//
// REGRESSION GUARD for the variadic-ioctl winsize fix. The guest declares
// ioctl variadic, so its third argument arrives via a va-list buffer; the engine
// used to write TIOCGWINSZ's winsize INTO that buffer instead of dereferencing
// it, so tmux read a 0x0 size and rendered a fixed 80x24 window regardless of
// the terminal — status bar stranded mid-screen with dead space below ("renders
// like shit"). The fix (yos_proc.mjs ioctl: deref the va-buffer) makes tmux size
// to the real terminal; these checks assert the status bar is on the LAST row.
//
// KNOWN REMAINING GAP (reported, not asserted): live RESIZE after the session is
// up is not tracked — the cooperative engine runs tmux's daemonised server to
// completion during new-session, so no live server proc remains to receive
// SIGWINCH. Tracked separately from the initial-size fix.
//
// Run: node tmux_xterm_render_test.mjs   (YOS_SHOT=path.png to also screenshot)
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import { readFile } from "node:fs/promises";
import { extname, join } from "node:path";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";

const CHROME = process.env.YOS_CHROME || "google-chrome-stable";
const PORT = 8156, DEBUG_PORT = 9366;
const here = new URL(".", import.meta.url);
const types = { ".html": "text/html", ".wasm": "application/wasm", ".mjs": "text/javascript", ".css": "text/css", ".js": "text/javascript" };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const server = createServer(async (req, res) => {
  const path = new URL("." + (req.url === "/" ? "/zsh.html" : req.url.split("?")[0]), here);
  try {
    const body = await readFile(path);
    res.setHeader("Content-Type", types[extname(path.pathname)] || "application/octet-stream");
    res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
    res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
    res.end(body);
  } catch { res.statusCode = 404; res.end("not found"); }
});
await new Promise((r) => server.listen(PORT, "127.0.0.1", r));

const profile = mkdtempSync(join(tmpdir(), "yos-tmux-xterm-"));
const chrome = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
  `--remote-debugging-port=${DEBUG_PORT}`, `--user-data-dir=${profile}`, "--window-size=760,520", "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });
const cleanup = () => { try { chrome.kill("SIGTERM"); } catch {} server.close(); };
process.on("exit", cleanup);

async function cdpUrl() { for (let i = 0; i < 100; i++) { try { const r = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/version`); if (r.ok) return; } catch {} await sleep(100); } throw new Error("no CDP"); }
await cdpUrl();
const target = await fetch(`http://127.0.0.1:${DEBUG_PORT}/json/new?about:blank`, { method: "PUT" }).then((r) => r.json());
const ws = new WebSocket(target.webSocketDebuggerUrl); let id = 0; const pend = new Map();
ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m.result); pend.delete(m.id); } };
await new Promise((r) => (ws.onopen = r));
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const evl = async (e) => (await send("Runtime.evaluate", { expression: e, awaitPromise: true, returnByValue: true })).result?.value;
await send("Runtime.enable"); await send("Page.enable");
await send("Page.navigate", { url: `http://127.0.0.1:${PORT}/zsh.html` });

let failed = 0;
const check = (n, ok, d) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${n}${ok ? "" : "  " + (d || "")}`); if (!ok) failed++; };

let up = false;
for (let i = 0; i < 200; i++) { if (await evl("!!(window.__zsh && window.__zsh.type)")) { up = true; break; } await sleep(150); }
check("zsh.html booted", up, await evl("window.__err || ''"));
await sleep(800);

// Read the xterm.js screen: status-bar row index, terminal rows, count of bars.
const readScreen = async () => JSON.parse(await evl(`(() => {
  const b = window.__term.buffer.active; const rows = [];
  for (let y = 0; y < b.length; y++) { const ln = b.getLine(y); rows.push(ln ? ln.translateToString(true) : ""); }
  const status = []; for (let y = 0; y < rows.length; y++) if (/^\\[\\d+\\]\\s+\\d+:/.test(rows[y].trimStart())) status.push(y);
  return JSON.stringify({ termRows: window.__term.rows, bufLen: b.length, statusRows: status });
})()`));

// Launch tmux from the prompt, char-by-char with real pauses (the render
// settles per keystroke, same as a human typing).
const typeSlow = async (s) => { for (const ch of s) { await evl(`window.__zsh.type(${JSON.stringify(ch)})`); await sleep(40); } await sleep(1400); };
await typeSlow("tmux new-session\r");
await sleep(1000);
await typeSlow("echo ONE\r");
await typeSlow("echo TWO\r");
await sleep(600);

if (process.env.YOS_SHOT) { const shot = await send("Page.captureScreenshot", { format: "png" }); const { writeFile } = await import("node:fs/promises"); await writeFile(process.env.YOS_SHOT, Buffer.from(shot.data, "base64")); console.log("  screenshot: " + process.env.YOS_SHOT); }

const s1 = await readScreen();
console.log(`  after tmux: termRows=${s1.termRows}, status-bar row(s)=${JSON.stringify(s1.statusRows)} (want exactly [${s1.termRows - 1}])`);
check("tmux status bar rendered", s1.statusRows.length >= 1, JSON.stringify(s1.statusRows));
check("exactly ONE status bar", s1.statusRows.length === 1, `found ${s1.statusRows.length}`);
check("status bar on the LAST terminal row (tmux uses full height)", s1.statusRows.length === 1 && s1.statusRows[0] === s1.termRows - 1, `at ${s1.statusRows[0]}, last row is ${s1.termRows - 1}`);

// Live-resize tracking is a KNOWN REMAINING GAP (see header) — reported as a
// diagnostic, NOT asserted, so this stays a green guard for the initial-size fix.
await send("Emulation.setDeviceMetricsOverride", { width: 700, height: 380, deviceScaleFactor: 1, mobile: false });
await evl("window.dispatchEvent(new Event('resize'))");
await sleep(1600);
await typeSlow("echo THREE\r");
await sleep(600);
const s2 = await readScreen();
const resizeTracked = s2.statusRows.length === 1 && s2.statusRows[0] === s2.termRows - 1;
console.log(`  [diagnostic] after resize to ${s2.termRows} rows: status-bar row(s)=${JSON.stringify(s2.statusRows)} (last row ${s2.termRows - 1})` + (resizeTracked ? "  — tracked" : "  — NOT tracked (known gap: server lifecycle)"));

ws.close(); cleanup();
console.log(failed ? `\n${failed} checks FAILED` : "\nALL CHECKS PASSED — tmux renders at the correct terminal size");
process.exit(failed ? 1 : 0);
