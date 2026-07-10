// Entry for tmux.html — runs the real tmux 3.4 wasm32 build as a LONG-LIVED
// INTERACTIVE session on the browser's own wasm engine, with a real zsh
// running in the pane. The same client/server/pty model desktop yos uses:
// tmux forks a server over a unix socket, hands it the tty via SCM_RIGHTS,
// spawns the pane shell on a pty, and renders the screen back to the client —
// all driven by the cooperative scheduler + asyncify suspend/rewind in
// yos_proc.mjs. Keystrokes (including the C-b prefix) flow straight through.
import { runInteractive } from "./yos_proc.mjs";

const term = new Terminal({
  fontFamily: "ui-monospace, monospace", fontSize: 14,
  theme: { background: "#0b1014", foreground: "#e0e5e4", cursor: "#74c5a5" },
  cursorBlink: true, convertEol: false, scrollback: 0,
});
const fit = typeof FitAddon !== "undefined" ? new FitAddon.FitAddon() : null;
if (fit) term.loadAddon(fit);
const host = document.getElementById("term");
term.open(host);
if (fit) try { fit.fit(); } catch {}
const grabFocus = () => term.focus();
host.addEventListener("mousedown", grabFocus);
document.addEventListener("mousedown", grabFocus);
window.addEventListener("focus", grabFocus);
grabFocus();

term.write("\x1b[38;2;107;168;146mloading real tmux.wasm + zsh.wasm …\x1b[0m\r\n");
const [tmux, zsh] = await Promise.all([
  WebAssembly.compile(await (await fetch("./tools/tmux.wasm")).arrayBuffer()),
  WebAssembly.compile(await (await fetch("./zsh.wasm")).arrayBuffer()),
]);

// Tools tmux's pane shell can exec. zsh is wired as BOTH the login shell
// (`sh`, what tmux spawns by default) and `zsh`; the rest are the freebsd
// coreutils wasms so commands inside the tmux session actually run.
const tools = new Map([["sh", zsh], ["zsh", zsh]]);
const TOOL_NAMES = ["pwd", "id", "hostname", "echo", "cat", "ls", "ps", "date", "true", "false"];
await Promise.all(TOOL_NAMES.map(async (name) => {
  try { tools.set(name, await WebAssembly.compile(await (await fetch(`./tools/${name}.wasm`)).arrayBuffer())); } catch {}
}));

let exited = false;
let raw = "";
const ctl = runInteractive(tmux, ["tmux", "-f", "/dev/null", "new-session"], {
  onOutput: (fd, text) => { raw += text; term.write(text); },
  onExit: (code) => { exited = true; term.write(`\r\n\x1b[38;2;85;97;98m[tmux exited ${code}] — reload to restart\x1b[0m\r\n`); },
  tools,
  env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/", "SHELL=/bin/sh"],
  cols: term.cols, rows: term.rows,
});

// xterm auto-REPLIES to tmux's terminal feature queries (Device Attributes,
// OSC 10/11 colour, DCS). Those replies arrive after tmux's query window has
// closed, so tmux would forward them to the pane shell as if typed ("rgb:…:
// command not found"). They are never real user keystrokes, so strip them
// before forwarding; tmux just falls back to its terminfo defaults.
const stripTerminalReplies = (s) =>
  s.replace(/\x1b\][\s\S]*?(\x07|\x1b\\)/g, "")  // OSC replies (colour, clipboard)
   .replace(/\x1bP[\s\S]*?\x1b\\/g, "")           // DCS replies
   .replace(/\x1b\[[?>][0-9;]*[A-Za-z]/g, "");    // private-mode CSI (DA1/DA2/DECRPM)

// INPUT: xterm already encodes keys into the right terminal byte sequences
// (arrows, function keys, C-b, …) — forward them verbatim to the tmux client.
term.onData((data) => { if (exited) return; const clean = stripTerminalReplies(data); if (clean) ctl.write(clean); });
term.onResize(({ cols, rows }) => ctl.resize(cols, rows));
if (fit) { try { fit.fit(); } catch {} ctl.resize(term.cols, term.rows); }
window.addEventListener("resize", () => { if (fit) { try { fit.fit(); } catch {} ctl.resize(term.cols, term.rows); } });
grabFocus();

// Headless test hook — drives the same interactive path as real typing.
window.__term = term;
window.__tmux = {
  type: (s) => ctl.write(s),
  running: () => ctl.running(),
  raw: () => raw,
  procs: () => ctl.mgr.procs.map((p) => `${p.pid}:${p.comm}:${p.exited ? "x" + p.exitCode : p.sched}`),
  screen: () => { const b = term.buffer.active; let s = ""; for (let y = 0; y < b.length; y++) { const ln = b.getLine(y); if (ln) s += ln.translateToString(true) + "\n"; } return s; },
};
