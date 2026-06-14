/* Repr helpers for the AST dumper: print identifiers/constants the way
 * ast.dump() renders them. Output goes to pyp_dump_fp (set by the dumper
 * entry point). Libpython-free. Part of the yetty project. */
#include "Python.h"
#include <stdio.h>

extern FILE *pyp_dump_fp;            /* defined in ast_dump.gen.c */
int pyp_is_printable(unsigned cp);   /* printable.gen.c */

/* Decode one UTF-8 code point starting at s[i]; advance *adv by its byte len. */
static unsigned
utf8_next(const char *s, Py_ssize_t n, Py_ssize_t i, int *adv)
{
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { *adv = 1; return c; }
    unsigned cp; int len;
    if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; len = 2; }
    else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; len = 3; }
    else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; len = 4; }
    else { *adv = 1; return c; }   /* invalid lead: treat as raw byte */
    if (i + len > n) { *adv = 1; return c; }
    for (int k = 1; k < len; k++) {
        cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
    }
    *adv = len;
    return cp;
}

/* Mirror CPython's str repr: choose the quote, escape specials, \xNN/\uNNNN/
 * \UNNNNNNNN for non-printable code points, and emit printable ones as UTF-8. */
static void
emit_repr(const char *s, Py_ssize_t n)
{
    int has_single = 0, has_double = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (s[i] == '\'') has_single = 1;
        else if (s[i] == '"') has_double = 1;
    }
    char quote = (has_single && !has_double) ? '"' : '\'';
    putc(quote, pyp_dump_fp);
    Py_ssize_t i = 0;
    while (i < n) {
        int adv = 1;
        unsigned cp = utf8_next(s, n, i, &adv);
        if (cp == (unsigned)quote || cp == '\\') { putc('\\', pyp_dump_fp); putc((int)cp, pyp_dump_fp); }
        else if (cp == '\t') fputs("\\t", pyp_dump_fp);
        else if (cp == '\n') fputs("\\n", pyp_dump_fp);
        else if (cp == '\r') fputs("\\r", pyp_dump_fp);
        else if (cp < 0x20 || cp == 0x7f) fprintf(pyp_dump_fp, "\\x%02x", cp);
        else if (cp < 0x7f) putc((int)cp, pyp_dump_fp);
        else if (pyp_is_printable(cp)) { for (int k = 0; k < adv; k++) putc((unsigned char)s[i + k], pyp_dump_fp); }
        else if (cp < 0x100) fprintf(pyp_dump_fp, "\\x%02x", cp);
        else if (cp < 0x10000) fprintf(pyp_dump_fp, "\\u%04x", cp);
        else fprintf(pyp_dump_fp, "\\U%08x", cp);
        i += adv;
    }
    putc(quote, pyp_dump_fp);
}

void
pyp_emit_identifier(PyObject *o)
{
    if (o == NULL) {
        fputs("None", pyp_dump_fp);
        return;
    }
    emit_repr(o->data, o->len);
}

/* CPython bytes repr: only printable ASCII stays literal; everything else
 * (controls AND >= 0x80) becomes \xNN. */
static void
emit_bytes_repr(const char *s, Py_ssize_t n)
{
    int has_single = 0, has_double = 0;
    for (Py_ssize_t i = 0; i < n; i++) {
        if (s[i] == '\'') has_single = 1;
        else if (s[i] == '"') has_double = 1;
    }
    char quote = (has_single && !has_double) ? '"' : '\'';
    putc(quote, pyp_dump_fp);
    for (Py_ssize_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == (unsigned char)quote || c == '\\') { putc('\\', pyp_dump_fp); putc(c, pyp_dump_fp); }
        else if (c == '\t') fputs("\\t", pyp_dump_fp);
        else if (c == '\n') fputs("\\n", pyp_dump_fp);
        else if (c == '\r') fputs("\\r", pyp_dump_fp);
        else if (c >= 0x20 && c < 0x7f) putc((int)c, pyp_dump_fp);
        else fprintf(pyp_dump_fp, "\\x%02x", c);
    }
    putc(quote, pyp_dump_fp);
}

void
pyp_emit_constant(PyObject *o)
{
    if (o == NULL) {
        fputs("None", pyp_dump_fp);
        return;
    }
    switch (o->kind) {
    case PYP_NONE:     fputs("None", pyp_dump_fp); break;
    case PYP_BOOL:     fputs(o->ival ? "True" : "False", pyp_dump_fp); break;
    case PYP_ELLIPSIS: fputs("Ellipsis", pyp_dump_fp); break;
    case PYP_LONG:     fputs(o->data ? o->data : "0", pyp_dump_fp); break;
    case PYP_FLOAT:    fputs(o->data ? o->data : "0.0", pyp_dump_fp); break;
    case PYP_COMPLEX:  fputs(o->data ? o->data : "0j", pyp_dump_fp); break;
    case PYP_STR:      emit_repr(o->data, o->len); break;
    case PYP_BYTES:    putc('b', pyp_dump_fp); emit_bytes_repr(o->data, o->len); break;
    default:           fputs("<const>", pyp_dump_fp);
    }
}
