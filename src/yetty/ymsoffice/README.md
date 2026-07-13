# ymsoffice — Microsoft Office documents (docx / xlsx / pptx)

Parses OOXML packages into a neutral in-memory document model and renders
that model into a ydraw buffer. The split is deliberate: the **parsers know
nothing about rendering targets**, so the same model can later feed a
yrich converter (editable ydoc / spreadsheet / slides) without touching the
format code.

```
bytes ─► opc.c (ZIP) ─► docx.c / xlsx.c / pptx.c ─► model.h ─► render.c ─► ydraw buffer
                              (yxml SAX)             (neutral)        (ycat / terminal-mime)
```

## Files

| File | Role |
|---|---|
| `opc.c` / `opc.h` | Read-only OPC (ZIP) container: central-directory walk, stored + deflate entries via zlib. Borrows the archive bytes; no zip64. |
| `xml.c` / `xml-internal.h` | Shared yxml SAX walker: local-name ancestor stack, attribute-value accumulation, text chunks. Prefix-agnostic (OOXML producers bind `w:`/`a:`/`p:` or a default namespace). |
| `rels.c` / `rels-internal.h` | `.rels` relationship parts → id/target map. |
| `model.c` / `model.h` | The neutral model: word (blocks: paragraphs of styled runs, tables, image placeholders), sheet (sparse cells), slides (shapes with text bodies). |
| `docx.c` | `word/document.xml` (+ `styles.xml` heading map, `numbering.xml` bullet/ordered). Skips `mc:Fallback`, `w:del`, drawings-beyond-placeholder. |
| `xlsx.c` | `xl/workbook.xml` + rels + `sharedStrings.xml` + worksheets. Values resolved to display text; formulas kept verbatim; no number-format evaluation. |
| `pptx.c` | `ppt/presentation.xml` + rels + slides. Frames in points (EMU/12700), preset geometry → box/ellipse/line, solid fills, text bodies. |
| `render.c` / `render.h` | Model → ydraw. Same conventions as ymarkdown (0.6·font byte advance, brand palette). Word: wrapped paragraphs, lists with computed ordinals, bordered tables, image placeholder boxes. Sheet: bordered grid with A/1 headers, right-aligned numbers. Slides: one scaled slide panel per slide with its shapes. |

## Consumers

- **ycat** — `handler-msoffice.c` (one handler for all three types; the
  container kind is sniffed from part names). Detection: extension
  (`docx/docm`, `xlsx/xlsm`, `pptx/pptm/ppsx`), OOXML MIME strings, and a
  ZIP+part-name content sniff for pipes.
- **terminal-mime** — the `YETTY_DCS_MIME_FILE` route renders msoffice
  envelopes terminal-side (`mime_render_msoffice`), same ingest path as
  markdown. ymime carries the matching DOCX/XLSX/PPTX types.

## Known limits (v1)

- Embedded images are placeholders (name + declared extent); media bytes
  are not decoded. Text boxes inside drawings are skipped.
- No theme resolution in pptx (`schemeClr` fills are ignored); group-shape
  child transforms are not remapped.
- xlsx number formats are not evaluated (raw value text); no merged-cell
  ranges, no column-width hints.
- Layout uses the flat 0.6·font_size byte advance — same approximation as
  ymarkdown, same reasons.
- Legacy binary formats (.doc/.xls/.ppt, OLE/CFB) are out of scope.

## Tests

`test/ut/ymsoffice/` — golden test over committed fixtures
(`make-fixtures.py` regenerates them; they are hand-authored minimal
OOXML, verified loadable by LibreOffice). Pins the extracted model
structure, the emitted primitive families, and byte-exact determinism.
