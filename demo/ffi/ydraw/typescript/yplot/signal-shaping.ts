// Conceptual reproduction of demo/scripts/yplot/signal-shaping.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) ADSR-shaped envelope drives three differently-pitched carriers; the
// same buffer is sampled by all three curves. A colored Buffer is also
// rendered as a reference curve.
console.log("(1) ADSR envelope x three harmonics");
show(new Plot({ width: 560, height: 200, xRange: [0, 1], yRange: [-1.1, 1.1], buffers: [
      new Buffer("env", { size: 16, color: "#364A47", values: [0, 0.4, 0.8, 1, 0.95, 0.85, 0.75, 0.65, 0.55, 0.45, 0.35, 0.25, 0.18, 0.12, 0.06, 0] }),
    ], functions: [
      new Function("env(x)*sin(x*6)", { name: "h1", color: "#FF6B6B" }),
      new Function("env(x)*sin(x*12)", { name: "h2", color: "#FFE66D" }),
      new Function("env(x)*sin(x*24)", { name: "h3", color: "#74C5A5" }),
    ] }));

// (2) Two control buffers driving a parametric expression; both rendered
// as faint reference curves.
console.log("(2) gain + bias driven carrier");
show(new Plot({ width: 560, height: 200, xRange: [0, 1], yRange: [-1.5, 1.5], buffers: [
      new Buffer("gain", { size: 8, color: "#556162", values: [0.1, 0.4, 0.7, 1, 1, 0.7, 0.4, 0.1] }),
      new Buffer("bias", { size: 8, color: "#9FA7A8", values: [0, 0.05, 0.1, 0.15, 0.1, 0, -0.1, -0.05] }),
    ], functions: [
      new Function("sin(x*60)*gain(x)+bias(x)", { name: "out", color: "#6BA892" }),
    ] }));

// (3) Time-animated mix: the envelope stays put, the modulator's phase is
// driven by `time` so the waveform travels across the window.
console.log("(3) static envelope, travelling phase");
show(new Plot({ width: 560, height: 200, xRange: [0, 1], yRange: [-1.1, 1.1], buffers: [
      new Buffer("env", { size: 12, color: "#5A8979", values: [0, 0.3, 0.7, 1, 0.95, 0.85, 0.7, 0.5, 0.3, 0.15, 0.05, 0] }),
    ], functions: [
      new Function("env(x)*sin(x*40 - time*4)", { name: "travel", color: "#74C5A5" }),
    ] }));
