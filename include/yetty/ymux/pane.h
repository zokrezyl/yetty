/*
 * pane.h — ymux_pane: the GPU-free logical-terminal owner.
 *
 * An ymux_pane owns one terminal's *model* object graph — the PTY, the wire
 * statemachine, the bare yvterm:grid ("the truth"), the input-emit yface, and
 * the host-side ywire channel connection — with no window, no GPU, and no
 * renderer figure. It is plain C: it merely holds the yclass grid object, it is
 * not itself a yclass class.
 *
 * All three ymux roles share this one core (docs/ymux.md §5.2):
 *   - legacy monolith: terminal.c wraps a pane with a yvterm:vterm renderer
 *     figure + a yui view (this is what exists after Phase 1a);
 *   - ymux server:     holds the pane bare, pumping PTYs headless with no GPU;
 *   - ymux client:     holds no pane at all — the truth lives on the server.
 */
#ifndef YETTY_YMUX_PANE_H
#define YETTY_YMUX_PANE_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_platform_pty;
struct yetty_ywire_wire_statemachine;
struct yetty_ywire_connection;
struct yetty_yclass_object;
struct yetty_yface;

/* Opaque — the field layout lives in pane.c; reach state through the accessors. */
struct yetty_ymux_pane;

YETTY_YRESULT_DECLARE(yetty_ymux_pane_ptr, struct yetty_ymux_pane *);

/* Fired after each successful PTY feed so the owner decides what to do with the
 * freshly-mutated model: the legacy renderer requests a frame; a server marks
 * the pane for delta shipping. Called on the event-loop thread. */
typedef struct yetty_ycore_void_result (*yetty_ymux_pane_notify_fn)(void *userdata);

/* Fired when the child process is confirmed gone (PTY EOF that outlived a
 * child-alive re-poll). The pane sets its shutting-down flag before invoking
 * this. The legacy renderer posts a CLOSE for its view; a server reaps the pane
 * / notifies clients. Called on the event-loop thread. */
typedef struct yetty_ycore_void_result (*yetty_ymux_pane_child_exit_fn)(void *userdata);

/* Raw PTY-output tap: fired with each chunk read off the PTY master BEFORE it
 * is fed to the wire statemachine. The ymux server uses it to ship the exact
 * byte stream to attached remote clients (model-shipping: the client re-runs
 * the same emulator, so it must see the identical bytes). Called on the
 * event-loop thread; the bytes are borrowed (valid only during the call). */
typedef void (*yetty_ymux_pane_output_tap_fn)(const char *data, size_t len, void *userdata);

/* Build the model core for one terminal: create the PTY via the context's
 * pty_factory, the wire statemachine over it, the emit yface, register the PTY
 * pipe with the event loop (async backends), mint the bare yvterm:grid and wire
 * its pty-write hook to this pane, bind the grid as the statemachine's default
 * sink, push the initial pixel dims to the PTY, and attach the host-side ywire
 * channel connection. No window, no GPU, no renderer. */
struct yetty_ymux_pane_ptr_result yetty_ymux_pane_create(struct yetty_ycore_grid_size grid_size,
                                                         const struct yetty_context *context);

/* Best-effort teardown (statemachine before pty; disposes the grid). Handles
 * NULL. Surfaces the first error after running every step. */
struct yetty_ycore_void_result yetty_ymux_pane_destroy(struct yetty_ymux_pane *pane);

/* Borrowed accessors (NULL/0 when pane is NULL). The grid is the yvterm:grid
 * model object; a renderer wraps it via
 * yetty_yvterm_vterm_figure_create_over_grid. */
struct yetty_yclass_object *yetty_ymux_pane_grid(struct yetty_ymux_pane *pane);
struct yetty_ywire_wire_statemachine *yetty_ymux_pane_sm(struct yetty_ymux_pane *pane);
struct yetty_platform_pty *yetty_ymux_pane_pty(struct yetty_ymux_pane *pane);
struct yetty_ywire_connection *yetty_ymux_pane_channel_host(struct yetty_ymux_pane *pane);
struct yetty_yface *yetty_ymux_pane_emit_yface(struct yetty_ymux_pane *pane);
uint32_t yetty_ymux_pane_cols(const struct yetty_ymux_pane *pane);
uint32_t yetty_ymux_pane_rows(const struct yetty_ymux_pane *pane);

/* Single write to the PTY master (one write(2); may short-write). */
struct yetty_ycore_size_result yetty_ymux_pane_write(struct yetty_ymux_pane *pane, const char *data,
                                                     size_t len);

/* Ship a fully-encoded envelope to the PTY master, looping until every byte is
 * written (backing off briefly on a full kernel buffer). Used as the emit
 * callback for the pane's channel host and by the terminal's DCS RPC server. */
struct yetty_ycore_void_result yetty_ymux_pane_dcs_emit(struct yetty_ymux_pane *pane,
                                                        const uint8_t *bytes, size_t n);

/* Track the pane's grid dims + push the pixel geometry to the PTY (fires the
 * child's SIGWINCH). The grid *model* resize is driven separately by the caller
 * (the legacy renderer resizes the grid through its vterm figure). */
struct yetty_ycore_void_result yetty_ymux_pane_resize_pty(struct yetty_ymux_pane *pane,
                                                          struct yetty_ycore_grid_size grid_size,
                                                          struct yetty_ycore_pixel_size cell_size);

/* Owner hooks (see the typedefs). Optional; unset = no-op. */
void yetty_ymux_pane_set_notify(struct yetty_ymux_pane *pane, yetty_ymux_pane_notify_fn fn,
                                void *userdata);
void yetty_ymux_pane_set_child_exit(struct yetty_ymux_pane *pane, yetty_ymux_pane_child_exit_fn fn,
                                    void *userdata);
void yetty_ymux_pane_set_output_tap(struct yetty_ymux_pane *pane,
                                    yetty_ymux_pane_output_tap_fn fn, void *userdata);

/* Shutting-down flag: set on child exit and on an owner SHUTDOWN. The pane's PTY
 * read path checks it to stop feeding a terminal being torn down. */
int yetty_ymux_pane_is_shutting_down(const struct yetty_ymux_pane *pane);
void yetty_ymux_pane_set_shutting_down(struct yetty_ymux_pane *pane, int value);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMUX_PANE_H */
