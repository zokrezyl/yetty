# ycircuit — electronic circuit schematic renderer

`ycircuit` is a GPU-less yclass module (class `ycircuit:circuit`, modelled on
`ymusic:music`). It turns a circuit description into a ydraw drawable list:
wires, resistor zigzags, capacitor plates, inductor humps, source circles,
transistor bodies etc. are SDF primitives (segments, circles, triangles);
reference designators, values and free labels are MSDF text spans using the
receiver's default font. The list ships through the standard ycat /
scrolling-layer path (`ycat schematic.circuit`), so a schematic scrolls inline
in the scrollback like any other rich card.

The circuit model is a flat element list with a stable id per element —
`hit_test` / `set_highlight` give an editor selection affordance, and the
`add_component` / `add_wire` / `add_junction` / `add_label` methods build a
model programmatically. Being a yclass class, `make codegen` emits the public
header, dispatch, `model.yaml` and the FFI / host-language bindings (python,
lua), so the same surface is scriptable from other languages.

## DSL

Line-based; `#` starts a comment; coordinates are in grid units (floats
allowed); the grid pitch in px comes from the `grid` directive, else
`configure()`, else a default.

```
circuit Half-wave rectifier      # optional title (rendered above the schematic)
grid 14                          # optional px-per-grid-unit hint

# component lines: <kind> <x> <y> [<rot>] [<name>] [<value>]
#   rot: h | v | r0 | r90 | r180 | r270 (default h)
vsource   2  6  v  V1  9V
diode     8  2  h  D1  1N4148
resistor 14  6  v  R1  10k

wire 2 2  5 2                    # wire x0 y0 x1 y1 [x2 y2 ...] (polyline)
wire 11 2  14 2
wire 2 10  14 10
dot 14 10                        # junction dot
gnd 8 10
label 15.5 2 Vout                # free text (rest of line, quotes optional)
```

## Component kinds (with aliases)

| kind | aliases | symbol |
|---|---|---|
| `resistor` | `r` | ANSI zigzag |
| `capacitor` | `cap`, `c` | parallel plates |
| `inductor` | `coil`, `l` | four humps |
| `diode` | `d` | filled triangle + bar |
| `led` | | diode + emission arrows |
| `battery` | `bat` | long/short plates, `+` mark |
| `vsource` | `v` | circle with `+`/`−` |
| `isource` | `i` | circle with arrow |
| `acsource` | `ac` | circle with sine |
| `gnd` | `ground` | three bars (connection point at the element position) |
| `vcc` | | stem + bar (connection point at the element position) |
| `npn` / `pnp` | | circle, base bar, emitter arrow (base pin at `(-3,0)` local) |
| `switch` | `sw` | pivot dots + blade |
| `opamp` | | triangle, inverting input top `(-3,-1)`, non-inverting bottom `(-3,1)`, output `(3,0)` |
| `ic` | `chip` | DIP-style box with named pins per side |

### Generic ICs

The `ic` kind takes per-side pin lists as extra `l:`/`r:`/`t:`/`b:` tokens
(comma-separated names, in pin order):

```
ic 14 8 h U1 NE555 l:GND,TRIG,OUT,RESET r:VCC,DIS,THR,CV
```

The body grows to fit the longest side (pin pitch 1.6 grid units, leads
1.2 units long); pin tips therefore sit at `±(half_width + 1.2)` /
`±(half_height + 1.2)` from the centre. Pin names render inside the body
next to their pin, the designator above the body, the part number centred
inside it. Labels stay horizontal, so ICs read best unrotated. The FFI
counterpart is `add_ic(x, y, rotation_deg, name, value, pins_left,
pins_right, pins_top, pins_bottom)`.

Two-terminal components span 6 grid units pin-to-pin (pins at local
`(±3, 0)`, rotated in quarter turns around the centre; screen y grows
downward, so `v`/`r90` turns the `+x` pin axis to point down).

## Method surface

`configure(grid_px, flags)`, `parse(input, len)`, `clear()`,
`add_component(kind, x, y, rotation_deg, name, value)`,
`add_ic(x, y, rotation_deg, name, value, pins_left, pins_right, pins_top, pins_bottom)`,
`add_wire(x0, y0, x1, y1)`, `add_junction(x, y)`, `add_label(x, y, text)`,
`render()` → drawable list (caller owns), `hit_test(x, y)` → element id,
`set_highlight(id)` (−1 clears), `destroy()`. The exposed
`yetty_ycircuit_emit_osc(list, fd)` serializes a rendered list as a
YDRAW_BIN OSC envelope for CLI / binding front-ends.
