// Headless check of the interactive shell stand-in: feed keystrokes
// into the guest through the wasm bridge, assert the rendered output.
// The browser path (terminal.html) wires the identical guest+bridge to
// xterm.js; this verifies the guest/bridge logic without a DOM.
// Run: node shell_test.mjs
import { readFile } from "node:fs/promises";

const PAGES = 256;
const memory = new WebAssembly.Memory({ initial: PAGES, maximum: PAGES });
const decoder = new TextDecoder();
let out = "";

function host_write_bytes(fd, buf, count) {
  out += decoder.decode(new Uint8Array(memory.buffer, buf, count));
  return count;
}

const bridgeMod = await WebAssembly.compile(await readFile(new URL("./bridge.wasm", import.meta.url)));
const shellMod = await WebAssembly.compile(await readFile(new URL("./shell_demo.wasm", import.meta.url)));
const bridge = await WebAssembly.instantiate(bridgeMod, {
  env: { memory },
  host: { write_bytes: host_write_bytes },
});
const shell = await WebAssembly.instantiate(shellMod, {
  env: { memory, write: bridge.exports.write },
});

const type = (text) => {
  for (const ch of text) shell.exports.feed_byte(ch.charCodeAt(0));
};

shell.exports.shell_start();
type("echo hello browser\r");
type("nope\r");
type("help\r");

const checks = [
  ["banner", out.includes("in the browser")],
  ["echo output", out.includes("hello browser")],
  ["command not found", out.includes("command not found: nope")],
  ["help builtins", out.includes("builtins: help")],
];

let ok = true;
for (const [name, pass] of checks) {
  console.error(`${pass ? "ok  " : "FAIL"}  ${name}`);
  ok = ok && pass;
}
process.exit(ok ? 0 : 1);
