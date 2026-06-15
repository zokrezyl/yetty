#ifndef YETTY_YFSPY_COMPILE_H
#define YETTY_YFSPY_COMPILE_H

/*
 * yfspy compiler — Python-subset shader source -> yfsvm bytecode, in-process.
 *
 * Uses the embedded libpython-free CPython parser (src/cpython, exposed as
 * yetty_cpython) to turn the source into an AST, then lowers the supported
 * subset to a yfsvm program (the same struct the yexpr path produces, so it
 * serializes and validates identically). No subprocess, no CPython runtime —
 * works on every target the rest of yetty builds for.
 *
 * The accepted language matches the reference Python frontend documented in
 * tools/yfspy/assets/README.md (def functions, scalar locals, arithmetic,
 * comparisons, and/or/not, if/elif/else, while, for-range, ternary, the
 * builtin + macro set). Unsupported constructs are rejected with a line
 * number rather than miscompiled.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/yfsvm/compiler.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_yfspy_status {
    YETTY_YFSPY_OK = 0,
    YETTY_YFSPY_PARSE_ERROR = 1,   /* the parser rejected the source */
    YETTY_YFSPY_COMPILE_ERROR = 2, /* parsed, but unsupported / invalid for the VM */
    YETTY_YFSPY_OOM = 3,
};

struct yetty_yfspy_program {
    struct yetty_yfsvm_program program;
    uint32_t function_count;
    int uses_x;
    int uses_y;
    int uses_time;
};

/* Compile `source` (a complete module) to a yfsvm program in `*out`.
 *
 * Returns YETTY_YFSPY_OK on success. On failure returns the matching status
 * and writes a human-readable message (with a line number where available)
 * into `errbuf` (truncated to `errcap`). `filename` is used only in parser
 * error messages and may be NULL.
 *
 * The produced program is already validated (yetty_yfsvm_program_validate). */
int yetty_yfspy_compile(const char *source, const char *filename, struct yetty_yfspy_program *out,
                        char *errbuf, size_t errcap);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFSPY_COMPILE_H */
