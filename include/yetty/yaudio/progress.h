/*
 * yaudio/progress.h - optional progress reporting for streaming passes.
 *
 * Both heavy analysis passes (envelope, intervals) stream the whole file
 * through their decode loops; on multi-hour recordings that takes real
 * wall-clock time. A caller that drives a UI can pass a callback to get
 * monotonic [0, 1] progress and keep a progress bar moving.
 */

#ifndef YETTY_YAUDIO_PROGRESS_H
#define YETTY_YAUDIO_PROGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Periodic progress callback. `fraction` is in [0, 1] and increases
 * monotonically across a single pass. Invoked from the decode loop on
 * the calling thread (the analysis functions are synchronous — if the
 * caller runs them on a worker thread, the callback runs there too).
 * Pass NULL to disable reporting. */
typedef void (*yetty_yaudio_progress_fn)(void *userdata, double fraction);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YAUDIO_PROGRESS_H */
