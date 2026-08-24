// ydraw client interface target sketch — ymesh complex drawables (TS).
// NOT RUNNABLE YET — see python/ymesh/gltf.py. Assets: demo/assets/ymesh/.
import { DrawableList, Mesh } from "@yetty/ydraw";

const ASSETS = "demo/assets/ymesh/";

function show(mesh: Mesh): void {
  const dlist = new DrawableList();
  dlist.add(mesh);
  dlist.dcsEmit();
  dlist.destroy();
}

// Default camera (frame-all), solid shading.
console.log("duck");
show(new Mesh(ASSETS + "Duck.glb", { width: 480, height: 360 }));

// Camera posed via the same parameters the tool's orbit drag mutates.
console.log("avocado, posed camera");
show(new Mesh(ASSETS + "Avocado.glb", { width: 480, height: 360,
                                        azimuth: 0.6, elevation: 0.3,
                                        zoom: 1.4 }));

// Wireframe toggle (the tool's W key).
console.log("box, wireframe");
show(new Mesh(ASSETS + "Box.glb", { width: 320, height: 240,
                                    wireframe: true }));
