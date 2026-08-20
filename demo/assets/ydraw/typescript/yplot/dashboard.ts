// Conceptual reproduction of demo/scripts/yplot/dashboard.sh
// NOT RUNNABLE YET — conceptual, translated from the python sketch.
import { DrawableList, Function, Plot, Buffer } from "@yetty/ydraw";

function show(plot: Plot): void {
  const dlist = new DrawableList();
  dlist.add(plot);
  dlist.dcsEmit();
  dlist.destroy();
}
console.log("CPU usage (last 60s, normalized)");
show(new Plot({ width: 520, height: 160, xRange: [0, 6.28], yRange: [0, 1], functions: [
      new Function("0.5+0.3*sin(x)+0.1*sin(3*x)", { name: "cpu", color: "#FF6B6B" }),
    ] }));
console.log("memory and swap");
show(new Plot({ width: 520, height: 160, xRange: [0, 6.28], yRange: [0, 1], functions: [
      new Function("0.6+0.2*sin(x/2)", { name: "mem", color: "#4ECDC4" }),
      new Function("0.1+0.05*sin(x*4)", { name: "swap", color: "#AA96DA" }),
    ] }));
console.log("network traffic (rx / tx)");
show(new Plot({ width: 520, height: 160, xRange: [0, 6.28], yRange: [-1, 1], functions: [
      new Function("sin(x)*cos(x/3)", { name: "rx", color: "#95E1D3" }),
      new Function("cos(x)*sin(x/2)", { name: "tx", color: "#FCBF49" }),
    ] }));
console.log("latency model (cubic vs linear)");
show(new Plot({ width: 520, height: 160, xRange: [-2, 2], yRange: [-4, 4], functions: [
      new Function("x*x*x", { name: "cubic", color: "#F38181" }),
      new Function("2*x", { name: "linear", color: "#72D6C9" }),
    ] }));
