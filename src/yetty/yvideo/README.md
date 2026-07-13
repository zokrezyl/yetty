# yvideo — streaming H.264 video as a stateful ydraw figure

yvideo plays an H.264 (Annex-B) video — optionally with Opus audio — inside a
ydraw composite primitive (type id `0x80000006`). Unlike yimage, the figure is
stateful on the receiving terminal: the INIT envelope creates a playing
instance, and the sender keeps streaming bytes to it through `CMD_UPDATE`
envelopes addressed via `CMD_GROUP(id)`. Decoding happens receiver-side with
openh264 ([`yvcodec`](../yvcodec/README.md)) and libopus
([`yacodec`](../yacodec/README.md)); the composite model is described in
[`ydraw`](../ydraw/README.md).

## Two-tier build

| target | contents | gating |
|--------|----------|--------|
| `yetty_yvideo_core` | wire serializer, OSC emit, MP4 demux — `yvideo.c`, `yvideo-gen-wire.c`, `yvideo-mp4.c` | always; minimp4 optional (`YETTY_ENABLE_LIB_MINIMP4`, stubs return ERR without it) |
| `yetty_yvideo` | generated factory + hand-written lifecycle hooks — `yvideo-gen.c`, `yvideo-hooks.c` | `YETTY_ENABLE_LIB_WEBGPU` + `YETTY_ENABLE_LIB_OPENH264`; audio further gated on `YETTY_ENABLE_FEATURE_YACODEC` + libopus + miniaudio |

## Wire model

The INIT payload carries 14 uniform words (`bounds_*`, `video_w/h`,
`chroma_w/h`, `fps`, `color_matrix`, `flags`, `audio_codec`,
`audio_sample_rate`, `audio_channels`) plus two buffers: `nal_stream` (raw
Annex-B bytes; at minimum SPS + PPS + first IDR) and `audio_stream`
(length-prefixed Opus packets). Subsequent `CMD_UPDATE` envelopes carry a
typed payload — `[u8 op][u8 reserved[3]][body…]`:

| op | constant | body |
|----|----------|------|
| 0x00 | `YETTY_YVIDEO_UPDATE_OP_APPEND_NAL` | raw Annex-B bytes |
| 0x01 | `YETTY_YVIDEO_UPDATE_OP_APPEND_AUDIO` | length-prefixed audio packets |
| 0x02 | `YETTY_YVIDEO_UPDATE_OP_SEEK_PTS_MS` | `u32` pts_ms (decoders reset) |
| 0x03 | `YETTY_YVIDEO_UPDATE_OP_SET_PLAYING` | `u8` play/pause |
| 0x04 | `YETTY_YVIDEO_UPDATE_OP_SET_SPEED` | `f32` (video clock only) |
| 0x05 | `YETTY_YVIDEO_UPDATE_OP_SET_LOOP` | `u8` (flag toggle only) |

## Receiver internals (`yvideo-hooks.c`)

The generated factory calls four hook entry points (`hooks: true` in
`yvideo.yaml`): instance create/destroy/update/render_pre. The hooks own the
prim-specific state: a per-instance openh264 decoder, an append-only NAL ring
with a start-code splitter (one access unit per decode call), a
length-prefixed audio packet ring, per-frame YUV→BGRA conversion into a CPU
scratch buffer, and a libuv-timer-driven pump at the source fps (render-driven
pumping would stall between envelopes — yetty only renders on input). With
audio active, the platform audio device's playback position is the master
clock for A/V sync; without audio, wall clock vs start time is used. Rings
are capped (64 MB NAL / 16 MB audio) and only already-decoded bytes are
dropped.

## Public API (`include/yetty/yvideo/yvideo.h`, `yvideo-mp4.h`)

```c
struct yetty_yvideo_render_config config = {
    .video_w = 0, .video_h = 0,      /* filled from the SPS by the MP4 path */
    .fps = 30.0f,
    .flags = YETTY_YVIDEO_FLAG_LOOP | YETTY_YVIDEO_FLAG_AUTOPLAY,
};
/* MP4 in, drawable list out — demux + SPS dimension parse + serialize: */
struct yetty_ydraw_drawable_list_result list_res =
    yetty_yvideo_render_from_mp4_file("clip.mp4", &config);
/* Or feed raw Annex-B NAL bytes (+ optional audio packets) directly: */
list_res = yetty_yvideo_render(nal_bytes, nal_len, audio_bytes, audio_len, &config);

yetty_yvideo_osc_bin_emit(list_res.value, stdout);
yetty_ydraw_drawable_list_destroy(list_res.value);
```

Streaming senders wrap the prim in a `CMD_GROUP(id)` themselves so later
`CMD_UPDATE` envelopes can target it — `demo/yvideo/video-source.c` is the
reference driver.

## File map

| file | role |
|------|------|
| `yvideo.c` | hand-written: config → wire serialize → drawable list, OSC emit |
| `yvideo-mp4.c` | hand-written: minimp4 demux, AVCC→Annex-B, SPS dimension parse (sender-side) |
| `yvideo.yaml` | composite schema — uniforms/buffers/texture layout, `hooks: true` |
| `yvideo-gen-wire.c` | generated wire serializer (CPU-only) |
| `yvideo-gen.c` | generated factory/instance skeleton; calls the hook surface |
| `yvideo-hooks.c` | hand-written lifecycle: decoders, rings, pump, A/V clock |
| `yvideo.wgsl` | hand-written shader — samples the per-frame texture into the AABB |
| `yvideo-gen.wgsl` | generated uniform accessors |

Generated files come from [`ydraw-gen`](../ydraw-gen/README.md)'s
`generate.py`; never edit them by hand.

## Status

Playback with streaming updates and v2 audio works. Known limits:
`CMD_UPDATE` is honoured only on the scene canvas (on the scrolling canvas the
INIT envelope still creates a playable instance but updates are dropped);
`SET_LOOP` toggles the flag but EOS-driven looping is not implemented; the MP4
demux runs sender-side (moving it server-side is tracked in `yvideo-mp4.h`).

## Consumers

- `tools/yvideo` — CLI sender (`yvideo clip.mp4` → OSC envelope on stdout).
- [`ycat`](../ycat/README.md) — `handler-video.c` for video files.
- [`ygui`](../ygui/README.md) — the `widgets/yvideo.c` figure widget.
- `demo/yvideo/video-source.c` — streaming CMD_UPDATE reference driver.
- Factory registration on the receiver: `yterminal/terminal.c`, `yui/yui.c`,
  `ydraw/scrolling-canvas.c`.
