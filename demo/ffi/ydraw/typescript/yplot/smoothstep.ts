// Conceptual reproduction of demo/scripts/yplot/smoothstep.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) Ease in/out versus a linear ramp.
console.log("(1) smoothstep ease vs linear ramp");
show(new Plot({ width: 600, height: 240, xRange: [0, 1], yRange: [-0.1, 1.1], functions: [
      new Function("x", { name: "linear", color: "#556162" }),
      new Function("smoothstep(0,1,x)", { name: "eased", color: "#74C5A5" }),
    ] }));

// (2) Iterated smoothstep — the "smootherstep" trick.
console.log("(2) iterated smoothstep");
show(new Plot({ width: 600, height: 240, xRange: [0, 1], yRange: [-0.1, 1.1], functions: [
      new Function("smoothstep(0,1,x)", { name: "once", color: "#5A8979" }),
      new Function("smoothstep(0,1,smoothstep(0,1,x))", { name: "twice", color: "#74C5A5" }),
    ] }));

// (3) A soft window: ramp up across one edge, back down across another.
console.log("(3) soft window");
show(new Plot({ width: 600, height: 240, xRange: [-4, 4], yRange: [-0.1, 1.1], functions: [
      new Function("smoothstep(-2,-1,x)*(1 - smoothstep(1,2,x))", { name: "gate", color: "#6BA892" }),
    ] }));

// (4) A soft staircase vs the hard floor() staircase.
console.log("(4) soft staircase vs hard floor");
show(new Plot({ width: 600, height: 240, xRange: [0, 5], yRange: [-0.2, 5.2], functions: [
      new Function("floor(x)", { name: "hard", color: "#364A47" }),
      new Function("floor(x)+smoothstep(0,1,fract(x))", { name: "soft", color: "#74C5A5" }),
    ] }));

// (5) A contrast curve: smoothstep between two interior edges remaps a
// 0..1 signal — the tone response of a contrast slider.
console.log("(5) contrast remap");
show(new Plot({ width: 600, height: 240, xRange: [0, 1], yRange: [-0.1, 1.1], functions: [
      new Function("x", { name: "identity", color: "#556162" }),
      new Function("smoothstep(0.3,0.7,x)", { name: "contrast", color: "#FFE66D" }),
    ] }));
