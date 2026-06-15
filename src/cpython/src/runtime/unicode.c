/* Unicode: a UTF-8-backed string box and the PyUnicode_* surface the parser
 * uses. Code points are addressed over the UTF-8 bytes (the parser treats the
 * source as UTF-8 throughout). Libpython-free. Part of the yetty project. */
#include "Python.h"

PyObject *pyp_new(enum pyp_kind kind, PyTypeObject *type);
static Py_ssize_t put_utf8(char *out, Py_ssize_t oi, unsigned cp);

/* Normalize a codec name: lowercase, keep alnum only. */
static void normalize_encoding(const char *enc, char *out, size_t outsz)
{
    size_t oi = 0;
    if (enc) {
        for (; *enc && oi + 1 < outsz; enc++) {
            unsigned char c = (unsigned char)*enc;
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                out[oi++] = (char)c;
            }
        }
    }
    out[oi] = '\0';
}

static PyObject *str_from(const char *bytes, Py_ssize_t size)
{
    if (size < 0) {
        size = bytes ? (Py_ssize_t)strlen(bytes) : 0;
    }
    PyObject *o = pyp_new(PYP_STR, &PyUnicode_Type);
    if (o == NULL) {
        return NULL;
    }
    o->data = malloc((size_t)size + 1);
    if (o->data == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    if (size && bytes) {
        memcpy(o->data, bytes, (size_t)size);
    }
    o->data[size] = '\0';
    o->len = size;
    return o;
}

PyObject *PyUnicode_FromString(const char *str)
{
    return str_from(str, -1);
}
PyObject *PyUnicode_FromStringAndSize(const char *str, Py_ssize_t size)
{
    return str_from(str, size);
}

const char *PyUnicode_AsUTF8AndSize(PyObject *unicode, Py_ssize_t *size)
{
    if (!PyUnicode_Check(unicode)) {
        PyErr_SetString(PyExc_TypeError, "expected str");
        return NULL;
    }
    if (size) {
        *size = unicode->len;
    }
    return unicode->data;
}

const char *PyUnicode_AsUTF8(PyObject *unicode)
{
    return PyUnicode_AsUTF8AndSize(unicode, NULL);
}

PyObject *PyUnicode_AsUTF8String(PyObject *unicode)
{
    if (!PyUnicode_Check(unicode)) {
        return NULL;
    }
    return PyBytes_FromStringAndSize(unicode->data, unicode->len);
}

/* start/end are code-point indices; with a UTF-8 byte view they map to bytes. */
PyObject *PyUnicode_Substring(PyObject *self, Py_ssize_t start, Py_ssize_t end)
{
    if (!PyUnicode_Check(self)) {
        return NULL;
    }
    if (start < 0) {
        start = 0;
    }
    if (end > self->len) {
        end = self->len;
    }
    if (end < start) {
        end = start;
    }
    return str_from(self->data + start, end - start);
}

PyObject *PyUnicode_InternFromString(const char *str)
{
    return str_from(str, -1);
}
void _PyUnicode_InternImmortal(void *interp, PyObject **p)
{
    (void)interp;
    (void)p;
}

int PyUnicode_CompareWithASCIIString(PyObject *unicode, const char *str)
{
    if (!PyUnicode_Check(unicode)) {
        return -1;
    }
    return strcmp(unicode->data, str);
}

int _PyUnicode_EqualToASCIIString(PyObject *unicode, const char *str)
{
    return PyUnicode_Check(unicode) && strcmp(unicode->data, str) == 0;
}

/* The parser always decodes source as UTF-8; treat the bytes as-is. */
PyObject *PyUnicode_DecodeUTF8Stateful(const char *s, Py_ssize_t size, const char *errors,
                                       Py_ssize_t *consumed)
{
    (void)errors;
    if (consumed) {
        *consumed = size;
    }
    return str_from(s, size);
}
PyObject *PyUnicode_DecodeUTF8(const char *s, Py_ssize_t size, const char *errors)
{
    return PyUnicode_DecodeUTF8Stateful(s, size, errors, NULL);
}
PyObject *PyUnicode_Decode(const char *s, Py_ssize_t size, const char *encoding, const char *errors)
{
    char norm[32];
    normalize_encoding(encoding, norm, sizeof norm);
    /* Latin-1 / ISO-8859-1: each byte is code point U+00..U+FF -> UTF-8. */
    if (!strcmp(norm, "latin1") || !strcmp(norm, "iso88591") || !strcmp(norm, "l1") ||
        !strcmp(norm, "latin") || !strcmp(norm, "cp819") || !strcmp(norm, "8859") ||
        !strcmp(norm, "iso8859") || !strcmp(norm, "iso885911")) {
        char *out = malloc((size_t)size * 2 + 1);
        if (out == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        Py_ssize_t oi = 0;
        for (Py_ssize_t i = 0; i < size; i++) {
            oi = put_utf8(out, oi, (unsigned char)s[i]);
        }
        PyObject *o = str_from(out, oi);
        free(out);
        return o;
    }
    /* utf-8 / ascii / unknown: treat as UTF-8 bytes (passthrough). */
    return PyUnicode_DecodeUTF8Stateful(s, size, errors, NULL);
}

/* Unicode-escape decoding (\n, \t, \xNN, \uXXXX, \N{...} are handled by the
 * higher-level string_parser path; here we pass through and report no invalid
 * escape, which is sufficient for the common decoded-string route). */
/* Append code point `cp` to `out` as UTF-8; return the new write index. */
static Py_ssize_t put_utf8(char *out, Py_ssize_t oi, unsigned cp)
{
    if (cp < 0x80) {
        out[oi++] = (char)cp;
    } else if (cp < 0x800) {
        out[oi++] = (char)(0xc0 | (cp >> 6));
        out[oi++] = (char)(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        out[oi++] = (char)(0xe0 | (cp >> 12));
        out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[oi++] = (char)(0x80 | (cp & 0x3f));
    } else {
        out[oi++] = (char)(0xf0 | (cp >> 18));
        out[oi++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        out[oi++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[oi++] = (char)(0x80 | (cp & 0x3f));
    }
    return oi;
}

PyObject *_PyUnicode_DecodeUnicodeEscapeInternal2(const char *string, Py_ssize_t length,
                                                  const char *errors, Py_ssize_t *consumed,
                                                  int *first_invalid_escape_char,
                                                  const char **first_invalid_escape_ptr)
{
    (void)errors;
    if (first_invalid_escape_char) {
        *first_invalid_escape_char = -1;
    }
    if (first_invalid_escape_ptr) {
        *first_invalid_escape_ptr = NULL;
    }
    char *out = malloc((size_t)length + 1);
    if (out == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    Py_ssize_t oi = 0;
    const char *p = string, *end = string + length;
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
        case '\n':
            p++;
            break;
        case '\\':
            out[oi++] = '\\';
            p++;
            break;
        case '\'':
            out[oi++] = '\'';
            p++;
            break;
        case '"':
            out[oi++] = '"';
            p++;
            break;
        case 'a':
            out[oi++] = '\a';
            p++;
            break;
        case 'b':
            out[oi++] = '\b';
            p++;
            break;
        case 'f':
            out[oi++] = '\f';
            p++;
            break;
        case 'n':
            out[oi++] = '\n';
            p++;
            break;
        case 'r':
            out[oi++] = '\r';
            p++;
            break;
        case 't':
            out[oi++] = '\t';
            p++;
            break;
        case 'v':
            out[oi++] = '\v';
            p++;
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7': {
            int v = 0, k = 0;
            while (k < 3 && p < end && *p >= '0' && *p <= '7') {
                v = v * 8 + (*p - '0');
                p++;
                k++;
            }
            oi = put_utf8(out, oi, (unsigned)v);
            break;
        }
        case 'x': {
            p++;
            int v = 0, k = 0;
            while (k < 2 && p < end && Py_ISXDIGIT(*p)) {
                int dgt = Py_ISDIGIT(*p) ? *p - '0' : (Py_TOLOWER(*p) - 'a' + 10);
                v = v * 16 + dgt;
                p++;
                k++;
            }
            oi = put_utf8(out, oi, (unsigned)v);
            break;
        }
        case 'u':
        case 'U':
        case 'N': {
            /* \uXXXX / \UXXXXXXXX encode a code point; \N{...} (named) is left
             * verbatim (would need the Unicode name database). */
            char which = *p++;
            if (which == 'N') {
                out[oi++] = '\\';
                out[oi++] = 'N';
                break;
            }
            int digits = (which == 'u') ? 4 : 8;
            unsigned cp = 0;
            int k = 0;
            while (k < digits && p < end && Py_ISXDIGIT(*p)) {
                int dgt = Py_ISDIGIT(*p) ? *p - '0' : (Py_TOLOWER(*p) - 'a' + 10);
                cp = cp * 16 + (unsigned)dgt;
                p++;
                k++;
            }
            oi = put_utf8(out, oi, cp);
            break;
        }
        default:
            if (first_invalid_escape_ptr && *first_invalid_escape_ptr == NULL) {
                *first_invalid_escape_ptr = p;
                if (first_invalid_escape_char) {
                    *first_invalid_escape_char = (unsigned char)*p;
                }
            }
            out[oi++] = '\\';
            out[oi++] = *p++;
            break;
        }
    }
    if (consumed) {
        *consumed = length;
    }
    PyObject *result = str_from(out, oi);
    free(out);
    return result;
}

int _PyUnicode_ScanIdentifier(PyObject *u)
{
    /* Return the number of leading identifier bytes. ASCII fast path; any
     * non-ASCII byte (UTF-8 lead) is accepted as an identifier continuation. */
    if (!PyUnicode_Check(u) || u->len == 0) {
        return 0;
    }
    Py_ssize_t i = 0;
    unsigned char *d = (unsigned char *)u->data;
    if (!(Py_ISALPHA(d[0]) || d[0] == '_' || d[0] >= 0x80)) {
        return 0;
    }
    for (i = 1; i < u->len; i++) {
        if (!(Py_ISALNUM(d[i]) || d[i] == '_' || d[i] >= 0x80)) {
            break;
        }
    }
    return (int)i;
}

int _PyUnicode_IsPrintable(Py_UCS4 ch)
{
    return ch >= 0x20 && ch != 0x7f;
}
int _PyUnicode_IsWhitespace(Py_UCS4 ch)
{
    return ch == ' ' || (ch >= 0x09 && ch <= 0x0d) || ch == 0x1c || ch == 0x1d || ch == 0x1e ||
           ch == 0x1f;
}

/* ----- UnicodeWriter: a growable UTF-8 byte buffer ----------------------- */
struct PyUnicodeWriter {
    char *buf;
    size_t len;
    size_t cap;
};

static int writer_reserve(PyUnicodeWriter *w, size_t extra)
{
    if (w->len + extra + 1 > w->cap) {
        size_t cap = w->cap ? w->cap * 2 : 64;
        while (cap < w->len + extra + 1) {
            cap *= 2;
        }
        char *nb = realloc(w->buf, cap);
        if (nb == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        w->buf = nb;
        w->cap = cap;
    }
    return 0;
}

PyUnicodeWriter *PyUnicodeWriter_Create(Py_ssize_t length)
{
    PyUnicodeWriter *w = calloc(1, sizeof(*w));
    if (w == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    if (length > 0) {
        writer_reserve(w, (size_t)length);
    }
    return w;
}

void PyUnicodeWriter_Discard(PyUnicodeWriter *writer)
{
    if (writer) {
        free(writer->buf);
        free(writer);
    }
}

PyObject *PyUnicodeWriter_Finish(PyUnicodeWriter *writer)
{
    if (writer == NULL) {
        return NULL;
    }
    PyObject *s = str_from(writer->buf ? writer->buf : "", (Py_ssize_t)writer->len);
    PyUnicodeWriter_Discard(writer);
    return s;
}

int PyUnicodeWriter_WriteUTF8(PyUnicodeWriter *writer, const char *str, Py_ssize_t size)
{
    if (size < 0) {
        size = (Py_ssize_t)strlen(str);
    }
    if (writer_reserve(writer, (size_t)size) < 0) {
        return -1;
    }
    memcpy(writer->buf + writer->len, str, (size_t)size);
    writer->len += (size_t)size;
    writer->buf[writer->len] = '\0';
    return 0;
}

int PyUnicodeWriter_WriteStr(PyUnicodeWriter *writer, PyObject *obj)
{
    if (!PyUnicode_Check(obj)) {
        PyObject *s = PyObject_Str(obj);
        if (s == NULL) {
            return -1;
        }
        int rc = PyUnicodeWriter_WriteUTF8(writer, s->data, s->len);
        return rc;
    }
    return PyUnicodeWriter_WriteUTF8(writer, obj->data, obj->len);
}

int PyUnicodeWriter_WriteChar(PyUnicodeWriter *writer, Py_UCS4 ch)
{
    /* encode the code point as UTF-8 */
    char tmp[4];
    int n;
    if (ch < 0x80) {
        tmp[0] = (char)ch;
        n = 1;
    } else if (ch < 0x800) {
        tmp[0] = (char)(0xc0 | (ch >> 6));
        tmp[1] = (char)(0x80 | (ch & 0x3f));
        n = 2;
    } else if (ch < 0x10000) {
        tmp[0] = (char)(0xe0 | (ch >> 12));
        tmp[1] = (char)(0x80 | ((ch >> 6) & 0x3f));
        tmp[2] = (char)(0x80 | (ch & 0x3f));
        n = 3;
    } else {
        tmp[0] = (char)(0xf0 | (ch >> 18));
        tmp[1] = (char)(0x80 | ((ch >> 12) & 0x3f));
        tmp[2] = (char)(0x80 | ((ch >> 6) & 0x3f));
        tmp[3] = (char)(0x80 | (ch & 0x3f));
        n = 4;
    }
    return PyUnicodeWriter_WriteUTF8(writer, tmp, n);
}

/* ----- PyUnicode_FromFormat(V): the subset CPython's API supports --------- */
PyObject *PyUnicode_FromFormatV(const char *format, va_list vargs)
{
    PyUnicodeWriter *w = PyUnicodeWriter_Create(0);
    if (w == NULL) {
        return NULL;
    }
    char numbuf[128];
    const char *p = format;
    while (*p) {
        if (*p != '%') {
            PyUnicodeWriter_WriteChar(w, (unsigned char)*p++);
            continue;
        }

        /* Capture the whole conversion spec into `spec` (e.g. "%.2x"). */
        char spec[32];
        size_t si = 0;
        spec[si++] = *p++; /* '%' */
        while (*p && si < sizeof spec - 2 &&
               (Py_ISDIGIT(*p) || *p == '.' || *p == '-' || *p == '+' || *p == ' ' || *p == '#' ||
                *p == '0')) {
            spec[si++] = *p++;
        }
        int length_mod = 0; /* 0, 'l', 'L'(ll), 'z' */
        while (*p == 'l' || *p == 'z' || *p == 't') {
            length_mod = (*p == 'l' && length_mod == 'l') ? 'L' : *p;
            p++;
        }
        char conv = *p ? *p++ : '\0';
        spec[si++] = conv;
        spec[si] = '\0';

        switch (conv) {
        case 'U':
        case 'S':
        case 'R':
        case 'A':
        case 'V': {
            PyObject *o = va_arg(vargs, PyObject *);
            if (conv == 'V') {
                const char *fallback = va_arg(vargs, const char *);
                if ((!o || !PyUnicode_Check(o)) && fallback) {
                    PyUnicodeWriter_WriteUTF8(w, fallback, -1);
                    break;
                }
            }
            if (o && PyUnicode_Check(o)) {
                PyUnicodeWriter_WriteUTF8(w, o->data, o->len);
            } else if (o) {
                PyObject *s = PyObject_Str(o);
                if (s) {
                    PyUnicodeWriter_WriteUTF8(w, s->data, s->len);
                }
            }
            break;
        }
        case 's': {
            const char *s = va_arg(vargs, const char *);
            /* honour width/precision via the captured spec */
            snprintf(numbuf, sizeof numbuf, spec, s ? s : "(null)");
            PyUnicodeWriter_WriteUTF8(w, numbuf, -1);
            break;
        }
        case 'c': {
            int v = va_arg(vargs, int);
            PyUnicodeWriter_WriteChar(w, (Py_UCS4)v);
            break;
        }
        case 'p': {
            void *v = va_arg(vargs, void *);
            snprintf(numbuf, sizeof numbuf, "%p", v);
            PyUnicodeWriter_WriteUTF8(w, numbuf, -1);
            break;
        }
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            /* rebuild a printf spec with the right length modifier */
            char real[40];
            size_t ri = 0;
            for (size_t k = 0; k + 1 < si && spec[k] != conv; k++) {
                real[ri++] = spec[k];
            }
            if (length_mod == 'z') {
                real[ri++] = 'z';
            } else if (length_mod == 'l') {
                real[ri++] = 'l';
            } else if (length_mod == 'L') {
                real[ri++] = 'l';
                real[ri++] = 'l';
            }
            real[ri++] = conv;
            real[ri] = '\0';
            if (length_mod == 'z') {
                snprintf(numbuf, sizeof numbuf, real, va_arg(vargs, size_t));
            } else if (length_mod == 'l') {
                snprintf(numbuf, sizeof numbuf, real, va_arg(vargs, long));
            } else if (length_mod == 'L') {
                snprintf(numbuf, sizeof numbuf, real, va_arg(vargs, long long));
            } else {
                snprintf(numbuf, sizeof numbuf, real, va_arg(vargs, int));
            }
            PyUnicodeWriter_WriteUTF8(w, numbuf, -1);
            break;
        }
        case '%':
            PyUnicodeWriter_WriteChar(w, '%');
            break;
        case '\0':
            break;
        default:
            PyUnicodeWriter_WriteUTF8(w, spec, -1);
            break;
        }
    }
    return PyUnicodeWriter_Finish(w);
}

PyObject *PyUnicode_FromFormat(const char *format, ...)
{
    va_list va;
    va_start(va, format);
    PyObject *r = PyUnicode_FromFormatV(format, va);
    va_end(va);
    return r;
}
