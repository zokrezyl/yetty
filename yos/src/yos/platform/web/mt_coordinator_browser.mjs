// Browser coordinator Worker: runs perfstress_mt with the mt engine
// (real fork + real threads). Runs in a Worker so Atomics.wait is
// allowed; the engine spawns nested pool Workers for threads. Output is
// streamed back to the page.
import { runMtProgram } from "./mt_engine.mjs";

self.onmessage = async (event) => {
  const { wasmUrl, argv } = event.data;
  try {
    const module = await WebAssembly.compile(await (await fetch(wasmUrl)).arrayBuffer());
    const poolURL = new URL("./mt_pool_worker_browser.mjs", import.meta.url);
    const res = await runMtProgram(module, argv, {
      onOutput: (fd, t) => self.postMessage({ out: t }),
      onUnimpl: () => {},
      spawnPoolWorker: ({ module, memory, slot }) => {
        const w = new Worker(poolURL, { type: "module" });
        // Surface pool-worker failures instead of dropping them silently.
        w.onmessage = (e) => { if (e.data.out) self.postMessage({ out: e.data.out }); if (e.data.error) self.postMessage({ done: true, error: "pool worker: " + e.data.error }); };
        w.postMessage({ module, memory, slot });
        return w;
      },
    });
    self.postMessage({ done: true, exitCode: res.exitCode, procs: res.procs });
  } catch (e) {
    self.postMessage({ done: true, error: String(e && e.stack || e) });
  }
};
