// fzy on the browser engine — the fuzzy finder driven exactly as a user
// does: batch filter mode in a pipeline, then the interactive picker
// (choices piped on stdin, UI on /dev/tty), typed filter, Enter select.
//
// Engine behaviours this pins (each was a real blocker):
//   - open("/dev/tty") resolves to the shared session terminal (fzy's
//     picker UI runs there while stdin carries the piped choices);
//   - pselect (fzy's per-keystroke tty wait) — timespec-flavoured
//     select, was an unimplemented import;
//   - ICRNL only in COOKED mode: a raw-mode app receives the real 0x0d
//     — the blanket CR→NL translation made Enter a no-op in the picker;
//   - stdin-sentinel reads follow dup2 (yos_fread class of bug, fixed
//     native-side; the engine's FILE layer already routed correctly).
//
// Run: node fzy_from_zsh_test.mjs   (or `make test-browser-fzy`)
import { runInteractive } from "./yos_proc.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const load = async (p) => compileGuest(await readFile(join(here, p)));

if (!existsSync(join(here, "tools/fzy.wasm"))) {
	process.stderr.write("SKIP: tools/fzy.wasm missing (nix build .#all; refresh tools symlinks)\n");
	process.exit(0);
}

const zsh = await load("zsh.wasm");
const tools = new Map([["sh", zsh], ["zsh", zsh], ["fzy", await load("tools/fzy.wasm")]]);

let raw = "";
const ctl = runInteractive(zsh, ["zsh", "-f", "+o", "promptsp"], {
	onOutput: (fd, text) => { raw += text; },
	tools,
	env: ["PATH=/bin:/usr/bin", "HOME=/", "TERM=xterm-256color", "PWD=/"],
	cols: 80, rows: 24,
});
const type = async (s, ms = 900) => { for (const ch of s) { ctl.write(ch); await sleep(40); } await sleep(ms); };
const plain = () => raw.replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "");
const alive = () => ctl.mgr.procs.filter((p) => !p.exited).map((p) => p.comm).join(",");

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

await sleep(500);

// batch filter mode in a pipeline
raw = "";
await type("printf 'one\\ntwo\\nthree\\n' | fzy -e tw\r", 2500);
check("batch filter (-e) matches through a pipe", /\btwo\b/.test(plain()), JSON.stringify(plain().replace(/\s+/g, " ").slice(-60)));

// interactive picker: choices on stdin, UI on /dev/tty
raw = "";
await type("printf 'apple\\nbanana\\ncherry\\n' | fzy\r", 3000);
check("picker painted (prompt + choices)", /apple/.test(plain()) && /cherry/.test(plain()), JSON.stringify(plain().replace(/\s+/g, " ").slice(-80)));

raw = "";
await type("che", 1800);
check("live filtering narrows to cherry", /cherry/.test(plain()), JSON.stringify(plain().replace(/\s+/g, " ").slice(-60)));

ctl.write("\r");
await sleep(2500);
check("Enter selects and fzy exits cleanly", ctl.mgr.procs.filter((p) => p.comm === "fzy").every((p) => p.exited && p.exitCode === 0), alive());

raw = "";
await type("echo after_$((6*7))\r", 1200);
check("shell alive after the pick", /after_42/.test(plain()), JSON.stringify(plain().slice(-60)));

ctl.dispose();
console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED — fzy filters and picks interactively on the browser engine");
process.exit(failed ? 1 : 0);
