// External module entry for zsh.html. Runs the UNIVERSAL zsh.wasm (the
// same binary desktop yos runs) as a LONG-LIVED INTERACTIVE process on the
// browser's own wasm engine. stdin is a real terminal: zsh's line editor
// blocks on read()/poll(), and the page resumes it on each keystroke via
// the asyncify suspend/rewind the binary already carries for fork. Shell
// state — cwd, variables, history, pipelines — persists across commands,
// because it is one continuous zsh process, not one-zsh-per-line.
import { runInteractive, loadLiblua } from "./yos_proc.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { parsePack } from "./fs_mount.mjs";

const term = new Terminal({
  fontFamily: "ui-monospace, monospace", fontSize: 14,
  theme: { background: "#0b1014", foreground: "#e0e5e4", cursor: "#74c5a5" },
  cursorBlink: true, convertEol: false, scrollback: 5000,
});
const fit = typeof FitAddon !== "undefined" ? new FitAddon.FitAddon() : null;
if (fit) term.loadAddon(fit);
const host = document.getElementById("term");
term.open(host);
if (fit) try { fit.fit(); } catch {}
// Make the whole page grab keyboard input: clicking anywhere focuses the
// terminal, and we re-focus on load so typing works without a click.
const grabFocus = () => term.focus();
host.addEventListener("mousedown", grabFocus);
document.addEventListener("mousedown", grabFocus);
window.addEventListener("focus", grabFocus);
grabFocus();

// Live input diagnostic (top-right). Proves which build is loaded and whether
// keystrokes are reaching the page at all. If "keys" stays 0 while you type,
// the keyboard event never fired (focus/cache); if it climbs but nothing
// happens in the shell, it's the engine.
const diag = document.createElement("div");
diag.style.cssText = "position:fixed;top:4px;right:6px;z-index:9999;font:11px ui-monospace,monospace;color:#74c5a5;background:#0008;padding:2px 6px;border-radius:4px;pointer-events:none";
document.body.appendChild(diag);
let keyCount = 0, outBytes = 0, lastKey = "";
const renderDiag = () => { diag.textContent = `BUILD det-clock-3 · keys:${keyCount}(${lastKey}) · out:${outBytes}B`; };
renderDiag();
const bumpDiag = (k) => { keyCount++; lastKey = JSON.stringify(k); renderDiag(); };

term.write("\x1b[38;2;107;168;146mloading universal zsh.wasm …\x1b[0m\r\n");
const mod = await compileGuest(await (await fetch("./zsh.wasm")).arrayBuffer());

// Load the tool wasms so zsh can exec external commands. The set comes
// DYNAMICALLY from the server's /tools/list.json (the nix bin directory), so
// the browser has exactly the desktop command set and any newly-built tool
// appears with no code change. execve() is synchronous, so every tool is
// compiled up front (that includes nvim at ~14 MB — expect a few seconds on
// first load). The tiny fallback list only applies if the listing endpoint is
// unavailable (e.g. served by a plain static server).
const FALLBACK_TOOLS = ["pwd", "id", "hostname", "echo", "cat", "ls", "ps", "date", "true", "false", "tmux"];
let TOOL_NAMES;
try {
  TOOL_NAMES = await (await fetch("./tools/list.json")).json();
} catch {
  TOOL_NAMES = FALLBACK_TOOLS;
}
const tools = new Map();
let loaded = 0;
await Promise.all(TOOL_NAMES.map(async (name) => {
  try {
    tools.set(name, await compileGuest(await (await fetch(`./tools/${name}.wasm`)).arrayBuffer()));
  } catch { /* a tool that fails to fetch/compile is simply absent from /bin */ }
  loaded++;
  term.write(`\r\x1b[38;2;85;97;98mloading tools ${loaded}/${TOOL_NAMES.length} …\x1b[0m`);
}));
term.write("\r\x1b[2K");
// zsh.wasm IS the shell: wire it as `sh`/`zsh` too so a tmux you launch from
// this prompt can spawn a real shell in its pane (tmux's default-shell is
// /bin/sh → basename `sh`). Without this, `tmux` opens but its pane is dead.
tools.set("sh", mod);
tools.set("zsh", mod);

// The Lua C API for nvim (liblua.wasm, served at ./lua/): without it nvim's
// 85 lua_*/luaL_* imports have no provider and instantiation fails loud.
await loadLiblua().catch(() => {});

// Mount the guest /usr/share tree (nvim's $VIMRUNTIME, zsh functions) from
// the server's single-blob pack. Entry data are zero-copy views into the
// fetched buffer; without this nvim boots but dies loading vim/_defaults.lua
// (its Lua runtime files live under /usr/share/nvim/runtime).
let mounts = [];
try {
  const pack = await (await fetch("./fs/pack.bin")).arrayBuffer();
  mounts = [{ at: "/usr/share", entries: parsePack(pack) }];
} catch { /* plain static server: no /usr/share; nvim will lack its runtime */ }

let exited = false;
let raw = "";
const ctl = runInteractive(mod, ["zsh", "-f", "+o", "promptsp"], {
  onOutput: (fd, text) => { raw += text; outBytes += text.length; renderDiag(); term.write(text); },
  onExit: (code) => { exited = true; term.write(`\r\n\x1b[38;2;85;97;98m[zsh exited ${code}] — reload to restart\x1b[0m\r\n`); },
  onUnimpl: () => {},
  tools,
  mounts,
  env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/", "SHELL=/bin/sh", "VIMRUNTIME=/usr/share/nvim/runtime"],
  cols: term.cols, rows: term.rows,
});

// INPUT: capture the keyboard at the window level (capture phase) and send
// raw bytes to zsh. This does NOT depend on the xterm element having focus
// — real browsers ignore programmatic focus on load, and clicking can miss
// the rows — so capturing on window guarantees keystrokes reach the shell.
// zsh's line editor echoes; we never echo here (that would double chars).
function keyToBytes(e) {
  // Paste shortcut (Ctrl+V / Cmd+V): return null WITHOUT mapping it to a control
  // byte, so the keydown handler skips preventDefault() and the browser emits
  // its native `paste` event — which the paste handler below forwards to the
  // shell. Must come before the generic Ctrl-letter mapping, which would
  // otherwise swallow Ctrl+V as ^V (0x16) and preventDefault away the paste.
  if ((e.ctrlKey || e.metaKey) && (e.key === "v" || e.key === "V")) return null;
  if (e.altKey || e.metaKey) return null;
  const k = e.key;
  if (e.ctrlKey) {
    if (k.length === 1) { const u = k.toUpperCase().charCodeAt(0); if (u >= 64 && u <= 95) return String.fromCharCode(u & 0x1f); } // ^A.. ^_
    return null;
  }
  switch (k) {
    case "Enter": return "\r";
    case "Backspace": return "\x7f";
    case "Tab": return "\t";
    case "Escape": return "\x1b";
    case "ArrowUp": return "\x1b[A";
    case "ArrowDown": return "\x1b[B";
    case "ArrowRight": return "\x1b[C";
    case "ArrowLeft": return "\x1b[D";
    case "Home": return "\x1b[H";
    case "End": return "\x1b[F";
    case "Delete": return "\x1b[3~";
    case "PageUp": return "\x1b[5~";
    case "PageDown": return "\x1b[6~";
    default: return k.length === 1 ? k : null; // printable
  }
}
window.addEventListener("keydown", (e) => {
  if (exited) return;
  bumpDiag(e.key);
  const bytes = keyToBytes(e);
  if (bytes == null) return; // let the browser keep shortcuts we don't map
  e.preventDefault();
  e.stopPropagation();
  ctl.write(bytes);
}, true); // capture phase: intercept before anything else
// Paste support (Ctrl/Cmd-V): forward pasted text to the shell.
window.addEventListener("paste", (e) => { if (exited) return; const text = (e.clipboardData || window.clipboardData).getData("text"); if (text) { e.preventDefault(); ctl.write(text); } });

term.onResize(({ cols, rows }) => ctl.resize(cols, rows));
if (fit) { try { fit.fit(); } catch {} ctl.resize(term.cols, term.rows); }
window.addEventListener("resize", () => { if (fit) { try { fit.fit(); } catch {} ctl.resize(term.cols, term.rows); } });
grabFocus();

// Headless test hook: feeds keystrokes through the exact interactive path.
window.__term = term;
window.__zsh = {
  type: (s) => ctl.write(s),
  running: () => ctl.running(),
  raw: () => raw,
  state: () => ({ blocked: ctl.proc.blocked, inbuf: ctl.mgr.tty.inbuf.length, exited: ctl.proc.exited, pendingRewind: ctl.proc.pendingRewind }),
  size: () => ({ termCols: term.cols, termRows: term.rows, ttyCols: ctl.mgr.tty.cols, ttyRows: ctl.mgr.tty.rows }),
  procs: () => ctl.mgr.procs.map((p) => `${p.pid}:${p.comm}:${p.exited ? "x" + p.exitCode : p.sched}${p.error ? "(" + p.error.slice(0, 30) + ")" : ""}`),
  screen: () => { const b = term.buffer.active; let s = ""; for (let y = 0; y < b.length; y++) { const ln = b.getLine(y); if (ln) s += ln.translateToString(true) + "\n"; } return s; },
};
