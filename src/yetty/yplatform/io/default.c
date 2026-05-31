/* POSIX impl: poll(2, timeout=0) then read(2). EAGAIN / EWOULDBLOCK /
 * EINTR collapse to "no bytes ready". */

#include <yetty/yplatform/io.h>

#include <errno.h>
#include <poll.h>
#include <unistd.h>

int yetty_yplatform_io_wait_readable(int fd, int timeout_ms)
{
    if (fd < 0) {
        return -1;
    }
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int pr;
    do {
        pr = poll(&pfd, 1, timeout_ms);
    } while (pr < 0 && errno == EINTR);
    return pr;
}

struct yetty_yplatform_io_size_result yetty_yplatform_io_read_nonblocking(int fd, void *buf,
                                                                          size_t cap)
{
    if (fd < 0 || !buf || cap == 0) {
        return YETTY_OK(yetty_yplatform_io_size, 0);
    }

    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int pr;
    do {
        pr = poll(&pfd, 1, 0);
    } while (pr < 0 && errno == EINTR);
    if (pr < 0) {
        return YETTY_ERR(yetty_yplatform_io_size, "io_read_nonblocking: poll failed");
    }
    if (pr == 0) {
        return YETTY_OK(yetty_yplatform_io_size, 0);
    }
    if (!(pfd.revents & (POLLIN | POLLHUP))) {
        return YETTY_OK(yetty_yplatform_io_size, 0);
    }

    ssize_t n;
    do {
        n = read(fd, buf, cap);
    } while (n < 0 && errno == EINTR);
    if (n > 0) {
        return YETTY_OK(yetty_yplatform_io_size, (size_t)n);
    }
    if (n == 0) {
        return YETTY_OK(yetty_yplatform_io_size, 0); /* EOF — caller decides */
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return YETTY_OK(yetty_yplatform_io_size, 0);
    }
    return YETTY_ERR(yetty_yplatform_io_size, "io_read_nonblocking: read failed");
}
