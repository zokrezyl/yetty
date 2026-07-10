/* Linux clone(2) flag bits — guest ABI.
 *
 * These are the wire-format constants the wasm guest passes to
 * yos_proc_clone (musl/glibc on Linux source these from <sched.h>).
 * Stable Linux UAPI; fine to mirror locally so the host runtime
 * compiles on darwin / Windows where <sched.h> doesn't define them.
 *
 * Guarded with #ifndef so a Linux build still picks up libc's copy
 * and we never collide. */
#ifndef YOS_CLONE_ABI_H
#define YOS_CLONE_ABI_H

#ifndef CSIGNAL
#  define CSIGNAL              0x000000ff
#endif
#ifndef CLONE_VM
#  define CLONE_VM             0x00000100
#endif
#ifndef CLONE_FS
#  define CLONE_FS             0x00000200
#endif
#ifndef CLONE_FILES
#  define CLONE_FILES          0x00000400
#endif
#ifndef CLONE_SIGHAND
#  define CLONE_SIGHAND        0x00000800
#endif
#ifndef CLONE_PIDFD
#  define CLONE_PIDFD          0x00001000
#endif
#ifndef CLONE_PTRACE
#  define CLONE_PTRACE         0x00002000
#endif
#ifndef CLONE_VFORK
#  define CLONE_VFORK          0x00004000
#endif
#ifndef CLONE_PARENT
#  define CLONE_PARENT         0x00008000
#endif
#ifndef CLONE_THREAD
#  define CLONE_THREAD         0x00010000
#endif
#ifndef CLONE_NEWNS
#  define CLONE_NEWNS          0x00020000
#endif
#ifndef CLONE_SYSVSEM
#  define CLONE_SYSVSEM        0x00040000
#endif
#ifndef CLONE_SETTLS
#  define CLONE_SETTLS         0x00080000
#endif
#ifndef CLONE_PARENT_SETTID
#  define CLONE_PARENT_SETTID  0x00100000
#endif
#ifndef CLONE_CHILD_CLEARTID
#  define CLONE_CHILD_CLEARTID 0x00200000
#endif
#ifndef CLONE_DETACHED
#  define CLONE_DETACHED       0x00400000
#endif
#ifndef CLONE_UNTRACED
#  define CLONE_UNTRACED       0x00800000
#endif
#ifndef CLONE_CHILD_SETTID
#  define CLONE_CHILD_SETTID   0x01000000
#endif
#ifndef CLONE_NEWCGROUP
#  define CLONE_NEWCGROUP      0x02000000
#endif
#ifndef CLONE_NEWUTS
#  define CLONE_NEWUTS         0x04000000
#endif
#ifndef CLONE_NEWIPC
#  define CLONE_NEWIPC         0x08000000
#endif
#ifndef CLONE_NEWUSER
#  define CLONE_NEWUSER        0x10000000
#endif
#ifndef CLONE_NEWPID
#  define CLONE_NEWPID         0x20000000
#endif
#ifndef CLONE_NEWNET
#  define CLONE_NEWNET         0x40000000
#endif
#ifndef CLONE_IO
#  define CLONE_IO             0x80000000
#endif

#endif /* YOS_CLONE_ABI_H */
