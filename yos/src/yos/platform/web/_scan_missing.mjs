import { readFileSync } from "node:fs";
const src = readFileSync(new URL("./yos_proc.mjs", import.meta.url), "utf8");
function provided(name) {
  // a libc fn is provided if it appears as an object key `name:` in buildLibc
  const esc = name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return new RegExp("(^|[\\s,{])" + esc + "\\s*:").test(src);
}
for (const file of process.argv.slice(2)) {
  const mod = new WebAssembly.Module(readFileSync(file));
  const need = WebAssembly.Module.imports(mod)
    .filter((i) => i.module === "env" && i.kind === "function")
    .map((i) => i.name);
  const missing = need.filter((n) => !provided(n) && !/^(__yos_|__main_argc)/.test(n));
  console.log(`${file}: ${need.length} env fns, ${missing.length} MISSING:`);
  console.log("  " + missing.sort().join(" "));
}
