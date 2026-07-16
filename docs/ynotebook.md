# Yetty Notebook Design

## Status

This document proposes a native Jupyter notebook frontend for Yetty. The
frontend uses Yetty's rendering and UI systems directly while remaining
compatible with Jupyter notebook files, servers, kernels, and rich display
messages.

The design deliberately separates four reusable concerns:

- `ymime` owns the shared MIME vocabulary and bundle data model: it classifies
  representations into native kinds and holds multi-representation bundles. It
  is a leaf library (`ycore` only) and links no renderer. This module already
  exists as Yetty's file-type detector and DCS file-envelope codec; the design
  extends it in place rather than introducing a second module of the same name.
- `yrender-mime` selects and instantiates a renderer for a MIME bundle. It
  depends on every renderer it can produce, so it sits at the top of the
  dependency graph — deliberately kept separate from leaf `ymime`.
- `yjupyter` communicates with Jupyter servers and kernels.
- `ynotebook` owns the notebook document and its execution semantics.

The initial design does not attempt to port JupyterLab or reproduce a browser
DOM. HTML and JavaScript are compatibility formats, not the foundation of the
notebook UI.

## Goals

- Open, display, edit, execute, and save nbformat 4 notebooks.
- Preserve notebook data and metadata that Yetty does not understand.
- Render common rich outputs through native Yetty figures.
- Connect to local and remote kernels through Jupyter Server.
- Route asynchronous kernel output to the correct cell.
- Support mutable displays, streaming output, errors, and standard input.
- Reuse MIME rendering outside notebooks, particularly from `ycat` and rich
  inspectors.
- Keep untrusted notebook content isolated from native Yetty capabilities.

## Non-goals

- Source compatibility with JupyterLab extensions.
- Immediate support for arbitrary third-party JavaScript widgets.
- Pixel-identical rendering with a browser notebook frontend.
- Treating terminal scrollback as the canonical notebook document model.
- Connecting directly to kernel ZeroMQ sockets in the first implementation.

## Architecture

```text
 .ipynb file                         Jupyter Server
      |                        REST API + kernel WebSocket
      v                                  |
+-------------+                          v
|  ynotebook  |<-------------------+-----------+
| document and|   protocol events  | yjupyter  |
| cell state  |------------------->| client    |
+------+------+ execute requests  +-----------+
       |
       | MIME bundle (ymime data model) + render context
       v
+--------------+
| yrender-mime |
| selection    |
| and dispatch |
+------+-------+
       |
       v
 ymarkdown, yimage, ysvg, ypdf, ygrid, ygui, sanitized ybrowser, text fallback

 ymime (leaf): native-kind classification + bundle data model, shared by
 ynotebook (model), yrender-mime (selection), and the terminal/ycat detect path.
```

The notebook UI is hosted as a native `yguiapp` or `yui` view. Each cell owns
its editor, prompts, execution state, and output container. Rich outputs are
child figures whose lifetime and placement are controlled by the notebook view.

The terminal may additionally expose an append-oriented notebook viewer or
runner. That mode can place static output in row-anchored ydraw content, but it
does not replace the mutable native notebook view.

## Object framework

`ynotebook`, `yjupyter`, and `yrender-mime` expose objects with lifecycles and
methods (documents, cells, sessions, kernels, renderers), so they are written as
yclass classes; codegen then generates their dispatch glue, `model.yaml`, RPC
skeletons, and FFI bindings.

What codegen provides is the *dispatch and binding surface*, not automatic
serialization or remote lifetime for these objects. A notebook document is
recursive JSON with variable-sized MIME bundles, binary buffers, and extension
fields; a renderer owns host-local figures and GPU resources. None of that is
wire-safe by virtue of being a yclass class. So the RPC story is deliberate,
not free:

- The **model** exposes explicit wire-safe DTO classes (a document/cell/output
  snapshot and mutation ops) with defined buffer ownership; the recursive,
  extension-bearing in-memory model is not itself the wire type.
- The **renderer** is treated as a local object with host GPU resources; only a
  narrow, explicitly designed RPC facade (create-from-bundle, resize, input,
  snapshot) crosses the wire. This is what a remote-kernel or `yrdawn`-style
  split frontend actually needs, and it is designed, not inherited.

The external-ABI surfaces that cannot be yclass — the `ytransport` WebSocket and
callback boundaries inside `yjupyter`, and the JSON codec — remain ordinary C at
the module's lower edge. Leaf `ymime` stays plain C: it is a pure classifier
plus renderer-independent bundle types (which carry ownership — the *json*
payload is a retained, reference-counted value — but no method-dispatch or RPC
surface), the justified exception to the yclass-first default.

## Module boundaries

### `ymime` (shared classification and bundle model)

`ymime` already exists in the tree as Yetty's file-type detector and DCS
file-envelope prologue codec: a deliberately leaf library (`ycore` only, no
libmagic, no renderer callbacks) so the terminal and thin clients such as
`ycat --raw` can link it without pulling in a renderer stack. The notebook
design **extends this module in place** rather than creating a second module of
the same name.

`ymime` answers: "What native kind is this representation, and how are multiple
representations of one output held together?"

It owns two things:

- **Native-kind classification.** The existing `enum yetty_ymime_type` and its
  MIME-string / extension / byte-sniff mapping. The notebook path never sniffs
  bytes — the kernel declares the MIME type — so it uses only the
  string-to-kind mapping (`yetty_ymime_type_from_mime`). The terminal and
  `ycat` detect and prologue paths are unchanged.
- **The bundle data model.** A `struct yetty_ymime_bundle` of ordered
  `struct yetty_ymime_repr` entries plus optional shared binary buffers. Each
  repr carries a MIME string, per-representation metadata, and a **tagged
  payload** that reflects how nbformat actually stores the value — the payload
  is not uniformly "bytes":
  - *text* — a UTF-8 string (possibly assembled from nbformat's list-of-lines
    form) for `text/plain`, `text/html`, `text/markdown`, `image/svg+xml`, …
  - *binary* — decoded bytes for `image/png`, `image/jpeg`, `application/pdf`,
    … (base64 in the `.ipynb`, decoded in memory).
  - *json* — a retained JSON value (object, array, number, boolean, or null)
    for `application/json` and `application/*+json`. These are **not**
    serialized text in nbformat; flattening them to bytes would lose their
    native type and break the preservation goal, so the repr keeps the original
    JSON subtree.

  The *json* variant is held through a **codec-independent owned value type**
  defined in the leaf layer — an opaque `yetty_json_value *` with
  retain/release, type inspection, and serialization hooks — **not** the
  selected parser's node type. The chosen parser backs that abstraction; it
  never becomes `ymime`'s public ABI. This keeps `ymime` leaf and terminal-
  linkable regardless of which JSON library is chosen, and lets the codec
  decision change without touching the bundle ABI.

  A single detected file is the degenerate one-entry, single-payload bundle; a
  Jupyter `display_data` output is a multi-entry bundle mixing these payload
  kinds. The type carries no renderer reference, so `ynotebook`'s model can own
  and serialize bundles without linking any renderer.

The enum is a *convenience* native-kind classification, not a registry key.
`yrender-mime` keys renderers on the full MIME string so it can handle open
vendor types (`application/vnd.plotly.v1+json`, widget-view types, …); an
unrecognized string simply classifies as unknown and falls through to a
string-keyed factory or the `text/plain` fallback. Two notebook-common native
kinds are added to the enum — `HTML` and `JSON` — so `text/html` and
`application/*+json` flow through the same native-kind fast path as images,
SVG, and PDF. All additions stay `ycore`-only and safe for the terminal link.

`ymime` knows nothing about notebook cells, execution counts, Jupyter message
IDs, kernels, `.ipynb` persistence, renderers, or GPU resources.

### `yrender-mime`

`yrender-mime` answers: "Which representation in a bundle should Yetty render,
and with which renderer?"

Its input is a `ymime` bundle, MIME-keyed metadata, optional binary buffers, and
a render context. Its output is a renderer instance or a structured fallback
result. Because it instantiates native figures, it depends on every renderer it
can produce (`ymarkdown`, `yimage`, `ysvg`, `ypdf`, `ygui`, `ybrowser`, …) and
therefore sits at the top of the dependency graph — a separate library from leaf
`ymime`, not part of it. Merging the two would force the terminal and
`ycat --raw` to link the whole renderer stack, which is exactly what leaf
`ymime` was built to avoid.

Responsibilities:

- Maintain a registry of MIME renderer factories keyed on MIME string.
- Score supported representations by fidelity, safety, user preference, and
  renderer availability.
- Instantiate native renderers and pass MIME-specific metadata to them.
- Fall back predictably to `text/plain`.
- Preserve the distinction between unsupported, invalid, and blocked content.
- Enforce output size and resource policies supplied by the host.
- Expose renderer capabilities such as interactivity, intrinsic sizing,
  printing, snapshotting, and trust requirements.

A renderer factory is a create-plus-methods vtable, so `yrender-mime` is modelled
as a yclass class (`class@yrender-mime:renderer`); codegen produces the dispatch
glue and `model.yaml`. A renderer instance stays a *local* object — it owns
figures and GPU resources — so remoting it means designing a narrow RPC facade
(create-from-bundle, resize, input, snapshot), not shipping the instance itself.
See "Object framework" for why yclass supplies the dispatch surface but not
object serialization.

`yrender-mime` does not know about notebook cells, execution counts, Jupyter
message IDs, kernels, or `.ipynb` persistence.

Initial renderer mapping:

| MIME type | Preferred Yetty renderer |
|---|---|
| `text/plain` | shaped text or terminal-style text figure |
| `text/markdown` | `ymarkdown` |
| `image/png`, `image/jpeg`, `image/gif` | `yimage` |
| `image/svg+xml` | `ysvg` |
| `application/pdf` | `ypdf` |
| `application/json`, `application/*+json` | `ygui` tree/table viewer |
| `text/html` | sanitized `ybrowser` figure |

Unknown representations remain in the document even when no renderer accepts
them. MIME selection must inspect the complete bundle rather than relying on
JSON key order.

The API should be usable by `ycat`, notebook output, object inspectors, and
future rich terminal tools. `ycat`'s existing per-type handler dispatch is the
same "pick a renderer for a detected type" problem over a one-entry bundle and
may migrate onto `yrender-mime` later; the `ycat --raw` thin path keeps linking
leaf `ymime` alone.

### `yjupyter`

`yjupyter` answers: "How does a Yetty client communicate with a Jupyter server
and kernel?"

Responsibilities:

- Discover server capabilities and manage authenticated REST requests.
- Create, attach to, interrupt, restart, and shut down kernels and sessions.
- Open the kernel WebSocket and negotiate its wire protocol.
- Encode and decode channel, header, parent header, metadata, content, and
  binary buffer fields.
- Generate message IDs, return each request's message ID to the caller
  synchronously (so `ynotebook` can map it to a cell), and correlate replies
  with requests.
- Expose shell, IOPub, stdin, and control messages as typed events.
- Track connection and kernel busy/idle state without assigning that state to a
  notebook cell.
- Reconnect safely and report gaps or uncertain state to its consumer.
- Implement completion, inspection, completeness checks, and comm transport.

The initial protocol surface includes:

- `kernel_info_request` and reply;
- `execute_request` and reply;
- `status` and `execute_input`;
- `stream`, `display_data`, `execute_result`, and `error`;
- `clear_output` and `update_display_data`;
- `input_request` and `input_reply`;
- completion, inspection, and `is_complete` messages;
- `comm_open`, `comm_msg`, and `comm_close`;
- kernel interrupt, restart, shutdown, and reconnect behavior.

`yjupyter` emits protocol events without deciding which cell owns an output or
how a MIME bundle is displayed. It may be reused by a console, inspector, or
other interactive client that has no notebook document.

Jupyter Server is the primary transport boundary. Direct ZeroMQ connectivity
may be added behind the same client interface later; it must not leak transport
details into `ynotebook`.

The kernel WebSocket builds on the existing `ytransport` WebSocket transport
(`src/yetty/ytransport/websocket-transport.c` — the same client-side
`open → on_connect → on_data → on_disconnect` lifecycle already used by `yssh`
and `ypty`); `yjupyter` adds only the Jupyter v5 message framing on top. The
REST half (session and kernel management, authentication) is new
infrastructure — the tree has no general HTTP client outside `ybrowser`'s
loader — and must be introduced as its own thin, testable HTTP client rather
than reaching into a renderer.

Because the transport boundary is Jupyter Server rather than raw ZeroMQ, the
server owns the Jupyter v5 HMAC message signing: `yjupyter` never holds a
connection-file signing key. This is a deliberate benefit of the server
boundary and one reason direct ZeroMQ is a non-goal for the first
implementation.

Both `yjupyter` and `ynotebook` require a JSON codec: every kernel message
(header, parent header, metadata, content) and every `.ipynb` document is JSON,
whereas Yetty's configuration substrate is YAML. Selecting or vendoring a JSON
parser/serializer for the main library set is a prerequisite for this whole
design, not merely an nbformat-schema question (see Open decisions).

### `ynotebook`

`ynotebook` answers: "What is the current notebook document, and how does an
execution change it?"

Responsibilities:

- Parse, validate, create, and serialize nbformat 4 notebooks.
- Own ordered cells, source, cell IDs, attachments, metadata, execution counts,
  and outputs.
- Preserve unknown fields and unsupported MIME representations.
- Track document mutations and dirty state.
- Implement cell insertion, deletion, movement, conversion, and output clearing.
- Submit executions through `yjupyter` and retain the request-to-cell mapping.
- Apply kernel events to the correct cell and output object.
- Implement Jupyter semantics for delayed clear and mutable displays.
- Supply output MIME bundles to `yrender-mime` without discarding alternatives.
- Coordinate autosave, atomic save, reload conflict detection, and recovery.
- Expose model notifications to one or more notebook views.

`ynotebook` does not decode WebSocket frames and does not contain individual
MIME renderers.

## Document model

The in-memory model mirrors nbformat concepts while retaining an extension map
at every level so that a load/save cycle does not erase unknown data.

```text
notebook
  nbformat, nbformat_minor
  metadata
  cells[]
    id
    cell_type
    source
    metadata
    attachments                 (markdown/raw cells only)
      <filename> -> MIME bundle
    execution_count
    outputs[]
      output_type
      stream fields or MIME bundle
      metadata
      execution_count
      extension fields
```

Source is represented canonically as UTF-8 text in memory. The serializer may
choose a stable JSON representation, but it should avoid unrelated formatting
churn when practical.

All mutations pass through model operations so dirty tracking, undo support,
view notification, and persistence cannot diverge.

## Execution and output routing

When a cell is executed, `ynotebook` records the request message ID returned by
`yjupyter.execute_request` against the cell ID. IOPub output is assigned by
`parent_header.msg_id`, never by the selected cell or a global "currently
executing" pointer.

```text
cell execution
  -> yjupyter.execute_request(code)
  -> request_id mapped to cell_id
  -> status busy
  -> zero or more IOPub events
  -> execute_reply
  -> status idle
  -> execution transaction complete
```

The transaction has two independent completion signals that must both be
tracked: receipt of the shell `execute_reply`, and the IOPub `status: idle`
**whose `parent_header.msg_id` matches the execution request**. Neither alone
closes it — the reply can arrive before the final IOPub output, and idle can
arrive first. A bare "any idle after the reply" rule is wrong: comm handlers,
other requests, and other clients sharing the kernel also publish busy/idle, so
only the parent-matched idle counts. The model must tolerate — and route by
`parent_header` — messages from all that unrelated background activity.

Output rules:

- Consecutive compatible `stream` records may be coalesced without changing
  their visible semantics.
- `clear_output(wait=false)` clears immediately.
- `clear_output(wait=true)` marks existing output for removal when the next
  output arrives.
- `display_data` with a transient `display_id` registers the produced display.
- `update_display_data` updates every live display associated with that ID.
- Transient identifiers are runtime state and are not serialized as ordinary
  notebook data.
- Errors retain structured `ename`, `evalue`, and traceback fields; ANSI
  styling is interpreted only by the presentation layer.
- Orphaned messages are retained in a diagnostic or session output stream
  rather than silently attached to an arbitrary cell.

## Notebook UI

The native view presents a vertically virtualized sequence of cells. A cell has:

- input prompt and execution state;
- multiline source editor;
- optional run controls;
- output container;
- collapsed, selected, and focused state;
- optional metadata and diagnostics surfaces.

Virtualization is required because notebook outputs may be large and rich
figures may own significant GPU resources. Off-screen cells retain model state
but may release presentation objects and recreate them through `yrender-mime`
when they return to the viewport.

Output layout must support intrinsic height changes. A streaming text output,
image decode, Markdown reflow, or interactive figure resize invalidates the
owning cell's measured height and the notebook's vertical layout. Scroll
anchoring should keep the user's visible location stable during relayout.

Keyboard and pointer input are routed by focus:

- notebook command mode handles cell-level operations;
- editor mode sends text-editing input to the source editor;
- focused interactive output receives its subscribed input;
- global application shortcuts remain owned by `yui` according to the normal
  precedence rules.

## Rich output lifetime

Stored notebook output and rendered output are distinct:

- The model owns MIME data and metadata.
- The view owns renderer instances and GPU resources.
- `yrender-mime` creates a renderer from model data and a host render context.

Deleting a cell, clearing its output, replacing a display, closing a view, or
virtualizing a cell must deterministically destroy the corresponding renderer
objects. Renderer destruction must not remove the underlying MIME data unless a
model operation explicitly does so.

Notebook figures belong to the native view's figure tree and are positioned in
view coordinates. They are not terminal-row-anchored content. A separate
append-oriented notebook runner may translate completed output into ydraw
scrollback content where appropriate.

## Widgets and comms

`yjupyter` provides generic comm transport. Widget interpretation lives above
it because comm payloads define application-specific protocols.

Widget support is divided into:

1. Preserve widget MIME and notebook widget-state metadata without rendering.
2. Implement a widget manager for selected core widget models and map their
   views onto `ygui` controls.
3. Optionally host compatible JavaScript widget views in an isolated web
   runtime.

The native widget manager owns model identity, trait state, binary buffer
references, view instances, and comm synchronization. Unknown widget models
must remain intact and present a clear fallback instead of being partially
executed.

No comm target receives native filesystem, process, network, or Yetty RPC
capabilities merely because a notebook is trusted.

## HTML and active content

Notebook HTML is rendered by `ybrowser`, Yetty's in-process HTML/CSS/JS engine,
under a locked-down policy. Notebook HTML is untrusted content. The default HTML
policy:

- removes scripts and inline event handlers;
- blocks frames and active embedded objects;
- restricts CSS features that can escape or obscure the output bounds;
- resolves attachments and approved data URLs through scoped resources;
- blocks uncontrolled network fetches;
- exposes no native Yetty objects to QuickJS;
- falls back to another MIME representation when safe rendering is unavailable.

Active HTML or JavaScript rendering, if provided, uses an explicit policy and an
isolated runtime. Notebook trust may affect whether active output is eligible,
but trust does not bypass process-level capability restrictions.

## Persistence

Notebook saves are atomic: serialize and validate a complete candidate, write a
temporary sibling, flush it as required by the platform, and replace the target.
The model records the file identity or content revision observed at load/save so
external modifications can be detected before replacement.

Persistence rules:

- Keep all MIME alternatives, including those Yetty cannot display.
- Preserve unknown metadata and extension fields.
- Preserve stable and unique cell IDs.
- Store cell attachments as a filename-keyed map of MIME bundles (valid on
  Markdown and raw cells). The filename key is authoritative: scoped
  `attachment:<filename>` URL resolution in the HTML renderer binds to it.
- Exclude transient display routing and live comm state from ordinary outputs.
- Validate before save and report schema problems without discarding data.
- Make output clearing and notebook trust changes explicit document mutations.

Autosave uses the same serialization and conflict checks as an explicit save.
Recovery snapshots are separate from the canonical notebook file.

## Local and remote kernels

Remote operation connects to a configured Jupyter Server URL using its supported
authentication mechanism. Tokens and cookies are held by platform credential
storage where available and are never written into notebook metadata.

For local operation, a launcher may start a loopback-only Jupyter Server and
connect through the same REST and WebSocket interfaces. Process lifecycle is a
host concern around `yjupyter`; the protocol client itself does not assume it
owns the server process.

Because the only transport is Jupyter Server, *executing* a notebook — even
locally — requires a running Jupyter Server reachable over loopback. In the
reference deployment that server is the Python `jupyter-server` implementation,
so a Python runtime is an *initial deployment dependency*, not a protocol
invariant: a compatible server exposing the same REST + WebSocket surface could
be implemented or packaged without Python, and `yjupyter` would not know the
difference. *Viewing* a notebook has no such dependency at all: the static
viewer, nbformat load/save, and MIME rendering depend only on `ynotebook`,
`ymime`, and `yrender-mime`, never on `yjupyter` or a kernel. This split keeps
the first feature milestone — a static nbformat viewer — free of any server or
Python dependency.

The UI must clearly distinguish disconnect, kernel death, kernel busy state,
restart, and an execution whose final state is unknown after connection loss.

## Resource limits

The host supplies limits, enforced at the appropriate boundary — `yrender-mime`
for rendering, `yjupyter` for message transport, `ynotebook` for the document —
for:

- decoded image dimensions and bytes;
- individual output and aggregate notebook output size;
- retained stream text;
- HTML/CSS complexity;
- JSON tree depth and node count;
- simultaneous live figures and GPU allocations;
- message buffer count and total payload size;
- queued executions and outstanding requests.

Crossing a limit produces a visible, structured placeholder. It does not
silently delete the original notebook data.

## Errors and diagnostics

Each module reports errors at its own boundary:

- `yjupyter`: transport, authentication, protocol, timeout, and kernel errors.
- `ynotebook`: schema, mutation, routing, conflict, and persistence errors.
- `yrender-mime`: unsupported, malformed, unsafe, oversized, and renderer errors.
- `ymime`: classification and bundle-construction errors.

User-visible output errors remain part of the cell. Internal diagnostics carry
cell ID, request ID, message type, MIME type, and renderer name where applicable,
without logging authentication secrets or complete sensitive cell contents by
default.

## Testing

Tests follow Yetty's deterministic intermediate-first approach.

### `ymime`

- MIME-string-to-native-kind classification and extension mapping.
- Bundle construction, ordering, and preservation of unknown representations.
- Prologue codec and detect-path regression vectors (existing behavior).

### `yrender-mime`

- MIME preference and fallback tables.
- Metadata propagation.
- Invalid and oversized payload handling.
- Sanitization policy fixtures.
- Renderer lifecycle and intrinsic-size notifications.

### `yjupyter`

- WebSocket wire-format vectors, including binary buffers.
- Message correlation and channel dispatch.
- Recorded execute, completion, inspection, stdin, and comm sessions.
- Reply/IOPub ordering variations.
- Disconnect, reconnect, malformed message, and authentication behavior.

### `ynotebook`

- nbformat load/save golden files.
- Preservation of unknown fields and unsupported MIME data.
- Cell ID validation and stable serialization.
- Request-to-cell routing with interleaved executions.
- `clear_output(wait=true)` and `display_id` update semantics.
- External modification conflicts and recovery serialization.

### Integration

- Execute notebooks against reference Python kernels.
- Compare stored output models rather than screenshots first.
- Exercise Markdown, PNG, SVG, streams, tracebacks, stdin, and mutable displays.
- Verify that closing, clearing, replacing, and virtualizing output releases
  renderer resources.
- Add visual tests only where layout or rendering cannot be asserted through a
  stable intermediate representation.

## Feature progression

Development proceeds through capability boundaries rather than browser parity:

1. Static nbformat viewer with plain text, Markdown, images, SVG, and errors.
2. Kernel execution with correct stream, result, clear, update, stdin, and
   lifecycle semantics.
3. Editing, completion, inspection, notebook commands, autosave, and conflict
   handling.
4. Structured data renderers and native adapters for selected rich MIME types.
5. Native core widgets through comms.
6. Optional isolated compatibility rendering for active HTML and third-party
   widget views.

Each boundary leaves unsupported content preserved and visible through a safe
fallback.

## Open decisions

- Which JSON parser/serializer the main library set adopts — a new dependency,
  since the configuration substrate is YAML. The codec gates both `yjupyter`
  wire decoding and `.ipynb` persistence. **This must be decided together with
  the leaf `yetty_json_value` abstraction** (see the `ymime` *json* payload):
  the two are coupled, because the point of the abstraction is that the codec
  never becomes `ymime`'s public ABI. Deciding the parser without also fixing
  the owned-value API — or vice versa — reintroduces the leak the abstraction
  exists to prevent. The chosen parser must also be terminal-linkable, not just
  notebook-linkable, since leaf `ymime` links it.
- Whether nbformat *validation* uses bundled schemas or a helper based on the
  reference Python package (separate from the codec choice above).
- The concrete C API for `ymime` bundles and zero-copy binary buffers, shared by
  the terminal detect path, `ynotebook`, and `yrender-mime`.
- Which existing text editor should back code cells and what syntax service it
  consumes.
- Whether notebook views own one root figure container or a container per
  materialized cell.
- The trust database format and its integration with Yetty configuration.
- Which vendor MIME types warrant native renderers before HTML compatibility.
- How much output state remains materialized when a cell leaves the viewport.
- Whether live collaboration is explicitly excluded or introduced as a separate
  synchronization layer above the document model.

## Design rule

The dependency direction is fixed:

```text
ynotebook      -> yjupyter        (execution transport)
ynotebook      -> ymime           (bundle model + classification; leaf)
notebook view  -> yrender-mime    (renderer selection; links the renderers)
yrender-mime   -> ymime
yrender-mime   -> ymarkdown, yimage, ysvg, ypdf, ygui, ybrowser, …
yjupyter       -> ytransport (kernel WebSocket) + HTTP client (REST)
```

`ymime`, `yjupyter`, and `yrender-mime` do not depend on `ynotebook`, and none
depends on the notebook UI. The notebook *model* (`ynotebook`) links only leaf
`ymime`, so it stays render-free and serializable; only the notebook *view*
links `yrender-mime` and the renderer stack. This keeps MIME rendering reusable,
kernel transport testable, and the notebook model independent of any particular
view.
