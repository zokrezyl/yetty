/*
 * yplatform/yclipboard/glfw.c — GLFW desktop clipboard subclass
 * (Linux / macOS / Windows).
 *
 * Port of the old yplatform/clipboard/default.c. GLFW requires clipboard calls
 * on the main thread (on X11 glfwGetClipboardString blocks pumping
 * SelectionNotify), so the render-thread ops only enqueue:
 *   - set_text       writes a YETTY_YCORE_COPY event (payload = heap text) to
 *                    output_pipe and wakes the main thread.
 *   - request_paste  writes a YETTY_YCORE_PASTE request (payload = NULL).
 * drain runs on the main thread between glfwWaitEvents: it performs the real
 * glfw* calls and posts the fetched text back on input_pipe as a
 * YETTY_YCORE_PASTE event (payload = heap text the receiver frees).
 *
 * Both pipes are private subclass state, owned by the caller and bound via
 * configure(). Unlike the old manager this subclass does NOT route non-clipboard
 * events to a window manager — the clipboard owns its own concern; pipe sharing
 * / event routing is an integration decision made where the abstractions are
 * wired together.
 */

#include <GLFW/glfw3.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_glfw_clipboard_ptr, struct yetty_yplatform_glfw_clipboard *);
struct yetty_yclass_ptr_result yetty_yplatform_glfw_clipboard_class_get(void);
struct yetty_yplatform_glfw_clipboard_ptr_result yetty_yplatform_glfw_clipboard_from(
    struct yetty_yclass_object *obj);

/* Private subclass state: the cross-thread pipes, borrowed (owned by caller). */
struct [[clang::annotate("class@yplatform:glfw_clipboard")]] [[clang::annotate("platform@glfw")]]
[[clang::annotate("parent@yplatform:clipboard")]] yetty_yplatform_glfw_clipboard {
    struct yetty_ycore_xthread_event_pipe *output_pipe; /* render → main marshalling bus */
    struct yetty_ycore_xthread_event_pipe *input_pipe;  /* main → render paste responses */
};

static struct yetty_yplatform_glfw_clipboard *glfw_clipboard_data(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_glfw_clipboard_ptr_result data = yetty_yplatform_glfw_clipboard_from(obj);
    if (!YETTY_IS_OK(data)) {
        yetty_ycore_error_destroy(data.error);
        return NULL;
    }
    return data.value;
}

/* Write `len` bytes of `text` to the X11/Wayland PRIMARY selection by piping
 * into a fire-and-forget helper (xclip -selection primary / wl-copy --primary).
 * GLFW's glfwSetClipboardString covers CLIPBOARD only; PRIMARY (middle-click
 * paste) needs a separate selection owner. Best-effort: a missing helper is not
 * an error — the CLIPBOARD path still works. */
#ifdef _WIN32
/* Windows has a single clipboard and no PRIMARY counterpart — no-op. */
static void write_primary_selection(const char *text, size_t len)
{
    (void)text;
    (void)len;
}
#else
static void write_primary_selection(const char *text, size_t len)
{
    if (!text || len == 0) {
        return;
    }
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    const char *helper;
    char *const xclip_argv[] = {"xclip", "-selection", "primary", "-in", NULL};
    char *const wlcopy_argv[] = {"wl-copy", "--primary", NULL};
    char *const *argv;
    if (wayland_display && *wayland_display) {
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
        /* Child: stdin from the pipe, stdout/stderr discarded. */
        dup2(pipefd[0], 0);
        int devnull = open("/dev/null", 1);
        if (devnull >= 0) {
            dup2(devnull, 1);
            dup2(devnull, 2);
            if (devnull > 2) {
                close(devnull);
            }
        }
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(helper, argv);
        _exit(127);
    }
    close(pipefd[0]);
    ssize_t offset = 0;
    while ((size_t)offset < len) {
        ssize_t written = write(pipefd[1], text + offset, len - (size_t)offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        offset += written;
    }
    close(pipefd[1]);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
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
#endif /* !_WIN32 */

/* Bind the render→main marshalling bus and the main→render response pipe. Both
 * are borrowed. Call once after create(), before any other slot. */
[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yplatform_glfw_clipboard_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe)
{
    if (!output_pipe || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void,
                         "glfw_clipboard_configure: output_pipe and input_pipe are required");
    }
    struct yetty_yplatform_glfw_clipboard_ptr_result data = yetty_yplatform_glfw_clipboard_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "glfw_clipboard_configure: data_get");
    data.value->output_pipe = output_pipe;
    data.value->input_pipe = input_pipe;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_clipboard:clipboard_set_text")]]
static struct yetty_ycore_void_result glfw_clipboard_set_text(struct yetty_yclass_object *obj,
                                                              const char *text, size_t len)
{
    struct yetty_yplatform_glfw_clipboard *data = glfw_clipboard_data(obj);
    if (!data || !data->output_pipe) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_set_text: not configured");
    }

    char *copy = malloc(len + 1);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_set_text: malloc for clipboard text failed");
    }
    memcpy(copy, text, len);
    copy[len] = '\0';

    /* Ownership of `copy` transfers to the main thread on a successful write;
     * on a write error the bytes never leave, so free locally. */
    struct yetty_yui_event event = {.type = YETTY_YCORE_COPY, .payload = copy};
    struct yetty_ycore_size_result write_result =
        data->output_pipe->ops->write(data->output_pipe, &event, sizeof(event));
    if (YETTY_IS_ERR(write_result)) {
        free(copy);
        return YETTY_ERR(yetty_ycore_void, "clipboard_set_text: output_pipe write failed",
                         write_result);
    }
    if (write_result.value != sizeof(event)) {
        free(copy);
        return YETTY_ERR(yetty_ycore_void, "clipboard_set_text: short write on output_pipe");
    }
    glfwPostEmptyEvent();
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_clipboard:clipboard_request_paste")]]
static struct yetty_ycore_void_result glfw_clipboard_request_paste(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_glfw_clipboard *data = glfw_clipboard_data(obj);
    if (!data || !data->output_pipe) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: not configured");
    }
    struct yetty_yui_event event = {.type = YETTY_YCORE_PASTE, .payload = NULL};
    struct yetty_ycore_size_result write_result =
        data->output_pipe->ops->write(data->output_pipe, &event, sizeof(event));
    if (YETTY_IS_ERR(write_result)) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: output_pipe write failed",
                         write_result);
    }
    if (write_result.value != sizeof(event)) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_request_paste: short write on output_pipe");
    }
    glfwPostEmptyEvent();
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_clipboard:clipboard_drain")]]
static struct yetty_ycore_void_result glfw_clipboard_drain(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_glfw_clipboard *data = glfw_clipboard_data(obj);
    if (!data || !data->output_pipe || !data->input_pipe) {
        return YETTY_ERR(yetty_ycore_void, "clipboard_drain: not configured");
    }

    for (;;) {
        struct yetty_yui_event event;
        struct yetty_ycore_size_result read_result =
            data->output_pipe->ops->read(data->output_pipe, &event, sizeof(event));
        if (YETTY_IS_ERR(read_result)) {
            return YETTY_ERR(yetty_ycore_void, "clipboard_drain: output_pipe read failed",
                             read_result);
        }
        if (read_result.value == 0) {
            break;
        }
        if (read_result.value != sizeof(event)) {
            return YETTY_ERR(yetty_ycore_void,
                             "clipboard_drain: partial event on output_pipe (framing broken)");
        }

        switch (event.type) {
        case YETTY_YCORE_COPY: {
            char *text = event.payload;
            if (text) {
                size_t text_len = strlen(text);
                glfwSetClipboardString(NULL, text);
                write_primary_selection(text, text_len);
                free(text);
            }
            break;
        }
        case YETTY_YCORE_PASTE: {
            const char *fetched = glfwGetClipboardString(NULL);
            yinfo("clipboard: get_clipboard_string -> %zu bytes", fetched ? strlen(fetched) : 0);
            char *copy = NULL;
            if (fetched) {
                size_t fetched_len = strlen(fetched);
                copy = malloc(fetched_len + 1);
                if (!copy) {
                    return YETTY_ERR(yetty_ycore_void,
                                     "clipboard_drain: malloc for paste text failed");
                }
                memcpy(copy, fetched, fetched_len + 1);
            }
            struct yetty_yui_event response = {.type = YETTY_YCORE_PASTE, .payload = copy};
            struct yetty_ycore_size_result write_result =
                data->input_pipe->ops->write(data->input_pipe, &response, sizeof(response));
            if (YETTY_IS_ERR(write_result)) {
                free(copy);
                return YETTY_ERR(yetty_ycore_void,
                                 "clipboard_drain: input_pipe write of paste result failed",
                                 write_result);
            }
            if (write_result.value != sizeof(response)) {
                free(copy);
                return YETTY_ERR(yetty_ycore_void,
                                 "clipboard_drain: short write of paste result on input_pipe");
            }
            break;
        }
        default:
            /* Not a clipboard event. The clipboard owns its own pipe, so this
             * should not occur; ignore it and keep draining rather than abort. */
            break;
        }
    }
    return YETTY_OK_VOID();
}

#include "glfw.gen.c"
