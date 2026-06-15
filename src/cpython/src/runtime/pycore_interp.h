#ifndef PYP_SHADOW_INTERP
#define PYP_SHADOW_INTERP
#include "pycore_pystate.h"
typedef struct {
    int parser_debug;
} _pyconfig_stub;
#define _PyInterpreterState_GetConfig(interp) ((void *)NULL)
#endif
