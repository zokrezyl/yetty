# ymux ↔ tmux text-renderer byte parity (#699)

The classical text path of ymux must produce **byte-identical** terminal output
to tmux (pinned commit `d5afb67`) for the same screen operations, geometry, and
terminal capabilities, carried as opaque yRPC bytes to the client `vtermgrid`.
This is **not** a semantic-cell protocol and **not** the legacy DCS transport.

## The emitter — `src/yetty/ymux/tty-render.{c,h}`

Faithful ports of tmux's `tty.c` / `tty-draw.c` emitters for the fixed
`xterm-256color` capability profile (no horizontal margins in v1). Plain-C leaf
helpers over a `struct yetty_ymux_tty` (the ymux analog of tmux's `struct tty`:
cursor cache, pen/SGR cache, geometry, scroll region, cursor visibility).

| Component | tmux origin | Emits |
|---|---|---|
| `yetty_ymux_tty_cursor` | `tty_cursor` | shortest move: home, `\r`/`\r\n`, cub1/cuf1/cuu1/cud1, cub/cuf/cuu/cud, hpa/vpa, absolute cup, cub1×2 |
| `yetty_ymux_tty_attributes` | `tty_attributes` | reset-minimized SGR (`sgr0=\e(B\e[m`) + colours + newly-set attr bits |
| `yetty_ymux_tty_colours` | `tty_colours` | `\e[39m`/`\e[49m` default, setaf/setab basic/bright/256 |
| `yetty_ymux_tty_cursor_visible` | `tty_update_mode` | cnorm `\e[?12l\e[?25h` / civis `\e[?25l` on change |
| `yetty_ymux_tty_putn` | `tty_putn` | verbatim text + width advance + **deferred autowrap** park |
| `yetty_ymux_tty_clear_line` / `_chars` | `tty_clearline` | el `\e[K` / ech `\e[<n>X` (BCE) |
| `yetty_ymux_tty_draw_line` | `tty_draw_line` | composes the above per row; trailing default blanks → EL |
| `yetty_ymux_tty_invalidate` | `tty_invalidate` | discards assumed state (next move absolute) for attach/resync/recovery |

Unit tests (byte-exact, hand-derived): `tty-render-test.c` (`ymux_tty_render`).

## The differential harness — `tmux-parity-harness.py`

Drives a **real** tmux over a pty attach on an **isolated** socket
(`-L ymux-parity-harness` — never the user's tmux), captures the exact bytes
tmux writes to its client, and asserts the emitter's byte forms appear verbatim.

```sh
python3 test/ut/ymux/tmux-parity-harness.py          # system tmux
YMUX_PARITY_TMUX=tmp/tmux/tmux python3 test/ut/ymux/tmux-parity-harness.py   # pinned
```

Non-hermetic (needs a `tmux` binary; forks a pty); **not** a ctest.

**Validated (11 checks, 8/9 primitives) against the PINNED tmux `d5afb67`
(`tmux next-3.8`):** the initial complete redraw of an empty 80×24 pane yields
sgr0, cnorm, civis, el, cup, and the per-row blank redraw `\e[K\r\n`; a driven
`printf 'X\e[1mY\e[31mZ\e[m'` yields bold `\e[1m`, red `\e[31m`, and verbatim
`X`/`Y`/`Z`. These are the exact bytes the emitter produces — empirically
validated against the #699 normative baseline, not just hand-derived.

### Building the pinned tmux (`tmp/tmux`, `d5afb67`)

Native `libevent`/`ncurses` come from nix; build in a **coherent nix-shell** so
the toolchain glibc matches the deps (the default-channel `ncurses` needs a newer
glibc than the dev-profile cc). `forkpty` is in glibc's libc here, but
configure's link test misfires, so force the cache var:

```sh
nix-shell -p libevent ncurses gcc gnumake bison flex pkg-config autoconf automake libtool \
  --run 'cd tmp/tmux && sh autogen.sh && ./configure ac_cv_search_forkpty="none required" && make -j4'
```

The harness auto-prefers `tmp/tmux/tmux` when present.

## The deterministic oracle — `tools/tmux-oracle/`

The live-attach capture above is **provably non-deterministic** for incremental
deltas (the same delta flips between a bare fast-path emission and one wrapped
in a redraw trailer, run to run — a scheduler race between tmux's flush and its
redraw timer). The oracle removes the scheduler: it links the pinned tmux's own
object files (`build.sh` renames `main` away with objcopy; no tmux source is
touched), fabricates the client/session/window/pane **in process** (pty pair
for the tty, terminfo caps shipped the way a real client ships them, UTF-8
locale, layout cell), feeds bytes straight into `input_parse_buffer`, and pumps
libevent **manually** — flush-to-quiescence before each `server_client_loop`,
exactly the regime a healthy real server converges to. Same (base, delta) pair
⇒ same bytes, every run.

```sh
./tools/tmux-oracle/build.sh          # → tmp/tmux-oracle-build/tmux-oracle
tmp/tmux-oracle-build/tmux-oracle 24 80 base.bin delta.bin   # stdout = delta bytes
```

## The differential — `tmux-diff.py`

Full-screen redraws (live capture, deterministic pre-detach snapshot):

```sh
python3 test/ut/ymux/tmux-diff.py                 # 16/16 byte-identical
```

Incremental deltas (oracle vs `vtdiff-driver <rows> <cols> <split> [profile]`):

```sh
python3 test/ut/ymux/tmux-diff.py --incremental   # 58/58 byte-identical, RAW
```

Incremental comparison is **RAW — zero canonicalization** (review #11): the
oracle is deterministic, so parity is exact bytes, including tmux's post-SU
scrollbar-pass cursor wrap (the projector emits it deliberately) and the
ordered-history cases (put-then-EL, ICH+DCH cancellation, same-window
overwrite) that a settled-state differ cannot produce. One case runs under a
negotiated **256-color profile** (oracle features `256`, driver profile
`256`): the RGB SGR downgrades to the same palette index tmux picks — this
case caught a real bug (DEFAULT = -1 carries the RGB flag bit; without the
explicit guard, 256-color clients painted default cells white).

The 18-case incremental matrix covers: append/overwrite echo, ICH `\e[1@`,
DCH `\e[1P`, bottom-row scroll (tmux's `\r\n` idiom — pushes the top line
into the CLIENT terminal's scrollback, unlike `\e[nS`), multi-line INDN
`\e[nS`, DECSTBM region scroll (`\e[t;br` + LF + `\e[K` + region reset),
region + full-screen reverse-index (`\eM`), EL shapes (`\e[K` at cursor /
whole line) with colored BCE, ED (`\e[J` split: block clear + row-tail EL),
SGR runs with the end-of-flush `\e(B\e[m` pen reset, wide CJK, combining-mark
staging (base glyph, then cursor-back + full cluster per mark — tmux's
`screen_write_combine` wire shape), and pure cursor motion.

Canonicalization (documented in `tmux-diff.py`): `\e[1;1H` → `\e[H` (tmux
emits either for home depending on wrap-pending state) and the no-op cursor
wrap `\e[?25l\e[?12l\e[?25h` (a redraw pass that painted zero cells).

## Full-mode scope

Full mode compares the complete redraw BODY (the settled segment) from a live
capture. The ATTACH PREAMBLE is additionally compared byte-for-byte in
`--preamble` mode: the oracle dumps tmux's real attach segment (home+2J,
cursor sync, mouse resets, 2004h/2031h, the ?996n theme query, pen reset,
region, hidden-cursor row clears, cursor show) and the projector emits the
same 347 bytes; three outer segments owned by the surface (\e[?1049h,
\e[22;0;0t, \e[?1h\e=) are verified-then-stripped and documented in the
comparator. Production attach advertises `YMUX_TERM_CAP_ATTACH_PREAMBLE`, so
the ?996n reply loop is exercised live. Both former dim/colour-intent XFAILs
are RETIRED via filter-carried pen state (no fork changes). GPU readbacks:
`e2e_ymux_grid_readback` gates bg fill, fg glyphs, reverse video, wide
placement, combining-cluster placement, selection, underline decoration,
cursor variants (block/bar/hidden via DECSCUSR), and last-column autowrap-off
clipping; running that lane in CI needs a software WebGPU adapter (lavapipe)
in the `yetty-build` image — an infra decision recorded for the operator.

## Terminfo/features state model (review #17 item 8)

The capability profile is resolved through tmux's pipeline
(`yetty_ymux_tty_caps_resolve`, mirroring tty-features.c): TERM-family base
capabilities (10 families: xterm/-256color/-nobce/-nocsr, screen/-256color,
tmux/-256color, linux, vt100 — each row verified against the pinned tmux's
terminfo) → TERM-implied default features (tty_default_features) → the
client's explicit features string (features only ever add; RGB implies 256,
usstyle implies Smulx+Setulc, hyperlinks implies OSC-8). The wire carries it:
ATTACH (proto 9) appends [term_len][features_len][term][features]; the daemon
resolves server-side (`projector_set_terminal`), the mask keeping only the
mode bits (VT_TEXT/ATTACH_PREAMBLE). The driver's `term:<name>[:<features>]`
profile routes the SAME strings to both sides, and seven `res-*` comparator
cases pin that resolution reproduces every hand-set profile plus the
screen-256color family. Boundary: arbitrary TERMs outside the family table
(a compiled-terminfo reader) fall back to the xterm-256color base.

## Remaining
- **Coverage extensions, comparator-driven** — the matrix now includes IL/DL
  (full-screen AND inside active margins, command-keyed with operation-time
  cursor), ECH/EL1, tabs, autowrap + phantom cursor, alternate-screen
  transitions (full pane redraws), exotic-pen attribute drops, and three
  negotiated profiles: 256-colour downgrade, no-ECH spaces fallback
  (TERM=screen), and no-BCE coloured clears (tty_fake_bce spaces across
  EL/EL1/ECH/ED, matched against a derived xterm-nobce terminfo the harness
  compiles with tic), and no-CSR (xterm-nocsr: EVERY scroll-shaped op —
  full-screen LF/RI and DECSTBM-scoped IL/DL — becomes tmux's
  tty_redraw_region, with tmux's small/large split ported exactly: a
  LARGE region (>= half the view, tty_large_region) defers to ONE
  whole-pane redraw of the FINAL screen absorbing every later op; a
  SMALL region redraws IMMEDIATELY per command from the operation-time
  shadow, so multi-op sequences (IL then DL) emit tmux's sequential
  intermediate redraws. The no-CSR cursor model is faithful too: without
  CSR tmux can never set its region cache, so rupper/rlower stay
  UINT_MAX and every same-column multi-row up-move emits VPA — 10
  broadened cases (DL, multi-line IL/DL, IL+DL one flush, 2-row scroll,
  scroll+write, two regions, region RI, large-partial ±write) all
  byte-identical). The
  bottom-right deferred-wrap idiom is PORTED (scroll first, pending rows
  shift, the wrapped run writes through at the pre-wrap origin with the
  trailing EL; 2- and 3-glyph variants pinned). Wide-char-at-boundary wrap
  and mode-2026 passthrough are pinned. Exotic pen, MEASURED: on the
  default profile tmux drops underline COLOUR (58/59) silently (no attr,
  no reset — now matched) while overline/extended-underline reset on
  clear (pinned: exotic-dropped-default). The ENABLED profile
  ("256,RGB,hyperlinks,usstyle") emits the VERBATIM colon forms and
  interned OSC-8 ids (`\e[4:3m`, `\e[58:5:196m`,
  `\e]8;id=tmux1;http://x\e\\`) — value carriage through the op stream
  (engine filter capture -> cell stamping -> cell-aware emission) is
  IMPLEMENTED and pinned (exotic-enabled-values / exotic-enabled-styles).
- **Copy-mode chrome (daemon seat)** — the chrome consumer is a real
  interactive mode: arrows move the copy cursor (view-bounded, edge
  scrolls), space anchors, enter copies the normalized span byte-exact
  into the daemon paste buffer, q releases (CHROME_RELEASE), the
  `paste` verb delivers through the mode-aware engine paste path; the
  whole machine is wire-pinned (daemon-test).
- **Anchored-view projection** — a scrolled-back (anchored) view
  invalidates the op-delta model (ops describe the LIVE screen): an
  anchor move or follow toggle projects a COMPLETE redraw of the new
  view, and while anchored the replay yields to the shadow cell-diff.
  Wire-pinned (the anchored projection's bytes carry the scrollback
  rows) and live-verified by the overlay scroll e2e.
- **YETTY presentation defects found by the live harness (outside ymux —
  the WGPU render target is CORRECT in every case, verified by dual
  capture):** (1) `yctl screenshot` reads the WGPU target texture while
  X11-tile presentation drives the visible window — the texture can lag
  minutes behind (the e2e now captures the X root); (2) the X11-tile path
  DROPS green-dominant pixels (same frame: target green+red, window red
  only, green rows render black) — e2e probes switched to blue/red and
  thresholds made presence-based until fixed; (3) intermittent
  non-presentation of a freshly printed row — the placement/clip phases
  now GATE after repaint-nudge retries (fresh damage repaints the stale
  tiles), and rows measured on either surface (X window or WGPU target)
  count. Reproducers: dual-capture flows in tmp/dbg34-36 recipes.
  FIXED during review #17 (yetty-side, by this branch): the yscene prim
  pass dropped `target->viewport` for non-absolute figures (staged
  overlay chrome hittable yet invisible), and seat_overlay'ed figures'
  hit space was displaced by the window chrome height (the compositor
  hit test ran in window coords against pane-local rects) — both fixed
  in scene.c / container.c with the seated flag.
