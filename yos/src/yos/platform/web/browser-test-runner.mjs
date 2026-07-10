// Headless browser test runner for the yos browser runtime (issue #21).
//
// Runs the web-platform test suite and reports a single pass/fail summary.
// Two groups:
//   - chrome:  real headless Chrome via the DevTools Protocol. Each test boots
//              its own page + wasm and asserts behaviour. Needs
//              `google-chrome-stable` (override with YOS_CHROME) and a node with
//              a global WebSocket (node >= 22).
//   - engine:  the SAME browser process engine (yos_proc.mjs) driven directly in
//              node — faster, no Chrome, validates the scheduler/syscall layer.
//
// The chrome tests each bind a fixed DevTools debug port, so they MUST run one
// at a time; this runner is strictly sequential.
//
// Usage:
//   node browser-test-runner.mjs            # run everything
//   node browser-test-runner.mjs --engine   # node-engine tests only (no Chrome)
//   node browser-test-runner.mjs --chrome   # headless-Chrome tests only
//   YOS_TEST_TIMEOUT=180 node browser-test-runner.mjs
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const TIMEOUT_MS = (Number(process.env.YOS_TEST_TIMEOUT) || 240) * 1000;

// Self-contained headless-Chrome tests (own http server + spawn Chrome).
const CHROME_TESTS = [
  "browser_test.mjs",
  "zsh_browser_test.mjs",
  "threads_browser_test.mjs",
  "multi_test.mjs",
  "probe_test.mjs",
  "tmux_browser_test.mjs",
  "tmux_xterm_render_test.mjs",
  "zsh_paste_test.mjs",
  "mt_fork_thread_browser_test.mjs", // fork-child thread memory safety via mt_engine
];
// Known gaps — deliberately NOT in the default suite (run them by name):
//   perfstress_zsh_test.mjs — drives the FULL `perfstress`, whose condvar/
//     rwlock phase needs a real cross-thread condvar wait. The single-process
//     engine here cannot signal across threads (that is the Worker-pool engine,
//     mt_engine.mjs), so it hangs at the condvar phase. The fork/proc/mutex
//     coverage in the browser is already exercised by zsh_browser_test.mjs.
// Node-only tests that drive the browser process engine directly.
const ENGINE_TESTS = [
  "import_manifest_test.mjs",
  "shell_test.mjs",
  "threads_test.mjs",
  "tmux_from_zsh_test.mjs",
  "tmux_render_test.mjs", // libvterm-oracle assessment of tmux's rendered screen
  "fd_lifecycle_test.mjs", // cloexec-on-exec + non-scheduler-exit fd release
  "onlcr_test.mjs", // terminal output newline cooking (LF->CRLF, no staircase)
  "serve_mime_test.mjs", // serve.sh sends correct Content-Type (css/mjs/wasm)
  "mt_test_node.mjs", // full perf-stress on the real-thread (worker-pool) engine
  "mt_fork_thread_test.mjs", // fork-child threads run against the child's memory
];

const only = process.argv.includes("--chrome") ? "chrome"
  : process.argv.includes("--engine") ? "engine" : "all";
const suite = [
  ...(only === "engine" ? [] : CHROME_TESTS.map((f) => ({ f, group: "chrome" }))),
  ...(only === "chrome" ? [] : ENGINE_TESTS.map((f) => ({ f, group: "engine" }))),
];

function runOne(file) {
  return new Promise((resolve) => {
    const child = spawn(process.execPath, [join(here, file)], { cwd: here, stdio: ["ignore", "pipe", "pipe"] });
    let out = "";
    child.stdout.on("data", (d) => (out += d));
    child.stderr.on("data", (d) => (out += d));
    const timer = setTimeout(() => { timedOut = true; child.kill("SIGKILL"); }, TIMEOUT_MS);
    let timedOut = false;
    child.on("close", (code) => {
      clearTimeout(timer);
      // Last line that looks like a verdict, for a one-line summary.
      const line = out.split("\n").map((l) => l.trim()).filter(Boolean)
        .reverse().find((l) => /PASS|FAIL|ALL|passed|failed|checks|^ok /i.test(l)) || "";
      resolve({ code: timedOut ? "timeout" : code, line, out });
    });
  });
}

const pad = (s, n) => String(s).padEnd(n);
let failed = 0;
const t0 = Date.now();
console.log(`\nyos browser test suite — ${suite.length} tests (timeout ${TIMEOUT_MS / 1000}s each)\n`);
for (const { f, group } of suite) {
  process.stdout.write(`  ${pad(group, 7)} ${pad(f, 26)} … `);
  const r = await runOne(f);
  const ok = r.code === 0;
  if (!ok) failed++;
  console.log(`${ok ? "PASS" : "FAIL"}  ${ok ? "" : `(exit=${r.code}) `}${(r.line || "").slice(0, 60)}`);
  if (!ok) { console.log("    ── output tail ──"); console.log(r.out.split("\n").slice(-12).map((l) => "    " + l).join("\n")); }
}
console.log(`\n${suite.length - failed}/${suite.length} passed in ${Math.round((Date.now() - t0) / 1000)}s${failed ? `  — ${failed} FAILED` : ""}\n`);
process.exit(failed ? 1 : 0);
