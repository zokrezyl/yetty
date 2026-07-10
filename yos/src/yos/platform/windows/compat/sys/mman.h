/* Tiny mmap/munmap shim over Win32 VirtualAlloc.
 *
 * Only intended for callers that need MAP_ANONYMOUS|MAP_PRIVATE with
 * PROT_READ|PROT_WRITE — i.e. wasm3's linear-memory allocator. Other
 * mmap variants (file mapping, MAP_SHARED, mprotect tricks) are NOT
 * implemented and will fail.
 *
 * mmap returns MAP_FAILED on error to match POSIX.
 */
#ifndef YOS_WIN_COMPAT_SYS_MMAN_H
#define YOS_WIN_COMPAT_SYS_MMAN_H

#include <stddef.h>
#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

static inline void *mmap(void *addr, size_t length, int prot, int flags,
                         int fd, long long offset)
{
    (void)addr; (void)fd; (void)offset;
    if (!(flags & MAP_ANONYMOUS)) return MAP_FAILED;

    DWORD protect = PAGE_NOACCESS;
    if ((prot & PROT_READ) && (prot & PROT_WRITE)) protect = PAGE_READWRITE;
    else if (prot & PROT_READ)                     protect = PAGE_READONLY;
    else if (prot & PROT_WRITE)                    protect = PAGE_READWRITE;
    if (prot & PROT_EXEC) {
        if (prot & PROT_WRITE)     protect = PAGE_EXECUTE_READWRITE;
        else if (prot & PROT_READ) protect = PAGE_EXECUTE_READ;
        else                       protect = PAGE_EXECUTE;
    }
    void *p = VirtualAlloc(NULL, length, MEM_COMMIT | MEM_RESERVE, protect);
    return p ? p : MAP_FAILED;
}

static inline int munmap(void *addr, size_t length)
{
    (void)length;
    return VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1;
}

static inline int mprotect(void *addr, size_t length, int prot)
{
    DWORD protect = PAGE_NOACCESS, old;
    if ((prot & PROT_READ) && (prot & PROT_WRITE)) protect = PAGE_READWRITE;
    else if (prot & PROT_READ)                     protect = PAGE_READONLY;
    return VirtualProtect(addr, length, protect, &old) ? 0 : -1;
}

#endif /* YOS_WIN_COMPAT_SYS_MMAN_H */
