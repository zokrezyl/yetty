// Conceptual reproduction of demo/scripts/yplot/stochastic.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) White noise vs smooth value noise at the same frequency.
console.log("(1) rand() vs noise()");
show(new Plot({ width: 600, height: 240, xRange: [0, 10], yRange: [-0.1, 1.1], functions: [
      new Function("rand(x*8)", { name: "white", color: "#556162" }),
      new Function("noise(x*8)", { name: "smooth", color: "#74C5A5" }),
    ] }));

// (2) Fractal (fBm) noise: summed octaves.
console.log("(2) fractal noise");
show(new Plot({ width: 600, height: 240, xRange: [0, 6], yRange: [-0.1, 1.1], functions: [
      new Function("0.5*noise(x*2)+0.25*noise(x*4)+0.125*noise(x*8)+0.0625*noise(x*16)", { name: "fbm", color: "#6BA892" }),
    ] }));

// (3) A clean signal corrupted by additive noise.
console.log("(3) signal + additive noise");
show(new Plot({ width: 600, height: 240, xRange: [0, 6.28], yRange: [-1.3, 1.3], functions: [
      new Function("sin(x)", { name: "clean", color: "#364A47" }),
      new Function("sin(x)+0.2*(rand(x*97)*2-1)", { name: "noisy", color: "#FF6B6B" }),
    ] }));
