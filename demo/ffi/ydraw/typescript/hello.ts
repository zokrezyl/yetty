// ydraw client interface target sketch — drawing from TypeScript.
// NOT RUNNABLE YET — same draw-list semantics as python/hello.py: one
// drawable list, immediate appends in call order; add() manages nothing
// and returns nothing; the user picks font ids (a field of the FONT
// record); the receiver measures content from record AABBs.
import { Box, Circle, DrawableList, Font, Segment, Star, Text } from "@yetty/ydraw";

const dlist = new DrawableList();

// Fonts: fontId is a field OF the record — the user picks it and
// references it from Text spans.
const SCORE_FONT = 7;
dlist.add(new Font({ fontId: SCORE_FONT, name: "Emmentaler" }));

// Shapes: paint prefix (z, fill, stroke, strokeWidth) + the schema's
// flattened geometry fields, exact names.
dlist.add(new Circle({ centerX: 96, centerY: 96, radius: 64,
                       fill: "#6BA892", stroke: "#364A47", strokeWidth: 2 }));
dlist.add(new Box({ centerX: 280, centerY: 96, halfWidth: 72,
                    halfHeight: 48, cornerRadius: 8, fill: "#1E262C", layer: 1 }));
dlist.add(new Star({ centerX: 460, centerY: 96, radius: 56, numPoints: 5,
                     innerRatio: 0.45, fill: "#74C5A5" }));
dlist.add(new Segment({ startX: 40, startY: 180, endX: 600, endY: 180,
                        stroke: "#9FA7A8", strokeWidth: 3 }));

// Text runs: fontId -1 (default) is the terminal's default face.
dlist.add(new Text("hello ydraw", { x: 40, y: 240, fontSize: 24,
                                    color: "#E0E5E4" }));
dlist.add(new Text("\u{1D11E}\u{1D122}", { x: 40, y: 290, fontSize: 32,
                                           fontId: SCORE_FONT,
                                           color: "#6BA892" }));

dlist.dcsEmit();     // envelope on stdout, scrolls with the text
// dlist.toBytes();  // same payload, for yscene nodeSetContent over RPC
dlist.destroy();
