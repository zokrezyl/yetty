// Headless-browser test for the tmux freeze behaviour (issue #21).
// Boots zsh.html in real Chrome, runs `tmux`, and measures how long the
// page is frozen (the synchronous run blocks the main thread) and that the
// shell survives + stays responsive afterwards. tmux can't run a session
// yet, but it must NOT freeze the tab or kill zsh.
//
// Run: node tmux_check.mjs [http://127.0.0.1:8126]
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const base = process.argv[2] || "http://127.0.0.1:8126";
const prof = mkdtempSync(join(tmpdir(), "yos-tmux-"));
const chrome = spawn(process.env.YOS_CHROME || "google-chrome-stable",
  ["--headless=new", "--no-sandbox", "--disable-gpu", "--remote-debugging-port=9360", `--user-data-dir=${prof}`, "about:blank"],
  { stdio: ["ignore", "ignore", "pipe"] });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (n, ok, d) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${n}${ok ? "" : "  " + (d || "")}`); if (!ok) failed++; };

async function main() {
  for (let i = 0; i < 100; i++) { try { if ((await fetch("http://127.0.0.1:9360/json/version")).ok) break; } catch {} await sleep(100); }
  const t = await (await fetch(`http://127.0.0.1:9360/json/new?${encodeURIComponent(base + "/zsh.html")}`, { method: "PUT" })).json();
  const ws = new WebSocket(t.webSocketDebuggerUrl);
  let id = 0; const pend = new Map();
  ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); } };
  await new Promise((r) => (ws.onopen = r));
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send("Runtime.enable");
  const ev = async (e) => (await send("Runtime.evaluate", { expression: e, awaitPromise: true, returnByValue: true })).result?.result?.value;

  for (let i = 0; i < 200 && !(await ev("!!window.__zsh")); i++) await sleep(250);
  check("page booted", await ev("!!(window.__zsh && window.__zsh.type)"), await ev("window.__err||''"));
  await sleep(1500);

  // Run `tmux`. __zsh.type runs the scheduler synchronously, so the time
  // this evaluate takes to return IS the freeze duration the user feels.
  const t0 = Date.now();
  await ev('window.__zsh.type("tmux\\r")');
  const freezeMs = Date.now() - t0;
  console.log(`  (tmux froze the page for ~${freezeMs} ms)`);
  check("tmux freeze is bounded (< 3s, not a hung tab)", freezeMs < 3000, `${freezeMs}ms`);
  check("shell survived tmux (still running)", await ev("window.__zsh.running()"));

  // The shell must be responsive immediately after.
  await ev('window.__zsh.type("echo alive-after-tmux\\r")');
  await sleep(600);
  const scr = (await ev("window.__zsh.screen()")) || "";
  check("shell is responsive after tmux (echo works)", /alive-after-tmux/.test(scr), JSON.stringify(scr.slice(-70)));
}

main().then(() => { console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED"); chrome.kill(); process.exit(failed ? 1 : 0); })
  .catch((e) => { console.error("driver error:", e.message); chrome.kill(); process.exit(2); });
