// Typed surface for @yetty/ydraw/ygui2 (see ygui2.mjs — the runnable
// ESM). HAND-WRITTEN alongside the module.

/** Packed 0xAABBGGRR wire color from a number or "#RRGGBB[AA]". */
export function color(value: number | string): number;

/** Flex layout options accepted by every builder (read-modify-write over
 * the C-side defaults). `pad` fans out to all four sides; `cross` is the
 * crossSize shorthand. */
export interface LayoutOptions {
  basis?: number;
  grow?: number;
  cross?: number;
  crossSize?: number;
  minMain?: number;
  gap?: number;
  pad?: number;
  padLeft?: number;
  padTop?: number;
  padRight?: number;
  padBottom?: number;
}

export interface PanelOptions extends LayoutOptions {
  bg?: number | string;
  border?: number | string;
  borderWidth?: number;
}

export interface LabelOptions extends LayoutOptions {
  text?: string;
  fg?: number | string;
}

export interface ButtonOptions extends LayoutOptions {
  label?: string;
  onClick?: () => void;
}

export interface CheckboxOptions extends LayoutOptions {
  label?: string;
  checked?: boolean;
  onToggle?: (node: Node) => void;
}

export interface ToggleOptions extends LayoutOptions {
  label?: string;
  checked?: boolean;
  onToggle?: (node: Node) => void;
}

export interface RadioOptions extends LayoutOptions {
  label?: string;
  group?: RadioGroup;
  selected?: boolean;
  onSelect?: (index: number) => void;
}

export interface SliderOptions extends LayoutOptions {
  value?: number;
  minimum?: number;
  maximum?: number;
  onChange?: (node: Node) => void;
}

export interface SpinnerOptions extends LayoutOptions {
  value?: number;
  minimum?: number;
  maximum?: number;
  step?: number;
  onChange?: (node: Node) => void;
}

export interface ProgressOptions extends LayoutOptions {
  value?: number;
  accent?: number | string;
}

export interface ChipOptions extends LayoutOptions {
  label?: string;
  selectable?: boolean;
  selected?: boolean;
  onToggle?: (node: Node) => void;
}

export interface StatusbarOptions extends LayoutOptions {
  left?: string;
  right?: string;
}

export interface StepperOptions extends LayoutOptions {
  count?: number;
  current?: number;
}

export interface TextinputOptions extends LayoutOptions {
  text?: string;
  placeholder?: string;
  onSubmit?: (node: Node) => void;
  onChange?: (node: Node) => void;
}

export interface DropdownOptions extends LayoutOptions {
  items?: string[];
  selected?: number;
  onChange?: (index: number) => void;
}

export interface ScrollareaOptions extends LayoutOptions {
  wheelStep?: number;
  maxScroll?: number;
}

export interface TableOptions extends LayoutOptions {
  columns?: string[];
  widths?: number[];
}

export interface DialogOptions extends LayoutOptions {
  title?: string;
  x?: number;
  y?: number;
  width?: number;
  height?: number;
  onClose?: () => void;
}

export interface TooltipOptions extends LayoutOptions {
  text?: string;
  x?: number;
  y?: number;
  width?: number;
  height?: number;
}

export interface RunOptions {
  tick?: () => void;
  tickMs?: number;
}

export declare class Node {
  row(options?: LayoutOptions): Node;
  column(options?: LayoutOptions): Node;
  panel(options?: PanelOptions): Node;
  label(options?: LabelOptions): Node;
  separator(options?: LayoutOptions): Node;
  button(options?: ButtonOptions): Node;
  checkbox(options?: CheckboxOptions): Node;
  toggle(options?: ToggleOptions): Node;
  radio(options?: RadioOptions): Node;
  slider(options?: SliderOptions): Node;
  spinner(options?: SpinnerOptions): Node;
  progress(options?: ProgressOptions): Node;
  chip(options?: ChipOptions): Node;
  statusbar(options?: StatusbarOptions): Node;
  stepper(options?: StepperOptions): Node;
  textinput(options?: TextinputOptions): Node;
  dropdown(options?: DropdownOptions): Node;
  scrollarea(options?: ScrollareaOptions): Node;
  table(options?: TableOptions): Node;

  layout(options: LayoutOptions): Node;
  setPosition(positionX: number, positionY: number): Node;
  setSize(width: number, height: number): Node;
  setVisible(visible: boolean): Node;
  rect(): { x: number; y: number; w: number; h: number };
  remove(): void;

  setText(text: string): Node;
  setValue(value: number): Node;
  sliderValue(): number;
  spinnerValue(): number;
  checkboxChecked(): boolean;
  toggleChecked(): boolean;
  inputText(): string;
  status(options: { left?: string; right?: string }): Node;
  stepperCurrent(current: number): Node;
  setColumns(columns: string[], widths?: number[]): Node;
  clearRows(): Node;
  addRow(cells: string[]): Node;
}

export declare class RadioGroup {
  constructor();
}

export interface AppOptions {
  /** false selects the inline reservation mode (declared-height reserve
   * in the scrollback flow) before the first emit; default fullscreen. */
  fullscreen?: boolean;
}

export declare class App {
  constructor(options?: AppOptions);
  readonly root: Node;

  /** Reservation mode — must be chosen BEFORE the first emit; the C side
   * rejects the call once inserted. */
  setFullscreen(fullscreen: boolean): void;
  /** The committed HiDPI input divisor. */
  contentScale(): number;

  dialog(options?: DialogOptions): Node;
  tooltip(options?: TooltipOptions): Node;

  run(options?: RunOptions): Promise<void>;
  quit(): void;
  close(): void;

  setSink(sink: (payload: Buffer) => void): void;
  emit(): void;
  setViewport(width: number, height: number): void;
  feedMouseButton(mouseX: number, mouseY: number, button: number, pressed: boolean,
    mods: number): void;
  feedMouseScroll(mouseX: number, mouseY: number, wheelDy: number): void;
}
