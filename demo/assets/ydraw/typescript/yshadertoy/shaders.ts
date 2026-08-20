// ydraw client interface target sketch — yshadertoy complex drawables (TS).
// NOT RUNNABLE YET — see python/yshadertoy/shaders.py. The payload IS the
// WGSL shader (mainImage contract). Assets: demo/assets/yshadertoy/.
import { readFileSync } from "node:fs";

import { DrawableList, Shadertoy } from "@yetty/ydraw";

const ASSETS = "demo/assets/yshadertoy/";

function show(shader: Shadertoy): void {
  const dlist = new DrawableList();
  dlist.add(shader);
  dlist.dcsEmit();
  dlist.destroy();
}

// Animated plasma — iTime drives it, no client-side ticking needed.
console.log("plasma");
show(new Shadertoy(ASSETS + "plasma.wgsl", { width: 560, height: 240 }));

// Swirl, taller rect.
console.log("swirl");
show(new Shadertoy(ASSETS + "swirl.wgsl", { width: 560, height: 320 }));

// Source can come from anywhere — a string works as well as a file.
console.log("palette, inline source");
show(new Shadertoy({ wgsl: readFileSync(ASSETS + "palette.wgsl", "utf8"),
                     width: 560, height: 160 }));
