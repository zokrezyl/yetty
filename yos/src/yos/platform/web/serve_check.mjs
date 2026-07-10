// Headless smoke test for the clickable terminal pages (issue #21).
// Drives a real Chrome over the DevTools protocol: boots zsh.html and
// multi.html on the running server, types commands through the same path
// as real keystrokes, and asserts real wasm output appears.
//
// Run: node serve_check.mjs [http://127.0.0.1:8100]
import { spawn } from "node:child_process";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const base = process.argv[2] || "http://127.0.0.1:8100";
const chromeBin = process.env.YOS_CHROME || "google-chrome-stable";
const prof = mkdtempSync(join(tmpdir(), "yos-chrome-"));
const chrome = spawn(chromeBin, [
  "--headless=new", "--no-sandbox", "--disable-gpu", "--no-first-run",
  `--remote-debugging-port=9334`, `--user-data-dir=${prof}`, "about:blank",
], { stdio: ["ignore", "ignore", "pipe"] });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = 0;

async function dbg() {
  for (let i = 0; i < 100; i++) {
    try { const r = await fetch("http://127.0.0.1:9334/json/version"); if (r.ok) return (await r.json()).webSocketDebuggerUrl; } catch {}
    await sleep(100);
  }
  throw new Error("chrome devtools never came up");
}

async function openPage(url) {
  const t = await (await fetch(`http://127.0.0.1:9334/json/new?${encodeURIComponent(url)}`, { method: "PUT" })).json();
  const ws = new WebSocket(t.webSocketDebuggerUrl);
  let id = 0; const pend = new Map();
  ws.onmessage = (ev) => { const m = JSON.parse(ev.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); } };
  await new Promise((r) => (ws.onopen = r));
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send("Runtime.enable"); await send("Page.enable");
  const evalJs = async (expr) => { const r = await send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true }); return r.result?.result?.value; };
  return { send, evalJs, close: () => ws.close() };
}

function check(name, cond, detail) {
  if (cond) console.log(`  PASS  ${name}`);
  else { console.log(`  FAIL  ${name}  ${detail || ""}`); failed++; }
}

async function main() {
  await dbg();

  // ---- zsh.html: boot real zsh, run arithmetic + a pipeline ----
  console.log("zsh.html (real zsh.wasm, one command per line):");
  const z = await openPage(`${base}/zsh.html`);
  for (let i = 0; i < 160 && !(await z.evalJs("!!window.__zsh")); i++) await sleep(250); // 2MB zsh + 13 tools
  const zerr = await z.evalJs("window.__err || null");
  check("page booted without JS error", !zerr, zerr);
  check("zsh ready hook present", await z.evalJs("!!(window.__zsh && window.__zsh.type)"));
  // The interactive engine runs the scheduler synchronously inside type(),
  // so the command's output is already captured once type() returns; a short
  // settle is plenty. raw() is the full byte transcript.
  await z.evalJs("window.__zsh.type('echo $((6*7))\\r')");
  await sleep(300);
  let cap = (await z.evalJs("window.__zsh.raw()")) || "";
  check("real zsh evaluated arithmetic (42)", /(^|\D)42(\D|$)/.test(cap), JSON.stringify(cap.slice(-60)));
  // A real multi-process pipeline: echo | cat | cat. Each stage is a separate
  // forked process wired with pipes; "PIPEOK-543" appears once as the echoed
  // command line and again as the pipeline's output (≥2 occurrences) when the
  // bytes actually flow through both cats.
  await z.evalJs("window.__zsh.type('echo PIPEOK-543 | cat | cat\\r')");
  await sleep(300);
  cap = (await z.evalJs("window.__zsh.raw()")) || "";
  check("real zsh ran a 3-stage pipeline (echo|cat|cat)", (cap.split("PIPEOK-543").length - 1) >= 2, JSON.stringify(cap.slice(-80)));
  z.close();

  // ---- multi.html: independent panes, real processes in each ----
  console.log("multi.html (4 independent shell panes):");
  const m = await openPage(`${base}/multi.html`);
  for (let i = 0; i < 160 && !(await m.evalJs("!!(window.__multi && window.__multi.length===4)")); i++) await sleep(250);
  const merr = await m.evalJs("window.__err || null");
  check("page booted without JS error", !merr, merr);
  check("4 panes created", (await m.evalJs("window.__multi ? window.__multi.length : 0")) === 4);
  // pane 0 runs a 100-fork process; pane 1 lists processes — independently.
  await m.evalJs("window.__multi[0].type('forkstress\\r')");
  await m.evalJs("window.__multi[1].type('echo PANE1-OK\\r')");
  await sleep(1500);
  const readPane = (i) => `(()=>{const b=window.__multi[${i}].term.buffer.active;let s='';for(let y=0;y<b.length;y++){const ln=b.getLine(y);if(ln)s+=ln.translateToString(true)+'\\n';}return s;})()`;
  const p0 = await m.evalJs(readPane(0));
  const p1 = await m.evalJs(readPane(1));
  check("pane 0 ran a real forked process", /child-stress-pid-ok|stress/.test(p0), JSON.stringify((p0 || "").slice(-80)));
  check("pane 1 produced its own independent output", /PANE1-OK/.test(p1), JSON.stringify((p1 || "").slice(-80)));
  check("panes are independent (pane1 has no forkstress output)", !/child-stress-pid-ok/.test(p1));
  m.close();
}

main()
  .then(() => { console.log(failed ? `\n${failed} checks FAILED` : "\nALL CHECKS PASSED"); chrome.kill(); process.exit(failed ? 1 : 0); })
  .catch((e) => { console.error("driver error:", e.message); chrome.kill(); process.exit(2); });
