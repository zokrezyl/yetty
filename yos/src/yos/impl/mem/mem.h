#ifndef YOS_MEM_H
#define YOS_MEM_H

#include "yos/types.h"

int32_t yos_brk(struct yos_exec_ctx *ctx, uint32_t addr);
int32_t yos_mmap(struct yos_exec_ctx *ctx);
int32_t yos_munmap(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t len);
int32_t yos_mprotect(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, int32_t prot);
int32_t yos_mlock(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len);
int32_t yos_munlock(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len);
int32_t yos_mlockall(struct yos_exec_ctx *ctx, int32_t flags);
int32_t yos_munlockall(struct yos_exec_ctx *ctx);
int32_t yos_mremap(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t old_len, uint32_t new_len, int32_t flags, uint32_t new_addr);
int32_t yos_mmap2(struct yos_exec_ctx *ctx, uint32_t addr, uint32_t length,
                       int32_t prot, int32_t flags, int32_t fd, uint32_t pgoffset);
int32_t yos_madvise(struct yos_exec_ctx *ctx, uint32_t start, uint32_t len, int32_t behavior);

#endif /* YOS_MEM_H */
