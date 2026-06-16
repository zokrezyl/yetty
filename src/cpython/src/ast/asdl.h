/* Libpython-free replacement for CPython's pycore_asdl.h.
 *
 * Same layout and macros as upstream, but the ASDL builtin leaf types resolve
 * to our own plain-C PyObject box (see ../runtime/Python.h) instead of the
 * CPython runtime's PyObject. Carries zero dependency on libpython.
 *
 * Part of the yetty project (see ../../LICENSE.md). Seq layout/macros modelled
 * on CPython's pycore_asdl.h (PSF License v2).
 */
#ifndef PYP_ASDL_H
#define PYP_ASDL_H

#include "Python.h" /* our runtime: PyObject, Py_ssize_t, PyErr_NoMemory */

/* Opaque parse arena (bump allocator). See arena.h. */
typedef struct pyp_arena PyArena;
void *_PyArena_Malloc(PyArena *arena, size_t size);
int _PyArena_AddPyObject(PyArena *arena, PyObject *obj);

/* AST node validation error sink (no exceptions, no libpython). */
void pyp_ast_error(const char *message);

/* ----- ASDL builtin leaf types: our own PyObject box --------------------- */
typedef PyObject *identifier; /* PYP_STR  */
typedef PyObject *string;     /* PYP_STR / PYP_BYTES */
typedef PyObject *object;     /* any */
typedef PyObject *constant;   /* literal value (PYP_LONG/STR/...) */

/* ----- sequence machinery (layout identical to CPython) ------------------ */
#define _Py_RVALUE(EXPR) (EXPR)

#define _ASDL_SEQ_HEAD                                                                             \
    Py_ssize_t size;                                                                               \
    void **elements;

typedef struct {
    _ASDL_SEQ_HEAD
} asdl_seq;

typedef struct {
    _ASDL_SEQ_HEAD
    void *typed_elements[1];
} asdl_generic_seq;

typedef struct {
    _ASDL_SEQ_HEAD
    void *typed_elements[1];
} asdl_identifier_seq;

typedef struct {
    _ASDL_SEQ_HEAD
    int typed_elements[1];
} asdl_int_seq;

asdl_generic_seq *_Py_asdl_generic_seq_new(Py_ssize_t size, PyArena *arena);
asdl_identifier_seq *_Py_asdl_identifier_seq_new(Py_ssize_t size, PyArena *arena);
asdl_int_seq *_Py_asdl_int_seq_new(Py_ssize_t size, PyArena *arena);

#define GENERATE_ASDL_SEQ_CONSTRUCTOR(NAME, TYPE)                                                  \
    asdl_##NAME##_seq *_Py_asdl_##NAME##_seq_new(Py_ssize_t size, PyArena *arena)                  \
    {                                                                                              \
        asdl_##NAME##_seq *seq = NULL;                                                             \
        size_t n;                                                                                  \
        if (size < 0 || (size && (((size_t)size - 1) > (SIZE_MAX / sizeof(void *))))) {            \
            PyErr_NoMemory();                                                                      \
            return NULL;                                                                           \
        }                                                                                          \
        n = (size ? (sizeof(TYPE *) * (size - 1)) : 0);                                            \
        if (n > SIZE_MAX - sizeof(asdl_##NAME##_seq)) {                                            \
            PyErr_NoMemory();                                                                      \
            return NULL;                                                                           \
        }                                                                                          \
        n += sizeof(asdl_##NAME##_seq);                                                            \
        seq = (asdl_##NAME##_seq *)_PyArena_Malloc(arena, n);                                      \
        if (!seq) {                                                                                \
            PyErr_NoMemory();                                                                      \
            return NULL;                                                                           \
        }                                                                                          \
        memset(seq, 0, n);                                                                         \
        seq->size = size;                                                                          \
        seq->elements = (void **)seq->typed_elements;                                              \
        return seq;                                                                                \
    }

#define asdl_seq_GET_UNTYPED(S, I) _Py_RVALUE((S)->elements[(I)])
#define asdl_seq_GET(S, I) _Py_RVALUE((S)->typed_elements[(I)])
#define asdl_seq_LEN(S) _Py_RVALUE(((S) == NULL ? 0 : (S)->size))
#define asdl_seq_SET(S, I, V) _Py_RVALUE((S)->typed_elements[(I)] = (V))
#define asdl_seq_SET_UNTYPED(S, I, V) _Py_RVALUE((S)->elements[(I)] = (V))

#endif /* PYP_ASDL_H */
