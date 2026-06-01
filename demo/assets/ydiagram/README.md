# ydiagram demo assets

Sample Mermaid diagrams used to exercise every code path in `yetty_ydiagram`.

## Shape coverage

Each `shapes-*.mmd` focuses on one node-shape token from the Mermaid
grammar so you can see exactly what that SDF primitive looks like in
isolation:

| file                         | shape         | mermaid syntax    |
|------------------------------|---------------|-------------------|
| `shapes-rectangle.mmd`       | rectangle     | `A[label]`        |
| `shapes-rounded-rect.mmd`    | rounded rect  | `A(label)`        |
| `shapes-circle.mmd`          | circle        | `A((label))`      |
| `shapes-diamond.mmd`         | diamond       | `A{label}`        |
| `shapes-hexagon.mmd`         | hexagon       | `A{{label}}`      |
| `shapes-cylinder.mmd`        | cylinder      | `A[(label)]`      |
| `shapes-stadium.mmd`         | stadium / pill| `A([label])`      |
| `shapes-parallelogram.mmd`   | parallelogram | `A[/label/]` etc. |
| `shapes-all.mmd`             | all of above  | mixed             |

## Layout and edge coverage

| file                  | covers                                            |
|-----------------------|---------------------------------------------------|
| `edges-arrows.mmd`    | every edge style (solid/dashed/thick, labels)     |
| `directions.mmd`      | header direction transform (try `TD/BT/LR/RL`)    |
| `subgraphs.mmd`       | `subgraph … end` clusters                         |
| `cycle.mmd`           | cycle removal phase                               |
| `long-edges.mmd`      | dummy-node insertion for multi-layer edges        |
| `state-machine.mmd`   | realistic mixed scene (flowchart approximation)   |

## Diagram families

Beyond flowcharts (`graph` / `flowchart`), the parser dispatches on the first
line's keyword to four more diagram families. One demo each:

| file                   | family         | keyword           |
|------------------------|----------------|-------------------|
| `state-diagram.mmd`    | state machine  | `stateDiagram-v2` |
| `class-diagram.mmd`    | UML class      | `classDiagram`    |
| `er-diagram.mmd`       | entity / ER    | `erDiagram`       |
| `sequence-diagram.mmd` | sequence       | `sequenceDiagram` |

state / class / ER fill the shared graph IR and flow through the Sugiyama
layout + renderer (class & ER use compartmented "record" nodes; UML and
crow's-foot arrow terminals); sequence diagrams own their layout (participant
columns + lifelines + time-ordered messages) and render straight to a buffer.

## Running

The `ydiagram` CLI (`tools/ydiagram/`) has three output modes:

```
# Default: ASCII rendering on stdout — visible in any terminal.
./build-desktop-ytrace-release/tools/ydiagram/ydiagram \
    demo/assets/ydiagram/state-machine.mmd

# --osc: emit OSC envelope into a running yetty ydraw pane.
./build-desktop-ytrace-release/tools/ydiagram/ydiagram --osc \
    demo/assets/ydiagram/state-machine.mmd

# -o: dump the raw ydraw buffer for inspection / piping.
./build-desktop-ytrace-release/tools/ydiagram/ydiagram \
    -o tmp/state-machine.ybin \
    demo/assets/ydiagram/state-machine.mmd
```
