// ygui2 from TypeScript — the static hello. RUNNABLE inside a yetty
// pane (node strips the types; koffi arrives via npm install):
//
//     cd demo/ffi/ygui2/typescript && npm install
//     node hello.ts
//
// One fullscreen frame over the drawable contract: a panel root, a
// column of labels, a separator, a progress bar, and a statusbar. After
// the first envelope the app sits idle — a clean frame ships ZERO
// bytes. Resize the pane: the toolkit relays out and ships only the
// widgets whose geometry actually changed. Ctrl-C quits (also `q` while
// no text input holds focus).
import { App } from "@yetty/ydraw/ygui2";

const app = new App();

const column = app.root.column({ grow: 1, gap: 10, pad: 16 });
column.label({ text: "ygui2 — hello from TypeScript", fg: "#74C5A5", basis: 24 });
column.separator({ basis: 8 });
column.label({ text: "every widget is a wire group; this whole page was ONE envelope",
  basis: 20 });
column.label({ text: "clean frames ship zero bytes — watch the pane stay silent",
  fg: "#9FA7A8", basis: 20 });
const row = column.row({ basis: 24, gap: 10 });
row.label({ text: "progress", fg: "#9FA7A8", basis: 90 });
row.progress({ value: 0.42, basis: 220, cross: 12 });
column.column({ grow: 1.0 }); // spacer pushes the statusbar to the bottom
column.statusbar({ left: "hello.ts — static frame", right: "Ctrl-C: quit", basis: 24 });

await app.run();
