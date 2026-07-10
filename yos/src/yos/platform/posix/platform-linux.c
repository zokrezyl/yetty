/* Linux host impls of the POSIX platform abstraction. */
#define _GNU_SOURCE
#include "platform.h"

#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

pid_t yos_plat_gettid(void) {
    return (pid_t)syscall(SYS_gettid);
}

int yos_plat_posix_fadvise(int fd, off_t offset, off_t len, int advice) {
    return posix_fadvise(fd, offset, len, advice);
}

int yos_plat_posix_fallocate(int fd, off_t offset, off_t len) {
    return posix_fallocate(fd, offset, len);
}
