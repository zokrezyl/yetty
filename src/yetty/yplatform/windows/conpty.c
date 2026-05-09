/* Windows ConPTY implementation */

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>
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
struct yetty_yplatform_win_pty_pipe_source {
    struct yetty_platform_pty_pipe_source base;
    HANDLE handle;
    int crt_fd; /* CRT fd from _open_osfhandle for libuv */
};

/* Windows ConPTY implementation - embeds base as first member */
struct yetty_yplatform_win_conpty {
    struct yetty_platform_pty base;
    struct yetty_yplatform_win_pty_pipe_source pipe_source;
    HPCON hpc;
    HANDLE pipe_in;
    HANDLE pipe_out;
    HANDLE process;
    uint32_t cols;
    uint32_t rows;
    int running;
};

/* Forward declarations */
static struct yetty_ycore_void_result win_conpty_destroy(struct yetty_platform_pty *self);
static struct yetty_ycore_size_result win_conpty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len);
static struct yetty_ycore_size_result win_conpty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len);
static struct yetty_ycore_void_result win_conpty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result win_conpty_stop(struct yetty_platform_pty *self);
static struct yetty_platform_pty_pipe_source *win_conpty_pipe_source(
    struct yetty_platform_pty *self);

/* Ops table */
static const struct yetty_platform_pty_ops win_conpty_ops = {
    .destroy = win_conpty_destroy,
    .read = win_conpty_read,
    .write = win_conpty_write,
    .resize = win_conpty_resize,
    .stop = win_conpty_stop,
    .pipe_source = win_conpty_pipe_source,
};

/* Windows PTY factory - embeds base as first member */
struct yetty_yplatform_win_pty_factory {
    struct yetty_yplatform_pty_factory base;
    struct yetty_yconfig_config *config;
    struct yetty_yplatform_yprocess *qemu_proc; /* spawned by qemu_start when --qemu is set */
};

/* Forward declarations for factory */
static void win_pty_factory_destroy(struct yetty_yplatform_pty_factory *self);
static struct yetty_yplatform_pty_result win_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop);

/* Factory ops table */
static const struct yetty_yplatform_pty_factory_ops win_pty_factory_ops = {
    .destroy = win_pty_factory_destroy,
    .create_pty = win_pty_factory_create_pty,
};

/* PTY implementation */

static struct yetty_ycore_void_result win_conpty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_win_conpty *pty =
        container_of(self, struct yetty_yplatform_win_conpty, base);

    struct yetty_ycore_void_result stop_r = win_conpty_stop(self);
    free(pty);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "win_conpty_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result win_conpty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len)
{
    struct yetty_yplatform_win_conpty *pty =
        container_of(self, struct yetty_yplatform_win_conpty, base);
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

static struct yetty_ycore_size_result win_conpty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct yetty_yplatform_win_conpty *pty =
        container_of(self, struct yetty_yplatform_win_conpty, base);
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

static struct yetty_ycore_void_result win_conpty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows)
{
    struct yetty_yplatform_win_conpty *pty =
        container_of(self, struct yetty_yplatform_win_conpty, base);
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

static struct yetty_ycore_void_result win_conpty_stop(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_win_conpty *pty =
        container_of(self, struct yetty_yplatform_win_conpty, base);

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

static struct yetty_platform_pty_pipe_source *win_conpty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_yplatform_win_conpty *pty =
        container_of(self, struct yetty_yplatform_win_conpty, base);
    return &pty->pipe_source.base;
}

/* Create ConPTY with shell */

static struct yetty_yplatform_pty_result win_conpty_create(struct yetty_yconfig_config *config)
{
    struct yetty_yplatform_win_conpty *pty;
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

    pty = malloc(sizeof(struct yetty_yplatform_win_conpty));
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

    ydebug("win_conpty_create: CreatePseudoConsole hr=0x%08lx hpc=%p", (long)hr, (void *)pty->hpc);

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

    /* Suppress std-handle leakage into cmd.exe.
     *
     * On Windows the std handles (stdin/stdout/stderr) propagate to a child
     * process by a path independent of bInheritHandles. When yetty's parent
     * (PowerShell `Start-Process -RedirectStandardOutput`, a `> file.log`
     * redirect, etc.) hands yetty a redirected stdout handle, that handle
     * leaks into cmd.exe even though we pass bInheritHandles=FALSE and have
     * already FreeConsole'd. cmd.exe then sees its stdout as "redirected
     * file" and switches to the redirected-mode behaviour: ANSI control
     * sequences (the terminal init) go to CONOUT$ which is the conpty (we
     * render those), but the banner and prompt text go straight to the
     * inherited stdout file — bypassing the conpty entirely. The yetty
     * window then renders an empty cleared screen and looks frozen.
     *
     * Fix: pass STARTF_USESTDHANDLES with all three std handles set to
     * NULL. PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE then provides the actual
     * std handles from the conpty, and cmd.exe sees them as a real
     * console rather than a redirect, so banner + prompt + everything
     * else flows through the pty pipe and we render it.
     *
     * Per CreateProcess docs, STARTF_USESTDHANDLES requires
     * bInheritHandles=TRUE — but we still don't want any of yetty's other
     * inheritable handles (libuv pipes, GLFW resources) to leak. The
     * empty-handle-list trick (PROC_THREAD_ATTRIBUTE_HANDLE_LIST with a
     * size of zero, expressed as a one-entry list pointing at the conpty
     * itself which is already passed via PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE)
     * isn't supported, so we accept that bInheritHandles=TRUE allows
     * inheritable handles in. We mitigate by NOT marking yetty-internal
     * handles as inheritable elsewhere. */
    si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = NULL;
    si.StartupInfo.hStdOutput = NULL;
    si.StartupInfo.hStdError = NULL;

    BOOL cp_ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL,
                                NULL, &si.StartupInfo, &pi);
    DWORD cp_err = cp_ok ? 0 : GetLastError();
    ydebug("win_conpty_create: CreateProcessW ok=%d err=%lu pid=%lu hProcess=%p hThread=%p",
           (int)cp_ok, (unsigned long)cp_err, (unsigned long)pi.dwProcessId, (void *)pi.hProcess,
           (void *)pi.hThread);
    if (!cp_ok) {
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
    struct yetty_yplatform_win_pty_factory *factory =
        container_of(self, struct yetty_yplatform_win_pty_factory, base);
    if (factory->qemu_proc) {
        yetty_yqemu_qemu_stop(factory->qemu_proc);
        factory->qemu_proc = NULL;
    }
    free(factory);
}

static struct yetty_yplatform_pty_result win_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop)
{
    struct yetty_yplatform_win_pty_factory *factory =
        container_of(self, struct yetty_yplatform_win_pty_factory, base);
    struct yetty_yconfig_config *config = factory->config;

    /* --telnet: connect to an already-running telnet server (qemu chardev,
     * a tinyemu+slirp hostfwd to in-guest telnetd, an external telnet
     * daemon, etc.). Pure client — owns no server lifecycle. Mirrors
     * unix-pty-factory.c. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_TELNET, 0)) {
        const char *host =
            config->ops->get_string(config, YETTY_YCONFIG_KEY_TELNET_HOST, "127.0.0.1");
        int port = config->ops->get_int(config, YETTY_YCONFIG_KEY_TELNET_PORT, 0);
        if (port <= 0 || port > 65535) {
            return YETTY_ERR(yetty_yplatform_pty, "--telnet requires telnet/port (1..65535)");
        }
        return yetty_ytelnet_telnet_pty_create_tcp(host, (uint16_t)port, event_loop);
    }

    /* --temu: in-process tinyemu RISC-V VM */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_TEMU, 0)) {
        return yetty_yplatform_tinyemu_pty_create(config);
    }

    /* --qemu: spawn qemu, then connect to its telnet console. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_QEMU, 0)) {
        if (!factory->qemu_proc) {
            factory->qemu_proc = yetty_yqemu_qemu_start(QEMU_TELNET_PORT);
            if (!factory->qemu_proc) {
                return YETTY_ERR(yetty_yplatform_pty, "failed to start QEMU");
            }
            /* 30 s: kernel boot + alpine init + telnetd bind from a cold
             * start can run 5–15 s on slow hardware; 5 s used to be enough
             * for the chardev path because QEMU bound the chardev port
             * before the guest finished booting. The slirp hostfwd path
             * needs the in-guest daemon up. */
            struct yetty_ycore_void_result wait_res =
                yetty_yqemu_qemu_wait_ready(QEMU_TELNET_PORT, 30000);
            if (YETTY_IS_ERR(wait_res)) {
                yetty_yqemu_qemu_stop(factory->qemu_proc);
                factory->qemu_proc = NULL;
                return YETTY_ERR(yetty_yplatform_pty, "QEMU telnet not ready", wait_res);
            }
        }
        return yetty_ytelnet_telnet_pty_create_tcp("127.0.0.1", QEMU_TELNET_PORT, event_loop);
    }

    /* Default: native ConPTY (cmd.exe / pwsh) */
    (void)event_loop;
    return win_conpty_create(factory->config);
}

/* Factory creation - the public API */

struct yetty_yplatform_pty_factory_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific)
{
    struct yetty_yplatform_win_pty_factory *factory;

    (void)os_specific;

    factory = calloc(1, sizeof(struct yetty_yplatform_win_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate pty factory");
    }

    factory->base.ops = &win_pty_factory_ops;
    factory->config = config;

    return YETTY_OK(yetty_yplatform_pty_factory, &factory->base);
}
