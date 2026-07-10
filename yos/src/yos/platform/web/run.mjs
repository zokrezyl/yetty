// Headless harness proving the browser model with no interpreter:
//   - one shared WebAssembly.Memory owned by the embedder
//   - guest.wasm runs on the engine natively, imports env.write/strlen
//   - bridge.wasm satisfies those imports, sharing the same memory
//   - the ONLY JS is host.write_bytes — the irreducible sandbox edge
//
// In the browser the identical wiring runs; host.write_bytes routes to
// a terminal instead of process.stdout. Run: node run.mjs
import { readFile } from "node:fs/promises";

const PAGES = 256; // 16 MiB — must cover the bridge's high --global-base
const memory = new WebAssembly.Memory({ initial: PAGES, maximum: PAGES });

let captured = "";
const decoder = new TextDecoder();

// The single host effect. Reads bytes straight out of the shared
// memory the embedder owns, at the offset the guest passed through the
// bridge untouched.
function host_write_bytes(fd, buf, count) {
  const bytes = new Uint8Array(memory.buffer, buf, count);
  const text = decoder.decode(bytes);
  captured += text;
  process.stdout.write(text);
  return count;
}

const bridgeMod = await WebAssembly.compile(await readFile(new URL("./bridge.wasm", import.meta.url)));
const guestMod = await WebAssembly.compile(await readFile(new URL("./guest.wasm", import.meta.url)));

// Instantiate the bridge first; it only needs the shared memory and the
// host effect. Its exports then satisfy the guest's env.* imports.
const bridge = await WebAssembly.instantiate(bridgeMod, {
  env: { memory },
  host: { write_bytes: host_write_bytes },
});

const guest = await WebAssembly.instantiate(guestMod, {
  env: {
    memory,
    write: bridge.exports.write,
    strlen: bridge.exports.strlen,
  },
});

guest.exports._start();

const expected = "hello from guest, served by a wasm bridge\n";
if (captured !== expected) {
  console.error(`\nFAIL: got ${JSON.stringify(captured)}`);
  process.exit(1);
}
console.error("PASS: guest -> wasm bridge -> shared memory -> host sink");
