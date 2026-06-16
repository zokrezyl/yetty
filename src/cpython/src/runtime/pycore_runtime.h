#ifndef PYP_SHADOW_RUNTIME
#define PYP_SHADOW_RUNTIME
#include "Python.h"

struct _pyruntime {
    struct {
        PyObject dummy_name; /* &_PyRuntime.parser.dummy_name is a sentinel */
    } parser;
};
extern struct _pyruntime _PyRuntime;

extern PyObject _PyRuntime_id_dummy;
#define _Py_ID(name) _PyRuntime_id_dummy /* &_Py_ID(x) -> PyObject*; SetAttr no-op */

#endif
