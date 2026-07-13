# yvcodec — H.264 encode/decode wrapping openh264

`yvcodec` is a small pure-C facade over openh264's C COM-style vtable API
(`ISVCEncoder` / `ISVCDecoder` as vtable pointers — not the C++ virtual-class
interface), plus scalar BGRA↔YUV420 color-conversion helpers. It is consumed by
the VNC server (framebuffer → H.264 stream), the VNC client (H.264 stream →
framebuffer), and the yvideo figure (inline video playback in the terminal).
Only dependencies: `ycore` (results), `ytrace` (openh264 log bridging), and
`openh264` itself.

## How it works

- **Encoder** (`encoder.c`) — created from a
  `struct yetty_yvcodec_encoder_config` (width/height/bitrate/fps/IDR
  interval/`screen_content` for openh264's `SCREEN_CONTENT_REAL_TIME` mode).
  One `encode()` call takes YUV420 planes and returns a
  `struct yetty_yvcodec_encoded_frame` whose `data` points into the encoder's
  internal buffer (valid until the next call). Rate-control skips surface as
  `size == 0`. `force_idr()` makes the next frame a keyframe — used when a new
  VNC client connects so it gets a decodable stream from frame 1.
- **Decoder** (`decoder.c`) — `create_h264()` needs no configuration;
  dimensions come from the first SPS NAL. `feed()` buffers incoming NALs,
  `get_frame()` runs `DecodeFrameNoDelay` and copies the planes out (openh264
  clobbers its internal buffers on the next decode), so callers get pointers
  stable until their next decoder call. `flush()` drains reordered frames at
  stream end; `reset()` restarts for a new stream (seek).
- **Color conversion** — `yetty_yvcodec_bgra_to_yuv420` (BT.709 video range,
  encoder side) and `yetty_yvcodec_yuv_frame_yuv420_to_bgra` (matrix selected
  by `frame->color_matrix`: BT.601/709/2020, decoder side). Pure scalar CPU
  paths, usable without a codec instance.
- openh264's internal log is bridged into ytrace (`yerror`/`ywarn`/`ydebug`).

## Public API sketch

```c
/* Encode (VNC server) */
struct yetty_yvcodec_encoder_config cfg;
yetty_yvcodec_encoder_config_defaults(&cfg, width, height);
struct yetty_yvcodec_encoder_ptr_result enc_res =
    yetty_yvcodec_encoder_config_encoder_create(&cfg);
struct yetty_yvcodec_encoded_frame out;
yetty_yvcodec_encoder_encode(enc, y, u, v, y_stride, uv_stride, &out);

/* Decode (VNC client, yvideo) */
struct yetty_yvcodec_decoder_ptr_result dec_res = yetty_yvcodec_decoder_create_h264();
yetty_yvcodec_decoder_feed(dec, nal_bytes, nal_size);
bool got = false;
struct yetty_yvcodec_yuv_frame yuv;
yetty_yvcodec_decoder_get_frame(dec, &yuv, &got);
if (got)
    yetty_yvcodec_yuv_frame_yuv420_to_bgra(&yuv, bgra_buf);
```

All fallible entry points return `YETTY_YRESULT_DECLARE`d result structs
(`yetty_yvcodec_encoder_ptr`, `yetty_yvcodec_decoder_ptr`,
`yetty_ycore_void`).

## Layout

| file | role |
|------|------|
| `encoder.c` | H.264 encoder (openh264 C vtable), BGRA→YUV420 helper |
| `decoder.c` | H.264 decoder, per-frame YUV copy-out, YUV420→BGRA helper |
| `include/yetty/yvcodec/types.h` | `yuv_frame`, `encoded_frame`, color-matrix enum |
| `include/yetty/yvcodec/encoder.h` / `decoder.h` | public API |

Built as `yetty_yvcodec` when openh264 is available
(`YETTY_ENABLE_LIB_OPENH264` / `YETTY_ENABLE_FEATURE_YVCODEC` in
`src/yetty/CMakeLists.txt`); consumers that must build without it (webasm)
guard their calls with `YETTY_HAS_YVCODEC`.

## Consumers

- **yvnc server** (`../yvnc/vnc-server.c`) — encodes the framebuffer as an
  H.264 stream per client; uses `force_idr` on connect and `set_bitrate` for
  runtime tuning.
- **yvnc client** (`../yvnc/vnc-client.c`) — decodes incoming H.264 rects to
  BGRA.
- **yvideo** (`../yvideo/yvideo-hooks.c`) — one decoder per video figure
  instance; NAL ring feeding, YUV upload to the GPU. Audio for the same
  stream goes through [yacodec](../yacodec/README.md).
- **ycat** ships H.264 Annex-B bytes toward yvideo (`../ycat/handler-video.c`,
  compile-gated on yvideo + yvcodec presence) but does not link yvcodec
  directly.

## See also

- [yacodec](../yacodec/README.md) — the audio-side mirror of this module.
- [yvideo](../yvideo/README.md) — the video figure that drives the decoder.
- [yvnc](../yvnc/README.md) — VNC server/client, the encoder's main consumer.
