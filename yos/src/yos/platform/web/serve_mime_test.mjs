// Regression test (issue #21): the dev server (serve.sh, behind `make serve-zsh`)
// must send correct Content-Type headers.
//
// The trap this pins: a stylesheet served with a non-CSS MIME type is REFUSED by
// browsers, so xterm.css never loads and its hidden helper textarea (the
// opacity:0 rule) shows as a stray box at the cursor. serve.sh's MIME map had
// only .html/.wasm/.mjs, so .css went out as application/octet-stream.
//
// Boots the REAL serve.sh on a throwaway port and asserts each asset's MIME.
// Run: node serve_mime_test.mjs
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const PORT = 8141;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const srv = spawn("./serve.sh", [String(PORT)], { cwd: here, stdio: ["ignore", "pipe", "pipe"] });
let srvOut = "";
srv.stdout.on("data", (d) => (srvOut += d));
srv.stderr.on("data", (d) => (srvOut += d));
const cleanup = () => { try { srv.kill("SIGTERM"); } catch {} };
process.on("exit", cleanup);

// Wait for the listener to come up.
let up = false;
for (let i = 0; i < 80; i++) {
  try { const r = await fetch(`http://127.0.0.1:${PORT}/zsh.html`); if (r.ok) { up = true; break; } } catch {}
  await sleep(100);
}

let failed = 0;
const check = (name, ok, detail) => { console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + (detail || "")}`); if (!ok) failed++; };

check("serve.sh came up", up, srvOut.slice(-200));

if (up) {
  // path -> the MIME prefix the browser requires for that asset to be honored.
  const expect = [
    ["/vendor/xterm.min.css", "text/css"],     // refused otherwise -> helper textarea shows
    ["/zsh.html", "text/html"],
    ["/zsh_main.mjs", "text/javascript"],       // refused otherwise -> module never runs
    ["/zsh.wasm", "application/wasm"],
  ];
  for (const [path, want] of expect) {
    let ct = "";
    try { ct = (await fetch(`http://127.0.0.1:${PORT}${path}`)).headers.get("content-type") || ""; } catch (e) { ct = "ERR " + e.message; }
    check(`${path} served as ${want}`, ct.startsWith(want), `content-type=${ct}`);
  }
}

cleanup();
console.log(failed ? `\n${failed} FAILED` : "\nALL PASSED");
process.exit(failed ? 1 : 0);
