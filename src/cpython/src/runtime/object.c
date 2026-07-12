/* Core object model: the plain-C PyObject box, singletons, type objects, and
 * the generic object/runtime stubs the parser references. Libpython-free.
 * Part of the yetty project (see ../../LICENSE.md). */
#include "Python.h"

/* ----- allocation (malloc + leak: parser runs once then exits) ----------- */
PyObject *pyp_new(enum pyp_kind kind, PyTypeObject *type)
{
    PyObject *o = calloc(1, sizeof(*o));
    if (o == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    o->ob_refcnt = 1;
    o->kind = kind;
    o->ob_type = type;
    return o;
}

/* ----- type objects ------------------------------------------------------ */
PyTypeObject PyUnicode_Type = {"str", PYP_STR};
PyTypeObject PyBytes_Type = {"bytes", PYP_BYTES};
PyTypeObject PyLong_Type = {"int", PYP_LONG};
PyTypeObject PyComplex_Type = {"complex", PYP_COMPLEX};
PyTypeObject PyType_Type = {"type", PYP_TYPE};

/* ----- singletons -------------------------------------------------------- */
PyObject _Py_NoneStruct = {1, NULL, PYP_NONE, 0, 0, 0, 0, 0, 0, 0};
PyObject _Py_TrueStruct = {1, &PyLong_Type, PYP_BOOL, 0, 0, 1, 0, 0, 0, 0};
PyObject _Py_FalseStruct = {1, &PyLong_Type, PYP_BOOL, 0, 0, 0, 0, 0, 0, 0};
PyObject _Py_EllipsisObject = {1, NULL, PYP_ELLIPSIS, 0, 0, 0, 0, 0, 0, 0};

PyObject *Py_GetConstant(unsigned int constant_id)
{
    switch (constant_id) {
    case Py_CONSTANT_NONE:
        return Py_None;
    case Py_CONSTANT_FALSE:
        return Py_False;
    case Py_CONSTANT_TRUE:
        return Py_True;
    case Py_CONSTANT_ELLIPSIS:
        return Py_Ellipsis;
    case Py_CONSTANT_ZERO:
        return PyLong_FromLong(0);
    case Py_CONSTANT_ONE:
        return PyLong_FromLong(1);
    case Py_CONSTANT_EMPTY_STR:
        return PyUnicode_FromString("");
    case Py_CONSTANT_EMPTY_BYTES:
        return PyBytes_FromStringAndSize("", 0);
    case Py_CONSTANT_EMPTY_TUPLE:
        return PyTuple_Pack(0);
    default:
        return Py_None;
    }
}

void _Py_Dealloc(PyObject *o)
{
    (void)o; /* one-shot lifetime: never actually freed */
}

int _Py_dup(int fd)
{
#ifdef _MSC_VER
    return _dup(fd); /* <io.h>, pulled in by Python.h */
#else
    extern int dup(int);
    return dup(fd);
#endif
}

/* ----- generic object ops (mostly used on the unicodedata path) ---------- */
PyObject *PyObject_Str(PyObject *o)
{
    if (o && o->kind == PYP_STR) {
        return Py_NewRef(o);
    }
    return PyUnicode_FromString(o ? "<obj>" : "None");
}

PyObject *PyObject_GetAttr(PyObject *o, PyObject *name)
{
    (void)o;
    (void)name;
    return NULL;
}
int PyObject_SetAttr(PyObject *o, PyObject *name, PyObject *v)
{
    (void)o;
    (void)name;
    (void)v;
    return 0;
}
PyObject *PyObject_CallNoArgs(PyObject *func)
{
    (void)func;
    return NULL;
}
PyObject *PyObject_CallFunction(PyObject *c, const char *fmt, ...)
{
    (void)c;
    (void)fmt;
    return NULL;
}
PyObject *PyObject_Vectorcall(PyObject *c, PyObject *const *a, size_t n, PyObject *k)
{
    (void)c;
    (void)a;
    (void)n;
    (void)k;
    return NULL;
}
PyObject *_PyObject_MakeTpCall(void *ts, PyObject *c, PyObject *const *a, Py_ssize_t n, PyObject *k)
{
    (void)ts;
    (void)c;
    (void)a;
    (void)n;
    (void)k;
    return NULL;
}
PyObject *_Py_CheckFunctionResult(void *ts, PyObject *c, PyObject *result, const char *where)
{
    (void)ts;
    (void)c;
    (void)where;
    return result;
}
PyObject *PyImport_ImportModuleAttrString(const char *m, const char *a)
{
    (void)m;
    (void)a;
    return NULL;
}

int PyType_IsSubtype(PyTypeObject *a, PyTypeObject *b)
{
    return a == b;
}

PyObject *_PyType_Name(PyTypeObject *type)
{
    return PyUnicode_FromString(type ? type->tp_name : "object");
}

/* ----- tuples / Py_BuildValue (minimal) ---------------------------------- */
PyObject *PyTuple_Pack(Py_ssize_t n, ...)
{
    PyObject *t = pyp_new(PYP_TUPLE, NULL);
    if (t == NULL) {
        return NULL;
    }
    t->items = calloc(n > 0 ? (size_t)n : 1, sizeof(PyObject *));
    t->nitems = n;
    va_list va;
    va_start(va, n);
    for (Py_ssize_t i = 0; i < n; i++) {
        t->items[i] = va_arg(va, PyObject *);
    }
    va_end(va);
    return t;
}

/* Supports the handful of formats the parser passes for token metadata. */
PyObject *Py_BuildValue(const char *format, ...)
{
    /* The parser only needs an object handle back; return None to keep the
     * metadata slot non-fatal. Spans are recomputed from token offsets. */
    (void)format;
    return Py_NewRef(Py_None);
}

/* ----- runtime singletons / stubs ---------------------------------------- */
#include "pycore_runtime.h"
struct _pyruntime _PyRuntime;
PyObject _PyRuntime_id_dummy; /* target of &_Py_ID(name) (SetAttr is a no-op) */

PyThreadState *PyThreadState_Get(void)
{
    return pyp_tstate();
}

int _Py_ReachedRecursionLimitWithMargin(void *tstate, int margin_count)
{
    (void)tstate;
    (void)margin_count;
    return 0;
} /* never hit the limit */

void _Py_FatalErrorFunc(const char *func, const char *msg)
{
    fprintf(stderr, "Fatal error in %s: %s\n", func ? func : "?", msg);
    abort();
}
