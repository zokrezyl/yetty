#!/bin/bash
# Create libpython-free shadow headers so the vendored parser's
# #include "pycore_*.h" / "Python.h" lines resolve to our runtime.
set -e
cd "$(dirname "$0")/.."
R=src/runtime

# --- trivial: just pull in the umbrella ---
for h in pyport.h object.h pycore_call.h pycore_signal.h pycore_unicodeobject.h \
         pycore_pyerrors.h pycore_warnings.h pycore_long.h pycore_unicodeobject.h \
         pycore_critical_section.h; do
  printf '#ifndef PYP_SHADOW_%s\n#define PYP_SHADOW_%s\n#include "Python.h"\n#endif\n' \
         "${h//./_}" "${h//./_}" > "$R/$h"
done

# --- AST / arena: redirect to our generated AST ---
cat > "$R/pycore_asdl.h" <<'EOF'
#ifndef PYP_SHADOW_ASDL
#define PYP_SHADOW_ASDL
#include "Python.h"
#include "asdl.h"          /* our libpython-free asdl (src/ast) */
#endif
EOF
cat > "$R/pycore_ast.h" <<'EOF'
#ifndef PYP_SHADOW_AST
#define PYP_SHADOW_AST
#include "Python.h"
#include "ast.gen.h"       /* our generated plain-C AST (src/ast) */
int _PyAST_Validate(mod_ty mod);
#endif
EOF
cat > "$R/pycore_pyarena.h" <<'EOF'
#ifndef PYP_SHADOW_ARENA
#define PYP_SHADOW_ARENA
#include "asdl.h"          /* PyArena + _PyArena_Malloc */
#include "arena.h"
int _PyArena_AddPyObject(PyArena *arena, PyObject *obj);
#endif
EOF

# --- runtime / interpreter stubs ---
cat > "$R/pycore_runtime.h" <<'EOF'
#ifndef PYP_SHADOW_RUNTIME
#define PYP_SHADOW_RUNTIME
#include "Python.h"
struct _pyruntime { int _dummy; };
extern struct _pyruntime _PyRuntime;
#define _Py_ID(name) (PyUnicode_InternFromString(#name))
#endif
EOF
cat > "$R/pycore_pystate.h" <<'EOF'
#ifndef PYP_SHADOW_PYSTATE
#define PYP_SHADOW_PYSTATE
#include "Python.h"
typedef struct { int _dummy; } PyInterpreterState;
#define _PyThreadState_GET() ((PyThreadState *)NULL)
#define _PyInterpreterState_GET() ((PyInterpreterState *)NULL)
#define PyThreadState_GET() ((PyThreadState *)NULL)
#endif
EOF
cat > "$R/pycore_interp.h" <<'EOF'
#ifndef PYP_SHADOW_INTERP
#define PYP_SHADOW_INTERP
#include "pycore_pystate.h"
typedef struct { int parser_debug; } _pyconfig_stub;
#define _PyInterpreterState_GetConfig(interp) ((void *)NULL)
#endif
EOF
cat > "$R/pycore_parser.h" <<'EOF'
#ifndef PYP_SHADOW_PARSER
#define PYP_SHADOW_PARSER
#include "Python.h"
#include "pycore_ast.h"
#define _PYPEGEN_NSTATISTICS 2000
struct _mod *_PyParser_ASTFromString(const char *str, PyObject *filename,
                                     int mode, PyCompilerFlags *flags, PyArena *arena);
#endif
EOF
cat > "$R/pycore_fileutils.h" <<'EOF'
#ifndef PYP_SHADOW_FILEUTILS
#define PYP_SHADOW_FILEUTILS
#include "Python.h"
#define _Py_BEGIN_SUPPRESS_IPH
#define _Py_END_SUPPRESS_IPH
char *_Py_UniversalNewlineFgetsWithSize(char *buf, int n, FILE *stream, PyObject *fobj, size_t *size);
#endif
EOF
cat > "$R/pycore_bytesobject.h" <<'EOF'
#ifndef PYP_SHADOW_BYTES
#define PYP_SHADOW_BYTES
#include "Python.h"
#endif
EOF

echo "shadow headers written:"
ls "$R"
