/* Bump-allocator arena: O(1) alloc, free-everything-at-once. Replaces CPython's
 * PyArena (which kept a PyList of objects to decref — unneeded now that AST
 * leaves are plain C). Part of the yetty project (see ../../LICENSE.md). */
#include "arena.h"
#include "ast.gen.h"   /* mod_ty for _PyAST_Validate */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PYP_ARENA_BLOCK (256 * 1024)

struct pyp_block {
    struct pyp_block *next;
    size_t used;
    size_t capacity;
    char data[];
};

struct pyp_arena {
    struct pyp_block *head;
    int oom;
};

void
pyp_ast_error(const char *message)
{
    fprintf(stderr, "py-parse: AST error: %s\n", message);
}

static struct pyp_block *
block_new(size_t capacity)
{
    struct pyp_block *block = malloc(sizeof(*block) + capacity);
    if (block == NULL) {
        return NULL;
    }
    block->next = NULL;
    block->used = 0;
    block->capacity = capacity;
    return block;
}

PyArena *
_PyArena_New(void)
{
    PyArena *arena = calloc(1, sizeof(*arena));
    if (arena == NULL) {
        return NULL;
    }
    arena->head = block_new(PYP_ARENA_BLOCK);
    if (arena->head == NULL) {
        free(arena);
        return NULL;
    }
    return arena;
}

void
_PyArena_Free(PyArena *arena)
{
    if (arena == NULL) {
        return;
    }
    struct pyp_block *block = arena->head;
    while (block != NULL) {
        struct pyp_block *next = block->next;
        free(block);
        block = next;
    }
    free(arena);
}

void *
_PyArena_Malloc(PyArena *arena, size_t size)
{
    /* Align to pointer size. */
    size = (size + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);

    struct pyp_block *block = arena->head;
    if (block->used + size > block->capacity) {
        size_t capacity = size > PYP_ARENA_BLOCK ? size : PYP_ARENA_BLOCK;
        struct pyp_block *fresh = block_new(capacity);
        if (fresh == NULL) {
            arena->oom = 1;
            return NULL;
        }
        fresh->next = arena->head;
        arena->head = fresh;
        block = fresh;
    }
    void *ptr = block->data + block->used;
    block->used += size;
    return ptr;
}

/* The three base sequence allocators (typed seqs are generated in ast.gen.c). */
GENERATE_ASDL_SEQ_CONSTRUCTOR(generic, void *)
GENERATE_ASDL_SEQ_CONSTRUCTOR(identifier, void *)
GENERATE_ASDL_SEQ_CONSTRUCTOR(int, int)

/* Arena PyObject tracking is a no-op (boxes are leaked, not refcounted). */
int
_PyArena_AddPyObject(PyArena *arena, PyObject *obj)
{
    (void)arena;
    (void)obj;
    return 0;
}

/* AST validation: accept everything (we trust the grammar). */
int
_PyAST_Validate(mod_ty mod)
{
    (void)mod;
    return 1;
}
