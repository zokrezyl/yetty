// Conceptual reproduction of demo/scripts/yplot/basic.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// Single function — defaults for everything else.
console.log("basic sin(x)");
show(new Plot({ functions: [
      new Function("sin(x)"),
    ] }));

// Multi-function with explicit names + per-curve colors. The names double
// as the legend labels, so pick descriptive ones.
console.log("named curves with colors");
show(new Plot({ functions: [
      new Function("sin(x)", { name: "sine", color: "#FF6B6B" }),
      new Function("cos(x)", { name: "cosine", color: "#4ECDC4" }),
    ] }));

// Custom dimensions and axis range.
console.log("custom size and ranges");
show(new Plot({ width: 480, height: 240, xRange: [-3, 3], yRange: [-2, 10], functions: [
      new Function("x*x", { name: "parabola", color: "#FFE66D" }),
      new Function("2*x+1", { name: "line", color: "#AA96DA" }),
    ] }));

// Minimal: no grid, no axes, no labels.
console.log("minimal chrome");
show(new Plot({ noGrid: true, noAxes: true, noLabels: true, functions: [
      new Function("sin(x)*cos(3*x)"),
    ] }));

// Chained spectrum-like plot.
console.log("audio harmonics");
show(new Plot({ width: 520, height: 200, xRange: [0, 6.28], yRange: [-1, 1], functions: [
      new Function("sin(x)", { name: "first", color: "#FF6B6B" }),
      new Function("sin(2*x)/2", { name: "second", color: "#4ECDC4" }),
      new Function("sin(3*x)/3", { name: "third", color: "#AA96DA" }),
      new Function("sin(x)+sin(2*x)/2+sin(3*x)/3", { name: "sum", color: "#FCBF49" }),
    ] }));
