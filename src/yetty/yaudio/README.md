# yaudio — offline audio analysis (WAV reader, RMS envelope, interval detector)

`yaudio` is a pure-C library for *analyzing* audio files offline: an
mmap-backed PCM WAV reader, a per-frame RMS envelope, and an energy-based
"active interval" detector. It is built for very large recordings
(multi-hour, many-GB sensor captures) — the file is mapped read-only and
decoded on the fly, never copied to the heap. Consumers are the
`tools/yaudio` analyzer GUI and the `tools/yaudio-intervals` batch CLI.
Dependencies: `ycore` and libm only.

It is **not** the playback path: audio playback lives in
`../yplatform/audio/` (miniaudio) and compressed-audio decode in
[yacodec](../yacodec/README.md).

## The three layers

1. **`wav.c`** — RIFF/WAVE parser over an mmap (POSIX `mmap` or Win32 file
   mapping). PCM only — int16/int24/int32/float32, interleaved multichannel;
   non-`fmt`/`data` chunks (LIST, JUNK, bext, …) are skipped.
   `yetty_yaudio_wav_read_channel_f32` decodes one channel into normalized
   [-1, 1] float32 straight from the mapping.
2. **`envelope.c`** — per-frame RMS of one channel
   (`struct yetty_yaudio_envelope`, default 1024-sample frames/hop). The same
   array serves display (a yplot waveform) and analysis.
3. **`intervals.c`** — two-pass detector: pass 1 computes the RMS
   distribution and picks the noise floor at a percentile (default 15th);
   pass 2 walks the envelope with hysteresis (open at floor + 10 dB, close at
   floor + 6 dB by default), then applies min-duration / min-gap filtering.
   Result: `struct yetty_yaudio_intervals` with per-interval start/end
   seconds, peak and mean dBFS, plus the thresholds used.

Both heavy passes accept an optional `yetty_yaudio_progress_fn` callback
(`include/yetty/yaudio/progress.h`) reporting a monotonic [0, 1] fraction —
the GUI runs them on a yworkpool worker and animates a progress bar from it.

## Public API sketch

```c
struct yetty_yaudio_wav_ptr_result wav_res = yetty_yaudio_wav_open(path);

struct yetty_yaudio_envelope_ptr_result env_res =
    yetty_yaudio_envelope_create(wav, /*channel=*/0, 0, 0, progress_cb, ud);

struct yetty_yaudio_intervals_config cfg;
yetty_yaudio_intervals_config_defaults(&cfg);
struct yetty_yaudio_intervals_ptr_result iv_res =
    yetty_yaudio_intervals_find(wav, 0, &cfg, progress_cb, ud);

yetty_yaudio_intervals_destroy(iv_res.value);
yetty_yaudio_envelope_destroy(env_res.value);
yetty_yaudio_wav_close(wav_res.value);
```

## Layout

| file | role |
|------|------|
| `wav.c` | mmap open/close, header parse, channel → float32 decode |
| `envelope.c` | RMS envelope over one channel |
| `intervals.c` | noise floor estimate + hysteresis interval walk |
| `include/yetty/yaudio/{wav,envelope,intervals,progress}.h` | public API |
| `include/yetty/yaudio/main.h` | *generated* yclass header for the `yaudio:app` class — its annotated source is `tools/yaudio/main.c`, not this directory |

Built as `yetty_yaudio` behind `YETTY_ENABLE_FEATURE_YAUDIO`
(`src/yetty/CMakeLists.txt`). The library CMake notes a planned WebGPU
compute path for the analysis passes; today everything is scalar CPU math.

## Consumers

- **`tools/yaudio`** — the analyzer GUI (yclass class `yaudio:app`, subclass
  of `yapp:app`): brings up the standard yframework runtime, plots the RMS
  envelope with yplot, and pans across detected intervals with Prev/Next
  buttons. Other GUI apps (`tools/ycompositor`, `yrich`'s app) reference its
  event-driven render-loop shape as the template.
- **`tools/yaudio-intervals`** — batch CLI: one TSV row per detected interval
  (`file channel start_s end_s dur_s peak_dbfs rms_dbfs`) across many files.

## See also

- [yacodec](../yacodec/README.md) — compressed-audio (Opus) decode for
  playback; a different subsystem.
- [yplot](../yplot/README.md) — renders the envelope in the analyzer GUI.
