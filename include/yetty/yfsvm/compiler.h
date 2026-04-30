#ifndef YETTY_YFSVM_COMPILER_H
#define YETTY_YFSVM_COMPILER_H

/*
 * yfsvm compiler - Compiles yexpr AST to yfsvm bytecode
 *
 * Takes an AST from yexpr parser and generates bytecode that
 * runs in the fragment shader virtual machine.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/yfsvm/yfsvm.gen.h>
#include <yetty/yexpr/yexpr.h>
#include <yetty/yrender/gpu-resource-set.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * Program - compiled bytecode for one or more functions
 *===========================================================================*/

struct yetty_yfsvm_function {
    uint32_t code_offset;
    uint32_t code_length;
};

struct yetty_yfsvm_program {
    float constants[YFSVM_MAX_CONSTANTS];
    uint32_t constant_count;

    uint32_t code[YFSVM_MAX_INSTRUCTIONS];
    uint32_t code_count;

    struct yetty_yfsvm_function functions[YFSVM_MAX_FUNCTIONS];
    uint32_t function_count;

    int uses_x;
    int uses_time;
};

YETTY_YRESULT_DECLARE(yetty_yfsvm_program, struct yetty_yfsvm_program);

/*=============================================================================
 * API
 *===========================================================================*/

/* Compile a single expression AST to a program with one function */
struct yetty_yfsvm_program_result yetty_yfsvm_compile(const struct yetty_yexpr_node *ast);

/* Compile a single expression string */
struct yetty_yfsvm_program_result yetty_yfsvm_compile_expr(const char *source, size_t len);

/* Compile multi-plot AST (multiple functions) */
struct yetty_yfsvm_program_result yetty_yfsvm_compile_multi(
    const struct yetty_yexpr_plot_expr *plot);

/* Compile multi-plot expression string */
struct yetty_yfsvm_program_result yetty_yfsvm_compile_multi_expr(const char *source, size_t len);

/* Serialize program to buffer for GPU upload.
 * Returns number of uint32_t words written, or 0 on error.
 * Layout: [magic][version][func_count][const_count][func_table...][constants...][code...]
 */
uint32_t yetty_yfsvm_program_serialize(const struct yetty_yfsvm_program *prog, uint32_t *buf,
                                       uint32_t buf_capacity);

/* Get static shader-only resource set (for ypaint layer to include yfsvm_execute) */
const struct yetty_ypaint_core_gpu_resource_set *yetty_yfsvm_get_shader_resource_set(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFSVM_COMPILER_H */
