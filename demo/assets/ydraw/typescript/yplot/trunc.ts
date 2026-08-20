// Conceptual reproduction of demo/scripts/yplot/trunc.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) The rounding family side by side — they split left of the origin.
console.log("(1) trunc vs floor vs round");
show(new Plot({ width: 600, height: 240, xRange: [-3, 3], yRange: [-3.5, 3.5], functions: [
      new Function("trunc(x)", { name: "truncated", color: "#74C5A5" }),
      new Function("floor(x)", { name: "floored", color: "#FF6B6B" }),
      new Function("round(x)", { name: "rounded", color: "#FFE66D" }),
    ] }));

// (2) A symmetric sawtooth: x - trunc(x) keeps the sign of x.
console.log("(2) signed sawtooth vs fract");
show(new Plot({ width: 600, height: 240, xRange: [-3, 3], yRange: [-1.1, 1.1], functions: [
      new Function("x-trunc(x)", { name: "signed_saw", color: "#6BA892" }),
      new Function("fract(x)", { name: "fract_saw", color: "#556162" }),
    ] }));

// (3) A quantizer / ADC: a sine snapped onto discrete steps.
console.log("(3) quantizer");
show(new Plot({ width: 600, height: 240, xRange: [0, 6.28], yRange: [-1.1, 1.1], functions: [
      new Function("sin(x)", { name: "signal", color: "#364A47" }),
      new Function("trunc(sin(x)*4)/4", { name: "quantized", color: "#74C5A5" }),
    ] }));

// (4) A bit-crushed ramp: sample-and-hold staircase.
console.log("(4) bit-crushed ramp");
show(new Plot({ width: 600, height: 240, xRange: [0, 4], yRange: [-0.2, 4.2], functions: [
      new Function("x", { name: "ramp", color: "#556162" }),
      new Function("trunc(x*3)/3", { name: "crushed", color: "#6BA892" }),
    ] }));
