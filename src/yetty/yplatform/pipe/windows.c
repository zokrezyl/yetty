/* Windows platform input pipe implementation */

#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ycore/types.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>

/* Windows input pipe - embeds base as first member */
struct yetty_yplatform_win_platform_input_pipe {
    struct yetty_ycore_xthread_event_pipe base;
    HANDLE read_handle;
    HANDLE write_handle;
    int read_fd; /* CRT fd for libuv */
};

/* Forward declarations */
static void win_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_size_result win_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                     const void *data, size_t size);
static struct yetty_ycore_size_result win_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                    void *data, size_t max_size);
static struct yetty_ycore_int_result win_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_void_result win_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yevent_event_loop *loop);
static struct yetty_ycore_void_result win_pipe_set_nonblocking_write(
    struct yetty_ycore_xthread_event_pipe *self);

/* Ops table */
static const struct yetty_platform_input_pipe_ops win_pipe_ops = {
    .destroy = win_pipe_destroy,
    .write = win_pipe_write,
    .read = win_pipe_read,
    .read_fd = win_pipe_read_fd,
    .set_event_loop = win_pipe_set_event_loop,
    .set_nonblocking_write = win_pipe_set_nonblocking_write,
};

static struct yetty_ycore_void_result win_pipe_set_nonblocking_write(
    struct yetty_ycore_xthread_event_pipe *self)
{
    /* TODO: Windows ConPTY path doesn't yet hit the deadlock the unix
     * impl works around (no shared-thread producer/consumer with a
     * kernel pipe). Make this a no-op until ConPTY grows the same
     * topology. */
    (void)self;
    return YETTY_OK_VOID();
}

/* Implementation */

static void win_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_win_platform_input_pipe *pipe_impl;

    pipe_impl = container_of(self, struct yetty_yplatform_win_platform_input_pipe, base);

    /* read_fd owns read_handle via _open_osfhandle */
    if (pipe_impl->read_fd >= 0) {
        _close(pipe_impl->read_fd);
    } else if (pipe_impl->read_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_impl->read_handle);
    }
    if (pipe_impl->write_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_impl->write_handle);
    }

    free(pipe_impl);
}

static struct yetty_ycore_size_result win_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                     const void *data, size_t size)
{
    struct yetty_yplatform_win_platform_input_pipe *pipe_impl =
        container_of(self, struct yetty_yplatform_win_platform_input_pipe, base);
    DWORD bytes_written = 0;

    if (pipe_impl->write_handle == INVALID_HANDLE_VALUE) {
        return YETTY_ERR(yetty_ycore_size, "pipe write handle not open");
    }

    if (size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    if (!WriteFile(pipe_impl->write_handle, data, (DWORD)size, &bytes_written, NULL)) {
        return YETTY_ERR(yetty_ycore_size, "WriteFile to pipe failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)bytes_written);
}

static struct yetty_ycore_size_result win_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                    void *data, size_t max_size)
{
    struct yetty_yplatform_win_platform_input_pipe *pipe_impl;
    DWORD bytes_read = 0;
    DWORD available = 0;

    pipe_impl = container_of(self, struct yetty_yplatform_win_platform_input_pipe, base);

    if (pipe_impl->read_handle == INVALID_HANDLE_VALUE) {
        return YETTY_ERR(yetty_ycore_size, "pipe read handle not open");
    }

    if (max_size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    /* Check if data is available (non-blocking) */
    if (!PeekNamedPipe(pipe_impl->read_handle, NULL, 0, NULL, &available, NULL) || available == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    if (!ReadFile(pipe_impl->read_handle, data, (DWORD)max_size, &bytes_read, NULL)) {
        return YETTY_ERR(yetty_ycore_size, "ReadFile from pipe failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)bytes_read);
}

static struct yetty_ycore_int_result win_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_win_platform_input_pipe *pipe_impl =
        container_of(self, struct yetty_yplatform_win_platform_input_pipe, base);
    return YETTY_OK(yetty_ycore_int, pipe_impl->read_fd);
}

static struct yetty_ycore_void_result win_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yevent_event_loop *loop)
{
    /* TODO: integrate with Windows event loop when needed */
    (void)self;
    (void)loop;
    return YETTY_OK_VOID();
}

/* Create function */

struct yetty_yplatform_input_pipe_result yetty_platform_input_pipe_create(void)
{
    struct yetty_yplatform_win_platform_input_pipe *pipe_impl;
    SECURITY_ATTRIBUTES sa;

    pipe_impl = malloc(sizeof(struct yetty_yplatform_win_platform_input_pipe));
    if (!pipe_impl) {
        return YETTY_ERR(yetty_yplatform_input_pipe, "failed to allocate input pipe");
    }

    pipe_impl->base.ops = &win_pipe_ops;
    pipe_impl->read_handle = INVALID_HANDLE_VALUE;
    pipe_impl->write_handle = INVALID_HANDLE_VALUE;

    /* Use a named pipe with FILE_FLAG_OVERLAPPED so libuv can poll it */
    static volatile LONG pipe_counter = 0;
    char pipe_name[256];
    LONG id = InterlockedIncrement(&pipe_counter);
    snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\yetty-input-%lu-%ld",
             (unsigned long)GetCurrentProcessId(), (long)id);

    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    pipe_impl->read_handle = CreateNamedPipeA(pipe_name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                              PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, &sa);
    if (pipe_impl->read_handle == INVALID_HANDLE_VALUE) {
        free(pipe_impl);
        return YETTY_ERR(yetty_yplatform_input_pipe, "CreateNamedPipe failed");
    }

    pipe_impl->write_handle =
        CreateFileA(pipe_name, GENERIC_WRITE, 0, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (pipe_impl->write_handle == INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_impl->read_handle);
        free(pipe_impl);
        return YETTY_ERR(yetty_yplatform_input_pipe, "CreateFile for pipe failed");
    }

    /* Create CRT fd so libuv can use uv_pipe_open */
    pipe_impl->read_fd = _open_osfhandle((intptr_t)pipe_impl->read_handle, _O_RDONLY);
    if (pipe_impl->read_fd < 0) {
        CloseHandle(pipe_impl->read_handle);
        CloseHandle(pipe_impl->write_handle);
        free(pipe_impl);
        return YETTY_ERR(yetty_yplatform_input_pipe, "_open_osfhandle failed");
    }

    return YETTY_OK(yetty_yplatform_input_pipe, &pipe_impl->base);
}
