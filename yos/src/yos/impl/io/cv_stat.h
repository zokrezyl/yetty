#ifndef YOS_IMPL_IO_CV_STAT_H
#define YOS_IMPL_IO_CV_STAT_H

/* FreeBSD-i386 struct stat fill — used by every bridge converting a
 * host `struct stat` into the wasm guest's buffer. See impl/io/cv_stat.c
 * for the offsets and tools/struct-offsets.py for how they were derived. */

#include <stdint.h>
struct stat;

void yos_cv_stat_fbi(uint8_t *w, const struct stat *h);

#endif
