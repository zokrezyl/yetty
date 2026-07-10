/* <sys/syscall.h> — placeholder. Windows has no Linux SYS_*; yos
 * Windows host slices never reach syscall(SYS_*) sites (those live in
 * Linux-only io-linux.c). Provide an empty header so includes parse. */
#ifndef YOS_WIN_COMPAT_SYS_SYSCALL_H
#define YOS_WIN_COMPAT_SYS_SYSCALL_H
#endif
