/*
 * miniaudio implementation TU.
 *
 * Single-header library: the implementation is gated behind
 * MA_IMPLEMENTATION and lives in exactly one .c file. We trim every
 * subsystem ydev doesn't use (decoders, encoders, resource manager,
 * waveform generators, low-level decoding) so the binary stays
 * compact — ydev only needs raw PCM I/O on a single device.
 *
 * Linux backends (ALSA / PulseAudio / JACK / PipeWire) are loaded at
 * runtime via dlopen, so no LGPL headers reach our build and no LGPL
 * libraries are linked at distribution time. The choice of backend is
 * made by miniaudio at ma_context_init time.
 *
 * License: miniaudio is dual-licensed Unlicense (public domain) /
 * MIT-0; either is compatible with BSL. See build-tools/miniaudio/
 * miniaudio.h, license footer.
 */

#define MA_IMPLEMENTATION

/* Trim everything ydev doesn't use. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE

/* ydev never needs to flush stderr from miniaudio; route logs nowhere
 * by default. Apps that want them can install a custom ma_log_callback. */
#define MA_DEBUG_OUTPUT

#include "miniaudio.h"
