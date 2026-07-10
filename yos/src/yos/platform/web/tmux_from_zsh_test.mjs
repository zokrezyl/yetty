// Regression test (issue #21): a full tmux session lifecycle driven from zsh
// on the browser/node process engine — start, run a command in the pane,
// create + switch windows, detach, re-attach, run again.
//
// It pins two engine bugs that made tmux-from-zsh unusable in the browser:
//
//  1. Repeat-binding key-table wedge. tmux binds next/previous-window with -r,
//     so C-b p arms a ~500ms repeat-timeout timer (libevent) that resets the
//     client key table to "root". The engine's virtual clock was frozen between
//     keystrokes, so that timer never elapsed — the client stayed stuck in the
//     "prefix" table and the next C-b was swallowed by send-prefix (leaked into
//     the pane as a literal ^B). Detach and every other prefix command silently
//     stopped working after switching windows. Fixed by advancing the virtual
//     clock by real wall-clock time before each interactive turn.
//
//  2. Re-attach hang. A listening unix socket was never reported readable by
//     poll/select/kevent, so once the server was in its event loop it never woke
//     to accept() a new connection. `tmux attach` connected but the server never
//     served it. Fixed by making a listening socket readable when its accept
//     queue is non-empty.
//
// The real pauses between keystrokes below (700ms > tmux's 500ms repeat-time)
// are load-bearing: they reproduce a human pausing between keys, which is what
// lets the idle timers fire.
//
// Run: node tmux_from_zsh_test.mjs
import { runInteractive } from "./yos_proc.mjs";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const load = async (p) => WebAssembly.compile(await readFile(join(here, p)));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const zsh = await load("zsh.wasm");
const tmux = await load("tools/tmux.wasm");
const tools = new Map([["sh", zsh], ["zsh", zsh], ["tmux", tmux]]);
for (const n of ["echo", "ls", "true", "pwd"]) { try { tools.set(n, await load(`tools/${n}.wasm`)); } catch {} }

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

let raw = "";
const ctl = runInteractive(zsh, ["zsh", "-f", "+o", "promptsp"], {
  onOutput: (fd, t) => { raw += t; },
  tools,
  env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/", "SHELL=/bin/sh"],
  cols: 80, rows: 24,
});
const mgr = ctl.mgr;
const procLine = () => mgr.procs.map((p) => `${p.pid}:${p.comm}:${p.exited ? "x" + p.exitCode : p.sched}`).join(" ");
const alive = (pid) => !!mgr.procs.find((p) => p.pid === pid && !p.exited);
// ctl.write drives the scheduler synchronously; the pause lets real time pass
// so the NEXT turn's clock advance lets idle guest timers (tmux repeat/status)
// fire — exactly what happens while a human pauses between keystrokes.
const type = async (keys, pauseMs = 700) => { ctl.write(keys); await sleep(pauseMs); };
// Type one char at a time with a REAL delay between keys, the way a human (or a
// browser keydown handler, one ctl.write per event) actually types. This is the
// load-bearing difference from batch type(): a frozen-then-jumped clock that
// fires tmux's timers mid-keystroke desyncs its incremental pane render, so the
// typed line never paints even though the bytes were delivered. Batch writes
// (all chars one synchronous turn) never reproduced that; per-key does.
const typePerKey = async (keys, perKeyMs = 45, settleMs = 900) => { for (const ch of keys) { ctl.write(ch); await sleep(perKeyMs); } await sleep(settleMs); };

await sleep(300);

// --- start a session and run a command in the pane ---
await type("tmux new-session\r", 1000);
check("tmux session started from zsh", /\[0\]\s+0:/.test(raw), JSON.stringify(raw.slice(-80)));
check("client + server + pane all alive", mgr.procs.filter((p) => !p.exited).length >= 4, procLine());

// --- per-key typing into the live pane MUST paint its output ---
// Regression for the per-key render race: type a command char-by-char and
// assert its output is drawn back to the terminal (not just that the command
// ran). Batch tests miss this entirely.
raw = "";
await typePerKey("echo PERKEY_OK\r");
check("per-key command output painted in the pane", /PERKEY_OK/.test(raw), JSON.stringify(raw.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "").replace(/\s+/g, " ").slice(-80)));

// Seed the pane with output. tmux batches the live pane redraw, so rather than
// race that render we assert it ran by restoring it on re-attach below (a
// stronger check — it proves the output persisted in the server's pane buffer).
// Live pane command execution is also covered by tmux_browser_test.mjs.
await type("echo MARK_PANE\r", 900);

// --- new window, then switch back with the -r (repeat) binding ---
await type("\x02c");                 // C-b c: new window (non-repeat)
check("C-b c created a second window", /0:sh.\s+1:sh/.test(raw), JSON.stringify(raw.slice(-90)));

await type("\x02p");                 // C-b p: previous-window (-r REPEAT) — the wedge trigger
check("C-b p switched window", /\[0\]\s+0:sh\*\s+1:sh-/.test(raw), JSON.stringify(raw.slice(-90)));

// --- detach (broken before fix #1) ---
raw = "";
await type("\x02d", 900);            // C-b d
const client = mgr.procs.find((p) => p.pid === 2);
check("detach exited the client cleanly", !!(client && client.exited && client.exitCode === 0), procLine());
check("detach printed the detached message", /detached/.test(raw), JSON.stringify(raw.slice(-120)));
check("outer zsh regained the prompt", !!(mgr.procs[0].blocked && mgr.procs[0].blocked.kind === "read"), `zsh blocked=${mgr.procs[0].blocked && mgr.procs[0].blocked.kind}`);
check("the tmux server survived the detach", alive(4), procLine());

// A command typed at the outer zsh must run in zsh, not leak into the pane as
// a literal ^B-prefixed string.
raw = "";
await type("echo BACK_AT_ZSH\r");
check("outer zsh runs commands after detach (no ^B leak)", /BACK_AT_ZSH/.test(raw) && !/\^B|command not found/.test(raw), JSON.stringify(raw.slice(-120)));

// --- re-attach to the surviving server (broken before fix #2) ---
raw = "";
await type("tmux attach\r", 1500);
check("re-attach restored the prior pane content (MARK_PANE)", /MARK_PANE/.test(raw), JSON.stringify(raw.slice(-160)));
check("re-attach restored the status bar", /\[0\]\s+0:/.test(raw), JSON.stringify(raw.slice(-90)));

raw = "";
await type("echo REATTACHED_OK\r");
check("re-attached session runs commands in the pane", /REATTACHED_OK/.test(raw), JSON.stringify(raw.slice(-120)));

console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED");
process.exit(failed ? 1 : 0);
