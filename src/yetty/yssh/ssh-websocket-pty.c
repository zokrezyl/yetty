/* ssh-websocket-pty.c — non-blocking libssh2 over a conn_transport.
 *
 * Data flow:
 *
 *   keyboard → pty write() → input ring → libssh2_channel_write
 *        └ libssh2 encrypts → LIBSSH2_CALLBACK_SEND → transport send
 *                                            (one ws binary message)
 *
 *   ws message → transport on_data → rx ring → pump()
 *        └ libssh2 state machine / channel reads pull bytes back out
 *          through LIBSSH2_CALLBACK_RECV; decrypted shell output goes
 *          to the output pipe (overflow-ring protected, same pattern
 *          as telnet-pty / websocket-pty).
 *
 * Everything runs on the browser main thread; "non-blocking" means
 * every libssh2 call may return LIBSSH2_ERROR_EAGAIN, in which case
 * we simply return to the event loop — the next transport callback
 * re-enters pump() and the state machine resumes where it left off.
 */

#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytransport/conn-transport.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <yetty/yssh/ssh-websocket-pty.h>

#include <libssh2.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Connect state machine. Order matters — pump() walks forward. */
enum ssh_websocket_pty_state {
    SSH_STATE_WAIT_TRANSPORT, /* transport open() fired, ws connecting */
    SSH_STATE_HANDSHAKE,      /* libssh2_session_handshake in progress */
    SSH_STATE_AUTH,           /* password auth in progress */
    SSH_STATE_CHANNEL_OPEN,   /* opening the session channel */
    SSH_STATE_PTY_REQUEST,    /* requesting the remote pty */
    SSH_STATE_SHELL_REQUEST,  /* requesting the shell */
    SSH_STATE_READY,          /* shell running, bytes flow */
    SSH_STATE_CLOSED,         /* remote closed / transport gone */
    SSH_STATE_FAILED,         /* unrecoverable error (logged) */
};

/* Simple growable ring: contiguous live region [head, tail). */
struct ssh_byte_ring {
    uint8_t *data;
    size_t cap;
    size_t head;
    size_t tail;
};

struct yetty_yssh_ssh_websocket_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;

    /* Owned. Carries the encrypted byte stream. */
    struct yetty_ytransport_conn_transport *transport;
    struct yetty_yevent_conn *conn;
    int transport_open;
    int transport_connected;

    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    enum ssh_websocket_pty_state state;

    char *username;
    char *password;
    char *term_type;

    /* Encrypted inbound bytes waiting for libssh2's recv callback. */
    struct ssh_byte_ring rx_ring;
    /* Plaintext keyboard input waiting for channel_write (EAGAIN). */
    struct ssh_byte_ring input_ring;
    /* Decrypted shell output that the output pipe couldn't take yet. */
    struct ssh_byte_ring output_overflow;

    /* Decrypted-output pipe — terminal reads via register_pty_pipe. */
    struct yetty_ycore_xthread_event_pipe *output_pipe;

    uint32_t cols;
    uint32_t rows;
    uint32_t pixel_w;
    uint32_t pixel_h;
    int size_dirty; /* a resize is latched, not yet shipped */
};

/* ---- Byte ring ---------------------------------------------------- */

static void ssh_ring_compact(struct ssh_byte_ring *ring)
{
    if (ring->head == 0) {
        return;
    }
    size_t live = ring->tail - ring->head;
    memmove(ring->data, ring->data + ring->head, live);
    ring->head = 0;
    ring->tail = live;
}

static int ssh_ring_append(struct ssh_byte_ring *ring, const uint8_t *src, size_t len)
{
    ssh_ring_compact(ring);
    if (ring->tail + len > ring->cap) {
        size_t need = ring->tail + len;
        size_t newcap = ring->cap ? ring->cap : 4096;
        while (newcap < need) {
            newcap *= 2;
        }
        uint8_t *grown = realloc(ring->data, newcap);
        if (!grown) {
            return -1;
        }
        ring->data = grown;
        ring->cap = newcap;
    }
    memcpy(ring->data + ring->tail, src, len);
    ring->tail += len;
    return 0;
}

static size_t ssh_ring_size(const struct ssh_byte_ring *ring)
{
    return ring->tail - ring->head;
}

static size_t ssh_ring_take(struct ssh_byte_ring *ring, uint8_t *dst, size_t max_len)
{
    size_t avail = ring->tail - ring->head;
    size_t taken = avail < max_len ? avail : max_len;
    memcpy(dst, ring->data + ring->head, taken);
    ring->head += taken;
    if (ring->head == ring->tail) {
        ring->head = 0;
        ring->tail = 0;
    }
    return taken;
}

/* ---- libssh2 transport callbacks ----------------------------------
 * Signatures dictated by libssh2 (LIBSSH2_RECV_FUNC / LIBSSH2_SEND_FUNC).
 * `*abstract` is the pty (set via libssh2_session_init_ex). The socket
 * argument is a dummy — all I/O goes through the conn_transport. */

YETTY_EXTERNAL_CALLBACK
static ssize_t ssh_websocket_pty_recv_callback(libssh2_socket_t socket_fd, void *buffer,
                                               size_t length, int flags, void **abstract)
{
    (void)socket_fd;
    (void)flags;
    struct yetty_yssh_ssh_websocket_pty *pty = *abstract;

    if (ssh_ring_size(&pty->rx_ring) == 0) {
        if (!pty->transport_connected) {
            /* No more bytes will ever arrive — report EOF-ish error so
             * libssh2 unwinds instead of waiting forever. */
            return -ECONNRESET;
        }
        return -EAGAIN;
    }
    return (ssize_t)ssh_ring_take(&pty->rx_ring, buffer, length);
}

YETTY_EXTERNAL_CALLBACK
static ssize_t ssh_websocket_pty_send_callback(libssh2_socket_t socket_fd, const void *buffer,
                                               size_t length, int flags, void **abstract)
{
    (void)socket_fd;
    (void)flags;
    struct yetty_yssh_ssh_websocket_pty *pty = *abstract;

    if (!pty->transport_connected || !pty->conn) {
        return -EAGAIN;
    }
    struct yetty_ycore_size_result send_res =
        pty->transport->ops->send(pty->transport, pty->conn, buffer, length);
    if (YETTY_IS_ERR(send_res)) {
        ywarn("ssh-ws: transport send failed: %s", send_res.error.msg);
        yetty_ycore_error_destroy(send_res.error);
        return -ECONNRESET;
    }
    return (ssize_t)send_res.value;
}

/* ---- Output pipe (decrypted shell bytes → terminal) --------------- */

static void ssh_websocket_pty_drain_output_once(struct yetty_yssh_ssh_websocket_pty *pty)
{
    if (ssh_ring_size(&pty->output_overflow) == 0) {
        return;
    }
    size_t avail = pty->output_overflow.tail - pty->output_overflow.head;
    struct yetty_ycore_size_result write_res = pty->output_pipe->ops->write(
        pty->output_pipe, pty->output_overflow.data + pty->output_overflow.head, avail);
    if (YETTY_IS_ERR(write_res)) {
        ywarn("ssh-ws: output pipe write error: %s", write_res.error.msg);
        yetty_ycore_error_destroy(write_res.error);
        return;
    }
    pty->output_overflow.head += write_res.value;
    if (pty->output_overflow.head == pty->output_overflow.tail) {
        pty->output_overflow.head = 0;
        pty->output_overflow.tail = 0;
    }
}

static void ssh_websocket_pty_emit_output(struct yetty_yssh_ssh_websocket_pty *pty,
                                          const uint8_t *data, size_t len)
{
    if (ssh_ring_append(&pty->output_overflow, data, len) != 0) {
        ywarn("ssh-ws: dropping %zu output bytes (oom)", len);
        return;
    }
    ssh_websocket_pty_drain_output_once(pty);
}

/* Surface a connect-phase failure to the user inside the terminal —
 * there is no other visible place in the browser UI. */
static void ssh_websocket_pty_emit_message(struct yetty_yssh_ssh_websocket_pty *pty,
                                           const char *text)
{
    char line[512];
    int line_len = snprintf(line, sizeof(line), "\r\n[yetty ssh] %s\r\n", text);
    if (line_len > 0) {
        ssh_websocket_pty_emit_output(pty, (const uint8_t *)line, (size_t)line_len);
    }
}

static void ssh_websocket_pty_fail(struct yetty_yssh_ssh_websocket_pty *pty, const char *what)
{
    char *error_msg = NULL;
    int error_code = pty->session
                         ? libssh2_session_last_error(pty->session, &error_msg, NULL, 0)
                         : 0;
    char detail[384];
    snprintf(detail, sizeof(detail), "%s failed (libssh2 rc=%d: %s)", what, error_code,
             error_msg && error_msg[0] ? error_msg : "no detail");
    yerror("ssh-ws: %s", detail);
    ssh_websocket_pty_emit_message(pty, detail);
    pty->state = SSH_STATE_FAILED;
}

/* ---- Connect state machine + data pumps --------------------------- */

static void ssh_websocket_pty_log_fingerprint(struct yetty_yssh_ssh_websocket_pty *pty)
{
    const char *hash = libssh2_hostkey_hash(pty->session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!hash) {
        ywarn("ssh-ws: no host key hash available");
        return;
    }
    char hex[32 * 2 + 1];
    for (int i = 0; i < 32; i++) {
        snprintf(hex + i * 2, 3, "%02x", (unsigned char)hash[i]);
    }
    /* NOT verified against known-hosts — see header. Make the
     * fingerprint at least visible so the operator can compare. */
    yinfo("ssh-ws: host key SHA256=%s (UNVERIFIED)", hex);
}

/* Flush latched keyboard input into the channel. */
static void ssh_websocket_pty_flush_input(struct yetty_yssh_ssh_websocket_pty *pty)
{
    while (ssh_ring_size(&pty->input_ring) > 0) {
        size_t avail = pty->input_ring.tail - pty->input_ring.head;
        ssize_t written = libssh2_channel_write(
            pty->channel, (const char *)pty->input_ring.data + pty->input_ring.head, avail);
        if (written == LIBSSH2_ERROR_EAGAIN) {
            return; /* retried on the next pump */
        }
        if (written < 0) {
            ssh_websocket_pty_fail(pty, "channel write");
            return;
        }
        pty->input_ring.head += (size_t)written;
        if (pty->input_ring.head == pty->input_ring.tail) {
            pty->input_ring.head = 0;
            pty->input_ring.tail = 0;
        }
    }
}

/* Ship a latched resize. */
static void ssh_websocket_pty_flush_resize(struct yetty_yssh_ssh_websocket_pty *pty)
{
    if (!pty->size_dirty) {
        return;
    }
    int rc = libssh2_channel_request_pty_size_ex(pty->channel, (int)pty->cols, (int)pty->rows,
                                                 (int)pty->pixel_w, (int)pty->pixel_h);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        return; /* still dirty, retried on the next pump */
    }
    if (rc != 0) {
        ywarn("ssh-ws: pty-size request failed rc=%d", rc);
    } else {
        ydebug("ssh-ws: sent resize %ux%u", pty->cols, pty->rows);
    }
    pty->size_dirty = 0;
}

/* Drain decrypted channel output into the output pipe. */
static void ssh_websocket_pty_drain_channel(struct yetty_yssh_ssh_websocket_pty *pty)
{
    char buffer[16384];
    for (;;) {
        ssize_t nread = libssh2_channel_read(pty->channel, buffer, sizeof(buffer));
        if (nread == LIBSSH2_ERROR_EAGAIN) {
            break;
        }
        if (nread < 0) {
            ssh_websocket_pty_fail(pty, "channel read");
            break;
        }
        if (nread == 0) {
            if (libssh2_channel_eof(pty->channel)) {
                yinfo("ssh-ws: channel EOF (remote shell exited)");
                ssh_websocket_pty_emit_message(pty, "connection closed by remote");
                pty->state = SSH_STATE_CLOSED;
            }
            break;
        }
        ssh_websocket_pty_emit_output(pty, (const uint8_t *)buffer, (size_t)nread);
    }
}

/* Advance the state machine as far as the buffered bytes allow. Every
 * step either completes (fall through to the next), parks on EAGAIN
 * (return; the next transport callback re-enters), or fails. */
static void ssh_websocket_pty_pump(struct yetty_yssh_ssh_websocket_pty *pty)
{
    int rc;

    switch (pty->state) {
    case SSH_STATE_WAIT_TRANSPORT:
    case SSH_STATE_CLOSED:
    case SSH_STATE_FAILED:
        return;

    case SSH_STATE_HANDSHAKE:
        rc = libssh2_session_handshake(pty->session, 1 /* dummy socket */);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            return;
        }
        if (rc != 0) {
            ssh_websocket_pty_fail(pty, "handshake");
            return;
        }
        yinfo("ssh-ws: handshake complete");
        ssh_websocket_pty_log_fingerprint(pty);
        pty->state = SSH_STATE_AUTH;
        /* fallthrough */

    case SSH_STATE_AUTH:
        rc = libssh2_userauth_password(pty->session, pty->username, pty->password);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            return;
        }
        if (rc != 0) {
            ssh_websocket_pty_fail(pty, "password authentication");
            return;
        }
        yinfo("ssh-ws: authenticated as %s", pty->username);
        pty->state = SSH_STATE_CHANNEL_OPEN;
        /* fallthrough */

    case SSH_STATE_CHANNEL_OPEN:
        pty->channel = libssh2_channel_open_session(pty->session);
        if (!pty->channel) {
            if (libssh2_session_last_errno(pty->session) == LIBSSH2_ERROR_EAGAIN) {
                return;
            }
            ssh_websocket_pty_fail(pty, "channel open");
            return;
        }
        yinfo("ssh-ws: channel open");
        pty->state = SSH_STATE_PTY_REQUEST;
        /* fallthrough */

    case SSH_STATE_PTY_REQUEST:
        rc = libssh2_channel_request_pty_ex(pty->channel, pty->term_type,
                                            (unsigned int)strlen(pty->term_type), NULL, 0,
                                            (int)pty->cols, (int)pty->rows, (int)pty->pixel_w,
                                            (int)pty->pixel_h);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            return;
        }
        if (rc != 0) {
            ssh_websocket_pty_fail(pty, "pty request");
            return;
        }
        pty->size_dirty = 0; /* initial size travelled with the pty-req */
        pty->state = SSH_STATE_SHELL_REQUEST;
        /* fallthrough */

    case SSH_STATE_SHELL_REQUEST:
        rc = libssh2_channel_shell(pty->channel);
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            return;
        }
        if (rc != 0) {
            ssh_websocket_pty_fail(pty, "shell request");
            return;
        }
        yinfo("ssh-ws: shell running");
        pty->state = SSH_STATE_READY;
        /* fallthrough */

    case SSH_STATE_READY:
        ssh_websocket_pty_flush_input(pty);
        if (pty->state != SSH_STATE_READY) {
            return;
        }
        ssh_websocket_pty_flush_resize(pty);
        if (pty->state != SSH_STATE_READY) {
            return;
        }
        ssh_websocket_pty_drain_channel(pty);
        return;
    }
}

/* ---- Transport callbacks ------------------------------------------ */

static void ssh_websocket_pty_on_connect(void *ctx, struct yetty_yevent_conn *conn)
{
    struct yetty_yssh_ssh_websocket_pty *pty = ctx;
    pty->conn = conn;
    pty->transport_connected = 1;
    yinfo("ssh-ws: transport connected, starting SSH handshake");
    pty->state = SSH_STATE_HANDSHAKE;
    ssh_websocket_pty_pump(pty);
}

static void ssh_websocket_pty_on_connect_error(void *ctx, const char *error)
{
    struct yetty_yssh_ssh_websocket_pty *pty = ctx;
    yerror("ssh-ws: transport connect failed: %s", error ? error : "(unknown)");
    ssh_websocket_pty_emit_message(pty, "websocket connection failed — is the bridge running?");
    pty->state = SSH_STATE_FAILED;
    pty->transport_open = 0;
}

static void ssh_websocket_pty_on_data(void *ctx, struct yetty_yevent_conn *conn, const char *data,
                                      long nread)
{
    struct yetty_yssh_ssh_websocket_pty *pty = ctx;
    (void)conn;
    if (nread <= 0) {
        return;
    }
    if (ssh_ring_append(&pty->rx_ring, (const uint8_t *)data, (size_t)nread) != 0) {
        ywarn("ssh-ws: dropping %ld encrypted bytes (oom)", nread);
        return;
    }
    ssh_websocket_pty_pump(pty);
    /* Give the output pipe another chance — the terminal-side reader
     * runs between browser callbacks, so earlier overflow may fit now. */
    ssh_websocket_pty_drain_output_once(pty);
}

static void ssh_websocket_pty_on_disconnect(void *ctx)
{
    struct yetty_yssh_ssh_websocket_pty *pty = ctx;
    yinfo("ssh-ws: transport disconnected");
    pty->transport_connected = 0;
    pty->conn = NULL;
    if (pty->state != SSH_STATE_FAILED) {
        if (pty->state != SSH_STATE_CLOSED) {
            ssh_websocket_pty_emit_message(pty, "connection closed");
        }
        pty->state = SSH_STATE_CLOSED;
    }
}

/* ---- PTY ops ------------------------------------------------------- */

static struct yetty_ycore_size_result ssh_websocket_pty_read(struct yetty_platform_pty *self,
                                                             char *buf, size_t max_len)
{
    struct yetty_yssh_ssh_websocket_pty *pty = (struct yetty_yssh_ssh_websocket_pty *)self;

    if (max_len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    return pty->output_pipe->ops->read(pty->output_pipe, buf, max_len);
}

static struct yetty_ycore_size_result ssh_websocket_pty_write(struct yetty_platform_pty *self,
                                                              const char *data, size_t len)
{
    struct yetty_yssh_ssh_websocket_pty *pty = (struct yetty_yssh_ssh_websocket_pty *)self;

    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (pty->state == SSH_STATE_FAILED || pty->state == SSH_STATE_CLOSED) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (ssh_ring_append(&pty->input_ring, (const uint8_t *)data, len) != 0) {
        return YETTY_ERR(yetty_ycore_size, "ssh-ws: input buffer alloc failed");
    }
    if (pty->state == SSH_STATE_READY) {
        ssh_websocket_pty_flush_input(pty);
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result ssh_websocket_pty_resize(struct yetty_platform_pty *self,
                                                               uint32_t cols, uint32_t rows,
                                                               uint32_t pixel_w, uint32_t pixel_h)
{
    struct yetty_yssh_ssh_websocket_pty *pty = (struct yetty_yssh_ssh_websocket_pty *)self;

    pty->cols = cols;
    pty->rows = rows;
    pty->pixel_w = pixel_w;
    pty->pixel_h = pixel_h;
    pty->size_dirty = 1;

    if (pty->state == SSH_STATE_READY) {
        ssh_websocket_pty_flush_resize(pty);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ssh_websocket_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_websocket_pty *pty = (struct yetty_yssh_ssh_websocket_pty *)self;

    if (pty->transport_open && pty->transport) {
        pty->transport->ops->close(pty->transport, pty->conn);
        pty->transport_open = 0;
        pty->transport_connected = 0;
        pty->conn = NULL;
    }
    if (pty->state != SSH_STATE_FAILED) {
        pty->state = SSH_STATE_CLOSED;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ssh_websocket_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_websocket_pty *pty = (struct yetty_yssh_ssh_websocket_pty *)self;

    struct yetty_ycore_void_result stop_res = ssh_websocket_pty_stop(self);

    /* Best-effort libssh2 teardown. The session is non-blocking and
     * the transport is already closed, so EAGAINs here are expected —
     * call each step once and move on rather than spinning. */
    if (pty->channel) {
        (void)libssh2_channel_free(pty->channel);
        pty->channel = NULL;
    }
    if (pty->session) {
        (void)libssh2_session_disconnect(pty->session, "yetty pty destroyed");
        (void)libssh2_session_free(pty->session);
        pty->session = NULL;
        libssh2_exit(); /* pairs with libssh2_init in create (refcounted) */
    }

    if (pty->output_pipe) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        pty->output_pipe = NULL;
    }
    if (pty->transport) {
        pty->transport->ops->destroy(pty->transport);
        pty->transport = NULL;
    }

    free(pty->rx_ring.data);
    free(pty->input_ring.data);
    free(pty->output_overflow.data);
    free(pty->username);
    free(pty->password);
    free(pty->term_type);
    free(pty);

    if (YETTY_IS_ERR(stop_res)) {
        return YETTY_ERR(yetty_ycore_void, "ssh_websocket_pty_destroy: stop failed", stop_res);
    }
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *ssh_websocket_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_yssh_ssh_websocket_pty *pty = (struct yetty_yssh_ssh_websocket_pty *)self;
    return &pty->pipe_source;
}

static const struct yetty_platform_pty_ops *ssh_websocket_pty_ops(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = ssh_websocket_pty_destroy,
        .read = ssh_websocket_pty_read,
        .write = ssh_websocket_pty_write,
        .resize = ssh_websocket_pty_resize,
        .stop = ssh_websocket_pty_stop,
        .pipe_source = ssh_websocket_pty_pipe_source,
    };
    return &ops;
}

/* ---- Create -------------------------------------------------------- */

static void ssh_websocket_pty_destroy_partial(struct yetty_yssh_ssh_websocket_pty *pty)
{
    if (pty->session) {
        libssh2_session_free(pty->session);
        libssh2_exit();
    }
    if (pty->output_pipe) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
    }
    free(pty->username);
    free(pty->password);
    free(pty->term_type);
    free(pty);
}

struct yetty_yplatform_pty_ptr_result yetty_yssh_ssh_websocket_pty_create(
    struct yetty_yconfig_config *config, struct yetty_ytransport_conn_transport *transport)
{
    if (!transport || !transport->ops) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "ssh_websocket_pty_create: transport required");
    }

    const char *username =
        config ? config->ops->get_string(config, "ssh/username", "") : "";
    const char *password =
        config ? config->ops->get_string(config, "ssh/password", "") : "";
    const char *term_type =
        config ? config->ops->get_string(config, "ssh/term-type", "xterm-256color")
               : "xterm-256color";
    if (!username[0]) {
        transport->ops->destroy(transport);
        return YETTY_ERR(yetty_yplatform_pty_ptr,
                         "ssh: username required (--ssh USER@HOST)");
    }
    if (!password[0]) {
        transport->ops->destroy(transport);
        return YETTY_ERR(yetty_yplatform_pty_ptr,
                         "ssh: password required (--ssh-password; only password "
                         "auth is supported on webasm for now)");
    }

    struct yetty_yssh_ssh_websocket_pty *pty = calloc(1, sizeof(*pty));
    if (!pty) {
        transport->ops->destroy(transport);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to allocate ssh pty");
    }

    pty->base.ops = ssh_websocket_pty_ops();
    pty->state = SSH_STATE_WAIT_TRANSPORT;
    pty->cols = 80;
    pty->rows = 24;
    pty->username = strdup(username);
    pty->password = strdup(password);
    pty->term_type = strdup(term_type);
    if (!pty->username || !pty->password || !pty->term_type) {
        transport->ops->destroy(transport);
        ssh_websocket_pty_destroy_partial(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to copy ssh credentials");
    }

    /* libssh2_init is internally refcounted; paired with libssh2_exit
     * in destroy. */
    if (libssh2_init(0) != 0) {
        transport->ops->destroy(transport);
        ssh_websocket_pty_destroy_partial(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "libssh2_init failed");
    }
    pty->session = libssh2_session_init_ex(NULL, NULL, NULL, pty);
    if (!pty->session) {
        libssh2_exit();
        transport->ops->destroy(transport);
        pty->session = NULL;
        ssh_websocket_pty_destroy_partial(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "libssh2_session_init failed");
    }
    libssh2_session_set_blocking(pty->session, 0);
    libssh2_session_callback_set2(pty->session, LIBSSH2_CALLBACK_RECV,
                                  (libssh2_cb_generic *)ssh_websocket_pty_recv_callback);
    libssh2_session_callback_set2(pty->session, LIBSSH2_CALLBACK_SEND,
                                  (libssh2_cb_generic *)ssh_websocket_pty_send_callback);

    struct yetty_yplatform_input_pipe_result pipe_res = yetty_platform_input_pipe_create();
    if (!pipe_res.ok) {
        transport->ops->destroy(transport);
        ssh_websocket_pty_destroy_partial(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to create output pipe");
    }
    pty->output_pipe = pipe_res.value;

    struct yetty_ycore_int_result fd_res = pty->output_pipe->ops->read_fd(pty->output_pipe);
    if (!fd_res.ok) {
        transport->ops->destroy(transport);
        ssh_websocket_pty_destroy_partial(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to obtain pipe read fd");
    }
    pty->pipe_source.abstract = (uintptr_t)fd_res.value;

    if (pty->output_pipe->ops->set_nonblocking_write) {
        struct yetty_ycore_void_result nonblocking_res =
            pty->output_pipe->ops->set_nonblocking_write(pty->output_pipe);
        if (YETTY_IS_ERR(nonblocking_res)) {
            ywarn("ssh_websocket_pty_create: set_nonblocking_write failed: %s",
                  nonblocking_res.error.msg);
            yetty_ycore_error_destroy(nonblocking_res.error);
        }
    }

    struct yetty_yevent_tcp_client_callbacks callbacks = {
        .ctx = pty,
        .on_connect = ssh_websocket_pty_on_connect,
        .on_connect_error = ssh_websocket_pty_on_connect_error,
        .on_data = ssh_websocket_pty_on_data,
        .on_disconnect = ssh_websocket_pty_on_disconnect,
    };

    if (transport->ops->open(transport, &callbacks) != 0) {
        transport->ops->destroy(transport);
        ssh_websocket_pty_destroy_partial(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "transport open failed");
    }
    pty->transport = transport;
    pty->transport_open = 1;

    yinfo("ssh-ws: transport open, user=%s term=%s (async connect in flight)", pty->username,
          pty->term_type);
    return YETTY_OK(yetty_yplatform_pty_ptr, &pty->base);
}
