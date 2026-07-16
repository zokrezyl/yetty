# yshadertoy demos

Two flavours of demo live here:

- **`basic.sh`** — runs `ycat -c shadertoy` over the bundled WGSL assets in
  `demo/assets/yshadertoy/` (palette / swirl / plasma). No network, no
  conversion — the WGSL is already in yetty's `mainImage` form.

- **`NN-<name>.sh` + `run-all.sh`** — the full import pipeline against real
  shaders on shadertoy.com, driven over yctl RPC and screenshotted with
  yetty's own `screenshot` op:

  ```
  yctl run  "yshadertoy.py <id>"    # fetch (no API key) -> GLSL -> SPIR-V -> WGSL -> ycat
  yctl screenshot <id>.ppm          # capture the real GPU frame
  ```

## The ten shaders

Each is a single Image pass with no texture channels, so it converts and
renders directly (multipass / `iChannel` shaders are not yet renderable).

| # | script | id | shader | author |
|---|--------|----|--------|--------|
| 01 | `01-creation.sh` | `XsXXDn` | Creation | Silexars |
| 02 | `02-seascape.sh` | `Ms2SD1` | Seascape | TDM |
| 03 | `03-star-nest.sh` | `XlfGRj` | Star Nest | Kali |
| 04 | `04-raymarching-primitives.sh` | `Xds3zN` | Raymarching - Primitives | iq |
| 05 | `05-protean-clouds.sh` | `3l23Rh` | Protean Clouds | nimitz |
| 06 | `06-menger-sponge.sh` | `4sX3Rn` | Menger Sponge | iq |
| 07 | `07-phantom-star.sh` | `ttKGDt` | Phantom Star | kasari39 |
| 08 | `08-auroras.sh` | `XtGGRt` | Auroras | nimitz |
| 09 | `09-combustible-voronoi.sh` | `4tlSzl` | Combustible Voronoi | Shane |
| 10 | `10-warping.sh` | `lsl3RH` | Warping - procedural 2 | iq |

## Running

Start a yetty with the RPC server, in a terminal that is **not** the one you
run the demos from:

```sh
./build-desktop-ytrace-release/yetty --rpc-port=9999 -e bash
```

Then, from another terminal:

```sh
export TINT=/path/to/tint            # from a dawn-exotic release tarball
./demo/scripts/yshadertoy/run-all.sh # all ten, one screenshot each
# or a single shader:
./demo/scripts/yshadertoy/03-star-nest.sh
```

If nothing is listening on the RPC port, each script launches a throwaway
yetty, drives it, and shuts it down again.

Screenshots land in `tmp/yshadertoy-demo/shots/<id>.png`.

## Knobs (environment)

| var | default | meaning |
|-----|---------|---------|
| `YCTL_HOST` / `YCTL_PORT` | `127.0.0.1` / `9999` | target instance |
| `YETTY_BUILD_DIR` | `build-desktop-ytrace-release` | build tree with `yetty` + `ycat` |
| `TINT` | — | path to the `tint` CLI (required for the WGSL step) |
| `YSHADERTOY_SETTLE` | `30` | seconds to wait for fetch+convert+draw before the shot |
| `YSHADERTOY_SHOTDIR` | `tmp/yshadertoy-demo/shots` | where screenshots land |

## No API key

The first fetch clears Cloudflare's bot challenge in a headless Chrome and
caches the `cf_clearance` cookie under `~/.cache/yshadertoy/cf-profile`, so
subsequent fetches are quick. No Shadertoy account or app key is needed.
