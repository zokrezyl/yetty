# yrich — documents, spreadsheets, slides (ydoc / ysheet / yslide)

`yrich` is the document-centric WYSIWYG layer: an editable object model for
rich-text documents (**ydoc**), spreadsheets (**yspreadsheet**), and slide
decks (**yslides**) that renders itself into a ydraw buffer. The three kinds
are *not* separate modules — they all live here as yclass classes. Built on
`yclass` (object model + codegen), `ydraw-list`/`ysdf` (rendering), and
libyaml (persistence).

## Class hierarchy (yclass)

Documents and their content elements are annotated yclass classes; all
dispatch goes through yclass — there is no hand-written ops vtable.

```
yrich:document                    (document.c — base)
  ├── yrich:ydoc                  (ydoc.c — paragraph-flow rich text)
  ├── yrich:spreadsheet           (spreadsheet.c — grid of cells)
  └── yrich:slides                (slides.c — deck of slides)

yrich:element                     (element.c — base)
  ├── yrich:paragraph             (ydoc.c — editable text block)
  ├── yrich:inline_image          (ydoc.c — placeholder image box)
  ├── yrich:cell                  (spreadsheet.c)
  └── yrich:shape                 (slides.c — rect/ellipse/textbox/line/arrow/image)

yrich:app                         (app.c — yapp:app subclass, window host)
```

The base document owns the element list, the selection, the operation log,
and the undo/redo history; concrete kinds override the `document_*` slots
(`render`, `apply_op`, input handlers, content-size queries) and chain up via
`yetty_yrich_super_void`. `model.yaml`, the `*.gen.c` units and the generated
public headers (`document.h`, `element.h`, `ydoc.h`, `spreadsheet.h`,
`slides.h`, `app.h`) are codegen outputs — never hand-edited (see
`../../yclass/README.md`).

## Supporting plain-C pieces

- **yrich-types.h** — POD value types: rects, packed-ABGR colours, text
  styles/runs, cell addressing, input enums.
- **yrich-selection.c** — tagged-union selection (none / element list / cell
  range / text range).
- **yrich-operation.c** — atomic document changes with inversion (so undo
  works). The POC's CRDT-ish sync layer is *not* ported: timestamps are
  simple monotonic counters and the op log is local-only.
- **yrich-command.c** — undo/redo facade; a command bundles the ops of one
  user action, default undo replays inverses.
- **yrich-yaml.c** — load/save of the POC YAML schema
  (`document.{pageWidth,margin,paragraphs[]}`, `spreadsheet.{rows,cols,…}`,
  `presentation.{slides[]}`).
- **yrich-shell.c** — ygui-decorated editor shells (toolbar + scrollarea
  `yrich_view` + statusbar) for each kind.
- **app.c** (`yrich-app.h`, entry `yetty_yrich_app_run`) — standalone window
  host: yinit + yframework + in-process yfigure container fed via the ygui
  framework's yclass slot path (no PTY, no OSC).

## Public API sketch

```c
#include <yetty/yrich/yrich.h>          /* umbrella header */

struct yetty_yclass_object_ptr_result doc = yetty_yrich_ydoc_create(ctx);
yetty_yrich_document_set_buffer(doc.value, drawable_list);
yetty_yrich_document_render(doc.value);
yetty_yrich_document_on_key_down(doc.value, YETTY_YRICH_KEY_ENTER, 0);
yetty_yrich_document_undo(doc.value);

/* Or load from YAML: */
struct yetty_yclass_object_ptr_result loaded =
    yetty_yrich_ydoc_load_yaml_file("sample.ydoc.yaml");
```

## Build targets

| target | contents |
|--------|----------|
| `yetty_yrich` | lean document model — no GPU / ygui deps |
| `yetty_yrich_app` | editor shells + window host (Linux + WebGPU only; OBJECT library bundling the platform bootstrap sources) |

Gated by `YETTY_ENABLE_FEATURE_YRICH`.

## Layout of the module

| file | role |
|------|------|
| `document.c` / `element.c` | base classes (annotated sources; each `#include`s its `.gen.c` at the foot) |
| `ydoc.c` | ydoc + paragraph + inline_image |
| `spreadsheet.c` | spreadsheet + cell |
| `slides.c` | slides + shape |
| `yrich-selection.c` / `yrich-operation.c` / `yrich-command.c` | selection / op log / undo-redo |
| `yrich-yaml.c` | YAML load/save |
| `yrich-shell.c` / `app.c` | ygui editor chrome / standalone window host |
| `model.yaml`, `*.gen.c`, `rpc.gen.c` | codegen outputs — do not edit |

## Consumers

- **tools/ydoc**, **tools/ysheet**, **tools/yslide** — thin entries that
  build or load a document and hand it to `yetty_yrich_app_run`.
- **ygui** — the `ygui:yrich_view` widget (`../ygui/widgets/yrich_view.c`)
  hosts a document inside a `ydraw_embed` and forwards pointer/key input.
- **tests** — `test/ut/yrich/geometry-test.c` pins the header-only geometry
  and style helpers.

## Status

Working editor model, ported from the C++ POC. Layout is deliberately naive
(paragraphs stack vertically); the multi-session sync layer is not ported —
`yetty_yrich_sync_cb` exists for a future sync consumer, but ops never leave
the process.

## Related

- `../../yclass/README.md` — annotation-driven class/RPC/codegen model
- [../ygui/README.md](../ygui/README.md) — widget chrome the shells compose
- [../ydraw-list/README.md](../ydraw-list/README.md) — the buffer documents
  render into
