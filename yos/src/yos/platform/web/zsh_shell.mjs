// One zsh shell bound to a DOM container — the reusable unit behind the
// split-pane terminal (multi.html). Each pane is an INDEPENDENT long-lived
// zsh process: its own xterm + its own interactive session, so cwd,
// variables, history and running jobs persist across commands within a pane
// and never leak between panes. This is the same persistent-shell model as
// zsh.html (runInteractive), not one-zsh-per-line.
//
// ENGINE CHOICE (issue #23 blocker #2). This interactive path runs on the
// single-process cooperative engine (yos_proc.mjs). That is CORRECT for zsh:
// zsh is single-threaded, so it never touches the cooperative pthread stubs.
// It is NOT a general threaded-guest runner — a real-threads (shared-memory)
// build must go through mt_engine (Web Workers + SharedArrayBuffer), which
// this interactive path does not implement. loadShared() therefore refuses a
// shared-memory module up front rather than silently running its threads on
// cooperative stubs.
import { runInteractive } from "./yos_proc.mjs";
import { wasmWantsRealThreads } from "./yos_run.mjs";

export async function createShell(container, shared, label) {
  const term = new Terminal({
    fontFamily: "ui-monospace, monospace", fontSize: 13,
    theme: { background: "#0b1014", foreground: "#e0e5e4", cursor: "#74c5a5" },
    cursorBlink: true, convertEol: false, scrollback: 4000,
  });
  term.open(container);

  const { mod, tools } = shared;
  let exited = false;
  let raw = "";
  const ctl = runInteractive(mod, ["zsh", "-f", "+o", "promptsp"], {
    onOutput: (fd, text) => { raw += text; term.write(text); },
    onExit: (code) => { exited = true; term.write(`\r\n\x1b[38;2;85;97;98m[zsh exited ${code}]\x1b[0m\r\n`); },
    tools,
    cols: term.cols, rows: term.rows,
  });

  // xterm encodes keystrokes (printable, arrows, ^C, …) into the right
  // terminal bytes; forward them straight to the interactive shell, which
  // echoes and line-edits itself.
  term.onData((data) => { if (!exited) ctl.write(data); });
  term.onResize(({ cols, rows }) => ctl.resize(cols, rows));

  return { term, ctl, type: (s) => ctl.write(s), running: () => ctl.running(), raw: () => raw };
}

// Compile the zsh module + tools once; every pane shares the compiled
// modules (cheap) but gets its own process/memory via runInteractive.
export async function loadShared() {
  const mod = await WebAssembly.compile(await (await fetch("./zsh.wasm")).arrayBuffer());
  if (wasmWantsRealThreads(mod)) {
    throw new Error(
      "zsh_shell: this interactive path is the single-process cooperative " +
        "engine (correct for single-threaded zsh). The loaded module imports " +
        "its memory (a real-threads / shared-memory build); run it through " +
        "mt_engine from a coordinator Worker instead (see mt_engine.mjs / " +
        "mt_coordinator_browser.mjs, issue #23).",
    );
  }
  const names = ["pwd", "id", "hostname", "echo", "cat", "ls", "ps", "date", "true", "false", "forkdemo", "forkstress", "perfstress"];
  const tools = new Map();
  await Promise.all(names.map(async (n) => { try { tools.set(n, await WebAssembly.compile(await (await fetch(`./tools/${n}.wasm`)).arrayBuffer())); } catch {} }));
  return { mod, tools };
}
