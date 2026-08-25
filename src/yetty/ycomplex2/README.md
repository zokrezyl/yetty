# ycomplex2 — v2 drawable classes for the complex kinds

One yclass class per complex kind, each deriving
[ydrawlist2](../ydrawlist2/README.md)`:drawable` and overriding its pack
slot with a call into the kind's existing GPU-less client emitter — no
wire layout is re-encoded here:

| class | packs via | source |
|---|---|---|
| `image` | `yetty_yimage_emit_into` (stb decode client-side) | `set_path` |
| `mesh` | `yetty_ymesh_render_path` → record lift | `set_glb` + camera properties |
| `shadertoy` | `yetty_yshadertoy_prim_serialize` + `add_prim` | `set_wgsl` (text) |
| `video` | `yetty_yvideo_render` → record lift (feature-gated) | `set_h264` + required `video_w`/`video_h` |

The plot kind lives in [api_yplot](../../api/yplot/) (its facade class IS
the drawable). Sources are referenced by inline path/text — no owned heap,
no destructors; the binding's generic object free reclaims the slices.

v2 limits: video is the one-shot record only (frame streaming via
CMD_UPDATE stays with the yvideo tool for now), video-only (no audio),
and does no SPS parsing — `video_w`/`video_h` are required properties.
The `2` suffix is transitional (epic #712).
