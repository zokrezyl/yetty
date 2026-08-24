// ydraw client interface target sketch — plots as complex drawables (TS).
// NOT RUNNABLE YET — see python/plot.py. A Plot is a drawable like any
// shape; add(plot) packs one binary yplot complex record. Subplots are
// rect arithmetic: several records at computed bounds.
import { Buffer, DrawableList, Function, Plot } from "@yetty/ydraw";

const dlist = new DrawableList();

// One plot, two symbolic curves.
dlist.add(new Plot({ x: 0, y: 0, width: 800, height: 240,
                     title: "harmonics", xRange: [-6.28, 6.28],
                     functions: [
                       new Function("sin(x)", { name: "first", color: "#6BA892" }),
                       new Function("sin(3*x)/3", { name: "third", color: "#74C5A5" }),
                     ] }));

// Data-driven: samples travel as a named buffer in the record.
dlist.add(new Plot({ x: 0, y: 260, width: 800, height: 180,
                     title: "measured", noGrid: true,
                     buffers: [
                       new Buffer("load", { values: [1.0, 1.4, 1.2, 2.1, 1.9, 2.8],
                                            color: "#E0E5E4" }),
                     ] }));

// A 2x2 subplot grid is rect arithmetic, nothing more.
["sin(x)", "cos(x)", "sin(x)*x", "1/x"].forEach((body, index) => {
  const column = index % 2;
  const row = Math.floor(index / 2);
  dlist.add(new Plot({ x: column * 400, y: 460 + row * 90,
                       width: 390, height: 80, noAxes: true,
                       functions: [new Function(body, { color: "#5A8979" })] }));
});

dlist.dcsEmit();
dlist.destroy();
