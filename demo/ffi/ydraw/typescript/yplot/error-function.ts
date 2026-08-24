// Conceptual reproduction of demo/scripts/yplot/error-function.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) erf and its complement erfc.
console.log("(1) erf(x) and erfc(x)");
show(new Plot({ width: 600, height: 240, xRange: [-3, 3], yRange: [-1.2, 2.2], functions: [
      new Function("erf(x)", { name: "erf_x", color: "#74C5A5" }),
      new Function("erfc(x)", { name: "erfc_x", color: "#F38181" }),
    ] }));

// (2) The normal CDF beside its bell-curve pdf.
console.log("(2) normal CDF with its pdf");
show(new Plot({ width: 600, height: 240, xRange: [-4, 4], yRange: [-0.1, 1.1], functions: [
      new Function("0.5*(1+erf(x/sqrt(2)))", { name: "cdf", color: "#6BA892" }),
      new Function("exp(-x*x/2)/sqrt(2*pi)", { name: "pdf", color: "#FFE66D" }),
    ] }));

// (3) erf next to tanh — two look-alike sigmoids.
console.log("(3) erf vs tanh");
show(new Plot({ width: 600, height: 240, xRange: [-3, 3], yRange: [-1.2, 1.2], functions: [
      new Function("erf(x)", { name: "erf_x", color: "#74C5A5" }),
      new Function("tanh(x)", { name: "tanh_x", color: "#556162" }),
    ] }));
