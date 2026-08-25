// Conceptual reproduction of demo/scripts/yplot/famous-functions.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}

// (1) Fourier synthesis of a square wave — the Gibbs phenomenon.
console.log("(1) Fourier square wave");
show(new Plot({ width: 600, height: 240, xRange: [-6.28, 6.28], yRange: [-1.4, 1.4], functions: [
      new Function("sign(sin(x))", { name: "target", color: "#556162" }),
      new Function("4/pi*sin(x)", { name: "first", color: "#74C5A5" }),
      new Function("4/pi*(sin(x)+sin(3*x)/3+sin(5*x)/5+sin(7*x)/7+sin(9*x)/9+sin(11*x)/11)", { name: "sum", color: "#FF6B6B" }),
    ] }));

// (2) Damped harmonic oscillator — ringdown.
console.log("(2) damped harmonic oscillator");
show(new Plot({ width: 600, height: 240, xRange: [0, 12.56], yRange: [-1.1, 1.1], functions: [
      new Function("exp(-x/4)", { name: "envelope", color: "#364A47" }),
      new Function("sin(x*3)*exp(-x/4)", { name: "ring", color: "#6BA892" }),
    ] }));

// (3) Gaussian vs Lorentzian bell curves.
console.log("(3) Gaussian vs Lorentzian");
show(new Plot({ width: 600, height: 240, xRange: [-6, 6], yRange: [-0.1, 1.15], functions: [
      new Function("exp(-x*x/2)", { name: "gaussian", color: "#6BA892" }),
      new Function("1/(1+x*x)", { name: "lorentzian", color: "#FFE66D" }),
    ] }));

// (4) Logistic sigmoid vs tanh — S-curves.
console.log("(4) logistic vs tanh");
show(new Plot({ width: 600, height: 240, xRange: [-6, 6], yRange: [-1.1, 1.1], functions: [
      new Function("1/(1+exp(-2*x))", { name: "logistic", color: "#74C5A5" }),
      new Function("tanh(x)", { name: "hyperbolic", color: "#F38181" }),
    ] }));

// (5) The catenary — a hanging chain (cosh, not a parabola).
console.log("(5) catenary");
show(new Plot({ width: 600, height: 240, xRange: [-2.5, 2.5], yRange: [0, 6.5], functions: [
      new Function("cosh(x)", { name: "chain", color: "#6BA892" }),
    ] }));

// (6) The cardinal sine — the sinc opcode patches the 0/0 at the origin.
console.log("(6) sinc");
show(new Plot({ width: 600, height: 240, xRange: [-15.7, 15.7], yRange: [-0.3, 1.1], functions: [
      new Function("sinc(x)", { name: "cardinal", color: "#74C5A5" }),
    ] }));

// (7) A wave packet: carrier under a Gaussian envelope.
console.log("(7) wave packet");
show(new Plot({ width: 600, height: 240, xRange: [-6, 6], yRange: [-1.1, 1.1], functions: [
      new Function("exp(-x*x/4)", { name: "envelope", color: "#364A47" }),
      new Function("exp(-x*x/4)*cos(x*6)", { name: "packet", color: "#6BA892" }),
    ] }));

// (8) smoothstep easing vs a linear ramp.
console.log("(8) smoothstep easing");
show(new Plot({ width: 600, height: 240, xRange: [0, 1], yRange: [-0.1, 1.1], functions: [
      new Function("x", { name: "linear", color: "#556162" }),
      new Function("smoothstep(0,1,x)", { name: "eased", color: "#74C5A5" }),
    ] }));
