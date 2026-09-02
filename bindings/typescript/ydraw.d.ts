// Type surface of @yetty/ydraw.
// GENERATED from model.yaml by tools/ffi-codegen/typescript/ffigen.py — do not edit.

export declare class Drawable {
  handle: unknown;
}

export interface FontOptions {
  name?: string;
  fontId?: number | boolean;
}

export declare class Font extends Drawable {
  constructor(options?: FontOptions);
  destroy(): void;
}

export interface TextOptions {
  body?: string;
  color?: string;
  x?: number | boolean;
  y?: number | boolean;
  fontSize?: number | boolean;
  layer?: number | boolean;
  fontId?: number | boolean;
  rotation?: number | boolean;
}

export declare class Text extends Drawable {
  constructor(primary?: string | TextOptions, options?: TextOptions);
  destroy(): void;
}

export interface ShapeOptions {
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Shape extends Drawable {
  constructor(options?: ShapeOptions);
  destroy(): void;
}

export interface CircleOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Circle extends Drawable {
  constructor(options?: CircleOptions);
  destroy(): void;
}

export interface BoxOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  halfWidth?: number | boolean;
  halfHeight?: number | boolean;
  cornerRadius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Box extends Drawable {
  constructor(options?: BoxOptions);
  destroy(): void;
}

export interface SegmentOptions {
  startX?: number | boolean;
  startY?: number | boolean;
  endX?: number | boolean;
  endY?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Segment extends Drawable {
  constructor(options?: SegmentOptions);
  destroy(): void;
}

export interface TriangleOptions {
  vertexAX?: number | boolean;
  vertexAY?: number | boolean;
  vertexBX?: number | boolean;
  vertexBY?: number | boolean;
  vertexCX?: number | boolean;
  vertexCY?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Triangle extends Drawable {
  constructor(options?: TriangleOptions);
  destroy(): void;
}

export interface EllipseOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radiusX?: number | boolean;
  radiusY?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Ellipse extends Drawable {
  constructor(options?: EllipseOptions);
  destroy(): void;
}

export interface ArcOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  apertureX?: number | boolean;
  apertureY?: number | boolean;
  radius?: number | boolean;
  thickness?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Arc extends Drawable {
  constructor(options?: ArcOptions);
  destroy(): void;
}

export interface RoundedBoxOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  halfWidth?: number | boolean;
  halfHeight?: number | boolean;
  radiusTopRight?: number | boolean;
  radiusBottomRight?: number | boolean;
  radiusTopLeft?: number | boolean;
  radiusBottomLeft?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class RoundedBox extends Drawable {
  constructor(options?: RoundedBoxOptions);
  destroy(): void;
}

export interface RhombusOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  halfWidth?: number | boolean;
  halfHeight?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Rhombus extends Drawable {
  constructor(options?: RhombusOptions);
  destroy(): void;
}

export interface PentagonOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Pentagon extends Drawable {
  constructor(options?: PentagonOptions);
  destroy(): void;
}

export interface HexagonOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Hexagon extends Drawable {
  constructor(options?: HexagonOptions);
  destroy(): void;
}

export interface StarOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radius?: number | boolean;
  numPoints?: number | boolean;
  innerRatio?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Star extends Drawable {
  constructor(options?: StarOptions);
  destroy(): void;
}

export interface PieOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  apertureX?: number | boolean;
  apertureY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Pie extends Drawable {
  constructor(options?: PieOptions);
  destroy(): void;
}

export interface RingOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  normalX?: number | boolean;
  normalY?: number | boolean;
  radius?: number | boolean;
  thickness?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Ring extends Drawable {
  constructor(options?: RingOptions);
  destroy(): void;
}

export interface HeartOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  scale?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Heart extends Drawable {
  constructor(options?: HeartOptions);
  destroy(): void;
}

export interface CrossOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  halfWidth?: number | boolean;
  halfHeight?: number | boolean;
  cornerRadius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Cross extends Drawable {
  constructor(options?: CrossOptions);
  destroy(): void;
}

export interface RoundedXOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  width?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class RoundedX extends Drawable {
  constructor(options?: RoundedXOptions);
  destroy(): void;
}

export interface CapsuleOptions {
  startX?: number | boolean;
  startY?: number | boolean;
  endX?: number | boolean;
  endY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Capsule extends Drawable {
  constructor(options?: CapsuleOptions);
  destroy(): void;
}

export interface MoonOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  offset?: number | boolean;
  radiusOuter?: number | boolean;
  radiusInner?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Moon extends Drawable {
  constructor(options?: MoonOptions);
  destroy(): void;
}

export interface EggOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radiusOuter?: number | boolean;
  radiusInner?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Egg extends Drawable {
  constructor(options?: EggOptions);
  destroy(): void;
}

export interface OctogonOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Octogon extends Drawable {
  constructor(options?: OctogonOptions);
  destroy(): void;
}

export interface HexagramOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Hexagram extends Drawable {
  constructor(options?: HexagramOptions);
  destroy(): void;
}

export interface PentagramOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Pentagram extends Drawable {
  constructor(options?: PentagramOptions);
  destroy(): void;
}

export interface LinearGradientBoxOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  halfWidth?: number | boolean;
  halfHeight?: number | boolean;
  cornerRadius?: number | boolean;
  gradX0?: number | boolean;
  gradY0?: number | boolean;
  gradX1?: number | boolean;
  gradY1?: number | boolean;
  color0?: number | boolean;
  color1?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class LinearGradientBox extends Drawable {
  constructor(options?: LinearGradientBoxOptions);
  destroy(): void;
}

export interface RadialGradientBoxOptions {
  centerX?: number | boolean;
  centerY?: number | boolean;
  halfWidth?: number | boolean;
  halfHeight?: number | boolean;
  cornerRadius?: number | boolean;
  gradCx?: number | boolean;
  gradCy?: number | boolean;
  gradRadius?: number | boolean;
  colorInner?: number | boolean;
  colorOuter?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class RadialGradientBox extends Drawable {
  constructor(options?: RadialGradientBoxOptions);
  destroy(): void;
}

export interface Sphere3dOptions {
  positionX?: number | boolean;
  positionY?: number | boolean;
  positionZ?: number | boolean;
  radius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Sphere3d extends Drawable {
  constructor(options?: Sphere3dOptions);
  destroy(): void;
}

export interface Box3dOptions {
  positionX?: number | boolean;
  positionY?: number | boolean;
  positionZ?: number | boolean;
  halfSizeX?: number | boolean;
  halfSizeY?: number | boolean;
  halfSizeZ?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Box3d extends Drawable {
  constructor(options?: Box3dOptions);
  destroy(): void;
}

export interface Torus3dOptions {
  positionX?: number | boolean;
  positionY?: number | boolean;
  positionZ?: number | boolean;
  majorRadius?: number | boolean;
  minorRadius?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Torus3d extends Drawable {
  constructor(options?: Torus3dOptions);
  destroy(): void;
}

export interface Cylinder3dOptions {
  positionX?: number | boolean;
  positionY?: number | boolean;
  positionZ?: number | boolean;
  radius?: number | boolean;
  halfHeight?: number | boolean;
  fill?: string;
  stroke?: string;
  id?: number | boolean;
  layer?: number | boolean;
  strokeWidth?: number | boolean;
}

export declare class Cylinder3d extends Drawable {
  constructor(options?: Cylinder3dOptions);
  destroy(): void;
}

export interface CurveOptions {
  name?: string;
  color?: string;
}

export declare class Curve extends Drawable {
  constructor(options?: CurveOptions);
  destroy(): void;
}

export interface FunctionOptions {
  body?: string;
  name?: string;
  color?: string;
}

export declare class Function extends Drawable {
  constructor(primary?: string | FunctionOptions, options?: FunctionOptions);
  destroy(): void;
}

export interface BufferOptions {
  values?: number[];
  size?: number | boolean;
  name?: string;
  color?: string;
}

export declare class Buffer extends Drawable {
  constructor(primary?: string | BufferOptions, options?: BufferOptions);
  destroy(): void;
}

export interface PlotOptions {
  expression?: string;
  functions?: Function[];
  title?: string;
  xLabel?: string;
  yLabel?: string;
  size?: [number, number];
  xRange?: [number, number];
  yRange?: [number, number];
  buffers?: Buffer[];
  view?: [number, number, number, number] | [[number, number], [number, number]];
  noGrid?: number | boolean;
  noAxes?: number | boolean;
  noLabels?: number | boolean;
  x?: number | boolean;
  y?: number | boolean;
  width?: number | boolean;
  height?: number | boolean;
  layer?: number | boolean;
  id?: number | boolean;
  chromeGroup?: number | boolean;
}

export declare class Plot extends Drawable {
  constructor(primary?: string | PlotOptions, options?: PlotOptions);
  destroy(): void;
}

export interface ImageOptions {
  path?: string;
  x?: number | boolean;
  y?: number | boolean;
  width?: number | boolean;
  height?: number | boolean;
  layer?: number | boolean;
  id?: number | boolean;
}

export declare class Image extends Drawable {
  constructor(primary?: string | ImageOptions, options?: ImageOptions);
  destroy(): void;
}

export interface MeshOptions {
  glb?: string;
  x?: number | boolean;
  y?: number | boolean;
  width?: number | boolean;
  height?: number | boolean;
  azimuth?: number | boolean;
  elevation?: number | boolean;
  zoom?: number | boolean;
  wireframe?: number | boolean;
  layer?: number | boolean;
  id?: number | boolean;
}

export declare class Mesh extends Drawable {
  constructor(primary?: string | MeshOptions, options?: MeshOptions);
  destroy(): void;
}

export interface ShadertoyOptions {
  source?: string;
  wgslPath?: string;
  x?: number | boolean;
  y?: number | boolean;
  width?: number | boolean;
  height?: number | boolean;
  layer?: number | boolean;
  id?: number | boolean;
}

export declare class Shadertoy extends Drawable {
  constructor(primary?: string | ShadertoyOptions, options?: ShadertoyOptions);
  destroy(): void;
}

export declare function hasVideo(): boolean;

export interface VideoOptions {
  h264?: string;
  x?: number | boolean;
  y?: number | boolean;
  width?: number | boolean;
  height?: number | boolean;
  id?: number | boolean;
  videoW?: number | boolean;
  videoH?: number | boolean;
  fps?: number | boolean;
  layer?: number | boolean;
}

export declare class Video extends Drawable {
  constructor(primary?: string | VideoOptions, options?: VideoOptions);
  destroy(): void;
}

export declare class DrawableList {
  constructor();
  add(drawable: Drawable): void;
  endGroup(): void;
  dcsEmit(): void;
  destroy(): void;
}
