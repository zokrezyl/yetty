// Conceptual reproduction of demo/scripts/yplot/sinc.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) The singularity, fixed: sinc(x) rides through a peak of 1 at the
// origin; bare sin(x)/x is 0/0 there.
console.log("(1) sinc(x) vs raw sin(x)/x");
show(new Plot({ width: 600, height: 240, xRange: [-15.7, 15.7], yRange: [-0.3, 1.1], functions: [
      new Function("sin(x)/x", { name: "raw", color: "#FF6B6B" }),
      new Function("sinc(x)", { name: "fixed", color: "#74C5A5" }),
    ] }));

// (2) Single-slit diffraction intensity: sinc squared.
console.log("(2) single-slit diffraction");
show(new Plot({ width: 600, height: 240, xRange: [-12.56, 12.56], yRange: [-0.05, 1.05], functions: [
      new Function("sinc(x)", { name: "amplitude", color: "#364A47" }),
      new Function("sinc(x)*sinc(x)", { name: "intensity", color: "#6BA892" }),
    ] }));

// (3) Fourier duality: wider aperture, narrower sinc.
console.log("(3) aperture width vs lobe width");
show(new Plot({ width: 600, height: 240, xRange: [-9.42, 9.42], yRange: [-0.3, 1.1], functions: [
      new Function("sinc(x)", { name: "narrow", color: "#5A8979" }),
      new Function("sinc(x*2)", { name: "wider", color: "#6BA892" }),
      new Function("sinc(x*4)", { name: "widest", color: "#74C5A5" }),
    ] }));

// (4) A Lanczos-3 resampling kernel: a sinc under a wider sinc window.
console.log("(4) Lanczos-3 kernel");
show(new Plot({ width: 600, height: 240, xRange: [-9.42, 9.42], yRange: [-0.3, 1.1], functions: [
      new Function("sinc(x/3)", { name: "window", color: "#364A47" }),
      new Function("sinc(x)*sinc(x/3)", { name: "lanczos", color: "#74C5A5" }),
    ] }));
