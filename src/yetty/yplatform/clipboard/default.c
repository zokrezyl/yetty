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
#include <yetty/yplatform/window-manager.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yevent/event.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <GLFW/glfw3.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

struct yetty_yplatform_glfw_clipboard_manager {
    struct yetty_platform_clipboard_manager base;
    /* Borrowed — owned by the caller. */
    struct yetty_ycore_xthread_event_pipe *output_pipe;
    struct yetty_ycore_xthread_event_pipe *input_pipe;
    /* Optional — non-clipboard events drained off the pipe are handed
     * off here. NULL means "drop unknown events". */
    struct yetty_yplatform_window_manager *window_manager;
};

/* Write `len` bytes of `text` to the X11 PRIMARY selection (Wayland's
 * primary-selection on Wayland) by spawning a child helper. GLFW's
 * glfwSetClipboardString covers CLIPBOARD only — the selection that
 * `Ctrl+V` reads. PRIMARY (what middle-click paste reads on X11 and
 * what most standard terminals auto-populate on selection) needs a
 * separate selection owner. Rather than build one inside the clipboard
 * manager (it would need its own X11 event pump to respond to
 * SelectionRequest events from peer apps), pipe the text into a
 * fire-and-forget helper that already does it correctly:
 *
 *   X11      → xclip -selection primary
 *   Wayland  → wl-copy --primary
 *
 * The helper reads stdin, then forks itself into the background and
 * owns the selection until a peer overwrites it. We close our half of
 * the pipe and reap; the child detaches itself. No-op if the helper
 * isn't installed; we don't bubble that up as an error because the
 * CLIPBOARD path still works and falling back silently matches the
 * "best-effort PRIMARY" intent. */
static void write_primary_selection(const char *text, size_t len)
{
    if (!text || len == 0) {
        return;
    }
    /* Pick the helper by session type. Default to xclip — most X11
     * users have it, and on a Wayland session that runs XWayland the
     * X11 helper still works for XWayland clients (good enough). The
     * Wayland-native path needs wl-copy if the user wants peer Wayland
     * apps to see PRIMARY. */
    const char *wl = getenv("WAYLAND_DISPLAY");
    const char *helper;
    char *const xclip_argv[] = {"xclip", "-selection", "primary", "-in", NULL};
    char *const wlcopy_argv[] = {"wl-copy", "--primary", NULL};
    char *const *argv;
    if (wl && *wl) {
        helper = "wl-copy";
        argv = wlcopy_argv;
    } else {
        helper = "xclip";
        argv = xclip_argv;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        ywarn("clipboard: PRIMARY pipe() failed");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        ywarn("clipboard: PRIMARY fork() failed");
        return;
    }
    if (pid == 0) {
        /* Child: stdin from the pipe, stdout/stderr discarded so the
         * helper's chatter doesn't pollute yetty's stdout. */
        dup2(pipefd[0], 0);
        int devnull = open("/dev/null", 1);
        if (devnull >= 0) {
            dup2(devnull, 1);
            dup2(devnull, 2);
            if (devnull > 2) close(devnull);
        }
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(helper, argv);
        _exit(127);
    }
    /* Parent: write text to child's stdin, close, reap. xclip/wl-copy
     * fork themselves to background once stdin EOFs, so waitpid here
     * returns immediately after the foreground half exits. */
    close(pipefd[0]);
    ssize_t off = 0;
    while ((size_t)off < len) {
        ssize_t w = write(pipefd[1], text + off, len - (size_t)off);
        if (w < 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += w;
    }
    close(pipefd[1]);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        /* execvp couldn't find the helper. First selection of the
         * session is enough to log once; subsequent ones stay silent. */
        static int warned = 0;
        if (!warned) {
            ywarn("clipboard: PRIMARY helper '%s' not installed — install %s for "
                  "middle-click paste from yetty to other apps",
                  helper, helper);
            warned = 1;
        }
    } else {
        yinfo("clipboard: wrote %zu bytes to PRIMARY via %s", len, helper);
    }
}

static struct yetty_ycore_void_result glfw_clipboard_destroy(
    struct yetty_platform_clipboard_manager *self)
{
    struct yetty_yplatform_glfw_clipboard_manager *m =
        container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);
    free(m);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result glfw_clipboard_set_text(
    struct yetty_platform_clipboard_manager *self, const char *text, size_t len)
{
    struct yetty_yplatform_glfw_clipboard_manager *m =
        container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);

    char *copy = malloc(len + 1);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void,
                         "glfw_clipboard_set_text: malloc for clipboard text copy failed");
    }
    memcpy(copy, text, len);
    copy[len] = '\0';

    struct yetty_yui_event ev = {.type = YETTY_YCORE_COPY, .payload = copy};
    /* Ownership of `copy` transfers to the main thread on a successful
     * write. On a write error the bytes never leave; free locally. */
    struct yetty_ycore_size_result wr =
        m->output_pipe->ops->write(m->output_pipe, &ev, sizeof(ev));
    if (YETTY_IS_ERR(wr)) {
        free(copy);
        return YETTY_ERR(yetty_ycore_void,
                         "glfw_clipboard_set_text: output_pipe write failed", wr);
    }
    if (wr.value != sizeof(ev)) {
        free(copy);
        return YETTY_ERR(yetty_ycore_void,
                         "glfw_clipboard_set_text: short write on output_pipe");
    }
    glfwPostEmptyEvent();
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result glfw_clipboard_request_paste(
    struct yetty_platform_clipboard_manager *self)
{
    struct yetty_yplatform_glfw_clipboard_manager *m =
        container_of(self, struct yetty_yplatform_glfw_clipboard_manager, base);

    struct yetty_yui_event ev = {.type = YETTY_YCORE_PASTE, .payload = NULL};
    struct yetty_ycore_size_result wr =
        m->output_pipe->ops->write(m->output_pipe, &ev, sizeof(ev));
    if (YETTY_IS_ERR(wr)) {
        return YETTY_ERR(yetty_ycore_void,
                         "glfw_clipboard_request_paste: output_pipe write failed", wr);
    }
    if (wr.value != sizeof(ev)) {
        return YETTY_ERR(yetty_ycore_void,
                         "glfw_clipboard_request_paste: short write on output_pipe");
    }
    glfwPostEmptyEvent();
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result glfw_clipboard_drain(
    struct yetty_platform_clipboard_manager *self)
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
        if (YETTY_IS_ERR(rr)) {
            return YETTY_ERR(yetty_ycore_void,
                             "glfw_clipboard_drain: output_pipe read failed", rr);
        }
        if (rr.value == 0) {
            break;
        }
        if (rr.value != sizeof(ev)) {
            /* Partial event — pipe framing is broken. */
            return YETTY_ERR(yetty_ycore_void,
                             "glfw_clipboard_drain: partial event on output_pipe (framing broken)");
        }

        switch (ev.type) {
        case YETTY_YCORE_COPY: {
            char *text = ev.payload;
            if (text) {
                size_t tlen = strlen(text);
                /* CLIPBOARD selection (Ctrl+V target on every desktop).
                 * GLFW owns the selection on X11 via its helper window
                 * and on Wayland via wl_data_device_set_selection. */
                glfwSetClipboardString(NULL, text);
                /* PRIMARY selection (middle-click paste target on X11
                 * and Wayland). GLFW doesn't expose it, so we pipe the
                 * text into xclip/wl-copy. Fire-and-forget — the helper
                 * detaches and owns the selection. */
                write_primary_selection(text, tlen);
                free(text);
            }
            break;
        }
        case YETTY_YCORE_PASTE: {
            /* Fetch on this thread (safe — we are the main thread) and
             * post the result back to the render thread as a PASTE event
             * with the text in payload. */
            const char *got = glfwGetClipboardString(NULL);
            yinfo("clipboard: get_clipboard_string -> %zu bytes",
                  got ? strlen(got) : 0);
            char *copy = NULL;
            if (got) {
                size_t glen = strlen(got);
                copy = malloc(glen + 1);
                if (!copy) {
                    return YETTY_ERR(yetty_ycore_void,
                                     "glfw_clipboard_drain: malloc for paste text copy failed");
                }
                memcpy(copy, got, glen + 1);
            }
            struct yetty_yui_event out = {.type = YETTY_YCORE_PASTE, .payload = copy};
            struct yetty_ycore_size_result wr =
                m->input_pipe->ops->write(m->input_pipe, &out, sizeof(out));
            if (YETTY_IS_ERR(wr)) {
                free(copy);
                return YETTY_ERR(yetty_ycore_void,
                                 "glfw_clipboard_drain: input_pipe write of paste result failed", wr);
            }
            if (wr.value != sizeof(out)) {
                free(copy);
                return YETTY_ERR(yetty_ycore_void,
                                 "glfw_clipboard_drain: short write of paste result on input_pipe");
            }
            break;
        }
        default:
            /* Hand non-clipboard events to the window manager. The
             * output_pipe is FIFO with a single reader, so this delegation
             * is how multiple producers share one pipe — clipboard owns
             * the read side and routes each event by type. */
            if (m->window_manager && m->window_manager->ops &&
                m->window_manager->ops->handle_event) {
                m->window_manager->ops->handle_event(m->window_manager, &ev);
            }
            break;
        }
    }
    return YETTY_OK_VOID();
}

static const struct yetty_platform_clipboard_manager_ops glfw_clipboard_ops = {
    .destroy = glfw_clipboard_destroy,
    .set_text = glfw_clipboard_set_text,
    .request_paste = glfw_clipboard_request_paste,
    .drain = glfw_clipboard_drain,
};

struct yetty_yplatform_clipboard_manager_result yetty_platform_clipboard_manager_create(
    struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe,
    struct yetty_yplatform_window_manager *window_manager)
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
    m->window_manager = window_manager;
    return YETTY_OK(yetty_yplatform_clipboard_manager, &m->base);
}
