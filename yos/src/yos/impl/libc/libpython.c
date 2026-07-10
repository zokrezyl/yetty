/* impl/libpython.c — host-side bridges that expose libpython 3.12 to
 * the wasm guest as env.Py_* imports.
 *
 * Architecture: the yos host binary links against libpython3.12.so
 * (or .a) at compile/link time. The wasm guest carries no Python
 * code — just a small driver that calls Py_Initialize(),
 * PyRun_SimpleString("..."), Py_Finalize() via env imports. yos
 * resolves those imports to the host wrappers below.
 *
 * Why this layout vs cross-compiling CPython to wasm32:
 *   - speed: native interpreter, no double interpretation through wasm3
 *   - size: tens-of-MB host libpython is shared with any other process
 *           on the host, vs each .wasm guest carrying its own copy
 *   - cross-compile pain: zero — we use the host's already-built libpython
 *   - cost: yos binary needs libpython3.12 at link time (per-platform),
 *           and the security boundary is now "whatever Python can do"
 *           (same as libc — see CLAUDE.md security note).
 *
 * Bridge convention here: opaque host pointers passed as i32 handles.
 * We don't (yet) implement a handle table — these three functions take
 * no PyObject* args. Once we bridge PyObject_*, we'll add one.
 */

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "wasm3.h"
#include "m3_env.h"
#include "m3_compile.h"      /* CompileFunction — table-lookup hits
                              * need on-demand compile; see liblua.c
                              * trampoline for the same pattern. */
#include "yos/types.h"
#include <yos/ytrace/ytrace.h>

/* libpython 3.12 forward decls. We deliberately don't #include
 * <Python.h> here — it pulls in the whole Python C API plus a pile of
 * macros (Py_INCREF, ...) that pollute every translation unit that
 * also includes this header. Forward-declare the precise subset we
 * use; host linker resolves them against libpython3.12.so.
 *
 * Per-guest isolation: every wasm guest gets its OWN Python
 * subinterpreter (Py_NewInterpreter), giving it independent
 * sys.modules, its own builtins, and (3.12+, PEP 684) its own GIL.
 * Without subinterpreters two guests sharing one libpython would see
 * each other's global state — `import os; os.path = trap` in guest A
 * would poison guest B's next `os.path.join(...)`. */
typedef struct _ts PyThreadState;
typedef struct _object PyObject;
typedef ssize_t Py_ssize_t;

extern void Py_Initialize(void);
extern void Py_Finalize(void);
extern int  PyRun_SimpleString(const char *command);
extern PyObject *PyRun_String(const char *str, int start,
                              PyObject *globals, PyObject *locals);
extern PyThreadState *Py_NewInterpreter(void);
extern void Py_EndInterpreter(PyThreadState *);
extern PyThreadState *PyThreadState_Swap(PyThreadState *);
extern int  Py_IsInitialized(void);

/* Refcount management. Macros in Python.h; the public functions
 * exist too for binding-language consumers. */
extern void Py_IncRef(PyObject *);
extern void Py_DecRef(PyObject *);

/* PyObject access. */
extern PyObject *PyObject_GetAttrString(PyObject *o, const char *attr);
extern int       PyObject_SetAttrString(PyObject *o, const char *attr, PyObject *v);
extern int       PyObject_HasAttrString(PyObject *o, const char *attr);
extern PyObject *PyObject_GetItem(PyObject *o, PyObject *key);
extern int       PyObject_SetItem(PyObject *o, PyObject *key, PyObject *v);
extern int       PyObject_DelItemString(PyObject *o, const char *key);
extern PyObject *PyObject_Str(PyObject *o);
extern PyObject *PyObject_Repr(PyObject *o);
extern Py_ssize_t PyObject_Length(PyObject *o);
extern int       PyObject_IsTrue(PyObject *o);
extern int       PyObject_Not(PyObject *o);
extern int       PyObject_RichCompareBool(PyObject *a, PyObject *b, int op);
extern PyObject *PyObject_Type(PyObject *o);
extern PyObject *PyObject_Call(PyObject *callable, PyObject *args, PyObject *kwargs);
extern PyObject *PyObject_CallObject(PyObject *callable, PyObject *args);

/* Containers. */
extern PyObject *PyDict_New(void);
extern int       PyDict_SetItemString(PyObject *p, const char *key, PyObject *val);
extern PyObject *PyDict_GetItemString(PyObject *p, const char *key);
extern int       PyDict_DelItemString(PyObject *p, const char *key);
extern Py_ssize_t PyDict_Size(PyObject *p);

extern PyObject *PyList_New(Py_ssize_t len);
extern int       PyList_Append(PyObject *list, PyObject *item);
extern Py_ssize_t PyList_Size(PyObject *list);
extern PyObject *PyList_GetItem(PyObject *list, Py_ssize_t index);
extern int       PyList_SetItem(PyObject *list, Py_ssize_t index, PyObject *item);

extern PyObject *PyTuple_New(Py_ssize_t len);
extern Py_ssize_t PyTuple_Size(PyObject *p);
extern PyObject *PyTuple_GetItem(PyObject *p, Py_ssize_t pos);
extern int       PyTuple_SetItem(PyObject *p, Py_ssize_t pos, PyObject *o);

/* Scalars. */
extern PyObject *PyLong_FromLongLong(long long v);
extern long long PyLong_AsLongLong(PyObject *o);
extern PyObject *PyFloat_FromDouble(double v);
extern double    PyFloat_AsDouble(PyObject *o);
extern PyObject *PyBool_FromLong(long v);

/* Strings. */
extern PyObject *PyUnicode_FromString(const char *u);
extern PyObject *PyUnicode_FromStringAndSize(const char *u, Py_ssize_t size);
extern const char *PyUnicode_AsUTF8AndSize(PyObject *unicode, Py_ssize_t *size);

/* Import. */
extern PyObject *PyImport_ImportModule(const char *name);
extern PyObject *PyImport_AddModule(const char *name);
extern PyObject *PyModule_GetDict(PyObject *module);

/* Error. */
extern PyObject *PyErr_Occurred(void);
extern void      PyErr_Clear(void);
extern void      PyErr_Print(void);
extern void      PyErr_SetString(PyObject *type, const char *message);
extern int       PyErr_ExceptionMatches(PyObject *exc);
extern void      PyErr_Fetch(PyObject **type, PyObject **value, PyObject **traceback);
extern void      PyErr_Restore(PyObject *type, PyObject *value, PyObject *traceback);

/* Singletons (immutable; identity-stable across guests). */
extern PyObject _Py_NoneStruct;
extern PyObject _Py_TrueStruct;
extern PyObject _Py_FalseStruct;

/* Common exception type singletons (set once at libpython init).
 * Identity stable; sharing across guests is correct by design. */
extern PyObject *PyExc_RuntimeError;
extern PyObject *PyExc_TypeError;
extern PyObject *PyExc_ValueError;

/* Variadic helpers — declare without the `...` since we only call
 * with a fixed arity (no marshal of variadic args from wasm). */
extern PyObject *PyErr_Format(PyObject *exception, const char *format, ...);

/* One-shot main-interpreter init. Py_Initialize is documented as
 * idempotent (no-op on second call), but we still want exactly one
 * call ever — guards the host-global runtime. */
static void libpython_ensure_initialized(void)
{
    static int inited = 0;
    if (inited) return;
    if (!Py_IsInitialized()) Py_Initialize();
    inited = 1;
}

/* Pull out the per-guest subinterpreter tstate held in ctx. Stored
 * as void* in struct yos_exec_ctx (avoids leaking PyThreadState into
 * the ctx struct definition). NULL means "this guest hasn't called
 * Py_Initialize yet". */
static PyThreadState *libpython_ctx_tstate(struct yos_exec_ctx *ctx)
{
    return ctx ? (PyThreadState *)ctx->py_tstate : NULL;
}

static void libpython_ctx_set_tstate(struct yos_exec_ctx *ctx, PyThreadState *ts)
{
    if (ctx) ctx->py_tstate = (void *)ts;
}

/* env.Py_Initialize — () → void.
 *
 * Two-stage per-guest setup:
 *   1. Ensure the host-process-wide CPython runtime exists
 *      (Py_Initialize, idempotent — runs the main interpreter).
 *   2. Create a SUBINTERPRETER for this wasm guest, store its
 *      PyThreadState* in the ctx. Subsequent Py_RunString from
 *      this guest swaps to that tstate first, so two guests
 *      see independent sys.modules, builtins, etc.
 *
 * Without (2): a malicious guest could `import os; os.path = X`
 * and poison the next guest's stdlib. With (2): each guest gets
 * its own modules dict; cross-guest mutation impossible. */
static const void *m3_yos_Py_Initialize(IM3Runtime runtime, IM3ImportContext _ctx,
                                        uint64_t *_sp, void *_mem)
{
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    (void)_ctx; (void)_sp; (void)_mem;
    ydebug("Py_Initialize (per-guest subinterpreter)\n");
    libpython_ensure_initialized();
    if (libpython_ctx_tstate(ctx) == NULL) {
        PyThreadState *prev = PyThreadState_Swap(NULL);
        PyThreadState *ts = Py_NewInterpreter();
        if (ts) {
            libpython_ctx_set_tstate(ctx, ts);
            /* Subinterpreter create leaves it as the current tstate;
             * swap back so the next call site does an explicit swap. */
            PyThreadState_Swap(prev);
        } else {
            PyThreadState_Swap(prev);
        }
    }
    return NULL;
}

/* env.Py_Finalize — () → void.
 *
 * Tear down THIS guest's subinterpreter only. The main interpreter
 * and the host-process runtime stay alive for other guests; we never
 * call Py_Finalize at the host level (would kill libpython for
 * every guest at once). */
static const void *m3_yos_Py_Finalize(IM3Runtime runtime, IM3ImportContext _ctx,
                                      uint64_t *_sp, void *_mem)
{
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    (void)_ctx; (void)_sp; (void)_mem;
    PyThreadState *ts = libpython_ctx_tstate(ctx);
    ydebug("Py_Finalize (subinterpreter %p)\n", (void *)ts);
    if (ts) {
        PyThreadState *prev = PyThreadState_Swap(ts);
        Py_EndInterpreter(ts);   /* consumes ts */
        libpython_ctx_set_tstate(ctx, NULL);
        /* Py_EndInterpreter cleared the slot; prev (if non-NULL) is
         * stale only if it was for the same ts we just killed.
         * Reattach the main tstate by swapping NULL → NULL is fine. */
        if (prev && prev != ts) PyThreadState_Swap(prev);
    }
    return NULL;
}

/* env.PyRun_SimpleString — (const char *) → int.
 * Wasm signature: i(i) — takes one i32 (wasm memory offset for the
 * command string), returns one i32 (libpython's return code).
 *
 * Swaps to THIS guest's subinterpreter tstate first so the running
 * code sees its own sys.modules / builtins. */
static const void *m3_yos_PyRun_SimpleString(IM3Runtime runtime,
                                             IM3ImportContext _ctx,
                                             uint64_t *_sp, void *_mem)
{
    struct yos_exec_ctx *ctx = (struct yos_exec_ctx *)m3_GetUserData(runtime);
    (void)_ctx;
    uint32_t cmd_off = (uint32_t)_sp[1];
    const char *cmd = "";
    if (ctx && ctx->memory && cmd_off < ctx->memory_size) {
        cmd = (const char *)(ctx->memory + cmd_off);
    }
    ydebug("PyRun_SimpleString(\"%.80s\")\n", cmd);

    PyThreadState *guest = libpython_ctx_tstate(ctx);
    PyThreadState *prev = guest ? PyThreadState_Swap(guest) : NULL;
    int rc = PyRun_SimpleString(cmd);
    if (guest) PyThreadState_Swap(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* ── PyObject handle table ──────────────────────────────────────
 *
 * The guest holds an i32 handle for every PyObject *. Slot 0 is
 * reserved so 0 == NULL == "no object". The three immortal
 * singletons (None / True / False) get permanent pre-bound handles
 * 1 / 2 / 3 — they're identity-stable across the whole process and
 * shared across guests on purpose (Python's `x is None` requires
 * pointer identity).
 *
 * For all other PyObject *, the bridge wraps fresh into the table.
 * Ref-count discipline: when a bridge wraps a NEW reference (from
 * a host call like PyObject_GetAttrString), the wrap returns a
 * handle but doesn't bump refcount on host side — the caller owns
 * the ref (per CPython convention, the function returned a new ref).
 * Py_DecRef bridge releases the handle slot AND decrefs the host
 * ref. For BORROWED refs (PyDict_GetItemString etc.), we Py_IncRef
 * before wrapping so the guest's handle owns its own ref.
 */

enum {
    YOS_PY_HANDLE_NONE  = 1,
    YOS_PY_HANDLE_TRUE  = 2,
    YOS_PY_HANDLE_FALSE = 3,
    YOS_PY_HANDLE_FIRST_FREE = 4,
};

#define YOS_PY_HANDLES_GROW 16

static int py_handles_reserve(struct yos_exec_ctx *ctx)
{
    if (!ctx) return -1;
    if (ctx->py_handles_cap == 0) {
        size_t cap = YOS_PY_HANDLES_GROW;
        void **slots = calloc(cap, sizeof(void *));
        if (!slots) return -1;
        /* Pre-bind singleton handles. */
        slots[YOS_PY_HANDLE_NONE]  = &_Py_NoneStruct;
        slots[YOS_PY_HANDLE_TRUE]  = &_Py_TrueStruct;
        slots[YOS_PY_HANDLE_FALSE] = &_Py_FalseStruct;
        ctx->py_handles = slots;
        ctx->py_handles_cap = (uint32_t)cap;
        return 0;
    }
    for (uint32_t i = YOS_PY_HANDLE_FIRST_FREE; i < ctx->py_handles_cap; ++i)
        if (!ctx->py_handles[i]) return 0;
    size_t newcap = (size_t)ctx->py_handles_cap + YOS_PY_HANDLES_GROW;
    void **next = realloc(ctx->py_handles, newcap * sizeof(void *));
    if (!next) return -1;
    memset(next + ctx->py_handles_cap, 0,
           (newcap - ctx->py_handles_cap) * sizeof(void *));
    ctx->py_handles = next;
    ctx->py_handles_cap = (uint32_t)newcap;
    return 0;
}

static uint32_t py_handles_wrap(struct yos_exec_ctx *ctx, PyObject *p)
{
    if (!p) return 0;
    if (py_handles_reserve(ctx) < 0) return 0;
    /* Singleton shortcuts — preserve identity. */
    if (p == &_Py_NoneStruct)  return YOS_PY_HANDLE_NONE;
    if (p == &_Py_TrueStruct)  return YOS_PY_HANDLE_TRUE;
    if (p == &_Py_FalseStruct) return YOS_PY_HANDLE_FALSE;
    for (uint32_t i = YOS_PY_HANDLE_FIRST_FREE; i < ctx->py_handles_cap; ++i)
        if (!ctx->py_handles[i]) { ctx->py_handles[i] = p; return i; }
    return 0;
}

static PyObject *py_handles_resolve(struct yos_exec_ctx *ctx, uint32_t h)
{
    if (!ctx || !ctx->py_handles) return NULL;
    if (h == 0 || h >= ctx->py_handles_cap) return NULL;
    return (PyObject *)ctx->py_handles[h];
}

static PyObject *py_handles_release(struct yos_exec_ctx *ctx, uint32_t h)
{
    if (!ctx || !ctx->py_handles) return NULL;
    if (h == 0 || h >= ctx->py_handles_cap) return NULL;
    /* Singletons aren't released; they live forever. */
    if (h <= YOS_PY_HANDLE_FALSE) return (PyObject *)ctx->py_handles[h];
    PyObject *p = (PyObject *)ctx->py_handles[h];
    ctx->py_handles[h] = NULL;
    return p;
}

/* Wrap a host string under the wasm guest's pool of return strings.
 * Mirrors lua/openssl's guest_stash_string — overwrites previous
 * stash slots; caller copies. Returns wasm offset. */
static uint32_t libpython_stash_string(struct yos_exec_ctx *ctx,
                                       const char *s, size_t len)
{
    if (!s || !ctx || !ctx->memory) return 0;
    if (ctx->memory_size < 4096 + 16) return 0;
    static __thread uint32_t bump;
    static __thread uint32_t base;
    if (!base) base = ctx->memory_size - 4096;
    if (bump + len + 1 > 4096) bump = 0;
    uint32_t at = base + bump;
    memcpy(ctx->memory + at, s, len);
    ctx->memory[at + len] = 0;
    bump += (uint32_t)len + 1;
    return at;
}

/* Memory accessors — copies of the libpython/openssl helpers. */
static const void *guest_buf_ro_py(struct yos_exec_ctx *ctx,
                                   uint32_t off, uint32_t len)
{
    if (!ctx || !ctx->memory) return NULL;
    if (len == 0) return ctx->memory + off;
    uint64_t end = (uint64_t)off + (uint64_t)len;
    if (off >= ctx->memory_size || end > ctx->memory_size) return NULL;
    return ctx->memory + off;
}
static void *guest_buf_rw_py(struct yos_exec_ctx *ctx,
                             uint32_t off, uint32_t len)
{
    return (void *)guest_buf_ro_py(ctx, off, len);
}
static const char *guest_cstr_py(struct yos_exec_ctx *ctx, uint32_t off)
{
    if (!ctx || !ctx->memory || off == 0 || off >= ctx->memory_size)
        return NULL;
    const char *p = (const char *)(ctx->memory + off);
    const char *end = (const char *)(ctx->memory + ctx->memory_size);
    for (const char *q = p; q < end; ++q) if (*q == 0) return p;
    return NULL;
}

/* Subinterpreter swap helper — used by every bridge that calls
 * into host libpython. Returns the previous tstate so the caller
 * swaps it back after the call. */
static PyThreadState *py_swap_in(struct yos_exec_ctx *ctx)
{
    PyThreadState *guest = libpython_ctx_tstate(ctx);
    return guest ? PyThreadState_Swap(guest) : NULL;
}
static void py_swap_out(PyThreadState *prev)
{
    /* prev may be NULL (no previous tstate) — PyThreadState_Swap(NULL)
     * is valid. */
    PyThreadState_Swap(prev);
}

#define PY_CTX(rt)  ((struct yos_exec_ctx *)m3_GetUserData(rt))

/* ── PyObject bridges ──────────────────────────────────────────── */

/* env.Py_IncRef — v(obj_h). On the host side we bump refcount; on
 * the wasm side the guest's handle stays valid. */
static const void *m3_yos_Py_IncRef(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[0]);
    if (!o) return NULL;
    PyThreadState *prev = py_swap_in(ctx);
    Py_IncRef(o);
    py_swap_out(prev);
    return NULL;
}

/* env.Py_DecRef — v(obj_h). Release the handle slot AND decref. */
static const void *m3_yos_Py_DecRef(IM3Runtime rt, IM3ImportContext _c,
                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    uint32_t h = (uint32_t)_sp[0];
    if (h <= YOS_PY_HANDLE_FALSE) return NULL;  /* singletons immortal */
    PyObject *o = py_handles_release(ctx, h);
    if (!o) return NULL;
    PyThreadState *prev = py_swap_in(ctx);
    Py_DecRef(o);
    py_swap_out(prev);
    return NULL;
}

/* Wrap-result helper: convert a fresh host PyObject * to a guest
 * handle. NULL → 0. The host function gave us a NEW reference; the
 * caller owns it via the handle's slot. Py_DecRef releases it. */
#define PY_WRAP_NEW(ctx, p)  ((uint64_t)py_handles_wrap((ctx), (p)))

/* For BORROWED references (PyDict_GetItemString, PyList_GetItem,
 * PyTuple_GetItem, PyModule_GetDict): bump refcount before wrapping
 * so the guest's handle owns its own ref. */
static uint32_t py_wrap_borrowed(struct yos_exec_ctx *ctx, PyObject *p)
{
    if (!p) return 0;
    Py_IncRef(p);
    return py_handles_wrap(ctx, p);
}

/* env.PyObject_GetAttrString — i32(obj_h, name_off). New ref. */
static const void *m3_yos_PyObject_GetAttrString(IM3Runtime rt, IM3ImportContext _c,
                                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *name = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    if (!o || !name) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyObject_GetAttrString(o, name);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyObject_SetAttrString — i32(obj_h, name_off, val_h). */
static const void *m3_yos_PyObject_SetAttrString(IM3Runtime rt, IM3ImportContext _c,
                                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *name = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    PyObject *v = py_handles_resolve(ctx, (uint32_t)_sp[3]);
    if (!o || !name) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyObject_SetAttrString(o, name, v);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyObject_HasAttrString — i32(obj_h, name_off). */
static const void *m3_yos_PyObject_HasAttrString(IM3Runtime rt, IM3ImportContext _c,
                                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *name = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    if (!o || !name) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyObject_HasAttrString(o, name);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyObject_Str / Repr — i32(obj_h). Both return NEW unicode object. */
static const void *m3_yos_PyObject_Str(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyObject_Str(o);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}
static const void *m3_yos_PyObject_Repr(IM3Runtime rt, IM3ImportContext _c,
                                        uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyObject_Repr(o);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyObject_Length — i32(obj_h). Py_ssize_t result narrowed. */
static const void *m3_yos_PyObject_Length(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    Py_ssize_t n = PyObject_Length(o);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)(int32_t)n;
    return NULL;
}

/* env.PyObject_IsTrue / Not — i32(obj_h). */
static const void *m3_yos_PyObject_IsTrue(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyObject_IsTrue(o);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyObject_Type — i32(obj_h). New ref. */
static const void *m3_yos_PyObject_Type(IM3Runtime rt, IM3ImportContext _c,
                                        uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyObject_Type(o);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyObject_Call — i32(callable_h, args_h, kwargs_h). */
static const void *m3_yos_PyObject_Call(IM3Runtime rt, IM3ImportContext _c,
                                        uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *fn  = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    PyObject *a   = py_handles_resolve(ctx, (uint32_t)_sp[2]);
    PyObject *kw  = (uint32_t)_sp[3] ? py_handles_resolve(ctx, (uint32_t)_sp[3]) : NULL;
    if (!fn || !a) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyObject_Call(fn, a, kw);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyObject_CallObject — i32(callable_h, args_h). */
static const void *m3_yos_PyObject_CallObject(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *fn  = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    PyObject *a   = (uint32_t)_sp[2] ? py_handles_resolve(ctx, (uint32_t)_sp[2]) : NULL;
    if (!fn) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyObject_CallObject(fn, a);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* ── containers ────────────────────────────────────────────────── */

/* env.PyDict_New — i32(). */
static const void *m3_yos_PyDict_New(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyDict_New();
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyDict_SetItemString — i32(dict_h, key_off, val_h). */
static const void *m3_yos_PyDict_SetItemString(IM3Runtime rt, IM3ImportContext _c,
                                               uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *d = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *k = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    PyObject *v = py_handles_resolve(ctx, (uint32_t)_sp[3]);
    if (!d || !k || !v) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyDict_SetItemString(d, k, v);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyDict_GetItemString — i32(dict_h, key_off). Returns BORROWED ref. */
static const void *m3_yos_PyDict_GetItemString(IM3Runtime rt, IM3ImportContext _c,
                                               uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *d = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *k = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    if (!d || !k) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyDict_GetItemString(d, k);
    py_swap_out(prev);
    _sp[0] = (uint64_t)py_wrap_borrowed(ctx, r);
    return NULL;
}

/* env.PyDict_Size — i32(dict_h). */
static const void *m3_yos_PyDict_Size(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *d = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!d) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    Py_ssize_t n = PyDict_Size(d);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)(int32_t)n;
    return NULL;
}

/* env.PyList_New — i32(int len). */
static const void *m3_yos_PyList_New(IM3Runtime rt, IM3ImportContext _c,
                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    int len = (int)_sp[1];
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyList_New((Py_ssize_t)len);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyList_Append — i32(list_h, item_h). */
static const void *m3_yos_PyList_Append(IM3Runtime rt, IM3ImportContext _c,
                                        uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *l = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    PyObject *i = py_handles_resolve(ctx, (uint32_t)_sp[2]);
    if (!l || !i) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyList_Append(l, i);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyList_Size — i32(list_h). */
static const void *m3_yos_PyList_Size(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *l = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!l) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    Py_ssize_t n = PyList_Size(l);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)(int32_t)n;
    return NULL;
}

/* env.PyList_GetItem — i32(list_h, int idx). BORROWED. */
static const void *m3_yos_PyList_GetItem(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *l = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    int idx = (int)_sp[2];
    if (!l) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyList_GetItem(l, (Py_ssize_t)idx);
    py_swap_out(prev);
    _sp[0] = (uint64_t)py_wrap_borrowed(ctx, r);
    return NULL;
}

/* env.PyList_SetItem — i32(list_h, int idx, item_h). STEALS ref. */
static const void *m3_yos_PyList_SetItem(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *l = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    int idx = (int)_sp[2];
    uint32_t h_item = (uint32_t)_sp[3];
    /* SetItem steals the reference to item — so we release the
     * guest's handle slot but DON'T decref host-side. */
    PyObject *item = py_handles_release(ctx, h_item);
    if (!l || !item) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyList_SetItem(l, (Py_ssize_t)idx, item);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyTuple_New — i32(int len). */
static const void *m3_yos_PyTuple_New(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    int len = (int)_sp[1];
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyTuple_New((Py_ssize_t)len);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyTuple_Size — i32(tuple_h). */
static const void *m3_yos_PyTuple_Size(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *t = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!t) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    Py_ssize_t n = PyTuple_Size(t);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)(int32_t)n;
    return NULL;
}

/* env.PyTuple_GetItem — i32(tuple_h, int idx). BORROWED. */
static const void *m3_yos_PyTuple_GetItem(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *t = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    int idx = (int)_sp[2];
    if (!t) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyTuple_GetItem(t, (Py_ssize_t)idx);
    py_swap_out(prev);
    _sp[0] = (uint64_t)py_wrap_borrowed(ctx, r);
    return NULL;
}

/* env.PyTuple_SetItem — i32(tuple_h, int idx, item_h). STEALS ref. */
static const void *m3_yos_PyTuple_SetItem(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *t = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    int idx = (int)_sp[2];
    uint32_t h_item = (uint32_t)_sp[3];
    PyObject *item = py_handles_release(ctx, h_item);
    if (!t || !item) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyTuple_SetItem(t, (Py_ssize_t)idx, item);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* ── scalars ───────────────────────────────────────────────────── */

/* env.PyLong_FromLongLong — i32(i64 v). */
static const void *m3_yos_PyLong_FromLongLong(IM3Runtime rt, IM3ImportContext _c,
                                              uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    long long v = (long long)_sp[1];
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyLong_FromLongLong(v);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyLong_AsLongLong — i64(obj_h). */
static const void *m3_yos_PyLong_AsLongLong(IM3Runtime rt, IM3ImportContext _c,
                                            uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    long long v = PyLong_AsLongLong(o);
    py_swap_out(prev);
    _sp[0] = (uint64_t)v;
    return NULL;
}

/* env.PyFloat_FromDouble — i32(f64). */
static const void *m3_yos_PyFloat_FromDouble(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    double v; memcpy(&v, &_sp[1], sizeof(double));
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyFloat_FromDouble(v);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyFloat_AsDouble — f64(obj_h). */
static const void *m3_yos_PyFloat_AsDouble(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { double z = 0.0; memcpy(&_sp[0], &z, sizeof z); return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    double v = PyFloat_AsDouble(o);
    py_swap_out(prev);
    memcpy(&_sp[0], &v, sizeof(v));
    return NULL;
}

/* env.PyBool_FromLong — i32(int v). */
static const void *m3_yos_PyBool_FromLong(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    int v = (int)_sp[1];
    /* Bypass the host call — bool is a singleton. */
    _sp[0] = v ? YOS_PY_HANDLE_TRUE : YOS_PY_HANDLE_FALSE;
    return NULL;
}

/* ── strings ───────────────────────────────────────────────────── */

/* env.PyUnicode_FromString — i32(str_off). New ref. */
static const void *m3_yos_PyUnicode_FromString(IM3Runtime rt, IM3ImportContext _c,
                                               uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    const char *s = guest_cstr_py(ctx, (uint32_t)_sp[1]);
    if (!s) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyUnicode_FromString(s);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyUnicode_FromStringAndSize — i32(buf_off, int size). */
static const void *m3_yos_PyUnicode_FromStringAndSize(IM3Runtime rt, IM3ImportContext _c,
                                                      uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    uint32_t off = (uint32_t)_sp[1];
    int      sz  = (int)_sp[2];
    const char *s = sz ? (const char *)guest_buf_ro_py(ctx, off, (uint32_t)sz) : "";
    if (sz > 0 && !s) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyUnicode_FromStringAndSize(s, (Py_ssize_t)sz);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyUnicode_AsUTF8AndSize — i32(obj_h, size_off).
 * Host returns const char * into Python-managed storage. Copy into
 * guest scratch and return offset. size_off is uint32_t *. */
static const void *m3_yos_PyUnicode_AsUTF8AndSize(IM3Runtime rt, IM3ImportContext _c,
                                                  uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t size_off = (uint32_t)_sp[2];
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    Py_ssize_t sz = 0;
    const char *s = PyUnicode_AsUTF8AndSize(o, &sz);
    py_swap_out(prev);
    if (!s) { _sp[0] = 0; return NULL; }
    if (size_off) {
        uint32_t *lp = (uint32_t *)guest_buf_rw_py(ctx, size_off, sizeof(uint32_t));
        if (lp) *lp = (uint32_t)sz;
    }
    _sp[0] = (uint64_t)libpython_stash_string(ctx, s, (size_t)sz);
    return NULL;
}

/* ── import ────────────────────────────────────────────────────── */

/* env.PyImport_ImportModule — i32(name_off). New ref. */
static const void *m3_yos_PyImport_ImportModule(IM3Runtime rt, IM3ImportContext _c,
                                                uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    const char *name = guest_cstr_py(ctx, (uint32_t)_sp[1]);
    if (!name) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyImport_ImportModule(name);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyImport_AddModule — i32(name_off). BORROWED. */
static const void *m3_yos_PyImport_AddModule(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    const char *name = guest_cstr_py(ctx, (uint32_t)_sp[1]);
    if (!name) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyImport_AddModule(name);
    py_swap_out(prev);
    _sp[0] = (uint64_t)py_wrap_borrowed(ctx, r);
    return NULL;
}

/* env.PyModule_GetDict — i32(module_h). BORROWED. */
static const void *m3_yos_PyModule_GetDict(IM3Runtime rt, IM3ImportContext _c,
                                           uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyModule_GetDict(o);
    py_swap_out(prev);
    _sp[0] = (uint64_t)py_wrap_borrowed(ctx, r);
    return NULL;
}

/* ── error ─────────────────────────────────────────────────────── */

/* env.PyErr_Occurred — i32(). BORROWED (or NULL). */
static const void *m3_yos_PyErr_Occurred(IM3Runtime rt, IM3ImportContext _c,
                                         uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyErr_Occurred();
    py_swap_out(prev);
    /* Don't bump refcount — PyErr_Occurred is documented as borrowed
     * and the exception type is alive for the lifetime of the
     * process. We wrap WITHOUT inc-ref since the handle is intended
     * as identity-only (guest does `if (PyErr_Occurred()) ...`). */
    _sp[0] = r ? (uint64_t)py_handles_wrap(ctx, r) : 0;
    return NULL;
}

/* env.PyErr_Clear — v(). */
static const void *m3_yos_PyErr_Clear(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_sp; (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyThreadState *prev = py_swap_in(ctx);
    PyErr_Clear();
    py_swap_out(prev);
    return NULL;
}

/* env.PyErr_Print — v(). */
static const void *m3_yos_PyErr_Print(IM3Runtime rt, IM3ImportContext _c,
                                      uint64_t *_sp, void *_m)
{
    (void)_sp; (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyThreadState *prev = py_swap_in(ctx);
    PyErr_Print();
    py_swap_out(prev);
    return NULL;
}

/* env.PyErr_SetString — v(exc_h, msg_off). */
static const void *m3_yos_PyErr_SetString(IM3Runtime rt, IM3ImportContext _c,
                                          uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *type = py_handles_resolve(ctx, (uint32_t)_sp[0]);
    const char *msg = guest_cstr_py(ctx, (uint32_t)_sp[1]);
    if (!type || !msg) return NULL;
    PyThreadState *prev = py_swap_in(ctx);
    PyErr_SetString(type, msg);
    py_swap_out(prev);
    return NULL;
}

/* env.PyErr_ExceptionMatches — i32(exc_h). */
static const void *m3_yos_PyErr_ExceptionMatches(IM3Runtime rt, IM3ImportContext _c,
                                                 uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *exc = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    if (!exc) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyErr_ExceptionMatches(exc);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* ── eval ──────────────────────────────────────────────────────── */

/* ── host → wasm trampoline for PyCFunction (PyMethodDef) ──────
 *
 * Python's PyCFunction is `PyObject *(*)(PyObject *self, PyObject *args)`.
 * Plain function pointer — there's no upvalue mechanism like Lua. We
 * give Python a HOST function pointer; Python stores it in
 * PyMethodDef.ml_meth. When a Python script calls the registered
 * function, host Python invokes that pointer.
 *
 * Each registered wasm function needs a *distinct host C function
 * pointer* so the trampoline knows which wasm function to dispatch
 * to. Without libffi (which would emit closures at runtime), we
 * pre-generate a FIXED POOL of trampoline functions, each hard-coded
 * to a unique slot id. Registration allocates one slot from the pool
 * and stores (ctx, wasm_idx); the slot's pre-built trampoline goes
 * into the PyMethodDef.
 *
 * Limit: YOS_PY_NTRAMPS slots = at most 128 simultaneously registered
 * wasm-backed PyCFunctions per host process. Most Python C extensions
 * are well under this; the os module has ~80 functions, sys has ~30.
 * Grow YOS_PY_NTRAMPS if a real consumer exhausts it.
 */
#define YOS_PY_NTRAMPS 128

typedef PyObject *(*PyCFunction)(PyObject *self, PyObject *args);

struct yos_py_tramp_slot {
    struct yos_exec_ctx *ctx;
    uint32_t wasm_idx;          /* 0 = unused */
    int      flags;             /* METH_VARARGS etc. — informational */
};
static struct yos_py_tramp_slot g_py_tramps[YOS_PY_NTRAMPS];

static int py_alloc_tramp(struct yos_exec_ctx *ctx, uint32_t wasm_idx,
                          int flags)
{
    for (int i = 0; i < YOS_PY_NTRAMPS; ++i) {
        if (g_py_tramps[i].wasm_idx == 0) {
            g_py_tramps[i].ctx = ctx;
            g_py_tramps[i].wasm_idx = wasm_idx;
            g_py_tramps[i].flags = flags;
            return i;
        }
    }
    return -1;
}

/* Shared dispatch — every per-slot trampoline forwards here with its
 * own slot index. Wraps self+args as guest handles, calls the wasm
 * function via wasm3, unwraps the returned handle. */
static PyObject *py_tramp_dispatch(int slot, PyObject *self, PyObject *args)
{
    struct yos_py_tramp_slot *s = &g_py_tramps[slot];
    if (!s->wasm_idx || !s->ctx) {
        PyErr_SetString(PyExc_RuntimeError, "yos: dead Py trampoline slot");
        return NULL;
    }
    struct yos_exec_ctx *ctx = s->ctx;
    PyThreadState *prev = py_swap_in(ctx);

    /* Python documents PyCFunction's (self, args) as BORROWED refs.
     * For the wasm side we wrap them under the per-guest handle
     * table so they survive the call duration. We Py_IncRef so the
     * wasm-side handle owns its own ref — wasm can Py_DecRef it
     * (the standard yos handle-table convention) without affecting
     * Python's accounting. The trampoline releases those handles
     * after the wasm function returns (in case the wasm guest did
     * not call Py_DecRef itself). */
    uint32_t self_h = self ? py_handles_wrap(ctx, self) : 0;
    if (self) Py_IncRef(self);
    uint32_t args_h = args ? py_handles_wrap(ctx, args) : 0;
    if (args) Py_IncRef(args);

    IM3Module module = (IM3Module)ctx->module;
    if (!module || s->wasm_idx >= module->table0Size) {
        PyErr_SetString(PyExc_RuntimeError,
                        "yos: wasm fn idx out of range");
        py_swap_out(prev);
        return NULL;
    }
    IM3Function fn = module->table0[s->wasm_idx];
    if (!fn) {
        PyErr_SetString(PyExc_RuntimeError, "yos: wasm fn idx null");
        py_swap_out(prev);
        return NULL;
    }
    if (!fn->compiled) {
        M3Result crc = CompileFunction(fn);
        if (crc) {
            PyErr_Format(PyExc_RuntimeError,
                         "yos: CompileFunction(%u) failed: %s",
                         (unsigned)s->wasm_idx, crc);
            py_swap_out(prev);
            return NULL;
        }
    }

    const void *fn_args[2] = { &self_h, &args_h };
    M3Result rc = m3_Call(fn, 2, fn_args);
    if (rc) {
        PyErr_Format(PyExc_RuntimeError,
                     "yos: Py trampoline wasm call failed: %s", rc);
        py_swap_out(prev);
        return NULL;
    }
    uint32_t result_h = 0;
    const void *retptrs[1] = { &result_h };
    (void)m3_GetResults(fn, 1, retptrs);

    /* Recover the host PyObject * from the returned handle. The wasm
     * side returned a NEW reference (Python convention); we pass
     * that ref through to the caller of the trampoline. We DO need
     * to release the self/args handle slots; the host refs we
     * IncRef'd above are owned by the wasm side and (assumed) DecRef'd
     * by the wasm function — but in case it didn't, we mop up. */
    PyObject *result = py_handles_resolve(ctx, result_h);

    /* Conservatively decref + release our self/args wrappers. If the
     * wasm function already Py_DecRef'd, that path released them via
     * the bridge and our slots are NULL now. */
    if (self_h && py_handles_resolve(ctx, self_h) == self) {
        py_handles_release(ctx, self_h);
        Py_DecRef(self);
    }
    if (args_h && py_handles_resolve(ctx, args_h) == args) {
        py_handles_release(ctx, args_h);
        Py_DecRef(args);
    }

    py_swap_out(prev);
    return result;
}

/* Generate YOS_PY_NTRAMPS distinct C functions, each hard-coding its
 * own slot index. List-and-paste; gross but trivial. */
#define YOS_PY_TR(n) \
static PyObject *py_tr_##n(PyObject *s, PyObject *a) \
{ return py_tramp_dispatch(n, s, a); }

YOS_PY_TR(0)   YOS_PY_TR(1)   YOS_PY_TR(2)   YOS_PY_TR(3)
YOS_PY_TR(4)   YOS_PY_TR(5)   YOS_PY_TR(6)   YOS_PY_TR(7)
YOS_PY_TR(8)   YOS_PY_TR(9)   YOS_PY_TR(10)  YOS_PY_TR(11)
YOS_PY_TR(12)  YOS_PY_TR(13)  YOS_PY_TR(14)  YOS_PY_TR(15)
YOS_PY_TR(16)  YOS_PY_TR(17)  YOS_PY_TR(18)  YOS_PY_TR(19)
YOS_PY_TR(20)  YOS_PY_TR(21)  YOS_PY_TR(22)  YOS_PY_TR(23)
YOS_PY_TR(24)  YOS_PY_TR(25)  YOS_PY_TR(26)  YOS_PY_TR(27)
YOS_PY_TR(28)  YOS_PY_TR(29)  YOS_PY_TR(30)  YOS_PY_TR(31)
YOS_PY_TR(32)  YOS_PY_TR(33)  YOS_PY_TR(34)  YOS_PY_TR(35)
YOS_PY_TR(36)  YOS_PY_TR(37)  YOS_PY_TR(38)  YOS_PY_TR(39)
YOS_PY_TR(40)  YOS_PY_TR(41)  YOS_PY_TR(42)  YOS_PY_TR(43)
YOS_PY_TR(44)  YOS_PY_TR(45)  YOS_PY_TR(46)  YOS_PY_TR(47)
YOS_PY_TR(48)  YOS_PY_TR(49)  YOS_PY_TR(50)  YOS_PY_TR(51)
YOS_PY_TR(52)  YOS_PY_TR(53)  YOS_PY_TR(54)  YOS_PY_TR(55)
YOS_PY_TR(56)  YOS_PY_TR(57)  YOS_PY_TR(58)  YOS_PY_TR(59)
YOS_PY_TR(60)  YOS_PY_TR(61)  YOS_PY_TR(62)  YOS_PY_TR(63)
YOS_PY_TR(64)  YOS_PY_TR(65)  YOS_PY_TR(66)  YOS_PY_TR(67)
YOS_PY_TR(68)  YOS_PY_TR(69)  YOS_PY_TR(70)  YOS_PY_TR(71)
YOS_PY_TR(72)  YOS_PY_TR(73)  YOS_PY_TR(74)  YOS_PY_TR(75)
YOS_PY_TR(76)  YOS_PY_TR(77)  YOS_PY_TR(78)  YOS_PY_TR(79)
YOS_PY_TR(80)  YOS_PY_TR(81)  YOS_PY_TR(82)  YOS_PY_TR(83)
YOS_PY_TR(84)  YOS_PY_TR(85)  YOS_PY_TR(86)  YOS_PY_TR(87)
YOS_PY_TR(88)  YOS_PY_TR(89)  YOS_PY_TR(90)  YOS_PY_TR(91)
YOS_PY_TR(92)  YOS_PY_TR(93)  YOS_PY_TR(94)  YOS_PY_TR(95)
YOS_PY_TR(96)  YOS_PY_TR(97)  YOS_PY_TR(98)  YOS_PY_TR(99)
YOS_PY_TR(100) YOS_PY_TR(101) YOS_PY_TR(102) YOS_PY_TR(103)
YOS_PY_TR(104) YOS_PY_TR(105) YOS_PY_TR(106) YOS_PY_TR(107)
YOS_PY_TR(108) YOS_PY_TR(109) YOS_PY_TR(110) YOS_PY_TR(111)
YOS_PY_TR(112) YOS_PY_TR(113) YOS_PY_TR(114) YOS_PY_TR(115)
YOS_PY_TR(116) YOS_PY_TR(117) YOS_PY_TR(118) YOS_PY_TR(119)
YOS_PY_TR(120) YOS_PY_TR(121) YOS_PY_TR(122) YOS_PY_TR(123)
YOS_PY_TR(124) YOS_PY_TR(125) YOS_PY_TR(126) YOS_PY_TR(127)
#undef YOS_PY_TR

#define YOS_PY_E(n) py_tr_##n,
static PyCFunction g_py_tramp_fns[YOS_PY_NTRAMPS] = {
    YOS_PY_E(0)   YOS_PY_E(1)   YOS_PY_E(2)   YOS_PY_E(3)
    YOS_PY_E(4)   YOS_PY_E(5)   YOS_PY_E(6)   YOS_PY_E(7)
    YOS_PY_E(8)   YOS_PY_E(9)   YOS_PY_E(10)  YOS_PY_E(11)
    YOS_PY_E(12)  YOS_PY_E(13)  YOS_PY_E(14)  YOS_PY_E(15)
    YOS_PY_E(16)  YOS_PY_E(17)  YOS_PY_E(18)  YOS_PY_E(19)
    YOS_PY_E(20)  YOS_PY_E(21)  YOS_PY_E(22)  YOS_PY_E(23)
    YOS_PY_E(24)  YOS_PY_E(25)  YOS_PY_E(26)  YOS_PY_E(27)
    YOS_PY_E(28)  YOS_PY_E(29)  YOS_PY_E(30)  YOS_PY_E(31)
    YOS_PY_E(32)  YOS_PY_E(33)  YOS_PY_E(34)  YOS_PY_E(35)
    YOS_PY_E(36)  YOS_PY_E(37)  YOS_PY_E(38)  YOS_PY_E(39)
    YOS_PY_E(40)  YOS_PY_E(41)  YOS_PY_E(42)  YOS_PY_E(43)
    YOS_PY_E(44)  YOS_PY_E(45)  YOS_PY_E(46)  YOS_PY_E(47)
    YOS_PY_E(48)  YOS_PY_E(49)  YOS_PY_E(50)  YOS_PY_E(51)
    YOS_PY_E(52)  YOS_PY_E(53)  YOS_PY_E(54)  YOS_PY_E(55)
    YOS_PY_E(56)  YOS_PY_E(57)  YOS_PY_E(58)  YOS_PY_E(59)
    YOS_PY_E(60)  YOS_PY_E(61)  YOS_PY_E(62)  YOS_PY_E(63)
    YOS_PY_E(64)  YOS_PY_E(65)  YOS_PY_E(66)  YOS_PY_E(67)
    YOS_PY_E(68)  YOS_PY_E(69)  YOS_PY_E(70)  YOS_PY_E(71)
    YOS_PY_E(72)  YOS_PY_E(73)  YOS_PY_E(74)  YOS_PY_E(75)
    YOS_PY_E(76)  YOS_PY_E(77)  YOS_PY_E(78)  YOS_PY_E(79)
    YOS_PY_E(80)  YOS_PY_E(81)  YOS_PY_E(82)  YOS_PY_E(83)
    YOS_PY_E(84)  YOS_PY_E(85)  YOS_PY_E(86)  YOS_PY_E(87)
    YOS_PY_E(88)  YOS_PY_E(89)  YOS_PY_E(90)  YOS_PY_E(91)
    YOS_PY_E(92)  YOS_PY_E(93)  YOS_PY_E(94)  YOS_PY_E(95)
    YOS_PY_E(96)  YOS_PY_E(97)  YOS_PY_E(98)  YOS_PY_E(99)
    YOS_PY_E(100) YOS_PY_E(101) YOS_PY_E(102) YOS_PY_E(103)
    YOS_PY_E(104) YOS_PY_E(105) YOS_PY_E(106) YOS_PY_E(107)
    YOS_PY_E(108) YOS_PY_E(109) YOS_PY_E(110) YOS_PY_E(111)
    YOS_PY_E(112) YOS_PY_E(113) YOS_PY_E(114) YOS_PY_E(115)
    YOS_PY_E(116) YOS_PY_E(117) YOS_PY_E(118) YOS_PY_E(119)
    YOS_PY_E(120) YOS_PY_E(121) YOS_PY_E(122) YOS_PY_E(123)
    YOS_PY_E(124) YOS_PY_E(125) YOS_PY_E(126) YOS_PY_E(127)
};
#undef YOS_PY_E

/* ── PyMethodDef / module-add bridges ─────────────────────────── */

/* PyMethodDef layout on wasm32 (4 i32 fields, 16 bytes total).
 * Host layout has 8-byte pointers and Py_ssize_t alignment, so we
 * build a fresh host-side array and forward the wasm-side method
 * table through it. */
#define YOS_PY_METHODDEF_WASM_SIZE  16

extern int PyModule_AddFunctions(PyObject *module, void *methods);
extern int PyModule_AddObject(PyObject *module, const char *name, PyObject *value);
extern int PyModule_AddIntConstant(PyObject *module, const char *name,
                                   long value);
extern int PyModule_AddStringConstant(PyObject *module, const char *name,
                                      const char *value);

/* Host-side PyMethodDef. Mirrors CPython's struct PyMethodDef. */
struct yos_host_PyMethodDef {
    const char *ml_name;
    PyCFunction ml_meth;
    int         ml_flags;
    const char *ml_doc;
};

/* env.PyModule_AddFunctions — i32(module_h, methods_off).
 *
 * Walks the wasm-side PyMethodDef[] (terminated by {NULL, NULL,
 * 0, NULL}), allocates a trampoline slot per entry, builds a host
 * PyMethodDef[] with host trampolines + strduped names/docs, and
 * calls host PyModule_AddFunctions.
 *
 * The host array + strdups leak — Python doesn't free them and
 * we don't track them. For embedded use that's bounded; for
 * unload/reload paths we'd add a per-ctx allocation list. */
static const void *m3_yos_PyModule_AddFunctions(IM3Runtime rt, IM3ImportContext _c,
                                                uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *m  = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t off = (uint32_t)_sp[2];
    if (!m || !ctx->memory) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }

    /* Count entries (terminator = all-zero). */
    uint32_t n = 0;
    for (uint32_t p = off; p + YOS_PY_METHODDEF_WASM_SIZE <= ctx->memory_size;
         p += YOS_PY_METHODDEF_WASM_SIZE) {
        uint32_t name = *(const uint32_t *)(ctx->memory + p);
        uint32_t meth = *(const uint32_t *)(ctx->memory + p + 4);
        if (name == 0 && meth == 0) break;
        ++n;
        if (n > 4096) break;   /* runaway guard */
    }

    struct yos_host_PyMethodDef *host =
        calloc(n + 1, sizeof(struct yos_host_PyMethodDef));
    if (!host) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }

    for (uint32_t i = 0; i < n; ++i) {
        uint32_t base = off + i * YOS_PY_METHODDEF_WASM_SIZE;
        uint32_t name_off = *(const uint32_t *)(ctx->memory + base);
        uint32_t meth_idx = *(const uint32_t *)(ctx->memory + base + 4);
        int      flags    = *(const int32_t  *)(ctx->memory + base + 8);
        uint32_t doc_off  = *(const uint32_t *)(ctx->memory + base + 12);

        const char *name = guest_cstr_py(ctx, name_off);
        const char *doc  = doc_off ? guest_cstr_py(ctx, doc_off) : NULL;
        host[i].ml_name  = name ? strdup(name) : NULL;
        host[i].ml_doc   = doc  ? strdup(doc)  : NULL;
        host[i].ml_flags = flags;
        if (meth_idx != 0) {
            int slot = py_alloc_tramp(ctx, meth_idx, flags);
            if (slot < 0) {
                /* trampoline pool exhausted — slot stays NULL,
                 * Python sees a nameless function and skips it. */
                ywarn("PyModule_AddFunctions: trampoline pool full "
                      "(YOS_PY_NTRAMPS=%d); function '%s' won't be "
                      "callable\n", YOS_PY_NTRAMPS, name ? name : "?");
                host[i].ml_meth = NULL;
            } else {
                host[i].ml_meth = g_py_tramp_fns[slot];
            }
        } else {
            host[i].ml_meth = NULL;
        }
    }
    /* host[n] stays zeroed → PyMethodDef sentinel. */

    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyModule_AddFunctions(m, host);
    py_swap_out(prev);

    /* Note: `host` array intentionally leaked. CPython does NOT take
     * ownership but it indexes into it for the lifetime of the
     * module; freeing here would invalidate every registered
     * function. */
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyModule_AddObject — i32(module_h, name_off, value_h).
 * STEALS the value reference on success — we release the handle
 * slot but DON'T Py_DecRef. */
static const void *m3_yos_PyModule_AddObject(IM3Runtime rt, IM3ImportContext _c,
                                             uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *m   = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *name = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    uint32_t h_v  = (uint32_t)_sp[3];
    PyObject *v   = py_handles_release(ctx, h_v);
    if (!m || !name || !v) {
        _sp[0] = (uint64_t)(uint32_t)-1; return NULL;
    }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyModule_AddObject(m, name, v);
    if (rc != 0) Py_DecRef(v);   /* on failure caller still owns; we released the slot */
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyModule_AddIntConstant — i32(module_h, name_off, i64 value). */
static const void *m3_yos_PyModule_AddIntConstant(IM3Runtime rt, IM3ImportContext _c,
                                                  uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *m = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *name = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    long v = (long)(int64_t)_sp[3];
    if (!m || !name) { _sp[0] = (uint64_t)(uint32_t)-1; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyModule_AddIntConstant(m, name, v);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* env.PyModule_AddStringConstant — i32(module_h, name_off, value_off). */
static const void *m3_yos_PyModule_AddStringConstant(IM3Runtime rt, IM3ImportContext _c,
                                                     uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *m = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    const char *name  = guest_cstr_py(ctx, (uint32_t)_sp[2]);
    const char *value = guest_cstr_py(ctx, (uint32_t)_sp[3]);
    if (!m || !name || !value) {
        _sp[0] = (uint64_t)(uint32_t)-1; return NULL;
    }
    PyThreadState *prev = py_swap_in(ctx);
    int rc = PyModule_AddStringConstant(m, name, value);
    py_swap_out(prev);
    _sp[0] = (uint64_t)(uint32_t)rc;
    return NULL;
}

/* ── PyBytes ──────────────────────────────────────────────────── */

extern PyObject  *PyBytes_FromStringAndSize(const char *v, Py_ssize_t len);
extern int        PyBytes_AsStringAndSize(PyObject *obj, char **s, Py_ssize_t *len);

/* env.PyBytes_FromStringAndSize — i32(buf_off, int size). New ref. */
static const void *m3_yos_PyBytes_FromStringAndSize(IM3Runtime rt, IM3ImportContext _c,
                                                    uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    uint32_t off = (uint32_t)_sp[1];
    int      sz  = (int)_sp[2];
    const char *s = sz ? (const char *)guest_buf_ro_py(ctx, off, (uint32_t)sz) : "";
    if (sz > 0 && !s) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    PyObject *r = PyBytes_FromStringAndSize(s, (Py_ssize_t)sz);
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* env.PyBytes_AsStringAndSize — i32(obj_h, size_off). Returns offset
 * of a stashed copy of the bytes (since the host pointer points into
 * Python-managed storage we can't expose directly). */
static const void *m3_yos_PyBytes_AsStringAndSize(IM3Runtime rt, IM3ImportContext _c,
                                                  uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    PyObject *o = py_handles_resolve(ctx, (uint32_t)_sp[1]);
    uint32_t size_off = (uint32_t)_sp[2];
    if (!o) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    char *s = NULL;
    Py_ssize_t sz = 0;
    int rc = PyBytes_AsStringAndSize(o, &s, &sz);
    py_swap_out(prev);
    if (rc != 0 || !s) { _sp[0] = 0; return NULL; }
    if (size_off) {
        uint32_t *lp = (uint32_t *)guest_buf_rw_py(ctx, size_off, sizeof(uint32_t));
        if (lp) *lp = (uint32_t)sz;
    }
    _sp[0] = (uint64_t)libpython_stash_string(ctx, s, (size_t)sz);
    return NULL;
}

/* env.PyRun_String — i32(str_off, int start, globals_h, locals_h).
 * Py_eval_input = 258, Py_file_input = 257, Py_single_input = 256.
 * New ref result. */
static const void *m3_yos_PyRun_String(IM3Runtime rt, IM3ImportContext _c,
                                       uint64_t *_sp, void *_m)
{
    (void)_c; (void)_m;
    struct yos_exec_ctx *ctx = PY_CTX(rt);
    const char *s = guest_cstr_py(ctx, (uint32_t)_sp[1]);
    int start = (int)_sp[2];
    PyObject *g = (uint32_t)_sp[3] ? py_handles_resolve(ctx, (uint32_t)_sp[3]) : NULL;
    PyObject *l = (uint32_t)_sp[4] ? py_handles_resolve(ctx, (uint32_t)_sp[4]) : NULL;
    if (!s) { _sp[0] = 0; return NULL; }
    PyThreadState *prev = py_swap_in(ctx);
    /* If globals/locals not provided, run inside __main__ dict. */
    if (!g) {
        PyObject *m = PyImport_AddModule("__main__");
        g = m ? PyModule_GetDict(m) : NULL;
    }
    if (!l) l = g;
    PyObject *r = (g && l) ? PyRun_String(s, start, g, l) : NULL;
    py_swap_out(prev);
    _sp[0] = PY_WRAP_NEW(ctx, r);
    return NULL;
}

/* Public entry point — called from main.c during import linkage. */
void yos_libpython_link(IM3Module mod)
{
    if (!mod) return;
    /* The original three lifecycle bridges. */
    m3_LinkRawFunction(mod, "env", "Py_Initialize",    "v()",  m3_yos_Py_Initialize);
    m3_LinkRawFunction(mod, "env", "Py_Finalize",      "v()",  m3_yos_Py_Finalize);
    m3_LinkRawFunction(mod, "env", "PyRun_SimpleString","i(i)", m3_yos_PyRun_SimpleString);

    /* Refcount control. */
    m3_LinkRawFunction(mod, "env", "Py_IncRef", "v(i)", m3_yos_Py_IncRef);
    m3_LinkRawFunction(mod, "env", "Py_DecRef", "v(i)", m3_yos_Py_DecRef);

    /* PyObject — attr / type / call / str / len. */
    m3_LinkRawFunction(mod, "env", "PyObject_GetAttrString", "i(ii)",  m3_yos_PyObject_GetAttrString);
    m3_LinkRawFunction(mod, "env", "PyObject_SetAttrString", "i(iii)", m3_yos_PyObject_SetAttrString);
    m3_LinkRawFunction(mod, "env", "PyObject_HasAttrString", "i(ii)",  m3_yos_PyObject_HasAttrString);
    m3_LinkRawFunction(mod, "env", "PyObject_Str",           "i(i)",   m3_yos_PyObject_Str);
    m3_LinkRawFunction(mod, "env", "PyObject_Repr",          "i(i)",   m3_yos_PyObject_Repr);
    m3_LinkRawFunction(mod, "env", "PyObject_Length",        "i(i)",   m3_yos_PyObject_Length);
    m3_LinkRawFunction(mod, "env", "PyObject_IsTrue",        "i(i)",   m3_yos_PyObject_IsTrue);
    m3_LinkRawFunction(mod, "env", "PyObject_Type",          "i(i)",   m3_yos_PyObject_Type);
    m3_LinkRawFunction(mod, "env", "PyObject_Call",          "i(iii)", m3_yos_PyObject_Call);
    m3_LinkRawFunction(mod, "env", "PyObject_CallObject",    "i(ii)",  m3_yos_PyObject_CallObject);

    /* Dict / List / Tuple. */
    m3_LinkRawFunction(mod, "env", "PyDict_New",            "i()",     m3_yos_PyDict_New);
    m3_LinkRawFunction(mod, "env", "PyDict_SetItemString",  "i(iii)",  m3_yos_PyDict_SetItemString);
    m3_LinkRawFunction(mod, "env", "PyDict_GetItemString",  "i(ii)",   m3_yos_PyDict_GetItemString);
    m3_LinkRawFunction(mod, "env", "PyDict_Size",           "i(i)",    m3_yos_PyDict_Size);
    m3_LinkRawFunction(mod, "env", "PyList_New",            "i(i)",    m3_yos_PyList_New);
    m3_LinkRawFunction(mod, "env", "PyList_Append",         "i(ii)",   m3_yos_PyList_Append);
    m3_LinkRawFunction(mod, "env", "PyList_Size",           "i(i)",    m3_yos_PyList_Size);
    m3_LinkRawFunction(mod, "env", "PyList_GetItem",        "i(ii)",   m3_yos_PyList_GetItem);
    m3_LinkRawFunction(mod, "env", "PyList_SetItem",        "i(iii)",  m3_yos_PyList_SetItem);
    m3_LinkRawFunction(mod, "env", "PyTuple_New",           "i(i)",    m3_yos_PyTuple_New);
    m3_LinkRawFunction(mod, "env", "PyTuple_Size",          "i(i)",    m3_yos_PyTuple_Size);
    m3_LinkRawFunction(mod, "env", "PyTuple_GetItem",       "i(ii)",   m3_yos_PyTuple_GetItem);
    m3_LinkRawFunction(mod, "env", "PyTuple_SetItem",       "i(iii)",  m3_yos_PyTuple_SetItem);

    /* Scalars + strings. */
    m3_LinkRawFunction(mod, "env", "PyLong_FromLongLong",          "i(I)",   m3_yos_PyLong_FromLongLong);
    m3_LinkRawFunction(mod, "env", "PyLong_AsLongLong",            "I(i)",   m3_yos_PyLong_AsLongLong);
    m3_LinkRawFunction(mod, "env", "PyFloat_FromDouble",           "i(F)",   m3_yos_PyFloat_FromDouble);
    m3_LinkRawFunction(mod, "env", "PyFloat_AsDouble",             "F(i)",   m3_yos_PyFloat_AsDouble);
    m3_LinkRawFunction(mod, "env", "PyBool_FromLong",              "i(i)",   m3_yos_PyBool_FromLong);
    m3_LinkRawFunction(mod, "env", "PyUnicode_FromString",         "i(i)",   m3_yos_PyUnicode_FromString);
    m3_LinkRawFunction(mod, "env", "PyUnicode_FromStringAndSize",  "i(ii)",  m3_yos_PyUnicode_FromStringAndSize);
    m3_LinkRawFunction(mod, "env", "PyUnicode_AsUTF8AndSize",      "i(ii)",  m3_yos_PyUnicode_AsUTF8AndSize);

    /* Import / module. */
    m3_LinkRawFunction(mod, "env", "PyImport_ImportModule", "i(i)", m3_yos_PyImport_ImportModule);
    m3_LinkRawFunction(mod, "env", "PyImport_AddModule",    "i(i)", m3_yos_PyImport_AddModule);
    m3_LinkRawFunction(mod, "env", "PyModule_GetDict",      "i(i)", m3_yos_PyModule_GetDict);

    /* Error. */
    m3_LinkRawFunction(mod, "env", "PyErr_Occurred",          "i()",   m3_yos_PyErr_Occurred);
    m3_LinkRawFunction(mod, "env", "PyErr_Clear",             "v()",   m3_yos_PyErr_Clear);
    m3_LinkRawFunction(mod, "env", "PyErr_Print",             "v()",   m3_yos_PyErr_Print);
    m3_LinkRawFunction(mod, "env", "PyErr_SetString",         "v(ii)", m3_yos_PyErr_SetString);
    m3_LinkRawFunction(mod, "env", "PyErr_ExceptionMatches",  "i(i)",  m3_yos_PyErr_ExceptionMatches);

    /* Eval. */
    m3_LinkRawFunction(mod, "env", "PyRun_String", "i(iiii)", m3_yos_PyRun_String);

    /* Module-level registration helpers + PyCFunction trampoline path. */
    m3_LinkRawFunction(mod, "env", "PyModule_AddFunctions",     "i(ii)",  m3_yos_PyModule_AddFunctions);
    m3_LinkRawFunction(mod, "env", "PyModule_AddObject",        "i(iii)", m3_yos_PyModule_AddObject);
    m3_LinkRawFunction(mod, "env", "PyModule_AddIntConstant",   "i(iiI)", m3_yos_PyModule_AddIntConstant);
    m3_LinkRawFunction(mod, "env", "PyModule_AddStringConstant","i(iii)", m3_yos_PyModule_AddStringConstant);

    /* PyBytes. */
    m3_LinkRawFunction(mod, "env", "PyBytes_FromStringAndSize", "i(ii)",  m3_yos_PyBytes_FromStringAndSize);
    m3_LinkRawFunction(mod, "env", "PyBytes_AsStringAndSize",   "i(ii)",  m3_yos_PyBytes_AsStringAndSize);
}

/* Per-ctx teardown — called from yos's proc shutdown. Releases all
 * still-live PyObject handles for this guest. Py_DecRef them under
 * the guest's tstate so the per-subinterpreter GC sees them. */
void yos_libpython_ctx_free(struct yos_exec_ctx *ctx)
{
    if (!ctx || !ctx->py_handles) return;
    PyThreadState *prev = py_swap_in(ctx);
    for (uint32_t i = YOS_PY_HANDLE_FIRST_FREE; i < ctx->py_handles_cap; ++i) {
        PyObject *o = (PyObject *)ctx->py_handles[i];
        if (o) Py_DecRef(o);
        ctx->py_handles[i] = NULL;
    }
    py_swap_out(prev);
    free(ctx->py_handles);
    ctx->py_handles = NULL;
    ctx->py_handles_cap = 0;
}
