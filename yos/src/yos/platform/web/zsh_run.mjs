// Run real zsh commands through the browser-safe host, in node, with the
// freebsd-tool wasms wired in so zsh can exec external commands.
// node zsh_run.mjs ['<zsh command>']
import { readFile, readdir } from "node:fs/promises";
import { runZsh } from "./zsh_host.mjs";

const zshMod = await WebAssembly.compile(await readFile(new URL("../../../../tmp/zsh.wasm", import.meta.url)));

// Load every tools/*.wasm into a name->module map.
const toolsDir = new URL("./tools/", import.meta.url);
const tools = new Map();
for (const f of (await readdir(toolsDir)).filter((f) => f.endsWith(".wasm"))) {
  tools.set(f.replace(/\.wasm$/, ""), await WebAssembly.compile(await readFile(new URL(f, toolsDir))));
}

function run(cmd) {
  let out = "";
  const res = runZsh(zshMod, ["zsh", "-f", "-c", cmd], (fd, t) => { out += t; }, () => {}, { tools });
  return { out, ...res };
}

const arg = process.argv[2];
const cmds = arg ? [arg] : [
  "echo builtin works",
  "pwd",
  "id",
  "hostname",
  "date",
  "ls",
  "ps",
];

console.log(`tools available: ${[...tools.keys()].sort().join(" ")}\n`);
for (const cmd of cmds) {
  const { out, exitCode } = run(cmd);
  console.log(`[exit ${exitCode}] ${JSON.stringify(cmd)}`);
  console.log(`   → ${JSON.stringify(out)}`);
}
