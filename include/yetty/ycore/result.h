#ifndef YETTY_YCORE_RESULT_H
#define YETTY_YCORE_RESULT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error info — head of a heap-linked chain. The top-of-chain error lives by
 * value inside a Result; deeper levels (the `cause` chain) are heap-allocated.
 *
 * Ownership: when a callee returns a Result with an error, the immediate
 * caller owns that error's `cause` chain. Two ways to discharge ownership:
 *   1) Wrap and forward: YETTY_ERR(type, "outer", inner_res) transfers the
 *      cause chain into the new error.
 *   2) Drop: yetty_ycore_error_destroy(inner_res.error) frees the chain.
 *
 * Future: more fields (file, line, position, code) — append below `msg`.
 */
struct yetty_ycore_error {
    const char *msg;
    struct yetty_ycore_error *cause; /* heap-allocated; NULL = end of chain */
};

/* Generate a result type: struct <type>_result.
 * `type` is the result type identifier (no `struct` keyword, no `_result`
 * suffix); the macro pastes `_result` onto it. `value_type` is the actual C
 * type held on the success side (e.g. int, struct foo *). */
#define YETTY_YRESULT_DECLARE(type, value_type)                                                    \
    struct type##_result {                                                                         \
        int ok;                                                                                    \
        union {                                                                                    \
            value_type value;                                                                      \
            struct yetty_ycore_error error;                                                        \
        };                                                                                         \
    }

/* Common result types in core namespace */
YETTY_YRESULT_DECLARE(yetty_ycore_void, int);
YETTY_YRESULT_DECLARE(yetty_ycore_int, int);
YETTY_YRESULT_DECLARE(yetty_ycore_size, size_t);

/* Helper for chaining: heap-copies `prev` so the new error owns its chain.
 * Returns NULL on alloc failure (chain is silently truncated — we're already
 * in an error path). Callers should not invoke directly; use YETTY_ERR. */
struct yetty_ycore_error *yetty_ycore_error_chain(struct yetty_ycore_error prev);

/* Walk the cause chain and free every node. The top-of-chain error itself
 * lives by value inside the caller's Result, so this only frees `err.cause`
 * onwards — not `err`. NULL-cause-safe. */
void yetty_ycore_error_destroy(struct yetty_ycore_error err);

/* Create success result (void) */
#define YETTY_OK_VOID() ((struct yetty_ycore_void_result){.ok = 1, .value = 0})

/* Create success result with value */
#define YETTY_OK(type, val) ((struct type##_result){.ok = 1, .value = (val)})

/*
 * YETTY_ERR — build an error Result. Variadic: 2 or 3 args.
 *
 *   YETTY_ERR(type, msg)              — root error, no upstream cause.
 *   YETTY_ERR(type, msg, prev_res)    — wraps `prev_res` (any *_result struct
 *                                       value); transfers ownership of its
 *                                       cause chain. Pass the WHOLE result,
 *                                       not its `.error` field.
 *
 * `type` is the result type identifier you registered with
 * YETTY_YRESULT_DECLARE — the part WITHOUT the `_result` suffix. Examples:
 * `yetty_ycore_void`, `yetty_yrich_cell_ptr`, `yetty_yterm_terminal_layer`.
 */
#define YETTY_ERR(...) YETTY_ERR_DISPATCH(__VA_ARGS__, YETTY_ERR_3, YETTY_ERR_2)(__VA_ARGS__)
#define YETTY_ERR_DISPATCH(_1, _2, _3, NAME, ...) NAME

#define YETTY_ERR_2(type, err_msg)                                                                 \
    ((struct type##_result){.ok = 0, .error = {.msg = (err_msg), .cause = NULL}})

#define YETTY_ERR_3(type, err_msg, prev_res)                                                       \
    ((struct type##_result){                                                                       \
        .ok = 0,                                                                                   \
        .error = {.msg = (err_msg), .cause = yetty_ycore_error_chain((prev_res).error)}})

/* Check result */
#define YETTY_IS_OK(res) ((res).ok)
#define YETTY_IS_ERR(res) (!(res).ok)

/*
 * YETTY_EXTERNAL_CALLBACK — mark a function whose signature is dictated by an
 * external library (libuv `uv_*_cb`, pthread start routine, emscripten main
 * loop, Android NDK `android_app` callbacks, pdfio paint callbacks, etc.) and
 * therefore cannot return a Result. The result-checker skips functions with
 * this annotation. Use sparingly — every annotated function is a place where
 * Result errors must be absorbed at the boundary.
 */
#if defined(__clang__) || defined(__GNUC__)
#define YETTY_EXTERNAL_CALLBACK __attribute__((annotate("yetty_external_callback")))
#else
#define YETTY_EXTERNAL_CALLBACK
#endif

/*
 * YETTY_RETURN_IF_ERR — propagate-with-context shorthand.
 *
 * If `res` holds an error, returns from the current function with a new
 * error of type `<type>_result` whose msg is `msg` and whose cause is the
 * transferred chain from `res`.
 *
 * `type` is the result type identifier of the CURRENT function (the part
 * without `_result`). `res` is the WHOLE result struct, not its `.error`.
 *
 * Usage:
 *   struct yetty_yrich_cell_ptr_result cr = ...ensure_cell(s, addr);
 *   YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "set_cell_value: ensure failed");
 *
 * Note: `res` is evaluated twice. Pass a local variable, not a call expression.
 */
#define YETTY_RETURN_IF_ERR(type, res, msg)                                                        \
    do {                                                                                           \
        if (YETTY_IS_ERR(res)) {                                                                   \
            return YETTY_ERR(type, msg, (res));                                                    \
        }                                                                                          \
    } while (0)

#ifdef __cplusplus
}

/* C++ helper — compound literals are a C-only feature */
inline struct yetty_ycore_void_result yetty_cpp_err(const char *msg)
{
    struct yetty_ycore_void_result r = {};
    r.ok = 0;
    r.error.msg = msg;
    r.error.cause = nullptr;
    return r;
}
#endif

#endif /* YETTY_YCORE_RESULT_H */
