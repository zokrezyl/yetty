// Headless proof that the clickable zsh.html is a REAL interactive shell:
// boots the universal zsh.wasm, types commands as real keystrokes, and
// asserts that shell state (cwd, variables) persists across commands.
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const base = process.argv[2] || "http://127.0.0.1:8100";
const prof = mkdtempSync(join(tmpdir(), "yos-iterm-"));
const chrome = spawn(process.env.YOS_CHROME || "google-chrome-stable",
  ["--headless=new", "--no-sandbox", "--disable-gpu", "--remote-debugging-port=9336", `--user-data-dir=${prof}`, "about:blank"],
  { stdio: ["ignore", "ignore", "pipe"] });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

async function main() {
  for (let i = 0; i < 100; i++) { try { if ((await fetch("http://127.0.0.1:9336/json/version")).ok) break; } catch {} await sleep(100); }
  const t = await (await fetch(`http://127.0.0.1:9336/json/new?${encodeURIComponent(base + "/zsh.html")}`, { method: "PUT" })).json();
  const ws = new WebSocket(t.webSocketDebuggerUrl);
  let id = 0; const pend = new Map();
  ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); } };
  await new Promise((r) => (ws.onopen = r));
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send("Runtime.enable"); await send("Page.enable");
  const ev = async (expr) => (await send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true })).result?.result?.value;

  // wait for the universal zsh to boot + reach its prompt
  for (let i = 0; i < 200 && !(await ev("!!window.__zsh")); i++) await sleep(250);
  check("page booted, interactive hook present", await ev("!!(window.__zsh && window.__zsh.type)"), await ev("window.__err||''"));
  await sleep(1500); // settle prompt
  check("zsh is still running (long-lived, not exited)", await ev("window.__zsh.running()"));

  const type = async (s) => { await ev(`window.__zsh.type(${JSON.stringify(s)})`); await sleep(700); };
  const screen = async () => (await ev("window.__zsh.screen()")) || "";

  await type("echo hello-interactive\r");
  check("runs a command (echo)", /hello-interactive/.test(await screen()), JSON.stringify((await screen()).slice(-80)));

  await type("cd /tmp\r");
  const before = await screen();
  await type("pwd\r");
  const scrCwd = await screen();
  // pwd must print /tmp that was NOT on screen before the pwd command.
  check("cwd PERSISTS across commands (cd /tmp ; pwd → /tmp)", /\/tmp/.test(scrCwd) && scrCwd.split("/tmp").length > before.split("/tmp").length, JSON.stringify(scrCwd.slice(-80)));

  await type("MYVAR=hello-state\r");
  await type("echo got=$MYVAR\r");
  let scr = await screen();
  check("shell variables PERSIST (real session)", /got=hello-state/.test(scr), JSON.stringify(scr.slice(-80)));

  await type("for i in a b c; do echo line-$i; done\r");
  scr = await screen();
  check("multi-line constructs (for loop)", /line-a[\s\S]*line-b[\s\S]*line-c/.test(scr), JSON.stringify(scr.slice(-100)));

  const beforeArith = await screen();
  await type("echo $((6 * 7))\r");
  scr = await screen();
  check("arithmetic expansion ($((6*7)) → 42)", /\b42\b/.test(scr) && scr.split("42").length > beforeArith.split("42").length, JSON.stringify(scr.slice(-60)));

  // The freeze repro: an external (forked) command must run and return.
  const beforeLs = await screen();
  await type("ls /\r");
  await sleep(800);
  scr = await screen();
  check("external command runs without freezing (ls /)", /README/.test(scr) && scr.length !== beforeLs.length, JSON.stringify(scr.slice(-80)));
  check("shell still alive after external command", await ev("window.__zsh.running()"));
}

main().then(() => { console.log(failed ? `\n${failed} FAILED` : "\nALL INTERACTIVE CHECKS PASSED"); chrome.kill(); process.exit(failed ? 1 : 0); })
  .catch((e) => { console.error("driver error:", e.message); chrome.kill(); process.exit(2); });
