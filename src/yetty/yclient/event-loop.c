/*
 * yclient-lib/event-loop.c — libuv-backed event loop, shared across all
 * yetty client apps (ymgui frontends, ygui, yrich, …).
 *
 * I/O model:
 *
 *   stdin  -> uv_poll (READABLE) -> read(2) into temp buf
 *               -> yetty_yface_feed_bytes(stream)
 *                    -> on_osc(code, payload, len) -> typed dispatch
 *                    -> on_raw(bytes, n)            -> app's raw cb
 *               -> set frame_pending
 *
 * Frame coalescing: a uv_check_t runs once per iteration after I/O. If
 * frame_pending is set (any input fired or request_frame was called) it
 * fires the user's frame_cb and clears the flag.
 *
 * Stdout watching is intentionally NOT here — write-side queueing belongs
 * to the consumer (ymgui's pending-write helper, ygui's emitter, …).
 * Consumers that need writability notifications can register their out_fd
 * via add_fd(WRITABLE) and remove it once their queue is drained.
 *
 * App extensions: small fixed-size tables of uv_poll_t / uv_timer_t /
 * pending posted tasks. Sized to typical app needs; can grow if required.
 */

#include <yetty/yclient/event-loop.h>

#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
typedef long long ssize_t;
#define read(fd, buf, n) _read((fd), (buf), (unsigned int)(n))
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#else
#include <unistd.h>
#endif
#include <errno.h>
#include <uv.h>

#define YETTY_YCLIENT_FD_SLOTS 16
#define YETTY_YCLIENT_TIMER_SLOTS 16
#define YETTY_YCLIENT_READ_BUF 8192

/*=============================================================================
 * App extension slots
 *===========================================================================*/

struct loop_fd_slot {
    uv_poll_t poll;
    int id;
    int fd;
    int events_mask;
    yetty_yclient_fd_cb cb;
    void *cb_user;
    int active;
    struct yetty_yclient_event_loop *loop;
};

struct loop_timer_slot {
    uv_timer_t timer;
    int id;
    int timeout_ms;
    int repeat_ms;
    yetty_yclient_timer_cb cb;
    void *cb_user;
    int active;
    struct yetty_yclient_event_loop *loop;
};

/* Single-linked queue of (fn, arg) callbacks scheduled via post(). Drained
 * by the post_async handler. */
struct yetty_yplatform_post_node {
    yetty_yclient_task_cb cb;
    void *cb_user;
    struct yetty_yplatform_post_node *next;
};

struct yetty_yclient_event_loop {
    uv_loop_t loop;

    int in_fd;
    void *user;

    /* I/O handles */
    uv_poll_t in_poll;
    int in_poll_active;

    /* Stop / wake */
    uv_async_t stop_async;
    uv_async_t frame_async;
    uv_async_t post_async;
    uv_check_t frame_check;

    /* Stream decoder (owns a yface configured with set_handlers). */
    struct yetty_yface *yface;

    /* Input callbacks */
    yetty_yclient_mouse_pos_cb on_pos;
    yetty_yclient_mouse_button_cb on_btn;
    yetty_yclient_mouse_wheel_cb on_wheel;
    yetty_yclient_resize_cb on_resize;
    yetty_yclient_focus_cb on_focus;
    yetty_yclient_key_cb on_key;
    yetty_yclient_raw_cb on_raw;
    yetty_yclient_frame_cb on_frame;

    /* Frame coalescing */
    int frame_pending;

    /* App extensions */
    struct loop_fd_slot fd_slots[YETTY_YCLIENT_FD_SLOTS];
    struct loop_timer_slot timer_slots[YETTY_YCLIENT_TIMER_SLOTS];
    int next_fd_id;
    int next_timer_id;

    /* Posted tasks (cross-thread) */
    uv_mutex_t post_mutex;
    struct yetty_yplatform_post_node *post_head;
    struct yetty_yplatform_post_node *post_tail;
};

/*=============================================================================
 * yface dispatch
 *
 * Decodes the wire structs that yetty's terminal sends (700000 mouse,
 * 700001 resize) and routes them to the per-event-type callbacks. The
 * actual wire structs live in <yetty/ymgui/wire.h> — yclient-lib depends
 * on them as the canonical wire format definition for both directions.
 *===========================================================================*/

static void on_yface_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                         const uint8_t *payload, size_t len)
{
    struct yetty_yclient_event_loop *L = user;
    (void)args;
    (void)args_len; /* mouse/resize codes carry no args */
    fprintf(stderr, "[yclient] on_yface_osc: code=%d args_len=%zu payload_len=%zu\n", osc_code,
            args_len, len);
    fflush(stderr);

    switch (osc_code) {
    case YMGUI_OSC_SC_MOUSE: {
        if (len < sizeof(struct yetty_ymgui_wire_input_mouse)) {
            return;
        }
        const struct yetty_ymgui_wire_input_mouse *m =
            (const struct yetty_ymgui_wire_input_mouse *)payload;
        if (m->magic != YMGUI_WIRE_MAGIC_INPUT_MOUSE) {
            return;
        }
        switch (m->kind) {
        case YETTY_YMGUI_INPUT_MOUSE_POS:
            if (L->on_pos) {
                L->on_pos(L->user, m->card_id, m->x, m->y, m->buttons_held);
            }
            break;
        case YETTY_YMGUI_INPUT_MOUSE_BUTTON:
            if (L->on_btn) {
                L->on_btn(L->user, m->card_id, m->button, m->pressed, m->x, m->y);
            }
            break;
        case YETTY_YMGUI_INPUT_MOUSE_WHEEL:
            if (L->on_wheel) {
                L->on_wheel(L->user, m->card_id, m->wheel_dy, m->x, m->y);
            }
            break;
        }
        L->frame_pending = 1;
        break;
    }
    case YMGUI_OSC_SC_RESIZE: {
        if (len < sizeof(struct yetty_ymgui_wire_input_resize)) {
            return;
        }
        const struct yetty_ymgui_wire_input_resize *r =
            (const struct yetty_ymgui_wire_input_resize *)payload;
        if (r->magic != YMGUI_WIRE_MAGIC_INPUT_RESIZE) {
            return;
        }
        if (L->on_resize) {
            L->on_resize(L->user, r->card_id, r->width, r->height);
        }
        L->frame_pending = 1;
        break;
    }
    case YMGUI_OSC_SC_FOCUS: {
        if (len < sizeof(struct yetty_ymgui_wire_input_focus)) {
            return;
        }
        const struct yetty_ymgui_wire_input_focus *f =
            (const struct yetty_ymgui_wire_input_focus *)payload;
        if (f->magic != YMGUI_WIRE_MAGIC_INPUT_FOCUS) {
            return;
        }
        if (L->on_focus) {
            L->on_focus(L->user, f->card_id, f->gained);
        }
        L->frame_pending = 1;
        break;
    }
    case YMGUI_OSC_SC_KEY: {
        if (len < sizeof(struct yetty_ymgui_wire_input_key)) {
            return;
        }
        const struct yetty_ymgui_wire_input_key *k =
            (const struct yetty_ymgui_wire_input_key *)payload;
        if (k->magic != YMGUI_WIRE_MAGIC_INPUT_KEY) {
            return;
        }
        if (L->on_key) {
            L->on_key(L->user, k->card_id, (int)k->kind, k->key, k->mods, k->codepoint);
        }
        L->frame_pending = 1;
        break;
    }
    default:
        /* Unknown OSC code — silently ignore so apps tolerate vendor
         * messages they don't recognise. */
        break;
    }
}

static void on_yface_raw(void *user, const char *bytes, size_t n)
{
    struct yetty_yclient_event_loop *L = user;
    if (L->on_raw) {
        L->on_raw(L->user, bytes, n);
    }
    /* Raw bytes (keyboard, CSI) usually do change app state — request a
     * frame so the app can react. */
    L->frame_pending = 1;
}

/*=============================================================================
 * stdin polling
 *===========================================================================*/

static void on_in_readable(uv_poll_t *handle, int status, int events)
{
    struct yetty_yclient_event_loop *L = handle->data;
    fprintf(stderr, "[yclient] on_in_readable: status=%d events=%d\n", status, events);
    fflush(stderr);
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }

    char buf[YETTY_YCLIENT_READ_BUF];
    for (;;) {
        ssize_t n = read(L->in_fd, buf, sizeof(buf));
        if (n > 0) {
            fprintf(stderr, "[yclient] read n=%zd first=[%c%c%c%c%c%c%c%c]\n", n,
                    n > 0 ? buf[0] : '?', n > 1 ? buf[1] : '?', n > 2 ? buf[2] : '?',
                    n > 3 ? buf[3] : '?', n > 4 ? buf[4] : '?', n > 5 ? buf[5] : '?',
                    n > 6 ? buf[6] : '?', n > 7 ? buf[7] : '?');
            fflush(stderr);
            yetty_yface_feed_bytes(L->yface, buf, (size_t)n);
            continue;
        }
        if (n == 0) {
            fprintf(stderr, "[yclient] read=0 EOF\n");
            fflush(stderr);
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "[yclient] read EAGAIN\n");
            fflush(stderr);
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        fprintf(stderr, "[yclient] read errno=%d\n", errno);
        fflush(stderr);
        break;
    }
}

/*=============================================================================
 * Frame coalescer + cross-thread asyncs
 *===========================================================================*/

static void on_frame_check(uv_check_t *handle)
{
    struct yetty_yclient_event_loop *L = handle->data;
    if (!L->frame_pending) {
        return;
    }
    fprintf(stderr, "[yclient] on_frame_check: firing on_frame (cb=%p user=%p)\n",
            (void *)L->on_frame, L->user);
    fflush(stderr);
    L->frame_pending = 0;
    if (L->on_frame) {
        L->on_frame(L->user);
    }
}

static void on_stop_async(uv_async_t *handle)
{
    struct yetty_yclient_event_loop *L = handle->data;
    uv_stop(&L->loop);
}

static void on_frame_async(uv_async_t *handle)
{
    struct yetty_yclient_event_loop *L = handle->data;
    L->frame_pending = 1;
}

static void on_post_async(uv_async_t *handle)
{
    struct yetty_yclient_event_loop *L = handle->data;

    struct yetty_yplatform_post_node *head;
    uv_mutex_lock(&L->post_mutex);
    head = L->post_head;
    L->post_head = NULL;
    L->post_tail = NULL;
    uv_mutex_unlock(&L->post_mutex);

    while (head) {
        struct yetty_yplatform_post_node *next = head->next;
        if (head->cb) {
            head->cb(head->cb_user);
        }
        free(head);
        head = next;
    }
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_yclient_event_loop *yetty_yclient_event_loop_create(
    const struct yetty_yclient_lib_event_loop_config *cfg)
{
    struct yetty_yclient_event_loop *L = calloc(1, sizeof(*L));
    if (!L) {
        return NULL;
    }

    L->in_fd = (cfg && cfg->in_fd >= 0) ? cfg->in_fd : STDIN_FILENO;
    L->user = cfg ? cfg->user : NULL;
    L->next_fd_id = 1;
    L->next_timer_id = 1;

    if (uv_loop_init(&L->loop) != 0) {
        goto fail;
    }

    /* Stream decoder. */
    {
        struct yetty_yface_ptr_result yr = yetty_yface_create();
        if (!yr.ok) {
            goto fail;
        }
        L->yface = yr.value;
        yetty_yface_set_handlers(L->yface, on_yface_osc, on_yface_raw, L);
    }

    if (uv_poll_init(&L->loop, &L->in_poll, L->in_fd) != 0) {
        goto fail;
    }
    L->in_poll.data = L;
    if (uv_poll_start(&L->in_poll, UV_READABLE, on_in_readable) != 0) {
        goto fail;
    }
    L->in_poll_active = 1;

    if (uv_async_init(&L->loop, &L->stop_async, on_stop_async) != 0) {
        goto fail;
    }
    L->stop_async.data = L;
    if (uv_async_init(&L->loop, &L->frame_async, on_frame_async) != 0) {
        goto fail;
    }
    L->frame_async.data = L;
    if (uv_async_init(&L->loop, &L->post_async, on_post_async) != 0) {
        goto fail;
    }
    L->post_async.data = L;

    if (uv_check_init(&L->loop, &L->frame_check) != 0) {
        goto fail;
    }
    L->frame_check.data = L;
    if (uv_check_start(&L->frame_check, on_frame_check) != 0) {
        goto fail;
    }

    if (uv_mutex_init(&L->post_mutex) != 0) {
        goto fail;
    }

    return L;

fail:
    yetty_yclient_event_loop_destroy(L);
    return NULL;
}

static void on_handle_close(uv_handle_t *h)
{
    (void)h;
}

void yetty_yclient_event_loop_destroy(struct yetty_yclient_event_loop *L)
{
    if (!L) {
        return;
    }

    if (L->in_poll_active) {
        uv_poll_stop(&L->in_poll);
    }
    uv_check_stop(&L->frame_check);

    for (int i = 0; i < YETTY_YCLIENT_FD_SLOTS; i++) {
        if (L->fd_slots[i].active) {
            uv_poll_stop(&L->fd_slots[i].poll);
            uv_close((uv_handle_t *)&L->fd_slots[i].poll, on_handle_close);
            L->fd_slots[i].active = 0;
        }
    }
    for (int i = 0; i < YETTY_YCLIENT_TIMER_SLOTS; i++) {
        if (L->timer_slots[i].active) {
            uv_timer_stop(&L->timer_slots[i].timer);
            uv_close((uv_handle_t *)&L->timer_slots[i].timer, on_handle_close);
            L->timer_slots[i].active = 0;
        }
    }

    if (uv_is_active((uv_handle_t *)&L->in_poll)) {
        uv_close((uv_handle_t *)&L->in_poll, on_handle_close);
    }
    uv_close((uv_handle_t *)&L->stop_async, on_handle_close);
    uv_close((uv_handle_t *)&L->frame_async, on_handle_close);
    uv_close((uv_handle_t *)&L->post_async, on_handle_close);
    uv_close((uv_handle_t *)&L->frame_check, on_handle_close);

    /* Drain pending closes. */
    uv_run(&L->loop, UV_RUN_NOWAIT);

    uv_mutex_destroy(&L->post_mutex);
    uv_loop_close(&L->loop);

    if (L->yface) {
        yetty_yface_destroy(L->yface);
    }

    /* Drain post queue (in case any tasks were posted after destroy). */
    while (L->post_head) {
        struct yetty_yplatform_post_node *n = L->post_head;
        L->post_head = n->next;
        free(n);
    }

    free(L);
}

/*=============================================================================
 * Callback wiring
 *===========================================================================*/
void yetty_yclient_event_loop_set_user(struct yetty_yclient_event_loop *L, void *u)
{
    L->user = u;
}
void yetty_yclient_event_loop_set_mouse_pos_cb(struct yetty_yclient_event_loop *L,
                                               yetty_yclient_mouse_pos_cb c)
{
    L->on_pos = c;
}
void yetty_yclient_event_loop_set_mouse_button_cb(struct yetty_yclient_event_loop *L,
                                                  yetty_yclient_mouse_button_cb c)
{
    L->on_btn = c;
}
void yetty_yclient_event_loop_set_mouse_wheel_cb(struct yetty_yclient_event_loop *L,
                                                 yetty_yclient_mouse_wheel_cb c)
{
    L->on_wheel = c;
}
void yetty_yclient_event_loop_set_resize_cb(struct yetty_yclient_event_loop *L,
                                            yetty_yclient_resize_cb c)
{
    L->on_resize = c;
}
void yetty_yclient_event_loop_set_focus_cb(struct yetty_yclient_event_loop *L,
                                           yetty_yclient_focus_cb c)
{
    L->on_focus = c;
}
void yetty_yclient_event_loop_set_key_cb(struct yetty_yclient_event_loop *L, yetty_yclient_key_cb c)
{
    L->on_key = c;
}
void yetty_yclient_event_loop_set_raw_cb(struct yetty_yclient_event_loop *L, yetty_yclient_raw_cb c)
{
    L->on_raw = c;
}
void yetty_yclient_event_loop_set_frame_cb(struct yetty_yclient_event_loop *L,
                                           yetty_yclient_frame_cb c)
{
    L->on_frame = c;
}

/*=============================================================================
 * Run
 *===========================================================================*/

int yetty_yclient_event_loop_run(struct yetty_yclient_event_loop *L)
{
    return uv_run(&L->loop, UV_RUN_DEFAULT);
}

int yetty_yclient_event_loop_poll(struct yetty_yclient_event_loop *L, int wait)
{
    return uv_run(&L->loop, wait ? UV_RUN_ONCE : UV_RUN_NOWAIT);
}

void yetty_yclient_event_loop_stop(struct yetty_yclient_event_loop *L)
{
    uv_async_send(&L->stop_async);
}

void yetty_yclient_event_loop_request_frame(struct yetty_yclient_event_loop *L)
{
    uv_async_send(&L->frame_async);
}

/*=============================================================================
 * App extensions: fds
 *===========================================================================*/

static struct loop_fd_slot *find_fd_slot(struct yetty_yclient_event_loop *L, int id)
{
    for (int i = 0; i < YETTY_YCLIENT_FD_SLOTS; i++) {
        if (L->fd_slots[i].active && L->fd_slots[i].id == id) {
            return &L->fd_slots[i];
        }
    }
    return NULL;
}

static void on_app_fd(uv_poll_t *handle, int status, int events)
{
    struct loop_fd_slot *slot = handle->data;
    if (status < 0 || !slot->cb) {
        return;
    }

    int mask = 0;
    if (events & UV_READABLE) {
        mask |= YETTY_YCLIENT_FD_READABLE;
    }
    if (events & UV_WRITABLE) {
        mask |= YETTY_YCLIENT_FD_WRITABLE;
    }
    slot->cb(slot->cb_user, slot->fd, mask);
}

int yetty_yclient_event_loop_add_fd(struct yetty_yclient_event_loop *L, int fd, int events_mask,
                                    yetty_yclient_fd_cb cb, void *cb_user)
{
    if (!L || fd < 0 || !cb) {
        return 0;
    }

    int idx = -1;
    for (int i = 0; i < YETTY_YCLIENT_FD_SLOTS; i++) {
        if (!L->fd_slots[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return 0;
    }

    struct loop_fd_slot *s = &L->fd_slots[idx];
    if (uv_poll_init(&L->loop, &s->poll, fd) != 0) {
        return 0;
    }
    s->poll.data = s;
    s->loop = L;
    s->id = L->next_fd_id++;
    s->fd = fd;
    s->events_mask = events_mask;
    s->cb = cb;
    s->cb_user = cb_user;
    s->active = 1;

    int uv_events = 0;
    if (events_mask & YETTY_YCLIENT_FD_READABLE) {
        uv_events |= UV_READABLE;
    }
    if (events_mask & YETTY_YCLIENT_FD_WRITABLE) {
        uv_events |= UV_WRITABLE;
    }
    if (uv_poll_start(&s->poll, uv_events, on_app_fd) != 0) {
        uv_close((uv_handle_t *)&s->poll, on_handle_close);
        s->active = 0;
        return 0;
    }
    return s->id;
}

void yetty_yclient_event_loop_remove_fd(struct yetty_yclient_event_loop *L, int id)
{
    struct loop_fd_slot *s = find_fd_slot(L, id);
    if (!s) {
        return;
    }
    uv_poll_stop(&s->poll);
    uv_close((uv_handle_t *)&s->poll, on_handle_close);
    s->active = 0;
}

/*=============================================================================
 * App extensions: timers
 *===========================================================================*/

static struct loop_timer_slot *find_timer_slot(struct yetty_yclient_event_loop *L, int id)
{
    for (int i = 0; i < YETTY_YCLIENT_TIMER_SLOTS; i++) {
        if (L->timer_slots[i].active && L->timer_slots[i].id == id) {
            return &L->timer_slots[i];
        }
    }
    return NULL;
}

static void on_app_timer(uv_timer_t *handle)
{
    struct loop_timer_slot *s = handle->data;
    if (s->cb) {
        s->cb(s->cb_user);
    }
}

int yetty_yclient_event_loop_add_timer(struct yetty_yclient_event_loop *L, int timeout_ms,
                                       int repeat_ms, yetty_yclient_timer_cb cb, void *cb_user)
{
    if (!L || timeout_ms < 0 || !cb) {
        return 0;
    }

    int idx = -1;
    for (int i = 0; i < YETTY_YCLIENT_TIMER_SLOTS; i++) {
        if (!L->timer_slots[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return 0;
    }

    struct loop_timer_slot *s = &L->timer_slots[idx];
    if (uv_timer_init(&L->loop, &s->timer) != 0) {
        return 0;
    }
    s->timer.data = s;
    s->loop = L;
    s->id = L->next_timer_id++;
    s->timeout_ms = timeout_ms;
    s->repeat_ms = repeat_ms;
    s->cb = cb;
    s->cb_user = cb_user;
    s->active = 1;

    if (uv_timer_start(&s->timer, on_app_timer, (uint64_t)timeout_ms,
                       (uint64_t)(repeat_ms > 0 ? repeat_ms : 0)) != 0) {
        uv_close((uv_handle_t *)&s->timer, on_handle_close);
        s->active = 0;
        return 0;
    }
    return s->id;
}

void yetty_yclient_event_loop_remove_timer(struct yetty_yclient_event_loop *L, int id)
{
    struct loop_timer_slot *s = find_timer_slot(L, id);
    if (!s) {
        return;
    }
    uv_timer_stop(&s->timer);
    uv_close((uv_handle_t *)&s->timer, on_handle_close);
    s->active = 0;
}

/*=============================================================================
 * App extensions: posted tasks
 *===========================================================================*/

void yetty_yclient_event_loop_post(struct yetty_yclient_event_loop *L, yetty_yclient_task_cb cb,
                                   void *cb_user)
{
    if (!L || !cb) {
        return;
    }
    struct yetty_yplatform_post_node *n = calloc(1, sizeof(*n));
    if (!n) {
        return;
    }
    n->cb = cb;
    n->cb_user = cb_user;

    uv_mutex_lock(&L->post_mutex);
    if (L->post_tail) {
        L->post_tail->next = n;
    } else {
        L->post_head = n;
    }
    L->post_tail = n;
    uv_mutex_unlock(&L->post_mutex);

    uv_async_send(&L->post_async);
}
