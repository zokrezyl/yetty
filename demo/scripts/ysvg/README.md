# ysvg demo scripts

Scripts that push SVG through `ycat`'s svg handler, which parses each file into
ydraw SDF + MSDF primitives that scroll into the host yetty terminal.

Run any of them from inside yetty, e.g.:

```sh
./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/gallery.sh
```

## Offline demos (bundled assets)

| Script | What it shows |
|---|---|
| `basic.sh` | A handful of the SVG assets in `demo/assets/svg/`. |
| `gallery.sh` | Every bundled asset once, flag → W3C acid test. |

## Infinite galleries (live, networked)

These loop forever, downloading SVGs from public "Flickr-for-SVG"-style
sources and showing them in random order. `ycat` fetches each URL itself via
libcurl; the scripts only pick *which* URL.

| Script | Source | Pool | Flavour |
|---|---|---|---|
| `wikimedia.sh` | Wikimedia Commons search API | millions | Real artwork: flags, coats of arms, maps, diagrams, silhouettes, seals, illustrations. Very diverse, often complex. |
| `openclipart.sh` | Openclipart (`openclipart.org`) | ~350k | Public-domain (CC0) community clip art — the original "Flickr for SVG". Older, varied authoring styles → good compatibility testing. |
| `svgrepo.sh` | SVG Repo (`svgrepo.com`) | huge | Free vectors & icons, simple → complex. **Cloudflare-gated — see note below.** |
| `iconify.sh` | Iconify API (`api.iconify.design`) | hundreds of sets, tens of thousands of icons | Aggregates most open icon sets; great for path/stroke regression across authoring styles. |
| `bioicons.sh` | Bioicons (`bioicons.com`) | ~2.8k | Biological / scientific icons (cells, molecules, lab apparatus). |
| `svg-testsuite.sh` | resvg test suite (GitHub via jsDelivr) | ~1.6k | **Renderer conformance cases**, not art — shapes/paths/painting/gradients/text/masking/clip/transform edge cases. Best for finding parser bugs. |
| `emoji.sh` | jsDelivr CDN — OpenMoji, Twemoji | ~8k | Full-colour vector emoji. |
| `icons.sh` | jsDelivr CDN — Tabler, Material Design Icons, Simple Icons, Bootstrap Icons, Lucide | ~20k | Icon / brand-logo line art. |

Shared plumbing lives in `_infinite.sh` (ycat resolution, `show_url` /
`show_svg_from_url`, the jsDelivr index cache, random-line picking, the parse-
failure log). It is sourced, not run directly.

**Two fetch paths.** `wikimedia.sh`, `emoji.sh`, `icons.sh` hand each URL
straight to ycat (`show_url`, ycat downloads). `openclipart.sh`, `svgrepo.sh`,
`iconify.sh`, `bioicons.sh`, `svg-testsuite.sh` download the bytes themselves
(`show_svg_from_url`) so they can drop placeholder redirects, **log SVGs that
fail to parse**, and emit fetch debug. Files ycat's svg handler rejects are
appended to `$FAIL_LOG` (their bytes saved under `$FAIL_DIR`) instead of being
dumped as raw text — that log is the point of the test-suite gallery.

### Knobs (env)

- `DEMO_PAUSE=<seconds>` — pause between figures (default `0.5`; `0` = flat out).
- `DEMO_WIDTH=<cells>` — card width in terminal cells (default `12`, small
  thumbnails; raise for larger renders).
- `DEMO_COUNT=<n>` — stop after `n` figures (default `0` = endless). Handy for
  a quick sample or a timed run:
  ```sh
  DEMO_COUNT=5 ./build-desktop-ytrace-release/yetty -e demo/scripts/ysvg/emoji.sh
  ```
- `YCAT=<path>` — override the ycat binary.

`wikimedia.sh` also takes an optional topic argument to narrow the stream:

```sh
./build-desktop-ytrace-release/yetty -e 'demo/scripts/ysvg/wikimedia.sh coat of arms'
```

### How the sources stay fresh

- **Wikimedia**: the Commons `list=search` generator with
  `filemime:image/svg+xml` and a random `gsroffset` samples the SVG corpus;
  each round shuffles a small batch, so order is effectively random.
- **Openclipart**: each round picks a random numeric artwork id and fetches
  `https://openclipart.org/download/<id>`, which redirects to the artwork's
  SVG. Ids with no artwork (removed pieces) are skipped silently.
- **SVG Repo**: the vector pool is discovered from `svgrepo.com`'s sitemap
  (`/svg/<id>/<slug>` pages → `/download/<id>/<slug>.svg` files), cached under
  `tmp/`, then sampled at random.
- **Iconify**: the set list (`/collections`) and each set's icon-name list
  (`/collection?prefix=<set>`) are cached; each round picks a random set then a
  random icon → `https://api.iconify.design/<set>/<name>.svg`.
- **Bioicons**: `icons.json` is turned into
  `…/icons/<license>/<category>/<author>/<name>.svg` URLs; a minority 404 on
  author/name slug quirks and are skipped.
- **SVG test suite**: the resvg suite's file tree is listed via the jsDelivr
  GitHub data API; every `/tests/**.svg` becomes a CDN URL.
- **jsDelivr sets**: each round resolves the package's *latest* published
  version from `data.jsdelivr.com`, so the pools track upstream without any
  pinned version in the script.

### Sources that don't work from a script

- **unDraw** (`undraw.co`) — no public listing/download API (all probed
  endpoints 404); illustrations are loaded via an internal, unstable JSON. Not
  scriptable without scraping the app bundle.
- **ManyPixels** (`manypixels.co`) — behind Cloudflare (HTTP 403 challenge),
  same wall as SVG Repo. Not scriptable from a plain client.

Both are omitted rather than shipped as scripts that never fetch anything.

### Requirements

`curl` and `python3` on `$PATH`, plus network access. With no network the
scripts print a one-line notice and exit cleanly after a few misses.

### Note on SVG Repo (Cloudflare)

`svgrepo.com` is served through Cloudflare bot protection. From an ordinary
desktop network it loads normally and `svgrepo.sh` works; from data-centre /
VPN / cloud IPs Cloudflare returns HTTP 429 to *every* request (including the
sitemap), so the script cannot enumerate anything and exits with a short
notice. There is no API key that bypasses this — run it from an unblocked
network.

### Note on Openclipart's old API

Openclipart's JSON search API is defunct — `/search/json/` now 302-redirects
to the homepage. `openclipart.sh` therefore does **not** use it; it fetches
`/download/<id>` by random id instead (that path still works and redirects to
the SVG). Don't reintroduce the JSON API without confirming it is live again.
