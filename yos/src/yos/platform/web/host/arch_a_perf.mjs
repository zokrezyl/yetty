// Architecture A performance measurement (epic #33, issue #41).
//
// Architecture A runs the guest under the wasm3 interpreter, which is ITSELF
// compiled to wasm and run by the browser/Node wasm engine. So a guest opcode
// costs "wasm3 dispatch, interpreted by V8" instead of "wasm3 dispatch, native".
// This measures that overhead against native yos on the same guest artifacts,
// so the A-vs-B decision (issue #41) rests on data, not guesswork.
//
// Run: node arch_a_perf.mjs   (needs `make all` for native yos + libexec, and
//      `make browser-host-runner` for yos-host.mjs)
import { readFileSync, existsSync, mkdtempSync, writeFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { tmpdir } from "node:os";

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..", "..", "..");
const libexec = join(repo, "result", "libexec");
const nativeYos = join(repo, "build-linux", "src", "yos", "yos");
const hostMjs = join(here, "yos-host.mjs");

for (const [w, p] of [["native yos", nativeYos], ["yos-host.mjs", hostMjs], ["libexec", libexec]]) {
	if (!existsSync(p)) { process.stderr.write(`arch_a_perf: missing ${w} at ${p}\n`); process.exit(2); }
}

const workdir = mkdtempSync(join(tmpdir(), "yos-perf-"));
const big = Array.from({ length: 20000 }, (_, i) => `line ${i} the quick brown fox`).join("\n") + "\n";
const bigPath = join(workdir, "big.txt");
writeFileSync(bigPath, big);
writeFileSync(join(workdir, "memfs_big"), big); // content for MEMFS

const { default: createHost } = await import(hostMjs);

function timeIt(fn, iters) {
	const t0 = process.hrtime.bigint();
	for (let i = 0; i < iters; i++) fn();
	const t1 = process.hrtime.bigint();
	return Number(t1 - t0) / 1e6 / iters; // ms per iter
}
async function timeAsync(fn, iters) {
	const t0 = process.hrtime.bigint();
	for (let i = 0; i < iters; i++) await fn();
	const t1 = process.hrtime.bigint();
	return Number(t1 - t0) / 1e6 / iters;
}

function native(tool, args) {
	execFileSync(nativeYos, [join(libexec, tool), ...args], { stdio: ["ignore", "ignore", "ignore"], timeout: 30000 });
}
async function host(tool, args, memFiles = {}) {
	const bytes = readFileSync(join(libexec, tool));
	await createHost({
		arguments: [tool, ...args],
		preRun: [(M) => { M.FS.writeFile("/guest.wasm", bytes); for (const [p, c] of Object.entries(memFiles)) M.FS.writeFile(p, c); }],
		print: () => {}, printErr: () => {},
	});
}

const CASES = [
	{ label: "echo (spawn/instantiate baseline)", tool: "echo", nargs: ["hi"], hargs: ["hi"], mem: {}, iters: 20 },
	{ label: "wc -l big.txt (20k lines, I/O+scan)", tool: "wc", nargs: ["-l", bigPath], hargs: ["-l", "/big.txt"], mem: { "/big.txt": big }, iters: 10 },
	{ label: "cat big.txt (20k lines, I/O)", tool: "cat", nargs: [bigPath], hargs: ["/big.txt"], mem: { "/big.txt": big }, iters: 10 },
];

process.stdout.write("\nArchitecture A perf — native yos vs. wasm3-in-wasm host (ms/run, lower is better)\n");
process.stdout.write("─".repeat(80) + "\n");
process.stdout.write(`  ${"case".padEnd(38)} ${"native".padStart(10)} ${"host".padStart(10)} ${"ratio".padStart(8)}\n`);
for (const c of CASES) {
	// warm up
	try { native(c.tool, c.nargs); } catch {}
	try { await host(c.tool, c.hargs, c.mem); } catch {}
	let nms = NaN, hms = NaN;
	try { nms = timeIt(() => native(c.tool, c.nargs), c.iters); } catch (e) { /* native gap */ }
	try { hms = await timeAsync(() => host(c.tool, c.hargs, c.mem), c.iters); } catch (e) { /* host gap */ }
	const ratio = nms && hms ? (hms / nms).toFixed(1) + "x" : "n/a";
	process.stdout.write(`  ${c.label.padEnd(38)} ${nms.toFixed(1).padStart(10)} ${hms.toFixed(1).padStart(10)} ${ratio.padStart(8)}\n`);
}
process.stdout.write("─".repeat(80) + "\n");
process.stdout.write("Note: the host figure includes per-run wasm3 setup + Emscripten module instantiation;\n");
process.stdout.write("native includes process spawn. Ratios are indicative, not micro-benchmarks.\n\n");
