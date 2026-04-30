/* Windows ConPTY implementation */

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/yconfig.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace.h>
#include <yetty/yqemu/qemu.h>
#include <yetty/ytelnet/telnet-pty.h>
/* unix-pty.h is the common header that declares tinyemu_pty_create /
 * telnet_pty_create / fork_pty_create. Despite the name, the declarations
 * are platform-neutral. */
#include "../shared/unix-pty.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

/* Windows PTY pipe source */
struct win_pty_pipe_source {
    struct yetty_yplatform_pty_pipe_source base;
    HANDLE handle;
    int crt_fd; /* CRT fd from _open_osfhandle for libuv */
};

/* Windows ConPTY implementation - embeds base as first member */
struct win_conpty {
    struct yetty_yplatform_pty base;
    struct win_pty_pipe_source pipe_source;
    HPCON hpc;
    HANDLE pipe_in;
    HANDLE pipe_out;
    HANDLE process;
    uint32_t cols;
    uint32_t rows;
    int running;
};

/* Forward declarations */
static struct yetty_ycore_void_result win_conpty_destroy(struct yetty_yplatform_pty *self);
static struct yetty_ycore_size_result win_conpty_read(struct yetty_yplatform_pty *self, char *buf,
                                                      size_t max_len);
static struct yetty_ycore_size_result win_conpty_write(struct yetty_yplatform_pty *self,
                                                       const char *data, size_t len);
static struct yetty_ycore_void_result win_conpty_resize(struct yetty_yplatform_pty *self,
                                                        uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result win_conpty_stop(struct yetty_yplatform_pty *self);
static struct yetty_yplatform_pty_pipe_source *win_conpty_pipe_source(
    struct yetty_yplatform_pty *self);

/* Ops table */
static const struct yetty_yplatform_pty_ops win_conpty_ops = {
    .destroy = win_conpty_destroy,
    .read = win_conpty_read,
    .write = win_conpty_write,
    .resize = win_conpty_resize,
    .stop = win_conpty_stop,
    .pipe_source = win_conpty_pipe_source,
};

/* Windows PTY factory - embeds base as first member */
struct win_pty_factory {
    struct yetty_yplatform_pty_factory base;
    struct yetty_yconfig *config;
    yprocess_t *qemu_proc; /* spawned by qemu_start when --qemu is set */
};

/* Forward declarations for factory */
static void win_pty_factory_destroy(struct yetty_yplatform_pty_factory *self);
static struct yetty_yplatform_pty_result win_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_ycore_event_loop *event_loop);

/* Factory ops table */
static const struct yetty_yplatform_pty_factory_ops win_pty_factory_ops = {
    .destroy = win_pty_factory_destroy,
    .create_pty = win_pty_factory_create_pty,
};

/* PTY implementation */

static struct yetty_ycore_void_result win_conpty_destroy(struct yetty_yplatform_pty *self)
{
    struct win_conpty *pty = container_of(self, struct win_conpty, base);

    struct yetty_ycore_void_result stop_r = win_conpty_stop(self);
    free(pty);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "win_conpty_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result win_conpty_read(struct yetty_yplatform_pty *self, char *buf,
                                                      size_t max_len)
{
    struct win_conpty *pty = container_of(self, struct win_conpty, base);
    DWORD bytes_read = 0;
    DWORD available = 0;

    if (pty->pipe_out == INVALID_HANDLE_VALUE) {
        return YETTY_ERR(yetty_ycore_size, "conpty output pipe not open");
    }

    /* Check if data is available (non-blocking) */
    if (!PeekNamedPipe(pty->pipe_out, NULL, 0, NULL, &available, NULL) || available == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    if (!ReadFile(pty->pipe_out, buf, (DWORD)max_len, &bytes_read, NULL)) {
        return YETTY_ERR(yetty_ycore_size, "ReadFile from conpty failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)bytes_read);
}

static struct yetty_ycore_size_result win_conpty_write(struct yetty_yplatform_pty *self,
                                                       const char *data, size_t len)
{
    struct win_conpty *pty = container_of(self, struct win_conpty, base);
    DWORD bytes_written = 0;

    if (pty->pipe_in == INVALID_HANDLE_VALUE) {
        return YETTY_ERR(yetty_ycore_size, "conpty input pipe not open");
    }

    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    if (!WriteFile(pty->pipe_in, data, (DWORD)len, &bytes_written, NULL)) {
        return YETTY_ERR(yetty_ycore_size, "WriteFile to conpty failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)bytes_written);
}

static struct yetty_ycore_void_result win_conpty_resize(struct yetty_yplatform_pty *self,
                                                        uint32_t cols, uint32_t rows)
{
    struct win_conpty *pty = container_of(self, struct win_conpty, base);
    COORD size;
    HRESULT hr;

    pty->cols = cols;
    pty->rows = rows;

    if (pty->hpc == NULL) {
        return YETTY_ERR(yetty_ycore_void, "conpty not initialized");
    }

    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;

    hr = ResizePseudoConsole(pty->hpc, size);
    if (FAILED(hr)) {
        return YETTY_ERR(yetty_ycore_void, "ResizePseudoConsole failed");
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result win_conpty_stop(struct yetty_yplatform_pty *self)
{
    struct win_conpty *pty = container_of(self, struct win_conpty, base);

    if (!pty->running) {
        return YETTY_OK_VOID();
    }

    pty->running = 0;

    if (pty->process != INVALID_HANDLE_VALUE) {
        TerminateProcess(pty->process, 0);
        CloseHandle(pty->process);
        pty->process = INVALID_HANDLE_VALUE;
    }

    if (pty->hpc != NULL) {
        ClosePseudoConsole(pty->hpc);
        pty->hpc = NULL;
    }

    if (pty->pipe_in != INVALID_HANDLE_VALUE) {
        CloseHandle(pty->pipe_in);
        pty->pipe_in = INVALID_HANDLE_VALUE;
    }

    /* crt_fd owns pipe_out via _open_osfhandle — _close closes both */
    if (pty->pipe_source.crt_fd >= 0) {
        _close(pty->pipe_source.crt_fd);
        pty->pipe_source.crt_fd = -1;
        pty->pipe_out = INVALID_HANDLE_VALUE;
    } else if (pty->pipe_out != INVALID_HANDLE_VALUE) {
        CloseHandle(pty->pipe_out);
        pty->pipe_out = INVALID_HANDLE_VALUE;
    }

    return YETTY_OK_VOID();
}

static struct yetty_yplatform_pty_pipe_source *win_conpty_pipe_source(
    struct yetty_yplatform_pty *self)
{
    struct win_conpty *pty = container_of(self, struct win_conpty, base);
    return &pty->pipe_source.base;
}

/* Create ConPTY with shell */

static struct yetty_yplatform_pty_result win_conpty_create(struct yetty_yconfig *config)
{
    struct win_conpty *pty;
    HANDLE pipe_pty_in = INVALID_HANDLE_VALUE;
    HANDLE pipe_pty_out = INVALID_HANDLE_VALUE;
    COORD size;
    HRESULT hr;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    SIZE_T attr_size = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = NULL;
    wchar_t cmd[] = L"cmd.exe";

    (void)config;

    pty = malloc(sizeof(struct win_conpty));
    if (!pty) {
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate conpty");
    }

    pty->base.ops = &win_conpty_ops;
    pty->hpc = NULL;
    pty->pipe_in = INVALID_HANDLE_VALUE;
    pty->pipe_out = INVALID_HANDLE_VALUE;
    pty->process = INVALID_HANDLE_VALUE;
    pty->cols = 80;
    pty->rows = 24;
    pty->running = 0;
    pty->pipe_source.base.abstract = (uintptr_t)-1;
    pty->pipe_source.handle = INVALID_HANDLE_VALUE;
    pty->pipe_source.crt_fd = -1;

    /* Create input pipe (we write, ConPTY reads) — regular pipe is fine */
    if (!CreatePipe(&pipe_pty_in, &pty->pipe_in, NULL, 0)) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "CreatePipe for input failed");
    }

    /* Create output pipe (ConPTY writes, we read via libuv uv_pipe_t)
     * Must use CreateNamedPipeW with FILE_FLAG_OVERLAPPED for IOCP */
    {
        static volatile LONG pipe_counter = 0;
        char pipe_name[256];
        LONG id = InterlockedIncrement(&pipe_counter);
        snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\yetty-pty-%lu-%ld",
                 (unsigned long)GetCurrentProcessId(), (long)id);

        pty->pipe_out = CreateNamedPipeA(pipe_name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                         PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
        if (pty->pipe_out == INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_pty_in);
            CloseHandle(pty->pipe_in);
            free(pty);
            return YETTY_ERR(yetty_yplatform_pty, "CreateNamedPipe for output failed");
        }

        pipe_pty_out = CreateFileA(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
        if (pipe_pty_out == INVALID_HANDLE_VALUE) {
            CloseHandle(pty->pipe_out);
            CloseHandle(pipe_pty_in);
            CloseHandle(pty->pipe_in);
            free(pty);
            return YETTY_ERR(yetty_yplatform_pty, "CreateFile for output pipe failed");
        }
    }

    /* Create pseudo console */
    size.X = (SHORT)pty->cols;
    size.Y = (SHORT)pty->rows;

    /* Detach from any inherited parent console BEFORE creating the ConPTY.
     * Without this, cmd.exe's std handles fall back to the parent console
     * (e.g. a shell that launched yetty), bypassing PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE. */
    {
        HWND parent_console = GetConsoleWindow();
        BOOL freed = FreeConsole();
        ydebug("win_conpty_create: parent_console=%p FreeConsole=%d", (void *)parent_console,
               (int)freed);
    }

    hr = CreatePseudoConsole(size, pipe_pty_in, pipe_pty_out, 0, &pty->hpc);
    CloseHandle(pipe_pty_in);
    CloseHandle(pipe_pty_out);

    if (FAILED(hr)) {
        CloseHandle(pty->pipe_in);
        CloseHandle(pty->pipe_out);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "CreatePseudoConsole failed");
    }

    /* Create process with pseudo console */
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);

    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    attr_list = malloc(attr_size);
    if (!attr_list) {
        ClosePseudoConsole(pty->hpc);
        CloseHandle(pty->pipe_in);
        CloseHandle(pty->pipe_out);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate attribute list");
    }

    if (!InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
        free(attr_list);
        ClosePseudoConsole(pty->hpc);
        CloseHandle(pty->pipe_in);
        CloseHandle(pty->pipe_out);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "InitializeProcThreadAttributeList failed");
    }

    if (!UpdateProcThreadAttribute(attr_list, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pty->hpc,
                                   sizeof(HPCON), NULL, NULL)) {
        DeleteProcThreadAttributeList(attr_list);
        free(attr_list);
        ClosePseudoConsole(pty->hpc);
        CloseHandle(pty->pipe_in);
        CloseHandle(pty->pipe_out);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "UpdateProcThreadAttribute failed");
    }

    si.lpAttributeList = attr_list;

    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                        &si.StartupInfo, &pi)) {
        DeleteProcThreadAttributeList(attr_list);
        free(attr_list);
        ClosePseudoConsole(pty->hpc);
        CloseHandle(pty->pipe_in);
        CloseHandle(pty->pipe_out);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "CreateProcessW failed");
    }

    DeleteProcThreadAttributeList(attr_list);
    free(attr_list);

    CloseHandle(pi.hThread);
    pty->process = pi.hProcess;
    pty->pipe_source.handle = pty->pipe_out;

    /* Create CRT fd for libuv's uv_pipe_open */
    pty->pipe_source.crt_fd = _open_osfhandle((intptr_t)pty->pipe_out, _O_RDONLY);
    if (pty->pipe_source.crt_fd < 0) {
        TerminateProcess(pty->process, 0);
        CloseHandle(pty->process);
        ClosePseudoConsole(pty->hpc);
        CloseHandle(pty->pipe_in);
        CloseHandle(pty->pipe_out);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "_open_osfhandle failed for pty output");
    }
    pty->pipe_source.base.abstract = (uintptr_t)pty->pipe_source.crt_fd;
    pty->running = 1;

    return YETTY_OK(yetty_yplatform_pty, &pty->base);
}

/* Factory implementation */

static void win_pty_factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct win_pty_factory *factory = container_of(self, struct win_pty_factory, base);
    if (factory->qemu_proc) {
        qemu_stop(factory->qemu_proc);
        factory->qemu_proc = NULL;
    }
    free(factory);
}

static struct yetty_yplatform_pty_result win_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_ycore_event_loop *event_loop)
{
    struct win_pty_factory *factory = container_of(self, struct win_pty_factory, base);
    struct yetty_yconfig *config = factory->config;

    /* --temu: in-process tinyemu RISC-V VM */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_TEMU, 0)) {
        return tinyemu_pty_create(config);
    }

    /* --qemu: spawn qemu, then connect to its telnet console. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_QEMU, 0)) {
        if (!factory->qemu_proc) {
            factory->qemu_proc = qemu_start(QEMU_TELNET_PORT);
            if (!factory->qemu_proc) {
                return YETTY_ERR(yetty_yplatform_pty, "failed to start QEMU");
            }
            if (!qemu_wait_ready(QEMU_TELNET_PORT, 5000)) {
                qemu_stop(factory->qemu_proc);
                factory->qemu_proc = NULL;
                return YETTY_ERR(yetty_yplatform_pty, "QEMU telnet not ready");
            }
        }
        return telnet_pty_create("127.0.0.1", QEMU_TELNET_PORT, event_loop);
    }

    /* Default: native ConPTY (cmd.exe / pwsh) */
    (void)event_loop;
    return win_conpty_create(factory->config);
}

/* Factory creation - the public API */

struct yetty_yplatform_pty_factory_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig *config, void *os_specific)
{
    struct win_pty_factory *factory;

    (void)os_specific;

    factory = calloc(1, sizeof(struct win_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate pty factory");
    }

    factory->base.ops = &win_pty_factory_ops;
    factory->config = config;

    return YETTY_OK(yetty_yplatform_pty_factory, &factory->base);
}
