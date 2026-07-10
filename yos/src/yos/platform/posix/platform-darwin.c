/* Darwin host impls of the POSIX platform abstraction. */
#include "platform.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

pid_t yos_plat_gettid(void) {
    uint64_t tid64 = 0;
    pthread_threadid_np(NULL, &tid64);
    return (pid_t)tid64;
}

/* POSIX advice constants — same numeric values on Linux/FreeBSD;
 * darwin has no <fcntl.h> definitions for them, so we hard-code. */
#define YOS_FADV_NORMAL      0
#define YOS_FADV_RANDOM      1
#define YOS_FADV_SEQUENTIAL  2
#define YOS_FADV_WILLNEED    3
#define YOS_FADV_DONTNEED    4
#define YOS_FADV_NOREUSE     5

int yos_plat_posix_fadvise(int fd, off_t offset, off_t len, int advice) {
    switch (advice) {
    case YOS_FADV_NORMAL:
    case YOS_FADV_SEQUENTIAL:
        return fcntl(fd, F_RDAHEAD, 1) < 0 ? errno : 0;
    case YOS_FADV_RANDOM:
        return fcntl(fd, F_RDAHEAD, 0) < 0 ? errno : 0;
    case YOS_FADV_WILLNEED: {
        /* F_RDADVISE is a hint to prefetch [offset, offset+count). */
        struct radvisory ra = {
            .ra_offset = offset,
            .ra_count  = (int)((len > (off_t)INT_MAX) ? INT_MAX : len),
        };
        return fcntl(fd, F_RDADVISE, &ra) < 0 ? errno : 0;
    }
    case YOS_FADV_DONTNEED:
        /* Closest darwin analogue: bypass the unified buffer cache for
         * subsequent IO on this fd. (Cannot evict a specific range.) */
        return fcntl(fd, F_NOCACHE, 1) < 0 ? errno : 0;
    case YOS_FADV_NOREUSE:
        /* Purely advisory; no darwin equivalent. */
        return 0;
    default:
        return EINVAL;
    }
}

int yos_plat_posix_fallocate(int fd, off_t offset, off_t len) {
    /* posix_fallocate must ensure the file is at least offset+len
     * bytes, with the range allocated and zero-filled. Darwin recipe:
     *   1. fcntl(F_PREALLOCATE) past EOF to reserve physical space
     *      (try contiguous first, then allow fragmented);
     *   2. ftruncate to extend the logical size — newly-extended bytes
     *      read as zero per POSIX. */
    if (offset < 0 || len <= 0) return EINVAL;

    struct stat st;
    if (fstat(fd, &st) < 0) return errno;

    off_t end = offset + len;
    if (end <= st.st_size) return 0;

    off_t need = end - st.st_size;
    fstore_t fst = {
        .fst_flags   = F_ALLOCATECONTIG,
        .fst_posmode = F_PEOFPOSMODE,
        .fst_offset  = 0,
        .fst_length  = need,
        .fst_bytesalloc = 0,
    };
    if (fcntl(fd, F_PREALLOCATE, &fst) < 0) {
        fst.fst_flags = F_ALLOCATEALL;
        if (fcntl(fd, F_PREALLOCATE, &fst) < 0) return errno;
    }
    if (ftruncate(fd, end) < 0) return errno;
    return 0;
}
