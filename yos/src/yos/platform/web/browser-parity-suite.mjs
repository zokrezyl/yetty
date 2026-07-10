// Run the shared command case table (tests/integration/cases/*.json) on BOTH
// backends and use libvterm as the shared correctness oracle (issue #25).
//
// The two disjoint test worlds collapse onto ONE declarative case table:
//   - NATIVE  : the host `yos` binary                (the source of truth)
//   - BROWSER : the JS process engine (yos_proc.mjs) (must match native)
// Both run the SAME tool wasm (<libexec>/<argv[0]>), so any difference is the
// engine, not a different build of the tool. Each backend's raw terminal stream
// is rendered through vterm_grid.c (the same oracle already proven with tmux)
// into a faithful rows×cols grid, and the case's `expect` block is asserted on
// the grid / stdout / exit code.
//
// Classification mirrors browser-libc-suite.mjs, extended from exit codes to
// rendered output:
//   - PASS        : browser satisfies `expect`
//   - BROWSER-GAP : browser fails but native satisfies `expect`  → engine bug
//   - BOTH-FAIL   : native fails too (guest/libc gap, e.g. rune-locale) → not a
//                   browser regression; a worklist entry, does NOT fail CI
// Exit is non-zero ONLY on a NEW BROWSER-GAP. A case may carry
// `knownBrowserGap: "<reason>"` to mark a PRE-EXISTING, documented engine gap:
// it is still run and reported (under its own heading), but does not gate CI —
// the same "reported, not hidden" precedent as parity_runner.mjs's KNOWN-GAP
// entries. Genuine regressions on the currently-passing cases still fail CI.
//
// Usage:
//   node browser-parity-suite.mjs [--filter <substr>] [--build build-linux] [--quiet]
import { runProgram } from "./yos_proc.mjs";
import { compileGuest } from "./wasm_patch.mjs";
import { renderStreamToGrid, frameGrid } from "./vterm_render.mjs";
import { readFileSync, readdirSync, existsSync, statSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join, isAbsolute } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, "..", "..", "..", "..");
const argOf = (flag, def) => { const i = process.argv.indexOf(flag); return i >= 0 ? process.argv[i + 1] : def; };
const BUILD = argOf("--build", "build-linux");
const FILTER = argOf("--filter", "");
const quiet = process.argv.includes("--quiet");
const casesDir = join(repo, "tests", "integration", "cases");

// ── locate the native yos binary + the tool wasm dir ────────────────────────
// Same discovery order the native freebsd-tools suite uses (see
// tests/integration/freebsd-tools/_tools_path.py): YOS_BIN / YOS_TOOLS_DIR
// overrides, then an umbrella `result*/{bin/yos,libexec}`, then the local meson
// build's yos with a result*/libexec. Read-only — never triggers a nix build.
function findYosAndTools() {
  const envYos = process.env.YOS_BIN, envTools = process.env.YOS_TOOLS_DIR;
  const isDir = (p) => { try { return statSync(p).isDirectory(); } catch { return false; } };
  if (envYos && envTools && existsSync(envYos) && isDir(envTools)) return { yos: envYos, tools: envTools };
  const results = readdirSync(repo).filter((f) => f.startsWith("result")).map((f) => join(repo, f)).sort();
  for (const sym of results) {
    const yos = join(sym, "bin", "yos"), libexec = join(sym, "libexec");
    if (existsSync(yos) && isDir(libexec)) return { yos, tools: libexec };
  }
  // Fall back to the local meson build's yos + whichever result*/libexec exists.
  const localYos = join(repo, BUILD, "src", "yos", "yos");
  const libexec = results.map((s) => join(s, "libexec")).find((p) => isDir(p) && existsSync(join(p, "wc")));
  if (existsSync(localYos) && libexec) return { yos: localYos, tools: libexec };
  return { yos: null, tools: libexec || null };
}

const { yos: nativeYos, tools: toolsDir } = findYosAndTools();
if (!toolsDir) {
  console.log("SKIP: freebsd-tools libexec not found (run `nix build .#all`)");
  process.exit(0);
}
if (!nativeYos || !existsSync(nativeYos)) {
  console.log(`SKIP: native yos binary not found (built at ${join(repo, BUILD, "src", "yos", "yos")}, or `
    + `set YOS_BIN); cannot classify browser failures without the oracle.`);
  process.exit(0);
}

// ── load the shared case table ──────────────────────────────────────────────
const cases = [];
for (const f of readdirSync(casesDir).filter((f) => f.endsWith(".json")).sort()) {
  let data;
  try { data = JSON.parse(readFileSync(join(casesDir, f), "utf8")); }
  catch (e) { console.error(`  bad case file ${f}: ${e.message}`); process.exit(2); }
  for (const c of (Array.isArray(data) ? data : [data])) cases.push({ ...c, _file: f });
}
const selected = cases.filter((c) => !FILTER || (c.id || "").includes(FILTER));

// ── oracle + evaluator ──────────────────────────────────────────────────────
const CR = (s) => String(s).replace(/\r/g, "");
// A cooked terminal maps bare LF -> CR-LF (termios ONLCR). Both backends write
// bare LFs to a pipe, so apply the same discipline before libvterm renders,
// otherwise every line staircases and the grid is not what a human would see.
const onlcr = (s) => s.replace(/\r?\n/g, "\r\n");
const toGrid = (streamText, cols, rows) => renderStreamToGrid(onlcr(streamText), { cols, rows });
// Trailing blank cells / rows carry no information; trim them so a golden grid
// stays stable and small.
const gridText = (grid) => grid.rows.map((r) => r.replace(/\s+$/, "")).join("\n").replace(/\n+$/, "") + "\n";

// Evaluate a backend result against a case's `expect`. Every present matcher
// must hold (AND). Missing `expect` => trivially satisfied.
function evalExpect(res, expect) {
  if (!expect) return true;
  if ("exitCode" in expect && res.exitCode !== expect.exitCode) return false;
  if ("rawStdout" in expect && CR(res.stdout) !== CR(expect.rawStdout)) return false;
  if ("stdoutContains" in expect && !CR(res.stdout).includes(CR(expect.stdoutContains))) return false;
  if ("gridContains" in expect) {
    const needles = Array.isArray(expect.gridContains) ? expect.gridContains : [expect.gridContains];
    for (const needle of needles) if (!res.grid.text.includes(needle)) return false;
  }
  if ("gridRow" in expect) {
    const { row, equals, contains } = expect.gridRow;
    const line = res.grid.rows[row] || "";
    if (equals != null && line.replace(/\s+$/, "") !== equals) return false;
    if (contains != null && !line.includes(contains)) return false;
  }
  if ("goldenGrid" in expect) {
    const goldenPath = isAbsolute(expect.goldenGrid) ? expect.goldenGrid : join(casesDir, expect.goldenGrid);
    if (!existsSync(goldenPath)) return false;
    if (gridText(res.grid) !== readFileSync(goldenPath, "utf8")) return false;
  }
  return true;
}

// ── backends ────────────────────────────────────────────────────────────────
// Native: run the SAME wasm through the host yos binary, feed stdin as a pipe.
function runNative(wasm, argv, stdin, timeoutMs) {
  const r = spawnSync(nativeYos, [wasm, ...argv.slice(1)],
    { input: stdin ?? Buffer.alloc(0), encoding: "buffer", timeout: timeoutMs, maxBuffer: 64 << 20 });
  const exitCode = typeof r.status === "number" ? r.status : (r.signal ? 139 : 1);
  return {
    stdout: (r.stdout || Buffer.alloc(0)).toString("utf8"),
    stderr: (r.stderr || Buffer.alloc(0)).toString("utf8"),
    exitCode,
  };
}

// Browser: the cooperative process engine. The same wasm, argv, and stdin pipe.
async function runBrowser(wasm, argv, stdin) {
  const mod = await compileGuest(readFileSync(wasm));
  let stdout = "", stderr = "", unimpl = null;
  const r = runProgram(mod, argv,
    (fd, t) => { if (fd === 2) stderr += t; else stdout += t; },
    (name) => { if (!unimpl) unimpl = name; },
    { stdin });
  return {
    stdout, stderr, unimpl,
    exitCode: typeof r.exitCode === "number" ? r.exitCode : 139,
    error: typeof r.exitCode === "number" ? null : (r.error || "trap"),
  };
}

// ── run ─────────────────────────────────────────────────────────────────────
console.log(`\nShared case table vs libvterm oracle — native yos vs browser engine`);
console.log(`cases=${selected.length}${FILTER ? `  filter=${FILTER}` : ""}  tools=${toolsDir}\n`);

const browserGaps = [], nativeGaps = [], knownGaps = [], knownNativeGaps = [], bothFail = [], missing = [];
let pass = 0;
const t0 = Date.now();

for (const c of selected) {
  const id = c.id || c.argv.join(" ");
  const name = c.argv[0];
  const wasm = join(toolsDir, name);
  if (!existsSync(wasm)) { missing.push(id); if (!quiet) console.log(`  MISS  ${id}  (tool wasm not built: ${wasm})`); continue; }
  const cols = c.cols || 80, rows = c.rows || 24;
  const timeoutMs = (c.timeout || 15) * 1000;
  const stdin = c.stdin != null ? Buffer.from(c.stdin, "utf8") : null;

  if (c.interactive) {
    // Interactive/TUI cases (scripted keystrokes, golden grids) are the Phase-2
    // path; report as skipped so their presence in the table is visible.
    if (!quiet) console.log(`  SKIP  ${id}  (interactive case — golden-grid path not yet driven here)`);
    continue;
  }

  let browser;
  try { browser = await runBrowser(wasm, c.argv, stdin); }
  catch (e) { browser = { stdout: "", stderr: "", exitCode: 139, error: e.message, unimpl: null }; }
  browser.grid = toGrid(browser.stdout, cols, rows);
  const browserOk = evalExpect(browser, c.expect);

  // The native binary is the source of truth, so ALWAYS run it — not only
  // when the browser fails. A browser-PASS + native-FAIL case (the browser
  // ahead of native) is a genuine native bug that a browser-only check would
  // silently pass; running native unconditionally is what surfaces it as a
  // NATIVE-GAP instead of hiding it behind a green PASS.
  const native = runNative(wasm, c.argv, stdin, timeoutMs);
  native.grid = toGrid(native.stdout, cols, rows);
  const nativeOk = evalExpect(native, c.expect);
  const parity = CR(browser.stdout) === CR(native.stdout) && browser.exitCode === native.exitCode;

  const bdetail = browser.unimpl ? `unimpl:${browser.unimpl}`
    : browser.error ? `browser ${browser.error}`
    : `browser exit=${browser.exitCode}${parity ? " (==native)" : " (!=native)"}`;
  const ndetail = `native exit=${native.exitCode}`;

  if (browserOk && nativeOk) {
    pass++;
    if (!quiet) console.log(`  PASS  ${id}`);
  } else if (browserOk && !nativeOk) {
    // Browser satisfies the oracle, native does not → a native-side bug the
    // browser is already ahead of. Reported, never gates CI (it is not a
    // browser regression) — the mirror image of BOTH-FAIL's worklist status.
    if (c.knownNativeGap) {
      knownNativeGaps.push({ id, detail: ndetail, reason: String(c.knownNativeGap) });
      if (!quiet) console.log(`  gap   ${id}  [KNOWN-NATIVE-GAP]  ${ndetail}`);
    } else {
      nativeGaps.push({ id, detail: ndetail, c, browser, native, cols });
      if (!quiet) console.log(`  FAIL  ${id}  [NATIVE-GAP]  ${ndetail}  (browser ahead of native)`);
    }
  } else if (!browserOk && nativeOk) {
    // Native works, browser doesn't → a browser-engine bug. The only class
    // that gates CI (unless documented as a known gap).
    if (c.knownBrowserGap) {
      knownGaps.push({ id, detail: bdetail, reason: String(c.knownBrowserGap) });
      if (!quiet) console.log(`  gap   ${id}  [KNOWN-BROWSER-GAP]  ${bdetail}`);
    } else {
      browserGaps.push({ id, detail: bdetail, c, browser, native, cols });
      if (!quiet) console.log(`  FAIL  ${id}  [BROWSER-GAP]  ${bdetail}`);
    }
  } else {
    // Neither backend satisfies the oracle → a shared guest/libc gap (e.g. the
    // rune-locale <_ctype.h> hole). Fix the guest, not the browser.
    bothFail.push({ id, detail: bdetail, parity });
    if (!quiet) console.log(`  fail  ${id}  [BOTH-FAIL]  ${bdetail}`);
  }
}

const secs = Math.round((Date.now() - t0) / 1000);
console.log(`\n────────────────────────────────────────────────────────`);
console.log(`PASS (both backends match the oracle)          : ${pass}/${selected.length}`);
console.log(`BROWSER-GAP (native passes, browser fails)     : ${browserGaps.length}${browserGaps.length ? "  <-- fails CI" : ""}`);
console.log(`NATIVE-GAP  (browser passes, native fails)     : ${nativeGaps.length}`);
console.log(`KNOWN-BROWSER-GAP (documented, non-gating)     : ${knownGaps.length}`);
console.log(`KNOWN-NATIVE-GAP  (documented, non-gating)     : ${knownNativeGaps.length}`);
console.log(`BOTH-FAIL   (native fails too; guest/libc gap) : ${bothFail.length}`);
if (missing.length) console.log(`MISSING tool wasm (not built)                 : ${missing.length}`);
console.log(`elapsed : ${secs}s\n`);

// Show the grids for real browser regressions so a failure is debuggable at a
// glance — the whole reason for rendering through libvterm.
for (const gap of browserGaps) {
  console.log(`BROWSER-GAP  ${gap.id}  (argv: ${JSON.stringify(gap.c.argv)})`);
  console.log(`  native grid (expected — the oracle):`);
  console.log(frameGrid(gap.native.grid.rows.slice(0, Math.min(gap.c.rows || 24, 6)), gap.cols));
  console.log(`  browser grid (what the engine produced):`);
  console.log(frameGrid(gap.browser.grid.rows.slice(0, Math.min(gap.c.rows || 24, 6)), gap.cols));
  console.log("");
}
// And the grids for native bugs the browser is ahead of — the browser grid is
// the one that matches the oracle here, so it is the "expected" side.
for (const gap of nativeGaps) {
  console.log(`NATIVE-GAP  ${gap.id}  (argv: ${JSON.stringify(gap.c.argv)})  — native yos is wrong; the browser engine is correct`);
  console.log(`  browser grid (expected — matches the oracle):`);
  console.log(frameGrid(gap.browser.grid.rows.slice(0, Math.min(gap.c.rows || 24, 6)), gap.cols));
  console.log(`  native grid (what the native binary produced):`);
  console.log(frameGrid(gap.native.grid.rows.slice(0, Math.min(gap.c.rows || 24, 6)), gap.cols));
  console.log("");
}
if (knownGaps.length) {
  console.log("KNOWN-BROWSER-GAP (documented engine gaps — tracked worklist, do not gate CI):");
  for (const g of knownGaps) console.log(`  • ${g.id.padEnd(28)} ${g.detail}\n      ${g.reason}`);
  console.log("");
}
if (knownNativeGaps.length) {
  console.log("KNOWN-NATIVE-GAP (documented native bugs the browser is already ahead of — worklist, do not gate CI):");
  for (const g of knownNativeGaps) console.log(`  • ${g.id.padEnd(28)} ${g.detail}\n      ${g.reason}`);
  console.log("");
}
if (bothFail.length && !quiet) {
  console.log("BOTH-FAIL (broken on native yos too — fix the guest/libc, not the browser):");
  for (const b of bothFail) console.log(`  • ${b.id.padEnd(28)} ${b.detail}`);
  console.log("");
}

// CI gate: fail ONLY on a genuine browser-engine regression. A NATIVE-GAP is a
// native-side bug (the browser is already correct), so it is reported but does
// not gate the always-on browser-regression net — same policy as BOTH-FAIL.
process.exit(browserGaps.length ? 1 : 0);
