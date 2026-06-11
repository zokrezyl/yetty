/*
 * yffi.c — aggregation unit for the FFI shared library (libyetty_ffi.so).
 *
 * The shared library is built ONLY in the dedicated PIC "double build" tree
 * (YETTY_BUILD_FFI_SHARED=ON, CMAKE_POSITION_INDEPENDENT_CODE=ON). The static
 * application build never links it, so the fast non-PIC archives stay
 * uncompromised. The CMake target force-includes the yclass module archives
 * whose `yetty_<module>_*` stubs the language bindings (bindings/<lang>/) call
 * via dlopen; this TU just gives the library a source + a version probe.
 */

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/ycore/result.h>
#include <yetty/yplatform/pty.h>

/* Version probe the bindings can call to confirm they loaded the expected
 * library. Bumped alongside the binding ABI. */
const char *yetty_ffi_version(void)
{
    return "0.0.1";
}

/*
 * fd-backed platform_pty — a convenience for FFI clients (the language
 * bindings) that want the ygui framework / yview to emit straight to a plain
 * file descriptor (their own stdout = the yetty PTY).
 *
 * The platform_pty vtable returns Result structs by value, which ctypes
 * cannot synthesize from a Python callback, so this small C shim provides a
 * ready-made pty whose write() blocks until all bytes are flushed. read()
 * yields nothing; the framework only writes.
 */
struct yetty_yffi_fd_pty {
    struct yetty_platform_pty base;
    int fd;
};

static struct yetty_ycore_size_result yetty_yffi_fd_pty_write(struct yetty_platform_pty *self,
                                                              const char *data, size_t len)
{
    struct yetty_yffi_fd_pty *pty = (struct yetty_yffi_fd_pty *)self;
    size_t written = 0;
    while (written < len) {
        ssize_t count = write(pty->fd, data + written, len - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return YETTY_ERR(yetty_ycore_size, "yffi fd pty: write failed");
        }
        written += (size_t)count;
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result yetty_yffi_fd_pty_read(struct yetty_platform_pty *self,
                                                             char *buf, size_t max_len)
{
    (void)self;
    (void)buf;
    (void)max_len;
    return YETTY_OK(yetty_ycore_size, 0);
}

static struct yetty_ycore_void_result yetty_yffi_fd_pty_noop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result yetty_yffi_fd_pty_resize(struct yetty_platform_pty *self,
                                                               uint32_t cols, uint32_t rows,
                                                               uint32_t pixel_w, uint32_t pixel_h)
{
    (void)self;
    (void)cols;
    (void)rows;
    (void)pixel_w;
    (void)pixel_h;
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *yetty_yffi_fd_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    (void)self;
    return NULL;
}

static const struct yetty_platform_pty_ops *yetty_yffi_fd_pty_ops(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = yetty_yffi_fd_pty_noop,
        .read = yetty_yffi_fd_pty_read,
        .write = yetty_yffi_fd_pty_write,
        .resize = yetty_yffi_fd_pty_resize,
        .stop = yetty_yffi_fd_pty_noop,
        .pipe_source = yetty_yffi_fd_pty_pipe_source,
    };
    return &ops;
}

/* Create a platform_pty that writes to `fd` (borrowed — the caller keeps it
 * open). Returns NULL on allocation failure. Free with
 * yetty_yffi_fd_pty_destroy. */
struct yetty_platform_pty *yetty_yffi_fd_pty_create(int fd)
{
    struct yetty_yffi_fd_pty *pty = calloc(1, sizeof(struct yetty_yffi_fd_pty));
    if (!pty) {
        return NULL;
    }
    pty->base.ops = yetty_yffi_fd_pty_ops();
    pty->fd = fd;
    return &pty->base;
}

void yetty_yffi_fd_pty_destroy(struct yetty_platform_pty *self)
{
    free(self);
}
