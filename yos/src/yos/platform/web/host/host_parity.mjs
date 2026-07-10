// yos-host parity harness (epic #33, issue #37).
//
// Runs the SAME desktop tool artifacts (result/libexec, NOT the stale
// src/yos/platform/web/tools/ copies) through TWO backends and compares:
//   - native: the desktop `yos` host binary (build-<host>/src/yos/yos)
//   - host wasm: yos-host.wasm (the emcc build of the same yos C runtime)
// and classifies each command:
//   MATCH       — both produce identical stdout
//   HOST-GAP    — native passes, host wasm differs/fails (a browser-host gap)
//   NATIVE-GAP  — native itself fails (guest/libc gap, not a browser regression)
//   BOTH-FAIL   — both fail the same way
//
// This is the Phase 2 oracle: a HOST-GAP means "the emcc host diverges from
// native," which should trend to zero because it is the same C. Tools that need
// runtime surface the emcc host does not yet wire (variadic stdio, procfs, …)
// show up here as HOST-GAP — visible, not hidden.
//
// Run: node host_parity.mjs   (or `make test-browser-host-parity`)
import { readFileSync, existsSync, mkdtempSync, writeFileSync, mkdirSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..", "..", "..");
const libexec = join(repo, "result", "libexec");
const nativeYos = join(repo, "build-linux", "src", "yos", "yos");
const hostMjs = join(here, "yos-host.mjs");

for (const [what, path] of [["result/libexec", libexec], ["native yos", nativeYos], ["yos-host.mjs", hostMjs]]) {
	if (!existsSync(path)) {
		process.stderr.write(`host_parity: missing ${what} at ${path}\n`);
		if (what === "yos-host.mjs") process.stderr.write("  build it: make browser-host-runner\n");
		process.exit(2);
	}
}

// Focused command set (issue #37 names ls/ps/sed/sort/tr/cut/grep/wc). Each case
// is file-argument based (no stdin) so it runs identically on both backends. The
// input file is written to a real temp dir for native and mirrored into MEMFS
// for the host at the same absolute path.
const INPUT = "banana\napple\ncherry\napple\nBANANA\n";
// knownNativeGap: native yos itself is known to diverge here (issue #40 —
// inherited native bugs, e.g. the rune-locale <_ctype.h> gap that makes native
// grep/sed match nothing). Such a case surfacing as NATIVE-GAP is EXPECTED and
// must not be counted as a browser regression; the fix lands once in shared C
// (impl/libc ctype/rune init), after which both backends agree.
const CASES = [
	{ name: "echo", args: ["hello", "world"], files: {} },
	{ name: "basename", args: ["/a/b/c.txt"], files: {} },
	{ name: "cat", args: ["@in"], files: { "@in": INPUT } },
	{ name: "wc", args: ["-l", "@in"], files: { "@in": INPUT } },
	{ name: "head", args: ["-2", "@in"], files: { "@in": INPUT } },
	{ name: "sort", args: ["@in"], files: { "@in": INPUT } },
	{ name: "cut", args: ["-c", "1-3", "@in"], files: { "@in": INPUT } },
	{ name: "tr", args: ["a-z", "A-Z"], files: {} },
	{ name: "grep", args: ["apple", "@in"], files: { "@in": INPUT }, knownNativeGap: "rune-locale ctype gap (#40) — native matches nothing" },
];

const workdir = mkdtempSync(join(tmpdir(), "yos-parity-"));

// Load the host factory once; re-instantiate per case for isolation.
const { default: createHost } = await import(hostMjs);

function resolveArgs(args, files, fsWrite) {
	// Replace @in tokens with a concrete file path, writing the content via
	// fsWrite(path, content). Returns the resolved argv.
	const out = [];
	for (const a of args) {
		if (a === "@in") {
			const p = "/parity_in.txt";
			fsWrite(p, files["@in"]);
			out.push(p);
		} else {
			out.push(a);
		}
	}
	return out;
}

function runNative(tool, args, files) {
	const toolPath = join(libexec, tool);
	const resolved = resolveArgs(args, files, (p, c) => {
		// Native reads the real filesystem; place the file at <workdir><p>.
		const real = join(workdir, p.replace(/^\//, ""));
		mkdirSync(dirname(real), { recursive: true });
		writeFileSync(real, c);
	}).map((a) => (a.startsWith("/parity_in") ? join(workdir, a.replace(/^\//, "")) : a));
	try {
		const out = execFileSync(nativeYos, [toolPath, ...resolved], { encoding: "utf8", timeout: 15000, stdio: ["ignore", "pipe", "pipe"] });
		return { ok: true, out };
	} catch (e) {
		return { ok: false, out: e.stdout ? String(e.stdout) : "", err: String(e.stderr ?? e.message) };
	}
}

async function runHost(tool, args, files) {
	const bytes = readFileSync(join(libexec, tool));
	const memFiles = {};
	const resolved = resolveArgs(args, files, (p, c) => { memFiles[p] = c; });
	let out = "", err = "";
	try {
		await createHost({
			arguments: [tool, ...resolved],
			preRun: [(M) => {
				M.FS.writeFile("/guest.wasm", bytes);
				for (const [p, c] of Object.entries(memFiles)) M.FS.writeFile(p, c);
			}],
			print: (s) => { out += s + "\n"; },
			printErr: (s) => { err += s + "\n"; },
		});
		return { ok: true, out };
	} catch (e) {
		return { ok: false, out, err: `${err}${e?.message ?? e}` };
	}
}

// Normalise: strip trailing whitespace and collapse the (necessarily
// different) input-file path each backend was given to a fixed token, so a
// tool that echoes its filename (wc, grep -H) isn't a false divergence.
const norm = (s) => (s ?? "").replace(/\S*parity_in\.txt/g, "INPUT").replace(/\s+$/g, "");

let match = 0, hostGap = 0, nativeGap = 0, knownGap = 0, bothFail = 0;
const rows = [];
for (const c of CASES) {
	const nat = runNative(c.name, c.args, c.files);
	const host = await runHost(c.name, c.args, c.files);
	const natOut = norm(nat.out), hostOut = norm(host.out);
	let verdict, note = c.knownNativeGap ?? "";
	if (!nat.ok || natOut === "") {
		if (host.ok && hostOut !== "") {
			// Native diverges. Expected iff flagged as a known native gap.
			verdict = c.knownNativeGap ? "NATIVE-GAP*" : "NATIVE-GAP";
			if (c.knownNativeGap) knownGap++; else nativeGap++;
		} else {
			verdict = "BOTH-FAIL"; bothFail++;
		}
	} else if (natOut === hostOut) {
		verdict = "MATCH"; match++;
	} else {
		verdict = "HOST-GAP"; hostGap++;
	}
	rows.push({ cmd: `${c.name} ${c.args.join(" ")}`, verdict, note, nat: natOut.replace(/\n/g, "|").slice(0, 22), host: hostOut.replace(/\n/g, "|").slice(0, 22) });
}

process.stdout.write("\nyos-host parity — desktop artifacts, native vs. emcc host wasm\n");
process.stdout.write("(* = known native gap, expected — tracked for #40, not a regression)\n");
process.stdout.write("─".repeat(76) + "\n");
for (const r of rows) {
	const tail = r.note ? `  ${r.note}` : `  native[${r.nat}] host[${r.host}]`;
	process.stdout.write(`  ${r.verdict.padEnd(12)} ${r.cmd.padEnd(20)}${tail}\n`);
}
process.stdout.write("─".repeat(76) + "\n");
process.stdout.write(`  MATCH=${match}  HOST-GAP=${hostGap}  NATIVE-GAP=${nativeGap}  KNOWN-NATIVE-GAP=${knownGap}  BOTH-FAIL=${bothFail}  (of ${CASES.length})\n\n`);

// The harness succeeds as long as it ran both backends and produced a
// classification. HOST-GAPs are reported, not fatal (tracked work). A NATIVE-GAP
// that is NOT a known gap IS a real problem the harness surfaces. At least one
// MATCH proves the same-artifact pipeline works end to end.
if (match === 0) {
	process.stderr.write("host_parity: FAIL — no command matched; the same-artifact pipeline is broken\n");
	process.exit(1);
}
if (nativeGap > 0) {
	process.stderr.write(`host_parity: WARN — ${nativeGap} UNEXPECTED native-gap(s); mark as knownNativeGap or fix in shared C\n`);
}
process.stderr.write(`host_parity: OK — ${match}/${CASES.length} match; ${hostGap} host-gap, ${knownGap} known-native-gap tracked\n`);
process.exit(0);
