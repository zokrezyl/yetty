// Conceptual reproduction of demo/scripts/yplot/heatmaps.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) Separable interference: the standing-wave checkerboard.
console.log("(1) standing-wave checkerboard");
show(new Plot({ width: 360, height: 360, xRange: [-6.28, 6.28], yRange: [-6.28, 6.28], functions: [
      new Function("sin(x)*cos(y)", { name: "field" }),
    ] }));

// (2) Concentric ripples from a point source.
console.log("(2) radial ripples");
show(new Plot({ width: 360, height: 360, xRange: [-6, 6], yRange: [-6, 6], functions: [
      new Function("sin(3*sqrt(x*x+y*y))", { name: "field" }),
    ] }));

// (3) Two-source interference — the double-slit pattern.
console.log("(3) two-source interference");
show(new Plot({ width: 360, height: 360, xRange: [-7, 7], yRange: [-7, 7], functions: [
      new Function("0.5*(sin(4*sqrt((x-2)*(x-2)+y*y))+sin(4*sqrt((x+2)*(x+2)+y*y)))", { name: "field" }),
    ] }));

// (4) A hyperbolic saddle.
console.log("(4) hyperbolic saddle");
show(new Plot({ width: 360, height: 360, xRange: [-4, 4], yRange: [-4, 4], functions: [
      new Function("tanh(x*y)", { name: "field" }),
    ] }));

// (5) Procedural terrain via fBm: three octaves of noise2 at decorrelated
// frequencies.
console.log("(5) procedural terrain");
show(new Plot({ width: 360, height: 360, xRange: [0, 4], yRange: [0, 4], functions: [
      new Function("(0.55*noise2(x,y)+0.3*noise2(x*2.13,y*2.13)+0.15*noise2(x*4.27,y*4.27))*2-1", { name: "field" }),
    ] }));
