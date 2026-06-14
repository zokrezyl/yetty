#ifndef PYP_SHADOW_PARSER
#define PYP_SHADOW_PARSER
#include "Python.h"
#include "pycore_ast.h"
#define _PYPEGEN_NSTATISTICS 2000
struct _mod *_PyParser_ASTFromString(const char *str, PyObject *filename,
                                     int mode, PyCompilerFlags *flags, PyArena *arena);
#endif
