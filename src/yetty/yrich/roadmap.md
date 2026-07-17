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
