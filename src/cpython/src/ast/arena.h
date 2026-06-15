/* Bump-allocator arena — libpython-free replacement for CPython's PyArena.
 * Part of the yetty project (see ../../LICENSE.md). */
#ifndef PYP_ARENA_H
#define PYP_ARENA_H

#include "asdl.h" /* PyArena, _PyArena_Malloc */

PyArena *_PyArena_New(void);
void _PyArena_Free(PyArena *arena);
/* _PyArena_Malloc is declared in asdl.h */

#endif /* PYP_ARENA_H */
