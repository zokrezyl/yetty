// Conceptual reproduction of demo/scripts/yplot/angle-conversion.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) Trig on a degree axis: the x-axis runs 0..360 in degrees, the
// domain is wrapped in radians() so the curves stay correct.
console.log("(1) sin & cos over a 0..360 degree axis");
show(new Plot({ width: 600, height: 240, xRange: [0, 360], yRange: [-1.2, 1.2], functions: [
      new Function("sin(radians(x))", { name: "sine", color: "#6BA892" }),
      new Function("cos(radians(x))", { name: "cosine", color: "#74C5A5" }),
    ] }));

// (2) Slope to angle: degrees(atan(slope)) reads out a line's inclination
// directly in degrees.
console.log("(2) slope to angle");
show(new Plot({ width: 600, height: 240, xRange: [-10, 10], yRange: [-90, 90], functions: [
      new Function("degrees(atan(x))", { name: "angle", color: "#FFE66D" }),
    ] }));

// (3) A 90-degree phase shift written in the same units as the axis.
console.log("(3) 90-degree phase shift on a degree axis");
show(new Plot({ width: 600, height: 240, xRange: [0, 360], yRange: [-1.2, 1.2], functions: [
      new Function("sin(radians(x))", { name: "reference", color: "#556162" }),
      new Function("sin(radians(x - 90))", { name: "shifted", color: "#74C5A5" }),
    ] }));
