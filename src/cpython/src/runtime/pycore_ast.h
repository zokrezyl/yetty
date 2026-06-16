#ifndef PYP_SHADOW_AST
#define PYP_SHADOW_AST
#include "Python.h"
#include "ast.gen.h" /* our generated plain-C AST (src/ast) */
int _PyAST_Validate(mod_ty mod);
#endif
