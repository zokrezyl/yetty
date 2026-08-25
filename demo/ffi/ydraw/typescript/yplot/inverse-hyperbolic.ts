// Conceptual reproduction of demo/scripts/yplot/inverse-hyperbolic.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) asinh as a signed-log compressor: linear core, log tails.
console.log("(1) asinh — signed-log compression");
show(new Plot({ width: 600, height: 240, xRange: [-10, 10], yRange: [-3.5, 3.5], functions: [
      new Function("x", { name: "identity", color: "#364A47" }),
      new Function("sign(x)*log(1+abs(x))", { name: "signed_log", color: "#556162" }),
      new Function("asinh(x)", { name: "arcsinh", color: "#74C5A5" }),
    ] }));

// (2) atanh, the Fisher z-transform — blows up at +/-1.
console.log("(2) atanh — Fisher transform");
show(new Plot({ width: 600, height: 240, xRange: [-0.99, 0.99], yRange: [-3, 3], functions: [
      new Function("atanh(x)", { name: "fisher", color: "#6BA892" }),
    ] }));

// (3) acosh, defined for x >= 1 — rapidity / catenary arc length.
console.log("(3) acosh");
show(new Plot({ width: 600, height: 240, xRange: [1, 10], yRange: [-0.2, 3.2], functions: [
      new Function("acosh(x)", { name: "arccosh", color: "#FFE66D" }),
    ] }));

// (4) The three together, each real on its own part of the domain.
console.log("(4) asinh vs atanh vs acosh");
show(new Plot({ width: 600, height: 240, xRange: [-3, 3], yRange: [-3, 3], functions: [
      new Function("asinh(x)", { name: "arcsinh", color: "#74C5A5" }),
      new Function("atanh(x)", { name: "fisher", color: "#FF6B6B" }),
      new Function("acosh(x)", { name: "arccosh", color: "#FFE66D" }),
    ] }));
