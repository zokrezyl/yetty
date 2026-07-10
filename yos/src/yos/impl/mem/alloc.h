#ifndef YOS_IMPL_ALLOC_H
#define YOS_IMPL_ALLOC_H

/* yos allocator subsystem — host-side mimalloc managing a region of the
 * guest's wasm linear memory. Bridges in yos_bridge.c (custom_alloc
 * routing) call into yos_<name>(ctx, ...) below; each returns a wasm
 * offset (uint32_t), not a host pointer.
 *
 * Lifecycle: lazy init on first allocation. yos_alloc_init reserves a
 * sub-region of ctx->memory between ctx->heap_end and ctx->memory_size/2,
 * registers it with mimalloc as an exclusive arena, and creates a
 * dedicated heap on top. ctx->heap_end is bumped past the arena so brk
 * cannot trample it.
 *
 * Per-ctx state lives on struct yos_exec_ctx as opaque void-ptr / int
 * fields (alloc.c casts to mi_heap_t and mi_arena_id_t) so mimalloc.h
 * stays out of yos/types.h. */

#include <stdint.h>

struct yos_exec_ctx;

uint32_t yos_malloc            (struct yos_exec_ctx *ctx, uint32_t size);
void     yos_free              (struct yos_exec_ctx *ctx, uint32_t off);
uint32_t yos_calloc            (struct yos_exec_ctx *ctx, uint32_t nmemb,
                                                          uint32_t size);
uint32_t yos_realloc           (struct yos_exec_ctx *ctx, uint32_t off,
                                                          uint32_t newsize);
uint32_t yos_reallocarray      (struct yos_exec_ctx *ctx, uint32_t off,
                                                          uint32_t nmemb,
                                                          uint32_t size);
int32_t  yos_posix_memalign    (struct yos_exec_ctx *ctx, uint32_t memptr_off,
                                                          uint32_t alignment,
                                                          uint32_t size);
uint32_t yos_aligned_alloc     (struct yos_exec_ctx *ctx, uint32_t alignment,
                                                          uint32_t size);
uint32_t yos_memalign          (struct yos_exec_ctx *ctx, uint32_t alignment,
                                                          uint32_t size);
uint32_t yos_valloc            (struct yos_exec_ctx *ctx, uint32_t size);
uint32_t yos_malloc_usable_size(struct yos_exec_ctx *ctx, uint32_t off);

#endif /* YOS_IMPL_ALLOC_H */
