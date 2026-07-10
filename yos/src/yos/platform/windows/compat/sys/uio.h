/* Minimal <sys/uio.h> compat. POSIX iovec struct. */
#ifndef YOS_WIN_COMPAT_SYS_UIO_H
#define YOS_WIN_COMPAT_SYS_UIO_H

#include <sys/types.h>
#include <stddef.h>

struct iovec {
    void  *iov_base;
    size_t iov_len;
};

#endif
