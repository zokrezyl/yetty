#ifndef YETTY_YCORE_RESULT_H
#define YETTY_YCORE_RESULT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error info — head of a heap-linked chain. The top-of-chain error lives by
 * value inside a Result; deeper levels (the `cause` chain) are heap-allocated.
 *
 * Location: `file`, `line`, `func` are captured at the YETTY_ERR call site
 * via __FILE__ / __LINE__ / __func__ — string literals from the binary's
 * read-only segment, never freed. Every level of a chain carries its own
 * call-site coords so the printed trail shows where each frame raised /
 * wrapped the error.
 *
 * Ownership: when a callee returns a Result with an error, the immediate
 * caller owns that error's `cause` chain. Two ways to discharge ownership:
 *   1) Wrap and forward: YETTY_ERR(type, "outer", inner_res) transfers the
 *      cause chain into the new error.
 *   2) Drop: yetty_ycore_error_destroy(inner_res.error) frees the chain.
 */
struct yetty_ycore_error {
    const char *msg;
    const char *file;
    const char *func;
    int line;
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
YETTY_YRESULT_DECLARE(yetty_ycore_float, float);
YETTY_YRESULT_DECLARE(yetty_ycore_uint32, uint32_t);
YETTY_YRESULT_DECLARE(yetty_ycore_uint64, uint64_t);
/* Owned heap string (caller frees value). Used by figure dump_state. */
YETTY_YRESULT_DECLARE(yetty_ycore_char_ptr, char *);
/* Owned heap bytes (caller frees value). Used by decompressors that hand
 * back a malloc'd buffer. */
YETTY_YRESULT_DECLARE(yetty_ycore_uint8_ptr, uint8_t *);
/* Borrowed strings/bytes (caller does NOT free). Used by ygui value accessors
 * that return a pointer into the widget's own state. */
YETTY_YRESULT_DECLARE(yetty_ycore_const_char_ptr, const char *);
YETTY_YRESULT_DECLARE(yetty_ycore_const_uint8_ptr, const uint8_t *);
/* Borrowed word span (caller does NOT free). Used by accessors that return a
 * pointer into an object's own backing array. */
YETTY_YRESULT_DECLARE(yetty_ycore_const_uint32_ptr, const uint32_t *);

/* Helper for chaining: heap-copies `prev` so the new error owns its chain.
 * Returns NULL on alloc failure (chain is silently truncated — we're already
 * in an error path). Callers should not invoke directly; use YETTY_ERR. */
struct yetty_ycore_error *yetty_ycore_error_chain(struct yetty_ycore_error prev);

/* Walk the cause chain and free every node. The top-of-chain error itself
 * lives by value inside the caller's Result, so this only frees `err.cause`
 * onwards — not `err`. NULL-cause-safe. */
void yetty_ycore_error_destroy(struct yetty_ycore_error err);

/* Fold one best-effort cleanup step into a running teardown chain WITHOUT
 * swallowing anything. Use it to accumulate the failures of a multi-step
 * destroy/resize where every step must run:
 *
 *   struct yetty_ycore_void_result teardown = YETTY_OK_VOID();
 *   teardown = yetty_ycore_void_chain(teardown, step_a());
 *   teardown = yetty_ycore_void_chain(teardown, step_b());
 *   ... surface `teardown` at the end (the root consumer destroys the chain).
 *
 * Semantics: an OK `step` returns `chain` unchanged; the first failure is
 * returned verbatim (chain intact); a later failure becomes the new head with
 * the accumulated `chain` linked beneath as its cause (the latest step's own
 * deeper cause is freed so it is not leaked). */
struct yetty_ycore_void_result yetty_ycore_void_chain(struct yetty_ycore_void_result chain,
                                                      struct yetty_ycore_void_result step);

/* Print an error and every link in its cause chain to `out`. Each frame
 * shows the file:line:func captured at its YETTY_ERR call site.
 * The first frame is labelled with `headline` (e.g. "fatal error" /
 * "warning") followed by `: `. Pass NULL for `headline` to omit the
 * label — the caller's own framing prefix is used as-is. */
void yetty_ycore_error_print(FILE *out, const char *headline, struct yetty_ycore_error err);

/* Format the error and its cause chain into `buf`. Same content as
 * yetty_ycore_error_print but written into a caller-supplied buffer, and
 * with no trailing newline. Truncates safely if the chain would overflow.
 * Returns the number of bytes written (excluding the terminating NUL). */
size_t yetty_ycore_error_snprint(char *buf, size_t bufsize, struct yetty_ycore_error err);

/* Serialize an error and its full cause chain into `buf` for transport over a
 * wire (used by the yclass RPC layer to carry a remote impl's error back to the
 * caller). Layout, all little-endian, frames in order (top frame first):
 *
 *   u32 frame_count                    (top frame + every cause, capped)
 *   repeat frame_count times:
 *     u32 msg_len ; msg bytes          (string contents only, no NUL)
 *     u32 file_len; file bytes
 *     u32 func_len; func bytes
 *     i32 line
 *
 * Returns the number of bytes written, or 0 if `buf` cannot hold the whole
 * chain (the caller is then free to fall back to a status-only response — a
 * lossy but non-fatal degradation). Does not allocate. */
size_t yetty_ycore_error_serialize(struct yetty_ycore_error err, uint8_t *buf, size_t bufsize);

/* Rebuild a heap cause-chain from bytes produced by yetty_ycore_error_serialize.
 * Returns the head node of the chain (suitable to attach as the `.cause` of a
 * locally-raised error), or NULL on empty / short / malformed input — a partial
 * parse is rolled back and dropped, never half-returned.
 *
 * Ownership: every node is a SINGLE allocation that also stores its msg/file/
 * func bytes inline, so yetty_ycore_error_destroy (which frees each cause node)
 * frees the strings too — no separate free of the string fields is needed, and
 * the chain follows the same lifetime rules as any locally-built one. */
struct yetty_ycore_error *yetty_ycore_error_deserialize(const uint8_t *buf, size_t len);

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
 * `yetty_ycore_void`, `yetty_yrich_cell_ptr`, `yetty_yterminal_layer`.
 */
#define YETTY_ERR(...) YETTY_ERR_DISPATCH(__VA_ARGS__, YETTY_ERR_3, YETTY_ERR_2)(__VA_ARGS__)
#define YETTY_ERR_DISPATCH(_1, _2, _3, NAME, ...) NAME

#define YETTY_ERR_2(type, err_msg)                                                                 \
    ((struct type##_result){.ok = 0,                                                               \
                            .error = {.msg = (err_msg),                                            \
                                      .file = __FILE__,                                            \
                                      .func = __func__,                                            \
                                      .line = __LINE__,                                            \
                                      .cause = NULL}})

#define YETTY_ERR_3(type, err_msg, prev_res)                                                       \
    ((struct type##_result){.ok = 0,                                                               \
                            .error = {.msg = (err_msg),                                            \
                                      .file = __FILE__,                                            \
                                      .func = __func__,                                            \
                                      .line = __LINE__,                                            \
                                      .cause = yetty_ycore_error_chain((prev_res).error)}})

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
 * `res` is evaluated TWICE — pass a local variable, NOT a call expression.
 * The macro can't capture `res` into a single local: `type` is the CURRENT
 * function's result-type id, which is usually DIFFERENT from `res`'s type
 * (that's the point — it re-wraps an upstream error into this function's
 * type; e.g. `YETTY_RETURN_IF_ERR(yetty_ycore_void, some_ptr_r, …)`), so a
 * `struct type##_result` temp can't hold `res`; and `__typeof__(res)` is not
 * portable to MSVC. Double-evaluating a plain variable is free, so the
 * contract is simply: assign the call to a variable, then pass the variable.
 */
#define YETTY_RETURN_IF_ERR(type, res, msg)                                                        \
    do {                                                                                           \
        if (YETTY_IS_ERR(res)) {                                                                   \
            return YETTY_ERR(type, msg, (res));                                                    \
        }                                                                                          \
    } while (0)

#ifdef __cplusplus
}

/* C++ helper — compound literals are a C-only feature.
 * Callers should use yetty_ycore_err(msg) — the macro fills file/line/func
 * from the call site. */
#define yetty_ycore_err(msg) yetty_ycore_err_at((msg), __FILE__, __func__, __LINE__)

inline struct yetty_ycore_void_result yetty_ycore_err_at(const char *msg, const char *file,
                                                         const char *func, int line)
{
    struct yetty_ycore_void_result r = {};
    r.ok = 0;
    r.error.msg = msg;
    r.error.file = file;
    r.error.func = func;
    r.error.line = line;
    r.error.cause = nullptr;
    return r;
}
#endif

#endif /* YETTY_YCORE_RESULT_H */
