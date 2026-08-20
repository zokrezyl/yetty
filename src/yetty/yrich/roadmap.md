# ydoc → Google Docs Parity: Findings & Roadmap

Scope: bring `ydoc` to the **editing feature set** of the Google Docs online editor — nothing more, nothing less — while retaining native `ydoc` persistence and file synchronization. Google Docs is the editing-behavior reference, not the storage architecture.

- **Included:** Google Docs-class editing, character/paragraph formatting, document structure, page layout, navigation, embedded content, accessibility, and editing tools (see the exhaustive classification in §9).
- **Included:** native `ydoc` open/save/Save-As, lossless versioned serialization, schema migration, atomic replacement, autosave, crash recovery, journaling, and **working file synchronization** — a backend-neutral contract *plus at least one implemented backend* (see §5 Phase 5). This is file/document synchronization, not Google-style collaboration.
- **Excluded:** Gemini/AI, voice typing, Google Drive APIs/behavior, Google accounts, Workspace administration, unrelated Google integrations.
- **Deferred (separate milestone, only if separately requested):** real-time multi-user collaboration, presence cursors, comments, suggesting/review mode, sharing, and org permissions. The Phase-0 model must *enable* these without building them.

This document is **self-contained and normative**. Provenance: derived from two independent audits — a feature audit and an architecture redesign — kept under `tmp/` as transient review notes only; nothing here depends on them.

---

## 1. Blunt verdict — where ydoc is today

ydoc is a **single-page, single-font rich-text notepad** with a toolbar. It implements only a small fraction of the Google Docs editing surface (§9 gives the defensible per-feature measure) — and, more importantly, several of its *foundations* rest on assumptions that Google Docs cannot be built on. It is not "a weak Docs"; it is a smaller, different thing that needs foundational rework before feature work counts.

The single most damaging fact: **layout uses a fake monospace metric** — every glyph is assumed to be `font_size × 0.6` wide (`src/yetty/yrich/ydoc.c:100`). Real proportional glyph metrics exist in the MSDF font module but are not used, so wrapping, caret placement, hit-testing, and selection are all cell-grid approximations.

Two more foundational limits:
- **Style runs carry only `format` + `color`, not size or font** (`ydoc.c:344`) → mixed sizes/fonts within a paragraph are *unrepresentable*.
- **Undo covers only text insert/delete** — formatting, headings, alignment, paragraph split/merge are all silently non-undoable (`ydoc.c:2234–2380`).

---

## 2. Feature matrix vs Google Docs

| Area | Have | Partial / faked | Missing |
|---|---|---|---|
| **Char format** | bold\*, underline, strike, text color (per-run) | italic (in model, **not rendered**), font size (paragraph-only, not per-run), headings (faked as size+bold) | **italic render, font family, highlight color, superscript/subscript, links, clear-formatting** |
| **Paragraph** | align L/C/R | — | **justify, line spacing, space before/after, indent (incl. hanging/first-line), bullet/numbered/checklist lists + nesting, semantic H1–H6, named styles** |
| **Objects** | inline_image *element* (placeholder box) | — | **real image render/decode, image anchoring, tables, drawings, charts, horizontal rule, columns, footnotes, TOC** |
| **Editing** | text insert/delete (undoable, coalesced), caret, single-paragraph range select, arrows/home/end/word-jump, clipboard (plain text) | double-click (not wired to GUI), shift-click (broken: `mods=0`) | **cross-paragraph selection, undo of everything else, find & replace, spell check, word count, paint-format, rich clipboard, triple-click, right-click menu** |
| **Page** | continuous scroll, one page-width + margins | — | **physical pages (Letter/A4), pagination, headers/footers, page numbers, page breaks, print layout, zoom, ruler, outline pane** |
| **IO** | custom YAML save/load (paragraph flow only) | — | **import/export (docx/odt/rtf/txt/html/md/pdf)** |
| **Persistence** | YAML load/save | source-path File>Save | **atomic Save/Save-As/Open, autosave, crash recovery, schema versioning/migration, complete image/object round-trip, working file synchronization (contract + ≥1 backend)** |

\* bold is faked by drawing glyphs twice at +0.6px (`ydoc.c:739`); YAML save silently drops images, per-run size/font, and all history.

---

## 3. Detailed current-state inventory (with evidence)

### 3.1 Document data model
- **Block types:** only `paragraph` (`ydoc.c:64`) and `inline_image` (`ydoc.c:975`), under `ydoc` (`ydoc.c:1069`) → base `document` (`document.c:55`) / `element` (`element.c:43`). No heading/list/table/blockquote/code block.
- **Text model:** base `char *text` + overlaid sorted style **runs** `{start, end, style}` (`ydoc.c:68–76`, `yrich-types.h:126–130`). Mixed formatting within a paragraph works — BUT runs override only `format` flags + text `color` (`ydoc.c:344–347, 433–435`). **Font size & family are paragraph-uniform, not per-run.**
- **Character formatting:** bold (model+render, `yrich-types.h:94`), underline (`:96`), strike (`:97`), text color per-run (`:104`, `ydoc.c:2266–2294`) — supported. Italic flag exists (`:95`) but **not rendered**. Superscript/subscript flags exist (`:98–99`) but unused. `bg_color` (`:105`) and `font_id` (`:107`) are **dead fields** (no setter). **No links.**
- **Paragraph formatting:** align L/C/R (`ydoc.c:2297–2312`); **no justify** (`yrich-types.h:168–172`). Headings 0–3 derived as font-size+bold only (`ydoc.c:2314–2353`) — **no semantic heading-level field**. Line height always `font_size×1.4` (no independent line spacing). **No indent, space-before/after, lists.**
- **Structure:** **no tables** (spreadsheet is a separate document kind). Images are placeholder boxes, not anchored (`ydoc.c:1298` `(void)paragraph_index; /* future: anchor to paragraph */`), pixels not drawn (`ydoc.c:1034` `/* Placeholder until image atlasing lands */`). **No columns, page/section model, headers/footers, footnotes, comments.**

### 3.2 Editing / selection / undo
- **Operations:** enum has 12 values (`yrich-operation.h:32–45`) but ydoc only *applies* `TEXT_INSERT`/`TEXT_DELETE` (`ydoc.c:2151–2212`). `OP_TEXT_FORMAT` has **no inverse** (`yrich-operation.c:174`). `CELL_CLEAR`/`CELL_FORMAT`/`SELECTION_SET` are enum-only.
- **Undoable:** text insert/delete only, with per-burst coalescing, stacks capped at 100 (`yrich-command.c:120–161, 18`). **Not undoable:** formatting, color, alignment, heading, font-size, paragraph split (`ydoc.c:1612–1615`) & merge (`ydoc.c:1707`).
- **Confirmed history-integrity bugs (verified in current tree):** (1) command merge is fallible but returns `void` — on `realloc` failure the just-typed keystroke is already applied to the document, but the donor op is dropped, so undo cannot remove it (`yrich-command.c:144`; the comment itself admits "the keystroke was already applied"). (2) `history_undo`/`history_redo` move the command to the opposite stack **unconditionally**, even when the undo/redo *application* returned an error — desyncing model state from history (`yrich-command.c:300, 334`). Both are exactly the transactional-integrity failures foundation #4 must close.
- **Selection:** caret + single-paragraph range (anchor/focus) (`yrich-selection.c:80–98`, `ydoc.c:1490–1500`). **No multi-cursor, no cross-paragraph text selection** (Ctrl+A selects only current paragraph, `ydoc.c:1917`).
- **Keyboard:** arrows, Home/End, word-jump (Ctrl+←/→), Ctrl+Z/Y, Ctrl+A (current para), Ctrl+B/I/U, Enter (split), Tab (2 spaces), Backspace/Delete (`ydoc.c:1895–2032`). No PageUp/Down, no standalone strike shortcut.
- **Mouse:** click-caret + drag-select (`ydoc.c:1785–1848`). Double-click word-select implemented at doc layer but **not wired into `yrich_view.c`**; the view passes `mods=0` so **shift-click extend is unreachable via GUI** (`yrich_view.c:302,325,348`). No triple-click, no right-click menu.
- **Clipboard:** plain-text copy/cut/paste via yplatform, app layer only (`app.c:252–296, 384–398`). **No rich clipboard.**
- **Command layer:** **no command palette / slash commands.** `yrich-command.c` is only undo/redo machinery; `yrich-shell.c` is GUI chrome (toolbar/menus).

### 3.3 Rendering / layout / chrome
- **Layout:** real greedy word-wrap (`ydoc.c:117–155`) — but on the fixed `font_size×0.6` metric (`ydoc.c:100`), not real glyph advances. Line height computed `font_size×1.4`. Free floating-point positioning via `yetty_ydraw_drawable_list_add_text` (`ydoc.c:732`). Self-described "naive" (`ydoc.c:9`).
- **Fonts:** multiple sizes across paragraphs (not within). Only the **default MSDF font** (`font_id` always 0). Bold = "poor man's" double-draw (`ydoc.c:740`). **Italic not rendered** (`ydoc.c:739–776`).
- **Page model:** continuous canvas; "page" is only pixel width + margins (`ydoc.c:38–40, 1071–1072`). **No pages, breaks, pagination, headers/footers.**
- **WYSIWYG gaps:** italic ignored; **per-run text color is flattened to one theme color by `yrich_view_retint_text()`** (`yrich_view.c:141–161`) — toolbar colors are lost in the hosted editor; mixed font sizes within a paragraph not representable.
- **Chrome (`yrich-shell.c`):** menubar + toolbar + scrollarea + statusbar. Toolbar: Undo/Redo, +Paragraph, B/I/U/S, A+/A-, L/C/R (`:639–674`). Menus: File(New, Save), Edit(Undo/Redo), Format(B/I/U/S, larger/smaller, align, 4 fixed colors), Styles(Normal, H1/H2/H3). **No font-family dropdown, no size dropdown, no color picker, no image/table/list buttons.** Scrolling yes; **zoom/ruler/page-nav: none.**

### 3.4 Persistence / IO
- **Format:** custom YAML (`yrich-yaml.c:741–893`). Saves `document.{pageWidth,margin}` and per-paragraph `text/fontSize/color/format/align` + per-run `{start,end,format,color}`.
- **Dropped on save:** **inline images entirely** (`images[]` never visited, `yrich-yaml.c:751,783`); per-run `font_size`/`bg_color`/`font_id`; paragraph `bg_color`/`font_id`; `source_path`. (Not persisting the live undo stack is *correct* — see §3.5.)
- **Not atomic:** saver writes straight to the destination via `fopen(path,"wb")` (`yrich-yaml.c:741`) — a crash or serialization failure can truncate an existing file. No temp-file→fsync→rename, no schema version, no migration.
- **Headings** persist only implicitly as fontSize+BOLD (no semantic level).
- **CLI (`tools/ydoc/main.c`):** positional file arg opens/loads; no arg → new doc seeded with demo paragraphs. **No save-on-exit, no autosave, no Ctrl+S** — only File>Save to `source_path` or `untitled.ydoc.yaml`. No Save-As/Open/Export/Import.
- **Import/Export:** **none** for txt/md/html/docx/pdf/rtf/odt — no reader, writer, or stub.

### 3.5 Undo vs recovery vs version history — three separate facilities
These are distinct and must not be conflated:
- **In-session undo/redo history** — transient; **not** persisted in native document files (correct default; do not require the live undo stack in the `.ydoc` file unless separately requested).
- **Autosave / crash-recovery journal** — a separate side-channel; defines what a crash restores. Delivered in Phase 5.
- **User-visible version history** — a separate, explicitly-defined feature; behavior is defined independently and is a deferred concern unless requested.

### 3.6 Known stubs / markers
- `element.c:112` — base render "not implemented by this element".
- `ydoc.c:1034` — image render placeholder. `ydoc.c:1298` — image paragraph-anchoring not implemented. `ydoc.c:740` — "poor man's bold". `ydoc.c:9` — "layout is naive".
- `yrich-operation.h:6–12`, `README.md:108` — CRDT/multi-session sync layer not ported (relevant only to the *deferred* collaboration milestone, not to file synchronization).
- Dead scaffolding: `OP_TEXT_FORMAT` (no inverse), `OP_CELL_CLEAR`/`OP_CELL_FORMAT`/`OP_SELECTION_SET` (no bodies), `bg_color`/`font_id`/`FMT_SUB/SUPERSCRIPT` (unused).

---

## 4. The foundations that must come first (7)

Feature work is wasted until these land, because everything downstream depends on them.

1. **Real measured/shaped text.** Replace `font_size × 0.6` with measured glyph advances and shaped runs; rebuild wrap, caret placement, hit testing, and selection rectangles on measured layout. Provide an API boundary for grapheme segmentation, bidi/script handling, font fallback, caret stops, and IME composition (delivered incrementally is fine).
2. **Complete per-run style.** Extend runs to carry font family/face, size, foreground, highlight/background, decorations, vertical position (super/subscript), and link metadata; thread these through editing, rendering, clipboard, and serialization.
3. **Canonical `ydoc` tree.** Replace flat base-element ownership + paragraph/image alias arrays with one hierarchy (sections, blocks, inline content, tables, images, headers/footers, footnotes), backed by a stable ID index that is *derived* state and does not own nodes.
4. **Atomic transaction-only mutation.** Route every user-visible mutation through atomic invertible transactions with complete operation payloads. Fix the two confirmed history bugs (§3.2). Transactions capture selection before/after and drive dirty revisions, invalidation, journaling, and synchronization hooks. Use `replace_text(node_id, range, old, new)` as the core text primitive.
5. **Cross-block selection + editor-session state.** Represent selection as logical tree positions rather than one paragraph's byte offsets. Move caret, selection, composition, hover, and drag out of persistent nodes into a separate editor-session layer (model / layout cache / editor session / renderer).
6. **Semantic command layer + remappable keymap + modal input.** Every editor action is a **named command** (e.g. `doc.save`, `edit.undo`, `format.bold`, `caret.left`, `mode.vi_insert`) held in a command registry and invoked *identically* by keys, menus, toolbar, and (later) scripting. **Keys never call actions directly** — a **keymap** maps `(mode, key-chord) → command`, and **every binding is remappable**. Input is **modal**: the keymap is keyed on an editor mode, with **`default`, `vi-normal`, and `vi-insert`** as first-class modes (normal-mode keys bind to motion/command actions; insert-mode passes text through; switching mode is itself a command). No shortcut is ever hardcoded in an event handler. Also: preserve modifiers through `yrich_view` (fix `mods=0`), wire double/triple click and context menus, and route keyboard, pointer, clipboard, and IME through this command layer.
7. **Versioned native persistence framework.** Establish, in Phase 0, the native format framework: strict validation, snapshot serialization, **atomic temp-file → flush/fsync → rename** save, document/saved-revision tracking, a schema-version + migration mechanism, and a round-trip test harness. Every later phase then adds lossless serialization + migration coverage for every new node, style, and resource it introduces (Definition of Done #5). Persistence cannot be postponed until after the model is full of new feature types.

### First down payment (Phase 0A)
A small, testable proof of the transactional and data-integrity invariants, landed *before* the canonical-tree refactor:
1. Fix failed command merge so an already-applied keystroke never disappears from undo history (`yrich-command.c:144`).
2. Fix undo/redo so a failed application leaves the command on its original stack (`yrich-command.c:300, 334`).
3. Add allocation-failure injection for command merging.
4. Add an operation whose apply path fails, to test history-stack invariants.
5. Replace destructive direct save with atomic snapshot saving (temp → fsync → rename).
6. Add save-failure tests proving the prior destination file remains intact.

---

## 5. Phased roadmap to parity

- **Phase 0 — Foundations** (the 7 above, incl. the persistence framework and the Phase-0A down payment). No broad feature expansion; make the existing editor accurate, transactional, cross-block selectable, and losslessly + atomically serializable. *Highest leverage of the whole plan; non-negotiable before UI expansion.*
- **Phase 1 — Typography & core blocks:** italic render (load an italic face), font-family + size dropdowns, highlight, super/subscript, links, clear-format; **semantic headings + named styles**; **lists** (bullet/number/checklist + nesting); line-spacing / indent / spacing; interactive color picker; internal rich clipboard + paste-without-formatting; document-wide select-all.
- **Phase 2 — Rich content nodes:** real **images** (decode png/jpeg/svg → atlas → render → anchor inline/wrap/float, crop/mask, placement, alt-text/accessibility, save round-trip); native `ydoc` **tables** as canonical tree nodes with cell selection/insert/delete/resize/merge/split/borders/fills and table properties (reuse the spreadsheet grid's *measurement/border/render* primitives only — not its ownership or formula semantics); horizontal rule, bookmarks, equations, special-character insertion, embedded objects. *A generic break node may be introduced here for model preparation, but all pagination semantics are deferred to Phase 3.*
- **Phase 3 — Page model, sections & print:** paged/pageless modes, sections, margins/orientation, **columns**, **page/section breaks**, section-specific headers/footers, page numbers, page color/background/borders, footnotes, repeated table header rows + table pagination, TOC, outline/navigation pane, zoom, ruler, print/export-quality layout.
- **Phase 4 — Editing polish & navigation:** find & replace, spelling/grammar hooks + dictionary/personal dictionary, word count + document statistics, paint-format, external rich clipboard (HTML), drag/drop editing, context menus, nonprinting-character display, document tabs/navigation, accessibility (reading order + semantics), and keyboard-shortcut parity.
- **Phase 5 — Native persistence completion & compatibility:** complete autosave, crash-recovery journal management, and **working file synchronization** — a backend-neutral contract *plus at least one implemented backend* with: stable document + revision identity, change detection, atomic pull/push, offline-divergence handling, conflict detection with a deterministic resolution policy, recovery after interrupted synchronization, and tests covering concurrent local/external changes. Then import/export adapters (plain text, HTML, Markdown, **PDF** via the existing pdfio+ydraw, ODT/RTF, and **.docx**) as compatibility layers over the native format. This phase does **not** imitate Google Drive, and synchronization here is file/document sync, not collaboration.

**Cross-phase rule:** every phase that introduces a new node, style, or resource must ship its lossless serialization + schema migration + round-trip test in the same phase — persistence never lags the model.

**Deferred (separate milestone, not in this roadmap):** real-time collaboration, presence cursors, comments, suggesting/review mode, sharing, version-history UI. The Phase-0 transaction/operation model (stable IDs, invertible ops, sync-batch hooks) *enables* these; they are built only when separately requested.

---

## 6. Architectural leverage (why this is achievable)

The bones are better than the feature set suggests:
- **MSDF font module** → real metrics + multiple faces are within reach (foundations 1–2).
- **ydraw + pdfio** → a PDF-export path already exists (Phase 5).
- **Spreadsheet grid primitives** (measurement/border/render) may be reusable for in-document tables, but the table model stays canonical to the `ydoc` tree.
- **Command/undo machinery** → exists; just needs every mutation routed through it (foundation 4).
- **yclass / yrpc** → the substrate for a backend-neutral file-synchronization contract and its concrete backend(s), without coupling `ydoc` to any external storage or making collaboration a prerequisite.

This is why Phase 0 pays off so heavily — the substrate is there.

---

## 7. Honest scoping

Genuine Google Docs-class editing parity is a multi-quarter effort. The ordering front-loads the unblockers, and every phase after 0 ships visible, user-facing capability. The highest-leverage single investment is **Phase 0** — measured/shaped text, complete run styles, a canonical tree, atomic transactions, cross-block selection, complete input wiring, and the versioned atomic persistence framework — because it converts `ydoc` from a cell-grid notepad into a real WYSIWYG editing substrate the remaining phases build on. Native persistence is mandatory throughout: every feature is incomplete until it survives save/reload losslessly.

---

## 8. Definition of done (per feature)

An editing feature is complete only when:
1. it is represented **canonically** in the model (not derived/faked);
2. all user mutations are **atomic and fully undoable/redoable**;
3. **selection is restored** correctly by undo/redo;
4. **rendering, caret placement, hit testing, and printing agree**;
5. it **survives native save/reload without loss** (with schema migration coverage);
6. **failure** leaves model, history, selection, dirty state, and saved revision coherent;
7. **focused tests** (model, input, layout, history, round-trip) cover it.

---

## 8b. Implementation progress log

Landed increments (each meets the §8 Definition of Done unless noted):

- **Command/input plumbing (Phase 0 substrate):** semantic command registry +
  remappable modal keymap (default / vi-normal / vi-insert); the toolbar,
  menubar, and key chords all dispatch the same commands. Fixed the ygui
  `clickable` mixin so `widget_subscribe(CLICK)` fires (the entire toolbar was
  previously dead). Menus are mutually exclusive and dismiss on click-outside.
- **Document-wide select-all** + whole-document formatting as one undoable step.
- **Highlight (per-run background color)** — Phase 1. Model (`char_attrs.bg_color`
  threaded through run compress/decompress + snapshots), render wash, Format-menu
  entries (yellow/green/pink/none), atomic + undoable, YAML `bg` round-trip,
  focused test.
- **Clear formatting** — Phase 1. Strips format flags + text color + highlight
  over the selection / whole paragraph / document; atomic, undoable, tested.
- **Superscript / subscript** — Phase 1. Render at a raised/lowered baseline +
  smaller glyphs; format flags already existed so undo + serialization are free.
- **Line spacing** — Phase 1. Per-paragraph multiplier (Single / 1.5 / Double),
  single `YDOC_DEFAULT_LINE_SPACING` constant (killed the scattered `1.4f`
  magic), undoable, `lineSpacing` YAML round-trip, tested.
- **Indent (Increase / Decrease)** — Phase 1. Per-paragraph left indent
  (`YDOC_INDENT_STEP`), re-wraps content, undoable, `indent` YAML round-trip.
- **Semantic heading level** — Phase 1. `heading_level` field (0..6) stored on
  the paragraph (was faked as size+bold only), undoable, `heading` YAML round-
  trip; enables a real outline/TOC later.
- **Per-run font size** — Phase 0 foundation #2. `char_attrs.font_size` threaded
  through the run compress/decompress; `paragraph_measure` sums per-font-size
  slices so wrap/caret/hit-test/render all agree; line height fits the tallest
  run; A+/A- resize the selected run(s) (mixed sizes in one paragraph);
  undoable; `fs` per-run YAML round-trip. (This is the "real measured text"
  substrate — advances are now per-run, not one paragraph-uniform size.)
- **Font-size presets** — Phase 1. `ydoc_set_font_size` (absolute) + Format-menu
  Size 12/16/20/28, applied per-run to the selection; complements A+/A-.
- **Lists (bullet / numbered)** — Phase 1. Per-paragraph `list_kind` +
  relayout-computed ordinal, gutter markers, `ydoc_set_list` (re-apply clears),
  undoable, Format-menu entries, `list` YAML round-trip (the YAML coverage
  landed after the initial increment — the cross-phase persistence rule was
  briefly violated and is now closed), tested.
- **Justify alignment** — Phase 1. `YETTY_YRICH_HALIGN_JUSTIFY`; soft-wrapped
  lines distribute slack across their spaces via per-line `line_metrics`
  (`x_offset` + `space_extra`), and render, selection wash, caret placement,
  and hit-testing all derive x positions from one shared prefix-width helper
  so they agree (§8 DoD 4). Lines ended by '\n' and the final line keep their
  natural width. Command `para.align_justify`, Ctrl+Shift+J (plus
  Ctrl+Shift+L/E/R for the other alignments), Format-menu entry, YAML `align`
  round-trip (numeric enum), undoable, tested.
- **Standalone menu clicks un-eaten by window chrome** (bug fix). The ychrome
  caption strip (34px) overlaps the menubar row, and the standalone event loop
  fed chrome BEFORE ygui, so no menu could open in standalone mode (terminal
  mode was unaffected). Rerouted per the ychrome contract: UI first, chrome
  fallback; chrome keeps the stream for motion and while a caption-drag /
  edge-resize gesture is live (`yetty_ychrome_host_in_gesture`).
- **Checklist** — Phase 1. `list_kind` 3 + per-paragraph `list_checked`;
  SDF-drawn checkbox in the gutter (outlined box, tick when checked — no glyph
  coverage needed); checked items render struck through; `ydoc_toggle_checked`
  (undoable; no-op with no history entry on non-checklist paragraphs);
  clearing the list kind resets the checked state; gutter click toggles the
  checkbox; commands `para.list_checklist` / `para.check_toggle`, Format-menu
  entries, `list`/`checked` YAML round-trip, tested.

- **Cross-paragraph text selection** — Phase 0 foundation #5 (user-visible
  core). `selection_text` gained a `focus_element_id` (anchor and focus may
  sit in different paragraphs); the ydoc projects the span onto per-paragraph
  washes during relayout (same mechanism as select-all). Drag and shift-click
  extend across paragraphs; copy concatenates the covered slices
  newline-separated; Backspace/Delete/Enter/typing replace the span (covered
  text removed, boundary paragraphs merged, caret at the collapse point);
  every character/paragraph formatting action applies to the covered ranges
  of all spanned paragraphs as ONE undoable command. Programmatic API:
  `ydoc_select_range(anchor_para, anchor_off, focus_para, focus_off)` +
  `ydoc_place_caret`. Known gaps, deliberately deferred: span deletion is not
  undoable (structural remove/merge still has no inverse op — same as
  split/merge), shift+arrow keys extend within the focus paragraph only (they
  do not yet grow the span), and merged paragraphs keep only the surviving
  paragraph's run formatting.

- **Double-click word select + word-granularity drag** — Phase 4 editing
  polish, pulled forward. Double-click selects the whole token under the
  pointer (`paragraph_word_extent`: the word, or the punctuation/whitespace
  run) and *arms* a word-drag: while held, dragging keeps the anchor word
  wholly covered and snaps the focus end to whole-word boundaries — forward,
  backward past the anchor, and across paragraphs — exactly like every desktop
  editor. A fresh single click disarms it (back to character granularity).
  Wired the platform's double-click event through the app (UI-first, chrome
  fallback) and a new `yrich_view` `feed_double_click` (it was previously
  unwired — the doc-layer handler existed but nothing called it). Tested
  (word select, forward/backward/cross-paragraph word extension, disarm) and
  verified live.

- **Insert menu — Horizontal rule + Special characters** — Phase 2 (insert
  objects), first increment. New **Insert** menubar menu. **Horizontal rule**
  is a canonical block: `paragraph.block_kind` (0 text / 1 divider), rendered
  as a full-width SDF line, fixed-height box, never a caret target (mouse-down
  redirects to the nearest text line; typing on one is ignored). Insert splits
  the line at the caret ([head][rule][tail], caret to the tail); Backspace at a
  line start / Delete at a line end removes an adjacent rule outright.
  `block` YAML round-trip; snapshot-preserved. Structural insert/delete is
  direct (not undoable — matches split/merge until the invertible-op
  foundation). **Special characters** (em-dash, arrow, bullet, check,
  copyright, degree, euro, multiply, ellipsis) insert at the caret through the
  normal undoable text-input path. Command `insert.horizontal_rule`; tested
  (insert/delete + `block` round-trip) and verified live (menu, rule render,
  glyph insertion). Still deferred in Phase 2: real image decode/atlas/render
  (placeholder only), tables, equations, bookmarks.

- **Paragraph spacing (space before/after)** — Phase 1. Per-paragraph
  `space_before`/`space_after` px gaps added around the box in relayout;
  snapshot-undoable; `spaceBefore`/`spaceAfter` YAML round-trip; Format-menu
  "Add space before/after paragraph" + "Remove"; tested (set + undo).
- **List nesting (multilevel)** — Phase 1. Per-paragraph `list_level` (0..7);
  **Tab / Shift+Tab** on a list item nests deeper/shallower (undoable), Tab
  elsewhere still inserts spaces. Layout indents by `LIST_INDENT*(level+1)`;
  bullet glyph cycles •→◦→▪ by depth; numbered ordinals use a per-level counter
  stack so sub-lists restart at 1 while the parent level keeps counting
  (verified: 1.First / 1.Sub A / 2.Sub B / 2.Second). `listLevel` YAML
  round-trip; tested (nest in/out + undo + non-list no-op) and verified live.

- **Word count / document statistics** — Phase 4. `ydoc_word_count` (codepoints,
  codepoints excluding whitespace, whitespace-delimited words, text paragraphs;
  dividers excluded), shown live on the status-bar right side, refreshed on every
  edit (gated on the doc's exact ydoc class, since `editor_refresh` passes an
  empty kind string). The status line reads "N words  M chars (K no spaces)
  P paras" — matching the Google Docs word-count dialog's "characters excluding
  spaces". Tested (counts + the no-spaces figure is positive and ≤ total).
- **Find & replace** — Phase 4. Engine: `ydoc_find_next(query)` selects the next
  match from the caret, wrapping once; `ydoc_replace_all(query, replacement)`
  replaces every occurrence across paragraphs as ONE undoable op-command
  (delete+insert per match, recorded right-to-left so offsets stay valid).
  "Find next (selection)" wired into the Edit menu (query = current selection,
  no text-input widget needed) — verified live cycling matches across
  paragraphs. Tested (count/find/replace-all + single undo). Still open: a
  find/replace BAR with text-input fields for arbitrary queries + replace UI
  (the `textinput` widget exists; needs focus/key routing).

- **Import/export: plain text + Markdown** — Phase 5 (compatibility layers).
  New `yrich-export.c`/`.h` (plain C over the public API, no model internals).
  **Export**: text (one paragraph per line) and Markdown (`#`..`######`
  headings, `- `/`1. ` lists indented by nesting level, `- [ ]`/`- [x]`
  checklists, `---` rules, inline `**bold**` / `_italic_` / `~~strike~~` from
  style runs) via File-menu "Export as Markdown / text" (writes next to the
  source path). **Import**: text and Markdown into a fresh ydoc; the `ydoc`
  tool picks the reader by extension (`.md`/`.markdown`/`.txt` vs native
  YAML). Tested (export→import round-trip preserving heading level, nested
  list kind/level, checklist checked, rule block, and a bold run) and verified
  live — a hand-written `.md` opened with a heading, 3-level nested bullets, a
  numbered list, checklists with checked state, a rule, and bold text. Still
  deferred in Phase 5: PDF (via pdfio+ydraw), ODT/RTF, and .docx; import UI to
  replace the live document in-editor (currently open-by-arg only).

- **Import/export: HTML** — Phase 5 (compatibility layer). Same `yrich-export.c`
  path. **Export** (`yetty_yrich_ydoc_export_html_file`): a
  `<!DOCTYPE html><body>` wrapper, `<h1>`..`<h6>` for headings, `<p>` for body,
  `<hr>` for rules, inline `<b>`/`<i>`/`<s>` driven by style-run transitions,
  with `&amp;`/`&lt;`/`&gt;` escaping. File-menu "Export as HTML" writes next to
  the source path. **Import** (`yetty_yrich_ydoc_import_html_file`): a small tag
  scanner that pairs each block tag with its *matching* close (`</p>`/`</hN>`,
  skipping inline `</b>` etc.), decodes `&lt;`/`&gt;`/`&amp;`/`&quot;`, and turns
  `<b>`/`<strong>`, `<i>`/`<em>`, `<s>`/`<strike>`/`<del>` into runs; the `ydoc`
  tool opens `.html`/`.htm` by extension. Tested (round-trip preserving H3, a
  `<hr>` block, a bold run, and a `<`-in-text entity). Not a full HTML parser —
  the subset export emits.

- **Import/export: RTF** — Phase 5 (compatibility layer; the RTF slice of the
  ODT/RTF/DOCX item). Same `yrich-export.c` path, no model changes. **Export**
  (`yetty_yrich_ydoc_export_rtf_file`): `{\rtf1\ansi}` with a font table and a
  stylesheet (`\s1`..`\s6` heading styles), `\pard\sN\fsN` per paragraph
  (heading level carried by the style ref, visual size by `\fs`), a
  `\pard\brdrb\brdrs` bottom-border paragraph for horizontal rules, inline
  `\b`/`\i`/`\strike` from style runs, and `\`/`{`/`}` escaping. File-menu
  "Export as RTF" writes next to the source path. **Import**
  (`yetty_yrich_ydoc_import_rtf_file`): a control-word state machine — a group
  brace stack for format push/pop, whole-group skipping of destination groups
  (`\fonttbl`/`\colortbl`/`\stylesheet`/`\info`/`\*`/…), `\'xx` hex + `\\`/`\{`/
  `\}` literal decoding, `\sN`→heading, `\brdrb`→rule, `\par`→paragraph flush.
  The `ydoc` tool opens `.rtf` by extension. Tested (round-trip preserving H2, a
  `\brdrb` rule block, a bold run, and a literal-brace `{`-in-text escape). Still
  deferred in Phase 5: ODT and .docx (zip+XML containers), PDF (pdfio+ydraw).

- **Schema version + migration hook** — Phase 0 framework. The saver now emits a
  `version: 1` key (`YETTY_YRICH_YDOC_SCHEMA_VERSION`) ahead of `pageWidth`; the
  loader parses `version`, accepts a missing key as legacy v0, and rejects any
  document whose version exceeds the current build (with a `migrate_ydoc_document()`
  seam documented for future upgrades). Tested (saved file carries `version`, a
  legacy no-version file still loads, a version-999 file is rejected).

- **Nonprinting characters display** — Phase 4. Insert-menu "Show nonprinting
  chars" toggles `ydoc->show_nonprinting` (a view preference, not persisted);
  render draws a font-independent SDF dot centred in every space and a small
  bar at each paragraph end. Verified live. (Font-glyph middots via `add_text`
  did NOT render — the MSDF face lacks U+00B7/U+00B6 coverage — so the marks
  are SDF boxes.)

- **Inline images (real decoded pixels)** — Phase 2 (done, verified live). The
  `inline_image` element used to render a grey placeholder box; it now composites
  the actual decoded image. Enablers: (1) the yrich app creates a
  `yetty_ydraw_complex_factory` (registering the yimage + yplot concrete
  factories) and passes it in the ygrid factory args, so the ydoc render ygrid
  runs a complex pass — previously the factory was NULL, which is why images
  could never render; (2) yimage gained `yetty_yimage_emit_into(buf, bytes, len,
  config)` (a refactor sharing the decode+serialize core with `yetty_yimage_render`)
  that appends ONE yimage complex prim into an existing drawable_list at the
  config bounds; (3) `inline_image_render` reads the element's `source` file,
  decodes via stb_image, and emits the prim at the image's document bounds
  (falling back to the placeholder box when there is no decodable source), with a
  thin selection outline when selected. New accessors: `inline_image_set_source`/
  `_source`, `_set_bounds`/`_bounds`, and `ydoc_image_count`/`_image_at` for
  enumeration. YAML round-trip via an `images:` sequence of `{source, x, y, w, h}`
  (tested: source + bounds survive save/reload). Verified live — a `.ydoc.yaml`
  referencing a PNG opened with the real photo composited at its bounds
  (screenshot). Known follow-ups: the image is absolutely positioned and does not
  yet reserve vertical space in the paragraph flow (text can overlap it); an
  Insert-menu "Image…" needs the file-picker/text-input that the find/replace bar
  and link editor also wait on; resize/crop/wrap/alt-text are later Phase-2 image
  increments.
- **Tables** — Phase 2. `block_kind` 2: a rows×cols grid of owned cell strings
  on the paragraph. Rendered as SDF grid lines + per-cell text with an
  active-cell wash; height = rows × cell height. **Insert menu** → Table
  2×2 / 3×3 / 3×2 (inserts after the caret + a trailing text line). Clicking a
  cell selects it (`table_cell_at_point`); typing appends to the active cell,
  Backspace deletes a codepoint, Tab advances to the next cell (wrapping).
  YAML round-trip (`tableRows`/`tableCols`/`cells` sequence) — tested; rendered
  + cell-select verified live (a 3×3 loaded from YAML). Cell edits are direct
  (not undoable yet).
- **Table row/column insert & delete** — Phase 2. Insert-menu "Table: insert
  row / insert column / delete row / delete column" operate around the active
  cell (`table_grid_edit` moves cell strings by pointer, rebuilds the grid,
  clamps the active cell). Tested with cell-content assertions (A|_|B shift on
  insert-col, A removed on delete-col) — the content check caught an
  out-of-bounds column-placement write the dimensions-only check had missed;
  fixed. Verified live (insert column preserves the shifted data). Next table
  increments: cell merge/split and per-cell formatting.
- **Table of contents** — Phase 3 (outline/navigation). Insert-menu "Table of
  contents" collects every heading (text + level) in document order and inserts
  a bold "Contents" title followed by one entry per heading, indented by level.
  A static snapshot (like a word processor's non-updating TOC). Tested (entry
  order + count) and verified live (Introduction/Conclusion flush, Background
  one indent, History two). Live document-outline pane is the follow-up.
- **Bookmarks** — Phase 2. A per-paragraph owned `bookmark` name (freed in the
  paragraph destructor). API: `paragraph_set_bookmark`/`paragraph_bookmark`,
  the caret-level `ydoc_set_bookmark(name)` (direct, like the other structural
  markers), and `ydoc_goto_bookmark(name)` which places the caret at the start
  of the matching paragraph (returns 1 hit / 0 miss). YAML round-trip via a
  per-paragraph `bookmark:` key. Insert menu → "Bookmark (from selection)"
  (name = selected text, the no-text-input path); Edit menu → "Go to bookmark
  (selection)". Tested (save→reload preserves the bookmark; goto hits the right
  paragraph and misses a bogus name cleanly). Follow-up: link-targets-bookmark
  (a hyperlink whose destination is a bookmark rather than a URL) and a
  bookmark-name editor, both gated on the text-input popup.
- **Semantic headings H1–H6 + Title / Subtitle named styles** — Phase 1.
  Extended the heading apply to distinct sizes for levels 1–6 (was 1–3), plus
  Title (level 7, largest) and Subtitle (level 8, muted, not bold). Styles menu
  gained Heading 4/5/6 and Title/Subtitle; the TOC includes only H1–H6 (Title/
  Subtitle are not outline entries). Undoable + `heading` YAML round-trip via
  the existing path.
- **Page break** — Phase 3 prep. `block_kind` 3: a non-caret-target marker
  rendered as a dashed line in a taller gap, sharing the divider's mouse-
  redirect / typing-ignore / Backspace-Delete-adjacent-removal machinery
  (generalized to `ydoc_block_is_rule_like`). Insert-menu "Page break"; `block`
  YAML round-trip (no extra code — the block field already serializes). Tested.
  Real pagination (content actually flowing to a new page) is Phase 3.
- **Paint format (format painter)** — Phase 4. `ydoc_copy_format` captures the
  character style (format flags + colour + highlight + size) at the caret into
  a paint clipboard on the ydoc; `ydoc_paint_format` overwrites the selection's
  style (or the whole active paragraph when collapsed) via a new
  `paragraph_apply_style_range`, undoably. Format menu "Copy formatting" /
  "Paint formatting". Tested (copy bold from one word, paint onto another,
  undo reverts).
- **Wider text-colour palette** — Phase 1. Format menu text colours expanded to
  default/red/orange/green/teal/blue/purple/gray (was 4). (An interactive
  colour-picker popup is still open — the ygui `colorpicker` widget lacks a
  change-callback in its header, so it needs event wiring first.)
- **Auto-linked URLs** — Phase 1 (links, first increment). `paragraph_url_at`
  detects `http://` / `https://` / `www.` tokens at word boundaries; render
  overdraws them in brand accent blue with an underline. Verified live
  (URLs styled, surrounding words and plain text untouched, no mid-word false
  matches).
- **Editable hyperlinks** — Phase 1 (links, core increment). A `link_id` now
  rides the per-character attribute engine (`struct char_attrs` +
  `struct yetty_yrich_text_run`), so links split/merge/shift across every text
  edit exactly like formatting — no separate bookkeeping. The ydoc owns a
  runtime link table (`id → owned URL`, interning dedups); ids are never
  serialized — YAML denormalizes the URL onto each run (`link:`) and re-interns
  on load. API (all undoable via the format snapshot): `ydoc_set_link(url)` over
  the selection or the word at the caret; `ydoc_remove_link()` clears the whole
  link span at the caret (or the selection); `ydoc_link_at_caret()` resolves the
  URL for click/UI. Loader/serializer helpers: `paragraph_run_link_id`,
  `ydoc_link_url`, `ydoc_apply_run_link`. Render paints linked runs in the brand
  link colour with an underline (segments already break on `link_id`). Insert
  menu → "Link (from selection)" (URL = selected text — the no-text-input path)
  and "Remove link". Tested: set-link over a selection, run/id/URL query,
  `link_at_caret`, YAML save→reload preserving the URL, and remove-link; the full
  76-test headless suite confirms the shared run-engine change did not regress
  formatting/edit. Still open: a link editor for a URL distinct from the display
  text (needs the text-input popup, same blocker as the find/replace bar), and
  click-to-open (needs pointer hit-testing + a platform URL-open facility).
- **Find hardening** — Phase 4. Find is now case-insensitive (`ydoc_ci_equal`),
  replace-all matches case-insensitively too, and `ydoc_find_prev` selects the
  previous match (wrapping); "Find previous (selection)" wired into the Edit
  menu. Tested (case-insensitive hit + find-previous).
- **Keyboard-shortcut parity** — Phase 4. Added digit keys (`KEY_0..9`) to the
  key enum + GLFW mapping under Ctrl/Alt, and default bindings: **Ctrl+Alt+0..3**
  → Normal/Heading 1-3, **Ctrl+Shift+7/8/9** → numbered/bulleted/checklist. Tested
  via the keymap lookup.
- **Table cell keyboard navigation** — Phase 2. Arrow keys move the active cell
  (Left/Right wrap linearly; Up/Down move by a column and wrap), alongside the
  existing Tab-advances-cell. Self-contained in the key handler.
- **PageUp / PageDown caret movement** — Phase 4 (noted gap). The caret jumps
  `YDOC_PAGE_STEP_LINES` (10) visual lines up/down by stepping the vertical-
  motion primitive, crossing paragraph boundaries. Tested (PageDown lands the
  caret ~10 single-line paragraphs down).

Still open in Phase 1: per-run font FAMILY, interactive color picker, rich
clipboard, list nesting (multilevel), named paragraph styles, tab stops,
paragraph space-before/after. (Links and italic/bold-face render are now done —
see below.)

- **Real bold + italic + bold-italic glyphs** — Phase 1 (done, verified live).
  Earlier notes wrongly blamed a "missing italic MSDF face"; the faces are
  committed (`assets/fonts/DejaVuSansMNerdFontMono-Oblique.ttf` / `-BoldOblique.ttf`)
  and their atlases ship (`3rdparty/fonts/…-Oblique.cdb`, `-BoldOblique.cdb`).
  The real gap was wiring, now closed via the four-`font_id` approach (no
  shared-primitive change): the drawable-list already resolves `font_id` →
  face by ygrid font slot, so (1) `struct yetty_ygrid_factory_args` gained
  `bold_font`/`italic_font`/`bold_italic_font`, registered at slots 1/2/3 in
  `ygrid_factory_impl`; (2) the yrich app builds three extra MSDF fonts from
  the `-Bold.cdb`/`-Oblique.cdb`/`-BoldOblique.cdb` atlases (best-effort — a
  missing face just falls back) and passes them in the factory args; (3) the app
  tells the document which faces loaded via
  `yetty_yrich_ydoc_set_styled_font_mask`, mirrored to paragraphs during
  relayout; (4) `paragraph_render` picks the styled slot per run from the format
  flags + mask, using the real bold face (dropping the synthetic sub-pixel bold)
  and the real italic slant, with graceful fallback when a face is absent.
  DejaVu Sans Mono is monospace so all four faces share advances — caret /
  hit-test stay aligned even though measurement still uses the Regular metrics
  font. Verified live: a doc with BOLD / ITALIC / BOLD-ITALIC runs renders heavy,
  slanted, and heavy-slanted respectively (screenshot). Caveat: this takes
  effect in the **standalone** yrich window (its own ygrid). The in-terminal
  (figure-over-RPC) path renders through the host yetty's ygrid, which registers
  only the Regular slot — wiring the styled faces there is the follow-up.

## 9. Feature classification (exhaustive) — the completion measure

Every editor-facing Google Docs capability is classified as Included (with target phase), Deferred, or Excluded, so "nothing more, nothing less" is verifiable. Percentages are intentionally avoided; completion is measured against this checklist.

| Feature | Disposition |
|---|---|
| Character formatting (bold/italic/underline/strike/color/highlight/super/subscript/clear) | Included — Phase 1 |
| Font family & size (per run) | Included — Phase 1 |
| Links | Included — Phase 1 |
| Paragraph: alignment/justify, line & paragraph spacing, indentation, tab stops, direction | Included — Phase 1 |
| Named paragraph styles & semantic headings | Included — Phase 1 |
| Lists (bulleted/numbered/multilevel/checklist) | Included — Phase 1 |
| Paste without formatting; document-wide select-all | Included — Phase 1 |
| Images: inline/positioned, resize, crop/mask, wrapping, placement, alt-text/accessibility | Included — Phase 2 |
| Tables: create, cell select, insert/delete row-col, resize, merge/split, borders, fills, properties | Included — Phase 2 |
| Horizontal rules, bookmarks, equations, special characters, embedded objects | Included — Phase 2 |
| Repeated table header rows & table pagination behavior | Included — Phase 3 |
| Paged/pageless modes, paper size, orientation, margins | Included — Phase 3 |
| Columns; page & section breaks | Included — Phase 3 |
| Section-specific headers/footers, page numbers, footnotes | Included — Phase 3 |
| Page color/background and page borders | Included — Phase 3 |
| Table of contents, document outline/navigation, rulers, zoom | Included — Phase 3 |
| Find & replace, word count & document statistics | Included — Phase 4 |
| Spelling/grammar hooks, dictionary/personal dictionary | Included — Phase 4 |
| Paint-format, external rich (HTML) clipboard, drag/drop editing | Included — Phase 4 |
| Context menus, nonprinting-character display, document tabs/navigation | Included — Phase 4 |
| Keyboard-shortcut parity; accessibility (reading order + semantics) | Included — Phase 4 |
| Semantic command registry + remappable keybindings (keys/menus/toolbar/scripting share one command set) | Included — Phase 0 |
| Modal editing: `default` + vi `normal`/`insert` modes (keymap keyed on mode) | Included — Phase 0 substrate + mode plumbing; full vi keymap Phase 1 |
| Native open/save/Save-As, versioned lossless serialization, schema migration | Included — Phase 0 framework, completed across phases |
| Atomic save, autosave, crash-recovery journal | Included — Phase 0 (atomic) / Phase 5 (autosave, recovery) |
| File synchronization (contract + ≥1 working backend, conflict/offline/recovery) | Included — Phase 5 |
| Import/export: txt, HTML, Markdown, PDF, ODT, RTF, DOCX | Included — Phase 5 |
| Real-time collaboration, presence, comments, suggesting mode, sharing, version-history UI | Deferred — separate milestone |
| Voice typing, Gemini/AI, Google Drive/account/Workspace integrations | Excluded |
