# yvterm rich drawables — addressing protocol & use cases

How a producer draws rich content into a yvterm terminal and addresses it later
(insert / update / delete). This is the protocol contract; read it before
touching the rich store, the ingest, or a producer.

**Vocabulary (fixed):** the drawn things are **drawables**. A drawable has one of
two types: **primitive** (box, line, text — static pixels) or **complex** (plot,
video, image — carries a live runtime). A **group** is a container that holds
drawables and other groups. Nothing here is called a "figure".

---

## 1. The model

Rich content is a **tree of nodes**. A node is a **group**, a **complex**, or a
**primitive**.

- **Groups and complexes MAY carry an id**: `GROUP(7)`, `Plot(id=7)`.
- **Primitives NEVER carry an id** and are **never addressable**. To manage
  primitive content later, place it inside an id-bearing group and target that
  group's subtree — the path resolves to the **group**, never to the primitive.
- A node that carries an id is **addressable**: its address is the **path of ids
  from the root** — `7`, `7.10000`, `dialog.row2.chart`.
- A node without an id is **anonymous**: drawn, but never targetable by any
  operation.

One rule to remember: **a path resolves to exactly ONE node, and what an
operation does depends on that node's kind — never on what happens to sit
inside it.**

---

## 1a. Positioning — where content lands (this is NOT addressing)

A yetty terminal is a vertically **scrolling stack of character rows**. Rich
content is **glued to rows and scrolls with the surrounding text** — a plot
stays stuck to its spot as you scroll past it. Positioning is terminal-native
and has **nothing to do with ids or groups**.

The placement unit is the **insertion**: one **command batch**'s worth of
**new** content. A command batch is transport-neutral: **one DCS envelope is one
batch; one RPC batch/request is one batch** (an RPC exposing one operation per
call makes each call its own batch — a batch RPC exists to send a whole tree as
one insertion). Equivalent trees get identical placement regardless of
transport. An insertion has:

1. **A rolling origin = the cursor** when the batch arrives — same as text
   landing at the cursor. That cursor row is the **origin (local row 0)** of the
   insertion's coordinate space, *not* the top of anything.

2. **Each drawable's own `(x, y, w, h)` in pixels**, relative to that origin.
   With `row_height` pixels per row, a drawable covers the half-open row range
   `[ origin + floor(y / row_height),  origin + ceil((y + h) / row_height) )`.
   Example: origin row 40, `y=200`, `h=100`, `row_height=10` → covers `[60, 70)`,
   i.e. rows 60–69. A large `y` pushes it down, leaving empty leading rows.

3. **An insertion row span**
   = `max(1, ceil(max_new_content_bottom_px / row_height))`, where
   `max_new_content_bottom_px` is the maximum **effective-AABB bottom** over the
   batch's **new content only** — its successful new-root declarations and
   anonymous drawables. Operations targeting previously existing insertions
   (replacements, child additions, updates, deletes) belong to **those**
   insertions and never enlarge this one. The `max(1, …)` floor means even an
   empty addressable group (`GROUP(7){}`) spans one row — it has an
   addressability lifetime and **one row of future capacity**: later children
   may be added only if their complete subtree fits that one row. A producer
   expecting taller future children must establish the needed span at creation
   (with content sized for it). All new content shares the one rolling origin;
   the span covers `[origin, origin + insertion_row_span)` and belongs to the
   **insertion**, not to any single drawable.

**Geometry contract.** Row coverage and the span are computed from each
drawable's validated **effective AABB** — its declared rectangle plus whatever
extends it visually (stroke width, text extents, a complex's derived bounds).
The effective AABB must be **finite**, must **not extend above `y = 0`**, and
must have **positive vertical coverage** for a visible drawable (so a
zero-height source line with a nonzero stroke is fine). Row calculations are
overflow-checked. The span runs origin-downward only — there is no negative-`y`
upward extent.

**Declared span (the viewport primitive).** A batch may DECLARE its insertion
row span instead of deriving it: `reserve(height_px)` fixes the span at
`max(1, ceil(height_px / row_height))` rows, and content taller than that
**never extends the reservation** — it is clipped by the projection. This is
how an app ships a tall page once and scrolls it inside a fixed window.
Without a declaration the span derives from the content bottom as always.
A declaration is a property of the batch's insertion, **not** new content by
itself: a batch whose only novelty is a `reserve` is mutation-only and takes
no placement. Declared heights (and derived content bottoms) are bounded by
**receiver limits** — a value beyond the receiver's maximum pixel/row bound
is clamped to it, not rejected; the resulting span is the observable one.

**Clip (projection state).** A group MAY carry a **clip rectangle**
(`update(path, GROUP_FIELD_CLIP, x, y, w, h)`, local content space,
pre-offset; zero size disables). Clipping is **non-layout PROJECTION
state**: it never affects the row span, placement, or the advance — groups
still have coordinates and never layout size. Rendering intersects a
node's subtree with every ancestor clip (each rect projected by its
owner's accumulated offsets); content outside is not painted but remains
attached and addressable. This is how a viewport widget (scrollarea)
bounds tall content.

**Attachment is a projection.** Content inside a group is pure data in the
group's content space; its row attachment is **computed per frame**:
`effective = declared local coords + accumulated ancestor group offsets`,
intersected with the insertion's row span. Inside the span → rendered
("attached", clipped at row granularity). Outside → **detached**: no rows, no
rendering, no history involvement — and still fully addressable (out of view
is NOT out of scope). Changing an offset re-derives the projection, so
re-attachment is automatic and free. Only terminal scrolling moves rows; an
offset never does.

Under the hood this is a **rolling-row** model: the insertion stores its
absolute timeline row and the renderer draws at `row − scroll_top`, so scrolling
is O(1) and never rewrites coordinates.

---

## 1b. Cursor movement

Content arrives in **command batches** (§1a — a DCS envelope is the DCS
realization of a batch; an RPC batch is the RPC one), and drawing is
**cumulative**, like printing text.

- **All new content one batch emits forms ONE insertion** — every new root
  node and all anonymous drawables together. They share one rolling origin and
  one combined row span, and the cursor advances **exactly once**, by that
  combined span. The next batch's insertion lands **below**; insertions
  stack down the scroll.
- **Everything else moves the cursor NOTHING**: replacing an existing node,
  adding a child under a live parent, `update`, `delete` — all mutate in place,
  contribute nothing to any row span, and leave the layout exactly where it is.

Within one batch (commands run in wire order, §4) the new insertion is
**provisional**: the first successful new-root / anonymous emission opens it at
the current cursor; further new content **monotonically enlarges** its row span;
once a node is installed, a later addressed mutation of it — even in the same
batch — follows the ordinary in-place rule and must fit the span accumulated
so far; `delete` and smaller replacements **never shrink** the provisional span.
The cursor advances once, by the final span.

**Batch finalization order** (observable, so fixed):
1. all parseable commands execute in declared order at the **pre-finalization**
   cursor/view state, accumulating the provisional span;
2. when command processing ends (batch end, or a framing stop — prior successes
   still finalize, execution is nontransactional), the new insertion finalizes:
   its row span is installed **before** any cursor movement, so the scrolling
   the advance triggers sees the insertion's true coverage (an insertion can
   never be sealed or destroyed by its own placement);
3. the cursor advances **once** by the final span, performing the resulting
   terminal scrolling. On a screen **without history** (the alternate screen)
   the advance **clamps at the last row** — there is nowhere for departing
   rows to go, so an insertion must not scroll away its own content; the
   cursor ends ON the insertion's last row instead of below it, and a span
   taller than the remaining rows clips to the screen;
4. sealing/invalidation caused by that scroll applies **after** the commands.

So an `update(old_path)` late in a batch still hits a target that the batch's
own new insertion is about to scroll into history. A mutation-only batch skips
steps 2–4.

```
Envelope 1 (insert):  Plot(id=1)          // span 15 → rows R..R+14,   cursor → R+15
Envelope 2 (insert):  GROUP(2){ Box }      // span 3  → rows R+15..R+17, cursor → R+18
Envelope 3 (update):  update(1, data)      // in place → feeds plot 1;  cursor stays R+18
Envelope 4 (insert):  insert(1, Plot(…))   // replace  → same rows;     cursor stays R+18
```

---

## 2. Node kinds

There are exactly **three** node kinds:

| kind          | drawn as                   | id        | live runtime? | update       | delete        |
|---------------|----------------------------|-----------|---------------|--------------|---------------|
| **group**     | `GROUP(id){ … }` container | optional  | no            | **state write (offset)** | yes |
| **complex**   | `Plot(id)` / video / image | optional  | **yes**       | **feeds it** | yes           |
| **primitive** | `Box` / `Line` / `Text`    | **never** | no            | no-op        | via its group |

- What `update` means depends on the target's **kind**: a **complex** receives
  the payload in its live runtime (it is the only drawable with one); a
  **group** receives a **state-field write** (§4, "Group state"); a primitive
  is never a target.
- A **primitive** is never addressable; manage it by targeting an enclosing
  id-bearing group (the path resolves to the group, never the primitive).
- Any group/complex without an id is **anonymous** — cumulative content that no
  operation can target (see UC-3).

---

## 3. Addressing: the path

- An address is the sequence of **ids** from the root: `a`, `a.b`, `a.b.c`.
- Only **id-bearing** nodes contribute a path segment. An **anonymous group is
  transparent to addressing**: an addressable descendant's scope is its nearest
  **id-bearing** ancestor, and an id must be **unique among the addressable
  nodes of that scope** (not merely among structural siblings).
- The three operations carry an **absolute path** of N id components, written
  `op(path, …)`. The nested wire tree `GROUP(local_id){ … }` **constructs**
  paths while emitting content — a nested `GROUP` record is itself content
  (that node), never mere navigation to a descendant.
- Internally a path folds to a **64-bit key** (`yetty_yvterm_group_key_fold`,
  order/position/depth sensitive), one binding `key → node`. Deleting a parent
  cascades through the node tree (`parent_slot`), not through the key.

---

## 4. The operations

Three verbs. Each targets an absolute **path**. (Emitting anonymous content is
not an operation on the tree — it is plain drawing, part of whatever insertion
carries it.)

| op         | signature            | effect                                                                  |
|------------|----------------------|-------------------------------------------------------------------------|
| **insert** | `insert(path, node)` | create the node at `path`, or **replace the node + its ENTIRE subtree**  |
| **update** | `update(path, data)` | kind-dispatched: **complex** → runtime payload; **group** → state-field write (offset); unresolvable → no-op |
| **delete** | `delete(path)`       | remove the node at `path` and its **whole subtree**                      |

`insert` is create-or-replace with **exact-subtree semantics**. (The name is
deliberate and kept; note the split: only the **new-root** case actually inserts
new rows and moves the cursor — every other `insert` mutates in place. The noun
**insertion** always means the placement unit of §1a.)

**Identity.** In `insert(path, node)` the **final path component IS the target
node's id** — the supplied root node carries no separate id, and a form that
contradicts the path (`insert(5, Plot(id=6))`) is malformed. Id-bearing
descendants inside the supplied subtree carry their normal local ids. The
supplied root must be a **group or complex** — `insert(path, Box)` is invalid,
because a primitive never carries an id and so can never be the node a path
resolves to.

**Two spellings, one operation.** An inline id-bearing declaration
(`GROUP(5){…}`, `Plot(id=7,…)`) is the **tree-emission form** of `insert`: its
path is constructed from its enclosing declaration scopes (§3). The absolute
form `insert(path, node)` carries the complete path, so its supplied root omits
the redundant id. Both forms have **identical create-or-exact-replace
semantics** — an inline `Plot(id=7)` emitted while path `7` is live is a
**replacement** of node 7, not new placement.

**Path preconditions.** A one-component path may create a new root. For a longer
path, **every prefix must already resolve**, every non-final component must
resolve to a **live group**, and missing intermediate groups are **never
synthesized**. (With anonymous groups being address-transparent, an addressed
insert beneath a scope becomes a **direct structural child of the resolved
id-bearing group** — a path cannot select an anonymous container.)

The three cases:

- **New root path** → the node joins **this batch's insertion** (§1b): one
  rolling origin and one combined row span shared with the batch's other new
  roots and anonymous content; the cursor advances once for the batch.
- **Missing descendant under a live parent** (`insert(dialog.newRow, …)`) → the
  node **joins the parent's insertion**: same rolling origin, must fit inside
  the existing row span, **no cursor movement**. Its paint position: **appended
  after the parent's existing children** in emission order (explicit z remains
  the primary key; emission order is the tie-breaker).
- **Existing path** → the node and everything below it become **exactly** the
  supplied content. Anything previously below the path and not supplied
  **disappears**. Ancestors and siblings are untouched. The replacement stays
  in the target's **insertion** (that insertion's rolling origin, coordinate
  origin and row span are unchanged) and keeps the target's **sibling /
  emission slot**; the supplied subtree's records get fresh internal order from
  their wire order. The node's **kind may change** (group ↔ complex) — it is a
  whole-node replacement.

**Failure — every semantic insert error is command-local and atomic.** This
covers them all: an **in-place** insert (replacement OR missing-descendant
creation) whose supplied subtree exceeds the containing insertion's row span; a
subtree declaring the **same address twice in one scope**; a root id
**contradicting the path**; a **primitive** as the addressed root; a **missing
or non-group path prefix**; **invalid/non-finite geometry**; **row-coordinate
overflow**. In every case: a failed **replacement** preserves the previous
node, subtree, runtimes, placement and paint order exactly; a failed
**creation** leaves the path **absent** and contributes nothing to the batch's
provisional row span; prior successful commands remain; later parseable
commands continue. (Whether a transport reports the failure is a transport
concern; the retained state is identical.) Distinct from all of these is a
**framing error** — corrupt command length/structure where the next command
boundary cannot be recovered: that is a parse failure and processing
necessarily stops there.

**Execution order (nontransactional).** Commands in one batch execute in
**wire order**. Each `insert` is atomic for its own target subtree; a failed
command **does not roll back** earlier successful commands, and later commands
continue, observing the state the earlier ones produced. Missing
`update`/`delete` targets are no-ops.

**Update delivery.** The addressing layer only resolves and delivers: the
terminal finds the exact live complex at `path` and hands it the payload.
Missing / wrong-kind / sealed targets are no-ops. Payload validation and state
atomicity belong to that complex's own update contract — a **runtime-rejected
update does not stop the batch; later commands continue**, and tree identity,
placement and row span are unaffected (whether the runtime's own state changed
is defined by that complex's contract).

**Update invariant.** `update` may mutate **only the complex's live runtime
content inside its declared geometry and paint placement** — never path
identity, node kind, subtree structure, `x/y/w/h` (or the effective AABB), z,
emission slot, rolling origin, or row span. Any such change requires
`insert(path, node)` and is subject to the ordinary in-place span-fit and
ordering rules. A complex's update contract must keep its rendered output
inside the node's declared geometry (video frames, plot samples etc. change
pixels/data, not layout bounds).

**Group state — the ONE mover.** `update` addressed at a GROUP node writes a
state field; the first field is the translation **offset**
(`GROUP_FIELD_OFFSET`, payload `[f32 x][f32 y]`, ABSOLUTE pixels). The offset
moves **rendering only**, via the projection of §1a — placement bookkeeping
(span, coverage, sealing, history) never sees it, so it does not violate the
update invariant: no row span, origin, or identity changes. Movement belongs
exclusively to groups (coordinates, never size); a complex that must move
lives in a group, and its runtime survives every move. Anything that moves is
given an id-bearing group at creation.

**Efficiency comes from addressing depth, not partial replacement:** to change
one small thing, give it an id and `insert` at *its* path — siblings are
untouched because they are siblings. You never resend a subtree you didn't
target.

---

## 5. Use cases

### UC-1 — anonymous primitive
```
Box(red)          // no id
```
Drawn at the cursor as part of this insertion. No id → no operation can ever
target it.

### UC-2 — addressable primitive (via a group)
```
GROUP(5) { Box(red) }
```
- `delete(5)` → the group (and its box) gone.
- `insert(5, GROUP{ Box(blue) })` → the box is swapped **in place**.
- `update(5, …)` → **no-op** (path 5 is a group; no runtime).

### UC-3 — anonymous content is cumulative
```
Plot(data)        // no id
Plot(data)        // no id — a SECOND plot, below the first
```
Both render. Neither can be updated or deleted. Emitting more anonymous content
**adds** to the scroll; it never replaces. Anonymous content lives until a
terminal clear/invalidation or scrollback eviction.

### UC-4 — addressable complex (the updatable case)
```
Plot(id=7, data)
```
Path `7` **is** the complex. `update(7, newsamples)` feeds the live plot new
data (no geometry resent). `delete(7)` removes it. `insert(7, Plot(…))`
replaces it in place.

### UC-5 — several complexes in one group, one batch
```
GROUP(7) {
    Plot(id=10000,     data)
    Plot(id=100012123, data)
}
```
Paths `7.10000` and `7.100012123` are the two complexes.
- `update(7.10000, d)` → the **first** plot only.
- `update(7.100012123, d)` → the **second** plot only.
- `delete(7)` → **both** gone (subtree).
- `delete(7.10000)` → **only the first** gone.

### UC-6 — nested composition (a widget / dialog)
```
GROUP(dialog) {
    GROUP(row1) { Box; Text("Volume") }
    GROUP(row2) { Box; Plot(id=chart, data) }
}
```
Addressable: `dialog`, `dialog.row1`, `dialog.row2`, `dialog.row2.chart`.
- `update(dialog.row2.chart, d)` → the chart (a complex).
- `delete(dialog.row1)` → row1 gone; row2 and the chart untouched (siblings).
- `insert(dialog.row2, GROUP{ Box'; Plot(id=chart, …) })` → row2's subtree
  becomes exactly this: the box replaced, the chart replaced. **Omit the chart
  and it disappears** — an insert makes the subtree exactly what you sent.
- To touch ONLY the chart, don't reinsert row2 — target the chart:
  `insert(dialog.row2.chart, Plot(…))` or `update(dialog.row2.chart, d)`.

### UC-7 — two instances of the same component (no collision)
```
GROUP(dialogA) { GROUP(ok){…} GROUP(cancel){…} }
GROUP(dialogB) { GROUP(ok){…} GROUP(cancel){…} }
```
Reused local ids live at `dialogA.ok` vs `dialogB.ok` — distinct paths, no
collision. Two dialogs with identical internal ids coexist; give each a
distinct root. (This replaced the old producer namespace.)

### UC-8 — change one small thing: address it, don't resend the parent
Given the dialog of UC-6, animating a combobox / hover highlight in `row1` does
NOT mean resending `dialog`:
- give the changing part its own id when first drawn
  (`GROUP(row1){ GROUP(hl){Box} Text }`),
- then `insert(dialog.row1.hl, GROUP{ Box' })` per frame — a few records on the
  wire; `row2`, the chart, and the rest of `row1` are never resent.
Design rule: **make everything you intend to mutate id-bearing at creation.**

### UC-9 — adding a child under a live parent
```
insert(dialog.row3, GROUP{ Box; Text("Balance") })
```
`row3` did not exist. It joins `dialog`'s insertion: same rolling origin, must
fit inside the existing row span, and the cursor does **not** move. It paints
**after** `dialog`'s existing children (emission-order tie-break; explicit z
still wins). Only new content at a new **root** joins a new insertion (§4).

### UC-10 — targeting a missing / deleted path
- `update(path)` with nothing live there → **no-op** (never an error; the rest
  of the batch proceeds).
- `delete(path)` of an unknown path → **no-op**.
- Reusing a path after `delete` → a **fresh node** (fresh identity; the old
  content never resurrects). Its placement depends on where it sits:
  - recreated **root** → a fresh insertion at the cursor;
  - recreated **descendant** of a still-live group → joins that parent's
    existing insertion (same origin/span, no cursor move), like any new child.

### UC-11 — classical terminal output onto rich rows
Rich rows are terminal rows, not a protected layout. Normal **sequential**
output lands **below** the insertion row span. But if the application moves the
cursor back and writes or erases a row **intersecting an insertion's row span**,
that **whole insertion is invalidated** — all its drawables and all its live ids
are removed (insertion-granular). Other insertions are unaffected.

The same policy covers **row-structure operations** (insert/delete line, scroll
regions, index/reverse-index inside margins, full-screen scroll):
- whole-screen upward scroll moves insertions **with the logical row stream**
  and may seal them into history (UC-12);
- a row-structure operation that moves an insertion's **complete** row span as
  one interval moves its rolling origin with those rows;
- an operation that **cuts through, clips, or only partly moves** a row span
  **invalidates the entire insertion**;
- **alternate-screen** (no-history) scrolling **invalidates** content that
  leaves the screen instead of archiving it;
- generally: **only whole-screen upward primary scrolling transfers content
  into scrollback history.** A row operation that moves a complete insertion
  out through any boundary that does **not** retain history (e.g. the bottom of
  a scroll region during reverse-index) **invalidates** it;
- these rules apply **during placement too**: if the batch's own reserve
  advance scrolls an active sub-screen scroll region (reservation newlines
  fed at its bottom margin), the provisional insertion rides the region
  scroll when wholly contained and is **invalidated whole** when the region
  boundary cuts it — an insertion can be destroyed by its own placement only
  through this explicit region rule, never by ordinary whole-screen flow.

### UC-12 — addressability lifetime: per-node scroll retirement
A node goes **out of scope** — and is **permanently discarded** — when a
**terminal row scroll** leaves its projected footprint entirely above the
live surface. Retirement is per **addressable node**, evaluated only at
scroll time:

- A node's **footprint** is the union of its subtree's effective extents
  (record AABBs) plus accumulated ancestor offsets, clamped to the
  insertion's row span — the exact projection the renderer draws.
- A node whose footprint still **intersects** the live screen stays fully
  addressable: the half-scrolled streaming plot keeps accepting `update`.
- A node whose footprint has **fully left** retires: update, delete,
  insert-at-path and offset writes stop resolving; its content stays
  **rendered as immutable history**. Retirement freezes the node's WHOLE
  subtree with it.
- **Detached content is exempt**: a node projected outside the span by
  offsets has no surface footprint, and out of view is NOT out of scope —
  offsets alone never retire anything; only terminal scrolling does.
  Unknown-extent content is likewise exempt (conservative: it stays
  addressable rather than being wrongly killed).
- **A live ancestor cannot mutate frozen descendants**: its `insert`
  (exact-subtree reopen) replaces only the LIVE remainder — retired
  subtrees are spared and keep rendering; its `delete` removes only the
  live remainder. Whole-block invalidation (a text write over a covered
  row) still removes everything, frozen included — that is the
  block-granular invalidation rule, not an addressed mutation.
- Once the insertion's **whole** span has entered history, the block seals:
  every remaining id dies at once. A later resize that re-shows retired or
  sealed content **renders it but never gives the ids back**; reusing a
  path creates a fresh live node.

**Resize / reflow of a still-live insertion:** an insertion is an
**indivisible interval** in the terminal's logical row stream. When a width
resize reflows the classical text above it, the insertion's **rolling origin is
remapped** so it keeps its document position relative to the surrounding text;
the drawables' **local pixel coordinates are never rewritten**, and the row
span stays fixed. The exception is a **cell-metric change** (`row_height`
changes, e.g. font/zoom): then **every retained insertion — live AND sealed
history** — has its span and rolling placement **recomputed from its stored
pixel geometry** (the pixels are authoritative; a recomputed span may grow or
shrink) and reflow re-lays the row stream accordingly. Archived tiers may do
this lazily, but the observable layout must equal a complete reflow — an old
span rendered with new metrics is not a defined layout. Recomputing placement
never restores addressability: if any reflow pushes a complete span above the
live boundary, the insertion seals once and permanently — a later reverse
reflow renders it again but never restores its bindings.

### UC-13 — insertions stack; update/delete don't move the cursor
```
Plot(id=1)          // insert: new root → cursor advances by its row span
GROUP(2){ Box }      // insert: new root → lands BELOW plot 1, cursor advances
update(1, data)      // update: feeds plot 1 in place; cursor does NOT move
insert(1, Plot(…))   // replace: same rows;             cursor does NOT move
```

### UC-14 — viewport scrolling (app-controlled)
```
reserve(280)                       // the viewport: a declared 280px span
GROUP(1) { …1160px of content… }   // sent ONCE — taller than the viewport
…
path(1); update(1, OFFSET, 0, -Δ)  // ~20 bytes per scroll tick
```
The declared span reserves the rows; the taller content clips to it; the app
pans by updating the root group's offset. Content panned out is **detached**
(no rows, no history) and stays addressable — a nested complex (`[1,9]`) keeps
accepting `update` while out of view and shows the streamed state when panned
back. Nothing is ever re-sent; ids are stable throughout; the transcript above
and below the viewport is untouched.

---

## 6. At a glance

```
batch                 one DCS envelope or one RPC batch/request; commands run in
                      declared order; at most one new insertion per batch.
insertion             one batch's new-root + anonymous content at one rolling
                      origin; one combined row span (min 1 row), a monotonic max
                      over the NEW content only; one cursor move. An empty group
                      gets ONE row of future capacity, not unlimited.

insert(path, node)    create at path, or make path's subtree EXACTLY the
                      supplied content (kind may change). Final path component
                      IS the id; root must be group/complex; prefixes must
                      resolve to live groups.
inline GROUP/Plot(id) relative-path spelling of the same insert (same
                      create-or-exact-replace semantics; live path → replace).
update(path, data)    complex -> deliver runtime payload; GROUP -> state-field
                      write (offset); anything else no-op.
offset                the one mover: absolute px on a group; moves rendering
                      via the projection, never rows/span/history; runtimes
                      survive movement.
reserve(height_px)    declared insertion span (viewport); taller content clips,
                      never extends; without it the span derives from content.
projection            attachment computed per frame from offsets; out of view =
                      detached (no rows, no history) and STILL addressable.
delete(path)          remove the node + its whole subtree; row layout unchanged.

new root              joins the batch's new insertion (cursor advances once).
new descendant        joins the live parent's existing insertion; appended after
                      existing children; no cursor move; must fit the span.
replace existing      stays in the target's insertion and sibling slot.
delete + recreate     fresh identity; placement per the two rules above.

ordering              wire order; per-insert atomicity; no rollback; later
                      commands see earlier results.
anonymous (no id)     cumulative drawing; never targetable; lives until clear
                      or eviction.
addressable while     the NODE's projected footprint intersects the live
                      screen (per-node retirement at terminal-scroll time;
                      detached / unknown-extent nodes exempt — UC-12).
history               rendered but immutable and permanently id-less; a live
                      ancestor's insert/delete touches only the live
                      remainder, never frozen descendants.
invalidation          classical write/erase into a span kills that insertion.
semantic insert error command-local, atomic (span overflow incl. new-child
                      creation, duplicate addresses, id/path contradiction,
                      primitive root, bad prefix, bad geometry, row overflow);
                      failed replacement retains the old subtree exactly, failed
                      creation leaves the path absent; later commands continue.
framing error         parse failure — processing stops where no next command
                      boundary can be recovered.
update invariant      runtime content only, inside declared geometry/placement;
                      geometry/z/slot/identity changes require insert.
batch finalization    commands first (pre-finalization view), then ONE cursor
                      advance + scroll, then retirement/sealing from that
                      scroll.
row operations        moving a COMPLETE span moves its origin with the rows;
                      cutting/clipping/partly moving a span invalidates the
                      whole insertion; scroll-off through any non-history
                      boundary (alt screen, scroll region) invalidates — only
                      whole-screen primary scroll reaches scrollback.
resize / reflow       remaps rolling origins with the logical rows; local pixel
                      coords never rewritten; a cell-metric (row_height) change
                      recomputes EVERY retained insertion (live and sealed)
                      from pixel geometry — lazily allowed, observably exact.
geometry              validated effective AABB (incl. stroke/text/derived
                      bounds): finite, none above y=0, positive vertical
                      coverage when visible; span runs origin-downward only.
```
