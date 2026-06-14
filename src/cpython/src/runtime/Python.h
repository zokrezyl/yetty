/* Libpython-free replacement for CPython's <Python.h>, scoped to exactly what
 * the vendored PEG parser + tokenizer reference. Objects are plain-C tagged
 * boxes (struct _object) — no reference CPython runtime, no libpython.
 *
 * Part of the yetty project (see ../../LICENSE.md). Declarations mirror the
 * CPython C-API names the parser calls (PSF-licensed API surface), but every
 * definition here is original and libpython-free.
 */
#ifndef PYP_PYTHON_H
#define PYP_PYTHON_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#define PY_MAJOR_VERSION 3
#define PY_MINOR_VERSION 14
#define CO_FUTURE_BARRY_AS_BDFL 0x40000
#define Py_SAFE_DOWNCAST(VALUE, WIDE, NARROW) ((NARROW)(VALUE))
#define PyObject_TypeCheck(ob, type) (Py_TYPE(ob) == (type))

/* ----- ports / attribute macros ------------------------------------------ */
typedef ptrdiff_t Py_ssize_t;
typedef Py_ssize_t Py_hash_t;
#define PY_SSIZE_T_MAX ((Py_ssize_t)(SIZE_MAX >> 1))
#define PY_SSIZE_T_MIN (-PY_SSIZE_T_MAX - 1)

#define PyAPI_FUNC(RTYPE) RTYPE
#define PyAPI_DATA(RTYPE) extern RTYPE
#define Py_LOCAL_INLINE(type) static inline type
#define Py_LOCAL(type) static type
#define Py_UNUSED(name) unused_##name
#define Py_UNREACHABLE() abort()
#define Py_MIN(a, b) ((a) < (b) ? (a) : (b))
#define Py_MAX(a, b) ((a) > (b) ? (a) : (b))
#define Py_ABS(x) ((x) < 0 ? -(x) : (x))
#define Py_ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))
#define Py_STRINGIFY(x) _Py_STRINGIFY(x)
#define _Py_STRINGIFY(x) #x
#define Py_CHARMASK(c) ((unsigned char)((c) & 0xff))
#ifndef Py_DEBUG
#define _PyObject_ASSERT(obj, expr) ((void)0)
#endif
#define _Py_CAST(type, expr) ((type)(expr))
#define _Py_STATIC_CAST(type, expr) ((type)(expr))

/* ASCII ctype (parser only ever classifies ASCII bytes here). */
#define Py_ISDIGIT(c)  ((c) >= '0' && (c) <= '9')
#define Py_ISALPHA(c)  (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define Py_ISALNUM(c)  (Py_ISALPHA(c) || Py_ISDIGIT(c))
#define Py_ISXDIGIT(c) (Py_ISDIGIT(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#define Py_ISSPACE(c)  ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define Py_ISLOWER(c)  ((c) >= 'a' && (c) <= 'z')
#define Py_ISUPPER(c)  ((c) >= 'A' && (c) <= 'Z')
#define Py_TOLOWER(c)  (Py_ISUPPER(c) ? (c) - 'A' + 'a' : (c))
#define Py_TOUPPER(c)  (Py_ISLOWER(c) ? (c) - 'a' + 'A' : (c))

/* ----- object model ------------------------------------------------------ */
enum pyp_kind {
    PYP_NONE, PYP_BOOL, PYP_ELLIPSIS,
    PYP_LONG, PYP_FLOAT, PYP_COMPLEX,
    PYP_STR, PYP_BYTES,
    PYP_TUPLE, PYP_LIST,
    PYP_TYPE, PYP_EXC, PYP_OTHER
};

typedef struct _typeobject PyTypeObject;

typedef struct _object {
    Py_ssize_t ob_refcnt;        /* tracked but never freed (one-shot CLI) */
    PyTypeObject *ob_type;
    enum pyp_kind kind;
    char *data;                  /* UTF-8 text (str), raw bytes, or numeric spelling */
    Py_ssize_t len;              /* byte length of data */
    long ival;                   /* bool / small int */
    double dval;                 /* float / complex real */
    double imag;                 /* complex imag */
    struct _object **items;      /* tuple / list elements */
    Py_ssize_t nitems;
} PyObject;

struct _typeobject {
    const char *tp_name;
    enum pyp_kind kind;
};

PyAPI_DATA(PyTypeObject) PyUnicode_Type;
PyAPI_DATA(PyTypeObject) PyBytes_Type;
PyAPI_DATA(PyTypeObject) PyLong_Type;
PyAPI_DATA(PyTypeObject) PyComplex_Type;
PyAPI_DATA(PyTypeObject) PyType_Type;

/* singletons */
PyAPI_DATA(PyObject) _Py_NoneStruct;
PyAPI_DATA(PyObject) _Py_TrueStruct;
PyAPI_DATA(PyObject) _Py_FalseStruct;
PyAPI_DATA(PyObject) _Py_EllipsisObject;
#define Py_None       (&_Py_NoneStruct)
#define Py_True       (&_Py_TrueStruct)
#define Py_False      (&_Py_FalseStruct)
#define Py_Ellipsis   (&_Py_EllipsisObject)
#define Py_RETURN_NONE return Py_NewRef(Py_None)

typedef enum { Py_CONSTANT_NONE, Py_CONSTANT_FALSE, Py_CONSTANT_TRUE,
               Py_CONSTANT_ELLIPSIS, Py_CONSTANT_ZERO, Py_CONSTANT_ONE,
               Py_CONSTANT_EMPTY_STR, Py_CONSTANT_EMPTY_BYTES,
               Py_CONSTANT_EMPTY_TUPLE } _py_constant_id;
PyObject *Py_GetConstant(unsigned int constant_id);

/* refcounting — values are arena/leak owned; keep ops cheap and safe. */
static inline PyObject *Py_NewRef(PyObject *o) { if (o) o->ob_refcnt++; return o; }
static inline PyObject *Py_XNewRef(PyObject *o) { return o ? Py_NewRef(o) : o; }
void _Py_Dealloc(PyObject *o);
#define Py_INCREF(o)   ((void)((PyObject *)(o))->ob_refcnt++)
#define Py_DECREF(o)   ((void)0)            /* never free: one-shot lifetime */
#define Py_XDECREF(o)  ((void)0)
#define Py_CLEAR(o)    do { (o) = NULL; } while (0)
#define Py_SETREF(dst, src) do { (dst) = (src); } while (0)
#define Py_XSETREF(dst, src) do { (dst) = (src); } while (0)
#define Py_TYPE(o)     (((PyObject *)(o))->ob_type)
#define Py_REFCNT(o)   (((PyObject *)(o))->ob_refcnt)
#define Py_SIZE(o)     (((PyObject *)(o))->nitems)
#define Py_IS_TYPE(o, t) (Py_TYPE(o) == (t))
#define Py_Is(a, b)    ((a) == (b))

/* type checks via the kind tag */
#define PyUnicode_Check(o)  ((o) && ((PyObject *)(o))->kind == PYP_STR)
#define PyUnicode_CheckExact(o) PyUnicode_Check(o)
#define PyBytes_Check(o)    ((o) && ((PyObject *)(o))->kind == PYP_BYTES)
#define PyLong_Check(o)     ((o) && ((PyObject *)(o))->kind == PYP_LONG)
#define PyLong_CheckExact(o) PyLong_Check(o)
#define PyFloat_Check(o)    ((o) && ((PyObject *)(o))->kind == PYP_FLOAT)
#define PyComplex_Check(o)  ((o) && ((PyObject *)(o))->kind == PYP_COMPLEX)
#define PyComplex_CheckExact(o) PyComplex_Check(o)
#define PyFloat_CheckExact(o) PyFloat_Check(o)
#define PyBytes_CheckExact(o) PyBytes_Check(o)
#define PyTuple_Check(o)    ((o) && ((PyObject *)(o))->kind == PYP_TUPLE)
#define PyList_Check(o)     ((o) && ((PyObject *)(o))->kind == PYP_LIST)

/* ----- memory ------------------------------------------------------------ */
void *PyMem_Malloc(size_t size);
void *PyMem_Calloc(size_t nelem, size_t elsize);
void *PyMem_Realloc(void *ptr, size_t size);
void PyMem_Free(void *ptr);
#define PyMem_New(type, n)  ((type *)PyMem_Malloc((n) * sizeof(type)))
#define PyMem_RawMalloc PyMem_Malloc
#define PyMem_RawCalloc PyMem_Calloc
#define PyMem_RawRealloc PyMem_Realloc
#define PyMem_RawFree PyMem_Free

/* ----- unicode ----------------------------------------------------------- */
typedef uint32_t Py_UCS4;
typedef struct PyUnicodeWriter PyUnicodeWriter;

PyObject *PyUnicode_FromString(const char *str);
PyObject *PyUnicode_FromStringAndSize(const char *str, Py_ssize_t size);
PyObject *PyUnicode_FromFormat(const char *format, ...);
PyObject *PyUnicode_FromFormatV(const char *format, va_list vargs);
const char *PyUnicode_AsUTF8(PyObject *unicode);
const char *PyUnicode_AsUTF8AndSize(PyObject *unicode, Py_ssize_t *size);
PyObject *PyUnicode_AsUTF8String(PyObject *unicode);
PyObject *PyUnicode_Substring(PyObject *self, Py_ssize_t start, Py_ssize_t end);
PyObject *PyUnicode_InternFromString(const char *str);
void _PyUnicode_InternImmortal(void *interp, PyObject **p);
int PyUnicode_CompareWithASCIIString(PyObject *unicode, const char *str);
int _PyUnicode_EqualToASCIIString(PyObject *unicode, const char *str);
PyObject *PyUnicode_Decode(const char *s, Py_ssize_t size, const char *encoding, const char *errors);
PyObject *PyUnicode_DecodeUTF8(const char *s, Py_ssize_t size, const char *errors);
PyObject *PyUnicode_DecodeUTF8Stateful(const char *s, Py_ssize_t size, const char *errors, Py_ssize_t *consumed);
PyObject *_PyUnicode_DecodeUnicodeEscapeInternal2(const char *string, Py_ssize_t length, const char *errors, Py_ssize_t *consumed, int *first_invalid_escape_char, const char **first_invalid_escape_ptr);
int _PyUnicode_ScanIdentifier(PyObject *u);
int _PyUnicode_IsPrintable(Py_UCS4 ch);
int _PyUnicode_IsWhitespace(Py_UCS4 ch);
#define Py_UNICODE_ISPRINTABLE(ch) _PyUnicode_IsPrintable(ch)

/* code-point access over the UTF-8 backing store (byte view) */
#define PyUnicode_GET_LENGTH(u)  (((PyObject *)(u))->len)
#define PyUnicode_KIND(u)        (1)            /* expose as 1-byte/UTF-8 */
#define PyUnicode_DATA(u)        ((void *)((PyObject *)(u))->data)
#define PyUnicode_1BYTE_DATA(u)  ((unsigned char *)((PyObject *)(u))->data)
#define PyUnicode_IS_ASCII(u)    (1)
#define PyUnicode_READ(kind, data, index)  ((Py_UCS4)((unsigned char *)(data))[(index)])
#define PyUnicode_READ_CHAR(u, index)      ((Py_UCS4)(unsigned char)((PyObject *)(u))->data[(index)])

PyUnicodeWriter *PyUnicodeWriter_Create(Py_ssize_t length);
void PyUnicodeWriter_Discard(PyUnicodeWriter *writer);
PyObject *PyUnicodeWriter_Finish(PyUnicodeWriter *writer);
int PyUnicodeWriter_WriteStr(PyUnicodeWriter *writer, PyObject *obj);
int PyUnicodeWriter_WriteChar(PyUnicodeWriter *writer, Py_UCS4 ch);
int PyUnicodeWriter_WriteUTF8(PyUnicodeWriter *writer, const char *str, Py_ssize_t size);

/* ----- bytes ------------------------------------------------------------- */
PyObject *PyBytes_FromStringAndSize(const char *v, Py_ssize_t len);
char *PyBytes_AsString(PyObject *o);
int PyBytes_AsStringAndSize(PyObject *obj, char **buffer, Py_ssize_t *length);
Py_ssize_t PyBytes_Size(PyObject *o);
void PyBytes_Concat(PyObject **pv, PyObject *w);
PyObject *_PyBytes_DecodeEscape2(const char *s, Py_ssize_t len, const char *errors, int *first_invalid_escape_char, const char **first_invalid_escape);
#define PyBytes_GET_SIZE(o) (((PyObject *)(o))->len)
#define PyBytes_AS_STRING(o) (((PyObject *)(o))->data)

/* ----- numbers ----------------------------------------------------------- */
typedef struct { double real; double imag; } Py_complex;
PyObject *PyLong_FromLong(long v);
PyObject *PyLong_FromString(const char *str, char **pend, int base);
PyObject *PyFloat_FromDouble(double v);
PyObject *PyComplex_FromCComplex(Py_complex v);

/* ----- tuples ------------------------------------------------------------ */
PyObject *PyTuple_Pack(Py_ssize_t n, ...);
PyObject *Py_BuildValue(const char *format, ...);

/* ----- generic object ops ------------------------------------------------ */
PyObject *PyObject_Str(PyObject *o);
PyObject *PyObject_GetAttr(PyObject *o, PyObject *name);
int PyObject_SetAttr(PyObject *o, PyObject *name, PyObject *value);
PyObject *PyObject_CallNoArgs(PyObject *func);
#define _PyObject_CallNoArgs(func) PyObject_CallNoArgs(func)
PyObject *PyObject_CallFunction(PyObject *callable, const char *format, ...);
PyObject *PyObject_Vectorcall(PyObject *callable, PyObject *const *args, size_t nargsf, PyObject *kwnames);
PyObject *_PyObject_MakeTpCall(void *ts, PyObject *callable, PyObject *const *args, Py_ssize_t nargs, PyObject *keywords);
PyObject *_Py_CheckFunctionResult(void *ts, PyObject *callable, PyObject *result, const char *where);
int PyType_IsSubtype(PyTypeObject *a, PyTypeObject *b);
PyObject *_PyType_Name(PyTypeObject *type);
PyObject *PyImport_ImportModuleAttrString(const char *modname, const char *attrname);

/* ----- exceptions -------------------------------------------------------- */
PyAPI_DATA(PyObject *) PyExc_SyntaxError;
PyAPI_DATA(PyObject *) PyExc_IndentationError;
PyAPI_DATA(PyObject *) PyExc_TabError;
PyAPI_DATA(PyObject *) PyExc_ValueError;
PyAPI_DATA(PyObject *) PyExc_TypeError;
PyAPI_DATA(PyObject *) PyExc_OverflowError;
PyAPI_DATA(PyObject *) PyExc_MemoryError;
PyAPI_DATA(PyObject *) PyExc_SystemError;
PyAPI_DATA(PyObject *) PyExc_OSError;
PyAPI_DATA(PyObject *) PyExc_LookupError;
PyAPI_DATA(PyObject *) PyExc_KeyboardInterrupt;
PyAPI_DATA(PyObject *) PyExc_UnicodeError;
PyAPI_DATA(PyObject *) PyExc_UnicodeDecodeError;
PyAPI_DATA(PyObject *) PyExc_SyntaxWarning;
PyAPI_DATA(PyObject *) PyExc_DeprecationWarning;
PyAPI_DATA(PyObject *) _PyExc_IncompleteInputError;
#define PyExc_IncompleteInputError _PyExc_IncompleteInputError

void PyErr_SetString(PyObject *exc, const char *msg);
void PyErr_SetObject(PyObject *exc, PyObject *value);
void PyErr_SetNone(PyObject *exc);
PyObject *PyErr_Format(PyObject *exc, const char *format, ...);
PyObject *PyErr_Occurred(void);
void PyErr_Clear(void);
int PyErr_ExceptionMatches(PyObject *exc);
int PyErr_GivenExceptionMatches(PyObject *given, PyObject *exc);
PyObject *PyErr_NoMemory(void);
void PyErr_Fetch(PyObject **ptype, PyObject **pvalue, PyObject **ptraceback);
void PyErr_Restore(PyObject *type, PyObject *value, PyObject *traceback);
PyObject *PyErr_GetRaisedException(void);
void PyErr_SetRaisedException(PyObject *exc);
PyObject *PyErr_SetFromErrnoWithFilename(PyObject *exc, const char *filename);
int PyErr_WarnExplicitObject(PyObject *category, PyObject *message, PyObject *filename, int lineno, PyObject *module, PyObject *registry);
void _PyErr_BadInternalCall(const char *filename, int lineno);
#define PyErr_BadInternalCall() _PyErr_BadInternalCall(__FILE__, __LINE__)
PyObject *_PyErr_ProgramDecodedTextObject(PyObject *filename, int lineno, const char *encoding);

/* ----- OS / runtime stubs ------------------------------------------------ */
double PyOS_string_to_double(const char *s, char **endptr, PyObject *overflow_exception);
long PyOS_strtol(const char *str, char **ptr, int base);
unsigned long PyOS_strtoul(const char *str, char **ptr, int base);
char *PyOS_Readline(FILE *sys_stdin, FILE *sys_stdout, const char *prompt);
int PySys_Audit(const char *event, const char *format, ...);
void PySys_WriteStderr(const char *format, ...);
void _Py_FatalErrorFunc(const char *func, const char *msg);
#define Py_FatalError(msg) _Py_FatalErrorFunc(__func__, msg)
int _Py_ReachedRecursionLimitWithMargin(void *tstate, int margin_count);

typedef struct { PyObject *current_exception; } PyThreadState;
PyThreadState *PyThreadState_Get(void);
PyThreadState *pyp_tstate(void);   /* the single global thread state */
int _Py_dup(int fd);   /* fd dup wrapper used by the file tokenizer */

/* ----- compiler flags / input modes ------------------------------------- */
typedef struct {
    int cf_flags;
    int cf_feature_version;
} PyCompilerFlags;
#define _PyCompilerFlags_INIT (PyCompilerFlags){0, 0}
#define PyCF_ONLY_AST 0x0400
#define PyCF_TYPE_COMMENTS 0x1000
#define PyCF_IGNORE_COOKIE 0x0200
#define PyCF_DONT_IMPLY_DEDENT 0x0
#define PyCF_ALLOW_INCOMPLETE_INPUT 0x4000

#define Py_single_input 256
#define Py_file_input 257
#define Py_eval_input 258
#define Py_func_type_input 345

#endif /* PYP_PYTHON_H */
