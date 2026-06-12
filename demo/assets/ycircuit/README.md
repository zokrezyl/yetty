# ycircuit demo assets

Sample schematics in the ycircuit DSL (see `src/yetty/ycircuit/README.md`
for the grammar). All of them render through `ycat <file>` — the `.circuit`
extension routes to the circuit handler, and the leading `circuit` keyword
makes the same content sniffable when piped through stdin.

## Real circuits

| file                     | covers                                              |
|--------------------------|-----------------------------------------------------|
| `voltage-divider.circuit`| battery (rotated, + up), vertical resistors, junction dots, tapped output label |
| `rc-lowpass.circuit`     | acsource, horizontal R / vertical C two-port, Vin/Vout labels |
| `rectifier.circuit`      | diode direction, multi-node rail, parallel C‖R branches |
| `common-emitter.circuit` | npn (three pins), vcc, bias divider, L-shaped base route |

## Coverage galleries

| file                 | covers                                                  |
|----------------------|---------------------------------------------------------|
| `symbols.circuit`    | every component kind once, with name + value labels; the `grid` directive |
| `rotations.circuit`  | a polarised symbol (diode) in all four quarter turns; vertical bodies; rotated npn |
