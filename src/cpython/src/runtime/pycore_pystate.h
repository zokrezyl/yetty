#ifndef PYP_SHADOW_PYSTATE
#define PYP_SHADOW_PYSTATE
#include "Python.h"
typedef struct {
    int _dummy;
} PyInterpreterState;
#define _PyThreadState_GET() pyp_tstate()
#define PyThreadState_GET() pyp_tstate()
#define _PyInterpreterState_GET() ((PyInterpreterState *)NULL)
#endif
