// ygui2 from TypeScript — interactive counter. RUNNABLE inside a yetty
// pane:
//
//     cd demo/ffi/ygui2/typescript && npm install
//     node counter.ts
//
// The full input round-trip in TypeScript: the pane forwards mouse
// envelopes, the toolkit hit-tests and dispatches, the callback mutates
// a label, and ONE addressed reopen ships back. A slider drives a
// progress bar the same way. Wheel over the scrollarea to see clipped
// offset-only scrolling. Ctrl-C quits (also `q` while no text input
// holds focus).
import { App, type Node } from "@yetty/ydraw/ygui2";

const app = new App();
let clicks = 0;

const column = app.root.column({ grow: 1, gap: 8, pad: 16 });
column.label({ text: "ygui2 counter — TypeScript callbacks over the wire",
  fg: "#74C5A5", basis: 24 });

const counterLabel = column.label({ text: "clicks: 0", basis: 20 });

column.button({ label: "click me", basis: 24, cross: 220,
  onClick: () => {
    clicks++;
    counterLabel.setText(`clicks: ${clicks}`);
  } });

const mirrorRow = column.row({ basis: 24, gap: 10 });
mirrorRow.label({ text: "slider", fg: "#9FA7A8", basis: 90 });
const bar = mirrorRow.progress({ value: 0.35, basis: 160, cross: 12 });
mirrorRow.slider({ value: 0.35, basis: 160,
  onChange: (node: Node) => bar.setValue(node.sliderValue()) });

column.checkbox({ label: "wheel scroll below", basis: 24, cross: 220 });
const scroll = column.scrollarea({ wheelStep: 24.0, maxScroll: 500.0,
  basis: 150, cross: 360, gap: 4 });
for (let line = 0; line < 12; line++) {
  scroll.label({
    text: `scrollable row ${String(line).padStart(2, "0")} — offsets only, no repaint`,
    fg: line % 2 === 0 ? "#6BA892" : "#9FA7A8",
    basis: 48 });
}

column.column({ grow: 1.0 });
column.statusbar({ left: "counter.ts — click, drag, wheel", right: "Ctrl-C: quit", basis: 24 });

await app.run();
