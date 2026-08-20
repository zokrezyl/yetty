/*
 * tmux-oracle — deterministic byte oracle for incremental terminal deltas.
 *
 * Links the pinned tmux's objects (minus its main) and fabricates the whole
 * server-side chain IN PROCESS: one client whose tty writes into a pty we
 * read, one session/window/pane whose input parser we feed directly. The
 * libevent loop is pumped manually, so the redraw scheduler runs exactly
 * when we say — the flush encoding for a given (base, delta) pair is the
 * same bytes on every run, which the live-attach capture (tmux-diff.py)
 * demonstrably is not.
 *
 * Usage: tmux-oracle <rows> <cols> <base-file> <delta-file>
 *   stdout = the bytes tmux emits for the DELTA alone (base emission and
 *   attach preamble are drained and discarded first).
 *
 * Build: tools/tmux-oracle/build.sh (compiles against tmp/tmux objects).
 */

#include "tmux.h"

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <locale.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int pty_master_fd = -1;

/* Drain everything currently readable from the pty master. Returns the
 * number of bytes read; appends to (*sink, *sink_len) when sink != NULL. */
static size_t oracle_drain(char **sink, size_t *sink_len)
{
    char buffer[65536];
    size_t total = 0;

    for (;;) {
        ssize_t got = read(pty_master_fd, buffer, sizeof buffer);
        if (got <= 0) {
            break;
        }
        if (sink != NULL) {
            *sink = xrealloc(*sink, *sink_len + (size_t)got);
            memcpy(*sink + *sink_len, buffer, (size_t)got);
            *sink_len += (size_t)got;
        }
        total += (size_t)got;
    }
    return total;
}

/* Pump the manual scheduler until the tty is quiescent: run the client loop
 * (redraw flags → screen_redraw_screen) and the event loop (bufferevent /
 * tty flush), draining the pty between rounds, until a full round moves no
 * bytes and no events remain pending. A fixed floor of rounds absorbs the
 * multi-step redraw → tty-write → flush chains. */
static void oracle_pump(char **sink, size_t *sink_len)
{
    int quiet_rounds = 0;
    int round;

    for (round = 0; round < 200 && quiet_rounds < 8; round++) {
        size_t moved = 0;
        int spin;

        /* Flush the tty COMPLETELY before running the client loop:
		 * check_redraw's defer path (taken when tty->out is
		 * non-empty) escalates a pending scrollbar flag into a full
		 * window redraw — an artifact a real server never shows,
		 * because its event loop empties the buffer first. */
        for (spin = 0; spin < 50; spin++) {
            event_loop(EVLOOP_NONBLOCK);
            size_t got = oracle_drain(sink, sink_len);
            moved += got;
            if (got == 0) {
                break;
            }
        }
        server_client_loop();
        event_loop(EVLOOP_NONBLOCK);
        moved += oracle_drain(sink, sink_len);
        if (moved == 0) {
            quiet_rounds++;
        } else {
            quiet_rounds = 0;
        }
    }
}

static char *oracle_read_file(const char *path, size_t *out_len)
{
    FILE *handle = fopen(path, "rb");
    char *data;
    long size;

    if (handle == NULL) {
        fprintf(stderr, "tmux-oracle: cannot open %s\n", path);
        exit(1);
    }
    fseek(handle, 0, SEEK_END);
    size = ftell(handle);
    fseek(handle, 0, SEEK_SET);
    data = xmalloc((size_t)size + 1);
    if (size > 0 && fread(data, 1, (size_t)size, handle) != (size_t)size) {
        fprintf(stderr, "tmux-oracle: short read on %s\n", path);
        exit(1);
    }
    fclose(handle);
    *out_len = (size_t)size;
    return data;
}

int main(int argc, char **argv)
{
    struct client *client;
    struct session *session;
    struct window *window;
    struct window_pane *pane;
    struct winlink *link;
    struct winsize window_size;
    const struct options_table_entry *table_entry;
    struct environ *session_env;
    struct options *session_opts;
    char *cause = NULL;
    char *base_bytes, *delta_bytes;
    size_t base_len, delta_len;
    char *delta_out = NULL;
    size_t delta_out_len = 0;
    int rows, cols;
    int peer_pair[2];
    int pane_pair[2];
    int slave_fd;

    if (argc < 5 || argc > 8) {
        fprintf(stderr, "usage: tmux-oracle <rows> <cols> <base-file> <delta-file> "
                        "[features] [term] [overrides]\n");
        return 1;
    }
    const char *features = argc >= 6 ? argv[5] : "256,RGB";
    const char *term_name = argc >= 7 ? argv[6] : "xterm-256color";
    /* Terminal-overrides (cycle-26): cap=value / cap@ applied to the term the
     * same way tmux applies the `terminal-overrides` server option — so el@,
     * el=..., csr@, etc. actually take effect in the oracle and the differential
     * exercises tmux's REAL missing/cancelled-capability behavior. */
    const char *overrides = argc >= 8 ? argv[7] : "";
    rows = atoi(argv[1]);
    cols = atoi(argv[2]);
    if (rows <= 0 || cols <= 0) {
        fprintf(stderr, "tmux-oracle: bad geometry\n");
        return 1;
    }
    base_bytes = oracle_read_file(argv[3], &base_len);
    delta_bytes = oracle_read_file(argv[4], &delta_len);

    /* --- process bootstrap (the slice of tmux.c main we need) --- */
    /* tmux's utf8 width machinery needs a UTF-8 LC_CTYPE (tmux.c main
	 * refuses to run without one); without it every multibyte glyph
	 * decodes to U+FFFD. */
    if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL && setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
        fprintf(stderr, "tmux-oracle: no UTF-8 locale available\n");
        return 1;
    }
    if (getenv("TMUX_ORACLE_VERBOSE") != NULL) {
        log_add_level();
        log_add_level();
    }
    event_init();
    socket_path = xstrdup("/tmp/tmux-oracle-none");

    global_environ = environ_create();
    environ_set(global_environ, "TERM", 0, "xterm-256color");
    environ_set(global_environ, "PATH", 0, "%s", getenv("PATH"));

    global_options = options_create(NULL);
    global_s_options = options_create(NULL);
    global_w_options = options_create(NULL);
    for (table_entry = options_table; table_entry->name != NULL; table_entry++) {
        if (table_entry->scope & OPTIONS_TABLE_SERVER) {
            options_default(global_options, table_entry);
        }
        if (table_entry->scope & OPTIONS_TABLE_SESSION) {
            options_default(global_s_options, table_entry);
        }
        if (table_entry->scope & OPTIONS_TABLE_WINDOW) {
            options_default(global_w_options, table_entry);
        }
    }
    /* Match the live harness conf: no status line, fixed window size. */
    options_set_number(global_s_options, "status", 0);
    options_set_number(global_w_options, "window-size", WINDOW_SIZE_MANUAL);
    options_set_number(global_w_options, "aggressive-resize", 0);

    server_proc = proc_start("oracle");

    /* The slice of server_start()'s state init the fabricated chain
	 * touches (server.c does this before accepting clients). */
    input_key_build();
    utf8_update_width_cache();
    RB_INIT(&windows);
    RB_INIT(&all_window_panes);
    TAILQ_INIT(&clients);
    RB_INIT(&sessions);
    key_bindings_init();
    control_build_events();
    hooks_build_events();
    TAILQ_INIT(&message_log);
    gettimeofday(&start_time, NULL);

    /* --- the fabricated client --- */
    if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, peer_pair) != 0) {
        fprintf(stderr, "tmux-oracle: socketpair failed\n");
        return 1;
    }
    client = server_client_create(peer_pair[0]);

    memset(&window_size, 0, sizeof window_size);
    window_size.ws_row = (unsigned short)rows;
    window_size.ws_col = (unsigned short)cols;
    if (openpty(&pty_master_fd, &slave_fd, NULL, NULL, &window_size) != 0) {
        fprintf(stderr, "tmux-oracle: openpty failed\n");
        return 1;
    }
    fcntl(pty_master_fd, F_SETFL, O_NONBLOCK);
    client->fd = slave_fd;
    client->ttyname = xstrdup(ttyname(slave_fd));
    client->term_name = xstrdup(term_name);
    if (features[0] != '\0') {
        tty_add_features(&client->term_features, features, ",");
    }
    /* What a real client ships in MSG_IDENTIFY_TERMINFO: the full cap
	 * list read on ITS side of the connection. */
    if (tty_term_read_list(term_name, slave_fd, &client->term_caps, &client->term_ncaps, &cause) !=
        0) {
        fprintf(stderr, "tmux-oracle: tty_term_read_list: %s\n", cause == NULL ? "?" : cause);
        return 1;
    }
    if (tty_init(&client->tty, client) != 0) {
        fprintf(stderr, "tmux-oracle: tty_init failed\n");
        return 1;
    }
    tty_resize(&client->tty);
    client->flags |= CLIENT_TERMINAL | CLIENT_UTF8;

    /* --- session/window/pane, no process spawned --- */
    window = window_create((u_int)cols, (u_int)rows, 0, 0);
    pane = window_add_pane(window, NULL, 0, 0);
    if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pane_pair) != 0) {
        fprintf(stderr, "tmux-oracle: pane socketpair failed\n");
        return 1;
    }
    if (getenv("TMUX_ORACLE_SPLIT") != NULL) {
        /* Split mode resizes the left pane (80 -> ~40), and
         * window_pane_send_resize drives TIOCSWINSZ on the pane fd — which a
         * socketpair fatal()s on. Give the pane a real PTY master instead. */
        int pane_master = -1, pane_slave = -1;
        if (openpty(&pane_master, &pane_slave, NULL, NULL, &window_size) != 0) {
            fprintf(stderr, "tmux-oracle: pane openpty failed\n");
            return 1;
        }
        pane->fd = pane_master;
    } else {
        pane->fd = pane_pair[0];
    }
    window_pane_set_event(pane);
    layout_init(window, pane);
    window_set_active_pane(window, pane, 0);

    session_env = environ_create();
    session_opts = options_create(global_s_options);
    session = session_create(NULL, "oracle", "/", session_env, session_opts, NULL);
    link = session_attach(session, window, -1, &cause);
    if (link == NULL) {
        fprintf(stderr, "tmux-oracle: session_attach: %s\n", cause == NULL ? "?" : cause);
        return 1;
    }
    session->curw = link;

    /* --- attach the client --- */
    client->session = session;
    client->flags |= CLIENT_ATTACHED | CLIENT_FOCUSED;
    if (server_client_open(client, &cause) != 0) {
        fprintf(stderr, "tmux-oracle: server_client_open: %s\n", cause == NULL ? "?" : cause);
        return 1;
    }
    /* Apply terminal-overrides DIRECTLY to the created term (cycle-26): the
     * global_options "terminal-overrides" array path is not usable in this
     * minimal fabrication (its RB array is unset), so drive tty_term_apply on
     * the term itself — the caps are colon-separated (el@:csr@:el=\E[9K). This
     * runs before the base+delta render so both use the overridden capabilities,
     * matching how ymux resolves the same features/overrides. */
    if (overrides[0] != '\0' && client->tty.term != NULL) {
        tty_term_apply(client->tty.term, overrides, 1);
        /* tty_term_apply does NOT re-derive the flags that tty_term_create
         * computes once from booleans (TERM_NOAM from am). An `am@` override
         * must therefore recompute NOAM here, or the oracle keeps writing the
         * bottom-right cell while the real tmux — which would have seen am
         * absent at create time — suppresses it. Mirror tty_term_create. */
        if (!tty_term_flag(client->tty.term, TTYC_AM)) {
            client->tty.term->flags |= TERM_NOAM;
        } else {
            client->tty.term->flags &= ~TERM_NOAM;
        }
    }
    /* Two-pane vertical split (TMUX_ORACLE_SPLIT=lr), done AFTER the session
     * and client are attached — the order a live `split-window` follows (the
     * window-linked event machinery needs s->curw set). This is the ONLY
     * configuration in which tmux renders a PARTIAL-WIDTH client rectangle:
     * a scroll in the left pane goes through the DECSLRM margin path
     * (tty_margin_pane) on a margins-capable client instead of a full
     * redraw. base/delta still feed the ORIGINAL (left) pane below. */
    if (getenv("TMUX_ORACLE_SPLIT") != NULL) {
        struct layout_cell *split_cell = layout_split_pane(pane, LAYOUT_LEFTRIGHT, -1, 0);
        if (split_cell == NULL) {
            fprintf(stderr, "tmux-oracle: layout_split_pane failed\n");
            return 1;
        }
        struct window_pane *right_pane = window_add_pane(window, pane, 0, 0);
        int right_master = -1, right_slave = -1;
        if (openpty(&right_master, &right_slave, NULL, NULL, &window_size) != 0) {
            fprintf(stderr, "tmux-oracle: right pane openpty failed\n");
            return 1;
        }
        right_pane->fd = right_master;
        window_pane_set_event(right_pane);
        layout_assign_pane(split_cell, right_pane, 0);
    }
    recalculate_sizes();
    server_redraw_client(client);

    /* Attach preamble + first full redraw: drain, discard (dumped to
	 * files when TMUX_ORACLE_DUMP_PREFIX is set — debugging aid). */
    char *attach_out = NULL, *base_out = NULL;
    size_t attach_out_len = 0, base_out_len = 0;
    const char *dump_prefix = getenv("TMUX_ORACLE_DUMP_PREFIX");
    oracle_pump(&attach_out, &attach_out_len);

    /* Base vector: apply, flush, discard. */
    input_parse_buffer(pane, (u_char *)base_bytes, base_len);
    oracle_pump(&base_out, &base_out_len);

    if (dump_prefix != NULL) {
        char dump_path[1024];
        FILE *dump;
        snprintf(dump_path, sizeof dump_path, "%s-attach.bin", dump_prefix);
        dump = fopen(dump_path, "wb");
        if (dump != NULL) {
            fwrite(attach_out, 1, attach_out_len, dump);
            fclose(dump);
        }
        snprintf(dump_path, sizeof dump_path, "%s-base.bin", dump_prefix);
        dump = fopen(dump_path, "wb");
        if (dump != NULL) {
            fwrite(base_out, 1, base_out_len, dump);
            fclose(dump);
        }
    }

    /* Delta vector: apply, flush, CAPTURE. */
    input_parse_buffer(pane, (u_char *)delta_bytes, delta_len);
    oracle_pump(&delta_out, &delta_out_len);

    if (delta_out_len > 0 && fwrite(delta_out, 1, delta_out_len, stdout) != delta_out_len) {
        fprintf(stderr, "tmux-oracle: stdout write failed\n");
        return 1;
    }
    fflush(stdout);
    return 0;
}
