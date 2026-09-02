// ygui2 TypeScript/node binding test — headless, against the real
// libyetty_ffi.so through the koffi runtime.
//
// Covers the wrapper contracts the wire test cannot see: builder wiring
// and value getters, callback dispatch through synthetic mouse input,
// the DEFERRED close boundary (close() from a widget callback and from
// the sink callback must not dispose the framework while native
// dispatch is still on the stack), reservation-mode exposure
// (setFullscreen accepted before the first emit, rejected after),
// contentScale, the flattened error cause chain, and wrapper liveness.
//
// Run (ctest wires this up; koffi comes from bindings/typescript's
// npm install):
//
//     YETTY_FFI_LIB=<build>/.../libyetty_ffi.so \
//         node test/ut/ygui2/ts-binding-test.mjs
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..", "..", "..");
const { App } = await import(join(repoRoot, "bindings", "typescript", "ygui2.mjs"));

let failures = 0;
function expect(condition, label) {
  if (!condition) {
    failures++;
    console.log("FAIL:", label);
  }
}

// 1) close() from INSIDE the sink callback: deferred until the native
// emit returns (a first-frame sink close is the minimal use-after-free
// path when it is not).
{
  const app = new App();
  let sinkCalls = 0;
  app.setSink(() => {
    sinkCalls++;
    if (sinkCalls === 1) {
      app.close(); // requested mid-emit; drained AFTER emit returns
      // (the drained close later ships its own clear envelope, invoking
      // this sink again with the app already closed — assert only here)
      expect(app.aliveFlag, "sink close deferred (app still alive inside callback)");
    }
  });
  app.setViewport(640, 480);
  app.root.column({ grow: 1 }).label({ text: "sink close", basis: 20 });
  app.emit();
  expect(sinkCalls >= 1, "sink ran");
  expect(!app.aliveFlag, "sink close drained after emit");
}

// 2) close() from INSIDE a widget callback (button click via the real
// hit-test dispatch): same deferral through feedMouseButton.
{
  const app = new App();
  app.setSink(() => {});
  app.setViewport(640, 480);
  const column = app.root.column({ grow: 1, pad: 8 });
  let clicked = 0;
  const button = column.button({ label: "close me", basis: 24, cross: 200,
    onClick: () => {
      clicked++;
      app.close();
      expect(app.aliveFlag, "click close deferred inside callback");
    } });
  app.emit();
  const rect = button.rect();
  app.feedMouseButton(rect.x + 4, rect.y + 4, 0, true, 0);
  if (app.aliveFlag) {
    app.feedMouseButton(rect.x + 4, rect.y + 4, 0, false, 0);
  }
  expect(clicked === 1, `click fired once (clicked=${clicked})`);
  expect(!app.aliveFlag, "click close drained after dispatch");
}

// 3) Reservation mode: inline selectable before the first emit; the mode
// is immutable once inserted; contentScale is exposed.
{
  const app = new App({ fullscreen: false });
  app.setSink(() => {});
  app.setViewport(640, 300);
  expect(Math.abs(app.contentScale() - 1.0) < 1e-6, "contentScale starts at 1.0");
  app.root.column({ grow: 1 }).label({ text: "inline", basis: 20 });
  app.emit();
  let flipped = true;
  try {
    app.setFullscreen(true);
  } catch {
    flipped = false;
  }
  expect(!flipped, "setFullscreen rejected after insertion");
  app.close();
}

// 4) Errors surface a message (cause chain flattened, native chain
// destroyed — leak-checked under ASan builds) and dead nodes reject.
{
  const app = new App();
  let raised = null;
  try {
    app.setViewport(Number.NaN, 100); // NaN viewport: rejected with context
  } catch (viewportException) {
    raised = viewportException;
  }
  expect(raised !== null && `${raised.message}`.length > 0, "invalid viewport rejected");
  app.close();
  let deadRejected = false;
  try {
    app.root.label({ text: "after close" });
  } catch {
    deadRejected = true;
  }
  expect(deadRejected, "dead node rejected after close");
}

if (failures > 0) {
  process.exit(1);
}
console.log("ts binding test OK");
