#ifndef YETTY_YPLATFORM_FATAL_REPORT_H
#define YETTY_YPLATFORM_FATAL_REPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Surface a fatal error to the hosting environment BEFORE the process
 * exits.
 *
 * On webasm the process's stderr is just the browser console — a fatal
 * _Exit() leaves the page frozen with its last "ready" status and the
 * user staring at a dead canvas with no visible message. This hook hands
 * the message to the hosting page (Module.onYettyFatal in terminal.html),
 * which opens an HTML error dialog.
 *
 * On native platforms this is a no-op: stderr + the trace log already
 * reach the operator.
 *
 * Call it on every path that is about to _Exit()/abort because of an
 * unrecoverable GPU/runtime error, with the same message that goes to
 * yerror/stderr. Best-effort by design — it must never fail or block.
 */
void yetty_yplatform_fatal_report(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_FATAL_REPORT_H */
