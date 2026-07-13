# yacodec — audio codec facade (Opus decode)

`yacodec` mirrors [yvcodec](../yvcodec/README.md) on the audio side:
compressed packet in → interleaved float32 PCM out, behind a codec-agnostic
API. v1 implements exactly one backend — Opus via libopus (xiph, BSD
3-Clause). Its one consumer is the yvideo figure, which decodes the optional
audio track of the v2 yvideo wire envelope and pushes the PCM into the
platform audio device. Dependencies: `ycore`, `ytrace`, `libopus`.

## Codec identifiers

`enum yetty_yacodec_codec` (`include/yetty/yacodec/types.h`) is a **wire
identifier** — it matches the `audio_codec` uniform in the v2 yvideo
envelope, so values are stable and must not be renumbered:

| value | codec | status |
|-------|-------|--------|
| 0 | `NONE` | no audio in the stream (create() rejects it) |
| 1 | `OPUS` | implemented (libopus) |
| 2–5 | `VORBIS`, `FLAC`, `MP3`, `WAV` | reserved — decoders exist vendored inside miniaudio (stb_vorbis, dr_flac, dr_mp3, dr_wav) but are not wired up; create() returns "reserved but not implemented" |

## How it works

- `create(codec, sample_rate, channels)` — Opus accepts 8/12/16/24/48 kHz
  (48 kHz recommended; lower-rate streams are upsampled transparently) and
  mono/stereo only (the wire envelope carries no more).
- `feed()` decodes **exactly one Opus packet** per call (libopus rejects
  multi-packet buffers; a wire CMD_UPDATE carrying several length-prefixed
  packets must be unpacked by the caller) into a 1-second PCM ring buffer.
  If the ring is full the packet is dropped with a warning — losing ~20 ms of
  audio is preferred over back-pressuring the wire.
- `pull_pcm()` drains up to `max_frames` interleaved float32 frames from the
  ring; `*out_frames_written == 0` means "feed more".
- `reset()` issues `OPUS_RESET_STATE` and clears the ring (seek support).

## Public API sketch

```c
struct yetty_yacodec_decoder_ptr_result res =
    yetty_yacodec_decoder_create(YETTY_YACODEC_CODEC_OPUS, 48000, 2);
yetty_yacodec_decoder_feed(dec, packet, packet_size);      /* one packet */
size_t written = 0;
yetty_yacodec_decoder_pull_pcm(dec, pcm_buf, max_frames, &written);
yetty_yacodec_decoder_reset(dec);                          /* on seek */
yetty_yacodec_decoder_destroy(dec);
```

## Layout

| file | role |
|------|------|
| `decoder.c` | codec dispatch + the Opus backend + the SPSC PCM ring |
| `include/yetty/yacodec/types.h` | wire-stable codec enum |
| `include/yetty/yacodec/decoder.h` | decoder API |

Built as `yetty_yacodec`, gated on
`YETTY_ENABLE_FEATURE_YACODEC AND YETTY_ENABLE_LIB_LIBOPUS`
(`src/yetty/CMakeLists.txt`). A comment in `decoder.c` anticipates per-codec
sibling TUs (e.g. `opus-decoder.c`); today the Opus backend lives inline in
`decoder.c`.

## Status

Opus decode is functional and used in production by yvideo. The four
reserved codecs are enum slots only — no encoder exists in this module at
all (the encode side of A/V streaming has not landed).

## Consumers

- **yvideo** (`../yvideo/yvideo-hooks.c`) — one decoder per video instance
  when the stream advertises `audio_codec != NONE`; decoded PCM goes to the
  miniaudio-backed playback device in `../yplatform/audio/` whose
  `played_out_sec()` clock drives A/V sync.

## See also

- [yvcodec](../yvcodec/README.md) — the H.264 counterpart this module is
  modeled on.
- [yvideo](../yvideo/README.md) — the figure that owns decoder instances.
- [yaudio](../yaudio/README.md) — unrelated despite the name: offline WAV
  analysis, not playback or codecs.
