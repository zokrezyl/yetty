/* Error + exception model: a single global "current error" (type + message)
 * plus the PyExc_* sentinel objects. No Python exception machinery, no
 * libpython. The parser checks PyErr_Occurred()/ExceptionMatches and formats
 * messages; we capture those into pyp_error for the driver to print.
 * Part of the yetty project (see ../../LICENSE.md). */
#include "Python.h"
#include <errno.h>

PyObject *pyp_new(enum pyp_kind kind, PyTypeObject *type);

/* ----- exception sentinels ----------------------------------------------- */
static PyObject exc_objs[16];
#define DEF_EXC(name, idx) PyObject *name = &exc_objs[idx]
DEF_EXC(PyExc_SyntaxError, 0);
DEF_EXC(PyExc_IndentationError, 1);
DEF_EXC(PyExc_TabError, 2);
DEF_EXC(PyExc_ValueError, 3);
DEF_EXC(PyExc_TypeError, 4);
DEF_EXC(PyExc_OverflowError, 5);
DEF_EXC(PyExc_MemoryError, 6);
DEF_EXC(PyExc_SystemError, 7);
DEF_EXC(PyExc_OSError, 8);
DEF_EXC(PyExc_LookupError, 9);
DEF_EXC(PyExc_KeyboardInterrupt, 10);
DEF_EXC(PyExc_UnicodeError, 11);
DEF_EXC(PyExc_UnicodeDecodeError, 12);
DEF_EXC(PyExc_SyntaxWarning, 13);
DEF_EXC(PyExc_DeprecationWarning, 14);
DEF_EXC(_PyExc_IncompleteInputError, 15);

/* ----- current error state ----------------------------------------------- */
struct pyp_error {
    PyObject *type;          /* one of the PyExc_* sentinels, or NULL */
    char message[1024];
    int has_message;
};
static struct pyp_error pyp_error;

/* Single global thread state. current_exception is a small value object whose
 * ob_type is the active exception sentinel, so the parser's
 * Py_TYPE(tstate->current_exception) == PyExc_ValueError checks work. */
static PyThreadState g_tstate;
static PyObject g_exc_value;
PyThreadState *pyp_tstate(void) { return &g_tstate; }

static void
sync_tstate(PyObject *exc)
{
    if (exc) {
        g_exc_value.ob_type = (PyTypeObject *)exc;
        g_tstate.current_exception = &g_exc_value;
    } else {
        g_tstate.current_exception = NULL;
    }
}

const char *pyp_error_message(void) { return pyp_error.has_message ? pyp_error.message : NULL; }
PyObject *pyp_error_type(void) { return pyp_error.type; }

static void
set_error_v(PyObject *exc, const char *format, va_list va)
{
    pyp_error.type = exc;
    sync_tstate(exc);
    if (format) {
        vsnprintf(pyp_error.message, sizeof pyp_error.message, format, va);
        pyp_error.has_message = 1;
    }
}

void
PyErr_SetString(PyObject *exc, const char *msg)
{
    pyp_error.type = exc;
    sync_tstate(exc);
    if (msg) {
        snprintf(pyp_error.message, sizeof pyp_error.message, "%s", msg);
        pyp_error.has_message = 1;
    }
}

void
PyErr_SetObject(PyObject *exc, PyObject *value)
{
    pyp_error.type = exc;
    sync_tstate(exc);
    if (value == NULL) {
        return;
    }
    /* SyntaxError is raised as a (message, (filename, lineno, ...)) tuple; the
     * message is the first string element. Capture it for the caller. */
    PyObject *msg = NULL;
    if (PyUnicode_Check(value)) {
        msg = value;
    } else if (value->kind == PYP_TUPLE) {
        for (Py_ssize_t i = 0; i < value->nitems; i++) {
            if (value->items[i] && PyUnicode_Check(value->items[i])) {
                msg = value->items[i];
                break;
            }
        }
    }
    if (msg) {
        snprintf(pyp_error.message, sizeof pyp_error.message, "%s", msg->data);
        pyp_error.has_message = 1;
    }
}

void PyErr_SetNone(PyObject *exc) { pyp_error.type = exc; sync_tstate(exc); }

PyObject *
PyErr_Format(PyObject *exc, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    set_error_v(exc, format, va);
    va_end(va);
    return NULL;
}

PyObject *PyErr_Occurred(void) { return pyp_error.type; }

void
PyErr_Clear(void)
{
    pyp_error.type = NULL;
    pyp_error.has_message = 0;
    pyp_error.message[0] = '\0';
    sync_tstate(NULL);
}

int PyErr_ExceptionMatches(PyObject *exc) { return pyp_error.type == exc; }
int PyErr_GivenExceptionMatches(PyObject *given, PyObject *exc) { return given == exc; }

PyObject *
PyErr_NoMemory(void)
{
    pyp_error.type = PyExc_MemoryError;
    sync_tstate(PyExc_MemoryError);
    snprintf(pyp_error.message, sizeof pyp_error.message, "out of memory");
    pyp_error.has_message = 1;
    return NULL;
}

void
PyErr_Fetch(PyObject **ptype, PyObject **pvalue, PyObject **ptraceback)
{
    if (ptype) *ptype = pyp_error.type;
    if (pvalue) *pvalue = NULL;
    if (ptraceback) *ptraceback = NULL;
    PyErr_Clear();
}

void
PyErr_Restore(PyObject *type, PyObject *value, PyObject *traceback)
{
    (void)value; (void)traceback;
    pyp_error.type = type;
    sync_tstate(type);
}

/* Hand back the pending error as an object that carries BOTH the type and the
 * message, so a Get/Set round-trip (used by the parser's error finalizer)
 * doesn't lose the message. */
PyObject *
PyErr_GetRaisedException(void)
{
    if (pyp_error.type == NULL) {
        return NULL;
    }
    PyObject *exc = pyp_new(PYP_EXC, (PyTypeObject *)pyp_error.type);
    if (exc && pyp_error.has_message) {
        size_t n = strlen(pyp_error.message);
        exc->data = malloc(n + 1);
        if (exc->data) {
            memcpy(exc->data, pyp_error.message, n + 1);
            exc->len = (Py_ssize_t)n;
        }
    }
    PyErr_Clear();
    return exc;
}

void
PyErr_SetRaisedException(PyObject *exc)
{
    if (exc == NULL) {
        PyErr_Clear();
        return;
    }
    PyObject *type = (exc->kind == PYP_EXC) ? (PyObject *)exc->ob_type : exc;
    pyp_error.type = type;
    sync_tstate(type);
    if (exc->kind == PYP_EXC && exc->data) {
        snprintf(pyp_error.message, sizeof pyp_error.message, "%s", exc->data);
        pyp_error.has_message = 1;
    }
}

PyObject *
PyErr_SetFromErrnoWithFilename(PyObject *exc, const char *filename)
{
    PyErr_Format(exc, "%s: %s", filename ? filename : "?", strerror(errno));
    return NULL;
}

int
PyErr_WarnExplicitObject(PyObject *category, PyObject *message, PyObject *filename,
                         int lineno, PyObject *module, PyObject *registry)
{
    (void)category; (void)message; (void)filename; (void)lineno; (void)module; (void)registry;
    return 0;   /* warnings are ignored */
}

void
_PyErr_BadInternalCall(const char *filename, int lineno)
{
    PyErr_Format(PyExc_SystemError, "%s:%d: bad internal call", filename, lineno);
}

/* Returns the decoded source line for an error message. We don't keep the
 * source around here, so return NULL — the parser handles a missing line. */
PyObject *
_PyErr_ProgramDecodedTextObject(PyObject *filename, int lineno, const char *encoding)
{
    (void)filename; (void)lineno; (void)encoding;
    return NULL;
}

/* ----- sys / audit stubs ------------------------------------------------- */
int PySys_Audit(const char *event, const char *format, ...) { (void)event; (void)format; return 0; }

void
PySys_WriteStderr(const char *format, ...)
{
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
}
