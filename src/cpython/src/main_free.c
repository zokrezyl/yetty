/* Libpython-free driver: parse a Python file with the ported PEG parser and
 * dump the AST. Part of the yetty project (see ../LICENSE.md). */
#include "Python.h"
#include "pycore_parser.h" /* _PyParser_ASTFromString */
#include "pycore_ast.h"    /* mod_ty */
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>

void pyp_dump_mod(mod_ty mod);       /* ast_dump.gen.c */
const char *pyp_error_message(void); /* errors.c */

static char *read_source(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "py-parse: cannot open '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <file.py>\n", argv[0]);
        return 2;
    }
    char *src = read_source(argv[1]);
    if (!src) {
        return 1;
    }

    PyArena *arena = _PyArena_New();
    PyObject *filename = PyUnicode_FromString(argv[1]);
    PyCompilerFlags flags = _PyCompilerFlags_INIT;

    mod_ty mod = _PyParser_ASTFromString(src, filename, Py_file_input, &flags, arena);
    if (mod == NULL) {
        const char *msg = pyp_error_message();
        fprintf(stderr, "SyntaxError: %s\n", msg ? msg : "(parse failed)");
        _PyArena_Free(arena);
        free(src);
        return 1;
    }

    pyp_dump_mod(mod);

    _PyArena_Free(arena);
    free(src);
    return 0;
}
