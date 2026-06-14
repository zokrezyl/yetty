/* Bytes box + the PyBytes_* surface and bytes escape decoding. Libpython-free.
 * Part of the yetty project (see ../../LICENSE.md). */
#include "Python.h"

PyObject *pyp_new(enum pyp_kind kind, PyTypeObject *type);

PyObject *
PyBytes_FromStringAndSize(const char *v, Py_ssize_t len)
{
    PyObject *o = pyp_new(PYP_BYTES, &PyBytes_Type);
    if (o == NULL) {
        return NULL;
    }
    o->data = malloc((size_t)len + 1);
    if (o->data == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    if (len && v) {
        memcpy(o->data, v, (size_t)len);
    }
    o->data[len] = '\0';
    o->len = len;
    return o;
}

char *
PyBytes_AsString(PyObject *o)
{
    if (!PyBytes_Check(o)) {
        PyErr_SetString(PyExc_TypeError, "expected bytes");
        return NULL;
    }
    return o->data;
}

int
PyBytes_AsStringAndSize(PyObject *obj, char **buffer, Py_ssize_t *length)
{
    if (!PyBytes_Check(obj)) {
        PyErr_SetString(PyExc_TypeError, "expected bytes");
        return -1;
    }
    if (buffer) *buffer = obj->data;
    if (length) *length = obj->len;
    return 0;
}

Py_ssize_t
PyBytes_Size(PyObject *o)
{
    return PyBytes_Check(o) ? o->len : -1;
}

void
PyBytes_Concat(PyObject **pv, PyObject *w)
{
    if (pv == NULL || *pv == NULL) {
        return;
    }
    if (w == NULL || !PyBytes_Check(*pv) || !PyBytes_Check(w)) {
        *pv = NULL;
        return;
    }
    Py_ssize_t n = (*pv)->len + w->len;
    char *buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        PyErr_NoMemory();
        *pv = NULL;
        return;
    }
    memcpy(buf, (*pv)->data, (*pv)->len);
    memcpy(buf + (*pv)->len, w->data, w->len);
    buf[n] = '\0';
    PyObject *out = PyBytes_FromStringAndSize(buf, n);
    free(buf);
    *pv = out;
}

/* Decode the escapes inside a bytes literal body (b"...\\n..."). */
PyObject *
_PyBytes_DecodeEscape2(const char *s, Py_ssize_t len, const char *errors,
                       int *first_invalid_escape_char, const char **first_invalid_escape)
{
    (void)errors;
    if (first_invalid_escape) *first_invalid_escape = NULL;
    if (first_invalid_escape_char) *first_invalid_escape_char = -1;

    char *out = malloc((size_t)len + 1);
    if (out == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    Py_ssize_t oi = 0;
    const char *p = s;
    const char *end = s + len;
    while (p < end) {
        if (*p != '\\') {
            out[oi++] = *p++;
            continue;
        }
        p++;
        if (p == end) {
            out[oi++] = '\\';
            break;
        }
        switch (*p) {
        case '\n': p++; break;
        case '\\': out[oi++] = '\\'; p++; break;
        case '\'': out[oi++] = '\''; p++; break;
        case '"':  out[oi++] = '"';  p++; break;
        case 'a':  out[oi++] = '\a'; p++; break;
        case 'b':  out[oi++] = '\b'; p++; break;
        case 'f':  out[oi++] = '\f'; p++; break;
        case 'n':  out[oi++] = '\n'; p++; break;
        case 'r':  out[oi++] = '\r'; p++; break;
        case 't':  out[oi++] = '\t'; p++; break;
        case 'v':  out[oi++] = '\v'; p++; break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
            int v = 0, k = 0;
            while (k < 3 && p < end && *p >= '0' && *p <= '7') { v = v * 8 + (*p - '0'); p++; k++; }
            out[oi++] = (char)v;
            break;
        }
        case 'x': {
            p++;
            int v = 0, k = 0;
            while (k < 2 && p < end && Py_ISXDIGIT(*p)) {
                int d = Py_ISDIGIT(*p) ? *p - '0' : (Py_TOLOWER(*p) - 'a' + 10);
                v = v * 16 + d; p++; k++;
            }
            out[oi++] = (char)v;
            break;
        }
        default:
            /* unknown escape: keep the backslash and the char (CPython warns) */
            if (first_invalid_escape && *first_invalid_escape == NULL) {
                *first_invalid_escape = p;
                if (first_invalid_escape_char) *first_invalid_escape_char = (unsigned char)*p;
            }
            out[oi++] = '\\';
            out[oi++] = *p++;
            break;
        }
    }
    PyObject *result = PyBytes_FromStringAndSize(out, oi);
    free(out);
    return result;
}
