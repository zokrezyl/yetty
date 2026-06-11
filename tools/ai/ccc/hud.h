/*
 * hud.h — ccc's non-scrolling status panel.
 *
 * A small ygui panel floating in the top-right corner of the pane, on
 * top of the scroll buffer. The conversation keeps scrolling in the
 * terminal; the HUD carries what must NOT scroll away: current state
 * (idle / thinking / running tool), the last turn's token/cost line and
 * the session totals.
 *
 * Drives the real ygui engine directly (no FFI layer): framework +
 * widget tree emitting compositor envelopes through a blocking-write
 * pty shim over stdout. Creation degrades gracefully: NULL means "no
 * HUD" and the caller prints the stats inline instead.
 */
#ifndef CCC_HUD_H
#define CCC_HUD_H

struct ccc_hud;

/* Create the panel, or return NULL when unavailable (not a tty, ygui
 * init failure, or CCC_NO_HUD=1). */
struct ccc_hud *ccc_hud_create(void);

/* Update one line. Cheap; nothing is written until ccc_hud_flush. */
void ccc_hud_set_state(struct ccc_hud *hud, const char *text);
void ccc_hud_set_turn(struct ccc_hud *hud, const char *text);
void ccc_hud_set_session(struct ccc_hud *hud, const char *text);

/* Emit a frame if anything changed. Flushes stdout first so the
 * envelope serializes after pending text (single-writer discipline). */
void ccc_hud_flush(struct ccc_hud *hud);

/* SIGWINCH: re-read the pane pixel size, reposition, re-emit. */
void ccc_hud_viewport_changed(struct ccc_hud *hud);

/* Clear the remote figure and tear the framework down. */
void ccc_hud_destroy(struct ccc_hud *hud);

#endif /* CCC_HUD_H */
