#ifndef PYP_SHADOW_ARENA
#define PYP_SHADOW_ARENA
#include "asdl.h"          /* PyArena + _PyArena_Malloc */
#include "arena.h"
int _PyArena_AddPyObject(PyArena *arena, PyObject *obj);
#endif
