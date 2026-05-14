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
| `state-machine.mmd`   | realistic mixed scene                             |

## Running

The `ydiagram` CLI (`tools/ydiagram/`) turns any file here into a ypaint
OSC envelope on stdout, or a raw serialized buffer with `-o`:

```
# emit OSC envelope into a running yetty pane
./build-desktop-ytrace-release/tools/ydiagram/ydiagram \
    demo/assets/ydiagram/state-machine.mmd

# dump the raw ypaint buffer for inspection
./build-desktop-ytrace-release/tools/ydiagram/ydiagram \
    -o tmp/state-machine.ybin \
    demo/assets/ydiagram/state-machine.mmd
```
