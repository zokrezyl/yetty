#!/usr/bin/env node
// "Browser yos" CLI: run a wasm guest through the SAME browser process engine
// (yos_proc.mjs) the in-page runtime uses, but from the command line — so the
// existing meson test runner (tests/ut/libc/run_libc_test.py) can drive the
// whole yos unit-test suite against the browser implementation simply by being
// pointed at this script as its `--yos` binary.
//
// It mimics the host `yos <prog.wasm>` contract: guest stdout -> our stdout,
// guest stderr -> our stderr, process exit code = guest exit code (a wasm trap
// or unresolved import exits non-zero, like the native runtime aborting).
//
//   node run-wasm-node.mjs <prog.wasm> [args...]
//
// Tools for execve()/fork-exec of OTHER programs are resolved from the same
// directory as <prog.wasm> by basename (best-effort; most unit tests are
// self-contained and fork the same image, which needs no tool map).
import { runYos } from "./yos_run.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { readFileSync, readdirSync } from "node:fs";
import { dirname, basename, join } from "node:path";

const wasmPath = process.argv[2];
if (!wasmPath) { process.stderr.write("usage: run-wasm-node.mjs <prog.wasm> [args...]\n"); process.exit(2); }

const mod = await compileGuest(readFileSync(wasmPath));

// Tool map for execve()/fork-exec of OTHER programs. Unit tests are
// self-contained (they fork the SAME image, which needs no tool map), so this
// is empty by default — eagerly compiling every sibling .wasm would be far too
// slow across a whole suite run. Set YOS_TOOLS=1 to opt in to sibling-wasm
// resolution for the rare guest that execve()s a separate program.
const tools = new Map();
if (process.env.YOS_TOOLS === "1") {
  try {
    const dir = dirname(wasmPath);
    for (const f of readdirSync(dir)) {
      if (f.endsWith(".wasm") && !f.endsWith("-raw.wasm")) {
        try { tools.set(f.replace(/\.wasm$/, ""), await compileGuest(readFileSync(join(dir, f)))); } catch {}
      }
    }
  } catch {}
}

// Optional /usr/share mount (nvim runtime, zsh functions): point YOS_SHARE_DIR
// at a share tree (e.g. result/share) to expose it to the guest. Lazy file
// loads, so the directory walk is the only up-front cost — still opt-in to
// keep the per-test overhead of the unit-test suite at zero.
const mounts = [];
if (process.env.YOS_SHARE_DIR) {
  const { entriesFromDir } = await import("./fs_mount.mjs");
  mounts.push({ at: "/usr/share", entries: await entriesFromDir(process.env.YOS_SHARE_DIR) });
}

// Buffer per fd; flush at the end (keeps stdout/stderr ordering stable for the
// substring checks run_libc_test.py performs on captured stdout).
let outBuf = "", errBuf = "";
const onOutput = (fd, text) => { if (fd === 2) errBuf += text; else outBuf += text; };
const unimpl = new Set();
const onUnimpl = (name) => unimpl.add(name);

// Route by memory model: shared-memory wasm → real threads (mt_engine), else
// the cooperative single-process engine. Unit-test wasm exports its own memory,
// so it lands on the cooperative engine exactly as before.
const r = await runYos(mod, [basename(wasmPath)], { onOutput, onUnimpl, tools, mounts });

if (outBuf) process.stdout.write(outBuf);
if (errBuf) process.stderr.write(errBuf);
if (unimpl.size) process.stderr.write(`yos-node: unimplemented imports: ${[...unimpl].join(", ")}\n`);

// Numeric guest exit code passes straight through. A trap / engine error has a
// non-numeric exitCode — surface it as a crash (139) so the test sees a clear
// failure rather than a silent exit 0.
const code = typeof r.exitCode === "number" ? r.exitCode : 139;
if (typeof r.exitCode !== "number" && r.error) process.stderr.write(`yos-node: ${r.error}\n`);
process.exit(code);
