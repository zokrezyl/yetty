/* GLFW clipboard manager — pipe-marshaled to the main thread.
 *
 * GLFW requires clipboard calls on the main thread (the one that ran
 * glfwInit). yetty's render thread can't call them directly without
 * deadlocking on X11 SelectionNotify.
 *
 * Architecture:
 *   - output_pipe (render → main): shared bus for any work that must
 *                 run on the main thread. Today: YETTY_YCORE_COPY (set
 *                 request, payload = heap UTF-8 text) and
 *                 YETTY_YCORE_PASTE (get request, payload = NULL).
 *                 Future: window minimize/maximize/setTitle and other
 *                 main-thread-only GLFW calls.
 *   - input_pipe  (main → render): the existing platform event pipe; we
 *                 push the fetched clipboard text back as
 *                 YETTY_YCORE_PASTE with text in payload.
 *
 * Render-thread ops are plain writes + glfwPostEmptyEvent. The main
 * thread reads output_pipe in drain() between glfwWaitEvents calls and
 * runs the real glfw* calls inline. drain ignores event types it
 * doesn't handle, so other producers can share the same pipe. */

#include <yetty/yplatform/clipboard-manager.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yevent/event.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

struct yetty_yplatform_glfw_clipboard_manager {
    struct yetty_platform_clipboard_manager base;
    /* Borrowed — owned by the caller. */
    struct yetty_ycore_xthread_event_pipe *output_pipe;
    struct yetty_ycore_xthread_event_pipe *input_pipe;
};

static void glfw_clipboard_destroy(struct yetty_platform_clipboard_manager *self)
{
    struct yetty_yplatform_glfw_clipboard_manager *m =
        container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);
    free(m);
}

static void glfw_clipboard_set_text(struct yetty_platform_clipboard_manager *self, const char *text,
                                    size_t len)
{
    struct yetty_yplatform_glfw_clipboard_manager *m =
        container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);

    char *copy = malloc(len + 1);
    if (!copy) {
        return;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';

    struct yetty_yui_event ev = {.type = YETTY_YCORE_COPY, .payload = copy};
    /* Ownership of `copy` transfers to the main thread on a successful
     * write. On a write error the bytes never leave; free locally. */
    struct yetty_ycore_size_result wr =
        m->output_pipe->ops->write(m->output_pipe, &ev, sizeof(ev));
    if (!YETTY_IS_OK(wr) || wr.value != sizeof(ev)) {
        free(copy);
        return;
    }
    glfwPostEmptyEvent();
}

static void glfw_clipboard_request_paste(struct yetty_platform_clipboard_manager *self)
{
    struct yetty_yplatform_glfw_clipboard_manager *m =
        container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);

    struct yetty_yui_event ev = {.type = YETTY_YCORE_PASTE, .payload = NULL};
    struct yetty_ycore_size_result wr =
        m->output_pipe->ops->write(m->output_pipe, &ev, sizeof(ev));
    if (!YETTY_IS_OK(wr) || wr.value != sizeof(ev)) {
        return;
    }
    glfwPostEmptyEvent();
}

static void glfw_clipboard_drain(struct yetty_platform_clipboard_manager *self)
{
    struct yetty_yplatform_glfw_clipboard_manager *m =
        container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);

    /* Read whatever has accumulated since the last drain. The pipe's
     * read fd is non-blocking; read() returns 0 when drained. Other
     * future producers may share this pipe — we handle the event
     * types we know and fall through on the rest so the queue stays
     * drained either way. */
    for (;;) {
        struct yetty_yui_event ev;
        struct yetty_ycore_size_result rr =
            m->output_pipe->ops->read(m->output_pipe, &ev, sizeof(ev));
        if (!YETTY_IS_OK(rr) || rr.value == 0) {
            break;
        }
        if (rr.value != sizeof(ev)) {
            /* Partial event — pipe framing is broken. Don't try to
             * interpret it. */
            break;
        }

        switch (ev.type) {
        case YETTY_YCORE_COPY: {
            char *text = ev.payload;
            if (text) {
                glfwSetClipboardString(NULL, text);
                free(text);
            }
            break;
        }
        case YETTY_YCORE_PASTE: {
            /* Fetch on this thread (safe — we are the main thread) and
             * post the result back to the render thread as a PASTE event
             * with the text in payload. */
            const char *got = glfwGetClipboardString(NULL);
            char *copy = NULL;
            if (got) {
                size_t glen = strlen(got);
                copy = malloc(glen + 1);
                if (copy) {
                    memcpy(copy, got, glen + 1);
                }
            }
            struct yetty_yui_event out = {.type = YETTY_YCORE_PASTE, .payload = copy};
            struct yetty_ycore_size_result wr =
                m->input_pipe->ops->write(m->input_pipe, &out, sizeof(out));
            if (!YETTY_IS_OK(wr) || wr.value != sizeof(out)) {
                free(copy);
            }
            break;
        }
        default:
            /* Not ours — leave the event alone (no payload to free,
             * since other producers are expected to allocate similarly
             * and free their own dropped payloads if they care). */
            break;
        }
    }
}

static const struct yetty_platform_clipboard_manager_ops glfw_clipboard_ops = {
    .destroy = glfw_clipboard_destroy,
    .set_text = glfw_clipboard_set_text,
    .request_paste = glfw_clipboard_request_paste,
    .drain = glfw_clipboard_drain,
};

struct yetty_yplatform_clipboard_manager_result yetty_platform_clipboard_manager_create(
    struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe)
{
    if (!output_pipe || !input_pipe) {
        return YETTY_ERR(yetty_yplatform_clipboard_manager,
                         "clipboard manager requires both output_pipe and input_pipe");
    }
    struct yetty_yplatform_glfw_clipboard_manager *m =
        calloc(1, sizeof(struct yetty_yplatform_glfw_clipboard_manager));
    if (!m) {
        return YETTY_ERR(yetty_yplatform_clipboard_manager, "failed to allocate clipboard manager");
    }
    m->base.ops = &glfw_clipboard_ops;
    m->output_pipe = output_pipe;
    m->input_pipe = input_pipe;
    return YETTY_OK(yetty_yplatform_clipboard_manager, &m->base);
}
