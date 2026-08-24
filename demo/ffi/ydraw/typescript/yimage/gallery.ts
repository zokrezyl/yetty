// ydraw client interface target sketch — yimage complex drawables (TS).
// NOT RUNNABLE YET — see python/yimage/gallery.py. Asset paths are
// repo-relative. Assets: demo/assets/yimage/.
import { Drawable, DrawableList, Image, Text } from "@yetty/ydraw";

const ASSETS = "demo/assets/yimage/";

function show(...drawables: Drawable[]): void {
  const dlist = new DrawableList();
  for (const drawable of drawables) {
    dlist.add(drawable);
  }
  dlist.dcsEmit();
  dlist.destroy();
}

console.log("rose");
show(new Image(ASSETS + "rose.png"));

console.log("hero, scaled to 480px wide");
show(new Image(ASSETS + "hero.png", { width: 480, height: 270 }));

// One record among others: caption in the same envelope.
console.log("wordmark with caption");
show(new Image(ASSETS + "wordmark.png", { x: 0, y: 0, width: 320, height: 96 }),
     new Text("terminal unchained", { x: 0, y: 112, fontSize: 18,
                                      color: "#9FA7A8" }));

console.log("thumbnail strip");
const strip = new DrawableList();
["gradient.png", "rose.png", "hero.png", "wordmark.png"].forEach((name, index) => {
  strip.add(new Image(ASSETS + name, { x: index * 170, y: 0,
                                       width: 160, height: 100 }));
});
strip.dcsEmit();
strip.destroy();
