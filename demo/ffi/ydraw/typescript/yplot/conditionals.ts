// Conceptual reproduction of demo/scripts/yplot/conditionals.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) Piecewise: a parabola for x<0, a sine for x>=0.
console.log("(1) piecewise  x<0 ? x^2 : sin(4x)");
show(new Plot({ width: 560, height: 240, xRange: [-3, 3], yRange: [-1.2, 2.2], functions: [
      new Function("select(x*x, sin(4*x), ge(x,0))", { name: "piecewise", color: "#74C5A5" }),
    ] }));

// (2) A rectangular pulse: 1 where |x|<1, else 0 — the boxcar window.
console.log("(2) boxcar pulse [|x| < 1]");
show(new Plot({ width: 560, height: 240, xRange: [-3, 3], yRange: [-0.2, 1.2], functions: [
      new Function("lt(abs(x),1)", { name: "boxcar", color: "#FFE66D" }),
    ] }));

// (3) ReLU and the Heaviside step.
console.log("(3) ReLU max(x,0) and Heaviside step");
show(new Plot({ width: 560, height: 240, xRange: [-3, 3], yRange: [-0.5, 3], functions: [
      new Function("max(x,0)", { name: "relu", color: "#6BA892" }),
      new Function("gt(x,0)", { name: "heaviside", color: "#F38181" }),
    ] }));

// (4) A staircase from summed steps: comparisons compose into quantizers.
console.log("(4) threshold staircase");
show(new Plot({ width: 560, height: 240, xRange: [-3, 3], yRange: [-0.2, 4.2], functions: [
      new Function("ge(x,-2)+ge(x,-1)+ge(x,0)+ge(x,1)+ge(x,2)", { name: "stairs", color: "#74C5A5" }),
    ] }));
