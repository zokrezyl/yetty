// Conceptual reproduction of demo/scripts/yplot/domain-and-view.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) Inline domain -> x_range.
console.log("(1) x in -pi..pi");
show(new Plot({ width: 520, height: 160, xRange: [-Math.PI, Math.PI], functions: [
      new Function("sin(x)", { name: "sine", color: "#FF6B6B" }),
      new Function("cos(x)", { name: "cosine", color: "#4ECDC4" }),
    ] }));

// (2) Inline x and y ranges together.
console.log("(2) x and y ranges");
show(new Plot({ width: 520, height: 160, xRange: [0, 2 * Math.PI], yRange: [-1.2, 1.2], functions: [
      new Function("sin(x)+0.3*sin(5*x)", { name: "ripple", color: "#FCBF49" }),
    ] }));

// (3) view= overrides the framing without changing the domain — zoom into
// a region without resampling the expression.
console.log("(3) view zoom-in");
show(new Plot({ width: 520, height: 160, xRange: [-10, 10], view: [-Math.PI, Math.PI, -0.5, 1.5], functions: [
      new Function("sin(x)/x", { name: "signal", color: "#74C5A5" }),
    ] }));

// (4) Wide domain, deliberately tighter viewport: evaluated across the
// full domain, only the viewport is rendered.
console.log("(4) wide eval, narrow view");
show(new Plot({ width: 520, height: 160, xRange: [-10, 10], view: [-2, 2, -1, 1], functions: [
      new Function("sin(x)*exp(-abs(x)/3)", { name: "damped", color: "#AA96DA" }),
    ] }));
