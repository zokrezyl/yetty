// Conceptual reproduction of demo/scripts/yplot/buffer-language.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) Single inline buffer rendered as a curve; its samples are spread
// across the X domain by the shader's linear-interpolation walk.
console.log("(1) inline buffer values");
show(new Plot({ width: 520, height: 160, xRange: [0, 1], yRange: [-1, 1], buffers: [
      new Buffer("data", { size: 8, values: [0, 0.3, 0.6, 0.9, 0.6, 0, -0.4, -0.2] }),
    ] }));

// (2) Buffer * expression: envelope(x) samples the buffer, multiplied by
// a high-frequency carrier.
console.log("(2) buffer * sinusoidal carrier");
show(new Plot({ width: 520, height: 160, xRange: [0, 1], yRange: [-1, 1], buffers: [
      new Buffer("envelope", { size: 8, values: [0, 0.3, 0.6, 0.9, 0.6, 0, -0.4, -0.2] }),
    ], functions: [
      new Function("envelope(x)*sin(x*60)", { name: "pulse", color: "#74C5A5" }),
    ] }));

// (3) Two buffers acting as inputs to one expression: y-over-x without a
// scatter primitive.
console.log("(3) y/x ratio of two buffers");
show(new Plot({ width: 520, height: 160, xRange: [0, 1], yRange: [0, 6], buffers: [
      new Buffer("bx", { size: 6, values: [1, 1.5, 2, 2.5, 3, 3.5] }),
      new Buffer("by", { size: 6, values: [1, 2.25, 4, 6.25, 9, 12.25] }),
    ], functions: [
      new Function("by(x)/bx(x)", { name: "ratio", color: "#FFE66D" }),
    ] }));

// (4) Animated buffer: amplitude-modulated by `time`. Referencing `time`
// auto-subscribes the plot to the animation timer, exactly as in the DSL.
console.log("(4) time-modulated buffer (animated)");
show(new Plot({ width: 520, height: 160, xRange: [0, 1], yRange: [-1.2, 1.2], buffers: [
      new Buffer("wave", { size: 16, values: [0, 0.4, 0.7, 0.95, 1, 0.95, 0.7, 0.4, 0, -0.4, -0.7, -0.95, -1, -0.95, -0.7, -0.4] }),
    ], functions: [
      new Function("wave(x)*(0.5+0.5*sin(time*2))", { name: "live", color: "#6BA892" }),
    ] }));
