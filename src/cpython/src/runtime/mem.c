/* Memory + a couple of file/readline helpers the tokenizer uses.
 * Libpython-free. Part of the yetty project (see ../../LICENSE.md). */
#include "Python.h"

void *PyMem_Malloc(size_t size) { return malloc(size ? size : 1); }
void *PyMem_Calloc(size_t nelem, size_t elsize) { return calloc(nelem ? nelem : 1, elsize ? elsize : 1); }
void *PyMem_Realloc(void *ptr, size_t size) { return realloc(ptr, size ? size : 1); }
void PyMem_Free(void *ptr) { free(ptr); }

char *PyOS_Readline(FILE *in, FILE *out, const char *prompt)
{
    (void)in; (void)out; (void)prompt;
    return NULL;   /* interactive input unsupported in this PoC */
}

/* Read a line, translating CRLF/CR to LF, like the tokenizer expects. */
char *
_Py_UniversalNewlineFgetsWithSize(char *buf, int n, FILE *stream, PyObject *fobj, size_t *size)
{
    (void)fobj;
    if (n <= 0) {
        return NULL;
    }
    int count = 0;
    int c;
    while (count < n - 1 && (c = getc(stream)) != EOF) {
        if (c == '\r') {
            int next = getc(stream);
            if (next != '\n' && next != EOF) {
                ungetc(next, stream);
            }
            buf[count++] = '\n';
            break;
        }
        buf[count++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    buf[count] = '\0';
    if (size) {
        *size = (size_t)count;
    }
    return count > 0 ? buf : NULL;
}
