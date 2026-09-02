// ygui2 from TypeScript — the widget catalog (a ygreeter2 port).
// RUNNABLE inside a yetty pane:
//
//     cd demo/ffi/ygui2/typescript && npm install
//     node catalog.ts
//
// Every phase-6 widget wired from TypeScript: chips, a toggle driving an
// overlay tooltip, a radio group driving a stepper, slider→progress
// binding, a spinner, a textinput that greets on Enter, a dropdown, a
// dialog in the overlay, and a statusbar mirroring every event.
// Tab/Shift-Tab walk focus, Esc closes overlays, Ctrl-C quits (also `q`
// while no text input holds focus).
import { App, RadioGroup, type Node } from "@yetty/ydraw/ygui2";

const app = new App();

const column = app.root.column({ grow: 1, gap: 10, pad: 16 });

const titleRow = column.row({ basis: 28, gap: 10 });
titleRow.label({ text: "ygui2 catalog — TypeScript edition", fg: "#74C5A5", basis: 260 });
["drawable", "contract", "toolkit"].forEach((chipText, index) => {
  titleRow.chip({ label: chipText, selectable: true, selected: index === 0,
    basis: 76, cross: 22 });
});

column.separator({ basis: 8 });

let status: Node | null = null; // created last; the closures capture the slot

function show(text: string): void {
  if (status !== null) {
    status.status({ left: text });
  }
}

const tooltip = app.tooltip({ text: "the toggle controls me", x: 150, y: 66 });

const switchRow = column.row({ basis: 28, gap: 10 });
switchRow.label({ text: "switches", fg: "#9FA7A8", basis: 110 });
switchRow.toggle({ label: "tooltip", basis: 120,
  onToggle: (node: Node) => {
    const checked = node.toggleChecked();
    tooltip.setVisible(checked);
    show(checked ? "toggle: on" : "toggle: off");
  } });
let stepper: Node | null = null;
const group = new RadioGroup();
for (let option = 0; option < 3; option++) {
  switchRow.radio({ label: `opt ${option + 1}`, group, selected: option === 0,
    basis: 90,
    onSelect: (index: number) => {
      stepper?.stepperCurrent(index);
      show(`radio: option ${index + 1}`);
    } });
}
stepper = switchRow.stepper({ count: 3, current: 0, basis: 80 });

const valueRow = column.row({ basis: 28, gap: 10 });
valueRow.label({ text: "values", fg: "#9FA7A8", basis: 110 });
let bar: Node | null = null;
valueRow.slider({ value: 0.35, basis: 160,
  onChange: (node: Node) => {
    const value = node.sliderValue();
    bar?.setValue(value);
    show(`slider: ${Math.round(value * 100)}%`);
  } });
bar = valueRow.progress({ value: 0.35, basis: 140, cross: 12 });
valueRow.spinner({ value: 3, minimum: 0, maximum: 10, step: 1, basis: 110,
  onChange: (node: Node) => show(`spinner: ${node.spinnerValue()}`) });

const entryRow = column.row({ basis: 28, gap: 10 });
entryRow.label({ text: "entry", fg: "#9FA7A8", basis: 110 });
entryRow.textinput({ placeholder: "type a name, Enter greets", basis: 180,
  onSubmit: (node: Node) => show(`hello, ${node.inputText() || "stranger"}`) });

entryRow.dropdown({ items: ["plasma", "aurora", "nebula"], basis: 130,
  onChange: (index: number) => show(`dropdown: item ${index + 1}`) });

const dialog = app.dialog({ title: "about the catalog", x: 140, y: 90, width: 300, height: 150,
  onClose: () => show("dialog closed") });
dialog.label({ text: "every widget, one wire contract", basis: 20 });
entryRow.button({ label: "open dialog", basis: 110,
  onClick: () => {
    dialog.setVisible(true);
    show("dialog opened");
  } });

column.column({ grow: 1.0 });
status = column.statusbar({ left: "ready",
  right: "Tab: focus  Esc: close  Ctrl-C: quit", basis: 24 });

await app.run();
