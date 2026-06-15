/* Public API wrapper for the libpython-free CPython parser subset.
 * Bridges the clean yetty/cpython/cpython.h surface to the internal parser +
 * AST dumper. Part of the yetty project (see ../LICENSE.md). */
#include "yetty/cpython/cpython.h"

#include "Python.h"
#include "pycore_parser.h" /* _PyParser_ASTFromString */
#include "pycore_ast.h"    /* mod_ty */
#include "arena.h"

extern FILE *pyp_dump_fp;            /* ast_dump.gen.c */
void pyp_dump_mod(mod_ty mod);       /* ast_dump.gen.c */
const char *pyp_error_message(void); /* errors.c */

int yetty_cpython_parse_and_dump(const char *source, const char *filename, FILE *out)
{
    PyArena *arena = _PyArena_New();
    if (arena == NULL) {
        return -1;
    }
    PyObject *filename_obj = PyUnicode_FromString(filename ? filename : "<string>");
    PyCompilerFlags flags = _PyCompilerFlags_INIT;

    mod_ty mod = _PyParser_ASTFromString(source, filename_obj, Py_file_input, &flags, arena);
    int rc;
    if (mod == NULL) {
        rc = 1;
    } else {
        pyp_dump_fp = out ? out : stdout;
        pyp_dump_mod(mod);
        rc = 0;
    }
    _PyArena_Free(arena);
    return rc;
}

const char *yetty_cpython_last_error(void)
{
    return pyp_error_message();
}
