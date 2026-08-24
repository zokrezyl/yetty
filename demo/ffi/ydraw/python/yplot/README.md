# ydraw client interface — yplot demos, reproduced conceptually

NOT RUNNABLE YET. Each file here reproduces the matching
`demo/scripts/yplot/<name>.sh` in the target Python surface of the ydraw
client interface (design-agreement artifacts, like
`demo/ffi/ydraw/python/`).

One yplot CLI invocation = one `Plot` packed into its own `DrawableList`
and emitted as one envelope that scrolls in at the cursor — the exact
behaviour of the shell demos. Every file uses the same tiny helper:

```python
def show(plot):
    dlist = DrawableList()
    dlist.add(plot)
    dlist.dcs_emit()
    dlist.destroy()
```

## DSL / CLI → object mapping

| yplot CLI / DSL | object surface |
|---|---|
| `-w N` / `-H N` | `Plot(width=N, height=N)` |
| `--xrange=a..b` / inline `x=a..b` | `x_range=(a, b)` |
| `--yrange=a..b` / inline `y=a..b` | `y_range=(a, b)` |
| `@view=xa..xb,ya..yb` | `view=((xa, xb), (ya, yb))` — framing without changing the domain |
| `--no-grid` / `--no-axes` / `--no-labels` | `nogrid=True` / `noaxes=True` / `nolabels=True` |
| `name=expr` | `Function("expr", name="name")` — the name doubles as the legend label |
| `@name.color=#RRGGBB` | `Function(..., color="#RRGGBB")` |
| `name=buffer; @name.size=N; @name.values=v,...` | `Buffer("name", size=N, values=[v, ...])` |
| `@bufname.color=...` (buffer drawn as a curve) | `Buffer(..., color="#RRGGBB")` |

Function bodies are yexpr syntax **verbatim** — including sampling a
buffer by name (`env(x)`) and referencing `time` (which auto-subscribes
the plot to the animation timer, exactly as in the DSL). The expression
language is the content; everything around it is objects.
