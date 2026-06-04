# Yetty Glossary

This document defines project vocabulary. Use these terms consistently in code,
comments, generated names, and documentation.

## Why `glossary.md`

Use `glossary.md` for this file. It defines shared terms and their meanings.
`terminology.md` would also be understandable, but it sounds more like a policy
or naming-convention document. This file is a reference vocabulary.

## Core ydraw Terms

### `drawable_list`

A `drawable_list` is an ordered container/stream of ydraw entries.

It is the producer-side object used to emit ydraw content. It may contain direct
primitives, composite entries, resources, commands, and entries that lower into
other drawable-list content.

Use `drawable_list` for the container formerly called `drawable_list`.

### `primitive`

A `primitive` is a direct ygrid render atom.

Primitives are already in the form the ygrid/render staging path can place and
render. They do not need materialization into a separate scene object before
entering the primitive grid.

Examples:

- SDF shapes
- glyphs

Do not use `primitive` for fonts, text spans/text runs, or heavyweight content
such as plots/images/videos.

### `resource`

A `resource` is reusable data declared or referenced by drawable-list entries.

Resources are not visible by themselves. They are dependencies for later entries.

Examples:

- font blob
- font hash/reference
- future image/shader/cache declarations

Current mapping:

- `FONT` should be named as a font resource, not as a primitive or drawable.

### `composite`

A `composite` is a heavyweight non-primitive ydraw entry.

Composites are not lowered into ygrid primitives by the basic primitive path.
They represent higher-level content with its own payload and, eventually, a
runtime scene object/lifetime.

Examples:

- plot
- image
- video
- shader-backed content
- future yfigure-backed ydraw object

Current mapping:

- the old `raw_figure` / `complex_drawable` wire record should become
  `composite`.

### `text drawable_list`

Text spans/text runs are neither primitives nor resources nor composites.

A text entry is best described as a text-specific `drawable_list`: compact text
and layout data that resolves into an ordered list of glyph primitives after
font/resource resolution.

Use names like:

- `text_drawable_list`
- `YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST`

Avoid `primitive_array` unless the payload literally stores encoded primitive
records. Avoid `macro` because this is data, not executable behavior.

## Terms To Avoid

### .entry`

Do not use .entry` in public ydraw/domain naming.

It is an implementation pattern, not a ydraw concept. The old.entry registry
handled multiple categories, including primitives, resources, commands, and
composites, so replacing it with `primitive` would also be wrong.

Preferred names:

- `drawable_list_entry`
- `drawable_list_registry`
- `drawable_list_entry_ops`

### `figure` In ydraw

Avoid `figure` in ydraw names.

`yfigure` already owns the term `figure` for the scene/tree object model: rect,
z, hidden/dirty state, input routing, and container hierarchy.

In ydraw:

- use `composite` for heavyweight ydraw entries
- use `primitive` for ygrid atoms
- use `resource` for reusable dependencies
- use `drawable_list` for ordered ydraw streams

## Layering Rule

Keep wire/data/model terminology separate from rendering terminology.

Shared CPU/data concepts:

- `drawable_list`
- `primitive`
- `resource`
- `composite`
- `text_drawable_list`
- `yfigure` scene objects

GPU/render concepts:

- renderer
- render instance
- GPU resource set
- binder
- pipeline

A yfigure subtype may store state for a composite, but rendering should be an
orthogonal concern handled by renderer/registry code, not by making every model
subclass own GPU behavior directly.
