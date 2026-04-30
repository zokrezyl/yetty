// Auto-generated from yplot.yaml - DO NOT EDIT
// YAML parser factory for yplot complex primitive

#include <yetty/yplot/yplot-gen.h>
#include <yetty/ypaint-yaml/ypaint-yaml.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/ytrace.h>
#include <yaml.h>
#include <stdlib.h>
#include <string.h>

#define YPLOT_MAX_FUNCTIONS 8

static int yplot_hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static uint32_t yplot_parse_color(const char *s)
{
    if (!s) {
        return 0;
    }
    if (*s == '#') {
        s++;
    }
    size_t len = 0;
    const char *p = s;
    while (*p && yplot_hex_digit(*p) >= 0) {
        len++;
        p++;
    }
    if (len != 6 && len != 8) {
        return 0;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        v = (v << 4) | (uint32_t)yplot_hex_digit(s[i]);
    }
    return (len == 6) ? (0xFF000000 | v) : v;
}

static const uint32_t YPLOT_COLOR_PALETTE[8] = {
    0xFFFF6B6B, 0xFF4ECDC4, 0xFFFFE66D, 0xFF95E1D3, 0xFFF38181, 0xFFAA96DA, 0xFF72D6C9, 0xFFFCBF49,
};

static struct yetty_ycore_void_result yplot_yaml_factory(struct yetty_ypaint_core_buffer *buffer,
                                                         yaml_parser_t *yaml_parser,
                                                         const char *primitive_type_name)
{
    (void)primitive_type_name;

    struct yetty_yplot_uniforms uniforms = {0};
    uniforms.bounds_x = 0.0f;
    uniforms.bounds_y = 0.0f;
    uniforms.bounds_w = 300.0f;
    uniforms.bounds_h = 200.0f;
    uniforms.x_min = -3.14159f;
    uniforms.x_max = 3.14159f;
    uniforms.y_min = -1.5f;
    uniforms.y_max = 1.5f;
    uniforms.flags = 7;
    uniforms.function_count = 0;

    char exprs[YPLOT_MAX_FUNCTIONS][256] = {{0}};
    int func_count = 0;

    char prop_key[64] = {0};
    int expect_value = 0;
    int in_array = 0, in_functions = 0, in_func_item = 0;
    int array_idx = 0;
    float array_vals[8] = {0};

    yaml_event_t event;
    int depth = 0, done = 0;

    while (!done) {
        if (!yaml_parser_parse(yaml_parser, &event)) {
            return YETTY_ERR(yetty_ycore_void, "yaml parse error");
        }

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            depth++;
            if (in_functions && !in_func_item) {
                in_func_item = 1;
                expect_value = 0;
                if (func_count < YPLOT_MAX_FUNCTIONS) {
                    uniforms.colors[func_count] = YPLOT_COLOR_PALETTE[func_count % 8];
                }
            }
            break;
        case YAML_MAPPING_END_EVENT:
            depth--;
            if (in_func_item) {
                in_func_item = 0;
                if (func_count < YPLOT_MAX_FUNCTIONS) {
                    func_count++;
                }
            }
            if (depth == 0) {
                done = 1;
            }
            break;
        case YAML_SEQUENCE_START_EVENT:
            if (strcmp(prop_key, "functions") == 0) {
                in_functions = 1;
            } else {
                in_array = 1;
                array_idx = 0;
            }
            break;
        case YAML_SEQUENCE_END_EVENT:
            if (in_functions) {
                in_functions = 0;
            } else if (in_array) {
                if (strcmp(prop_key, "position") == 0 && array_idx >= 2) {
                    uniforms.bounds_x = array_vals[0], uniforms.bounds_y = array_vals[1];
                } else if (strcmp(prop_key, "size") == 0 && array_idx >= 2) {
                    uniforms.bounds_w = array_vals[0], uniforms.bounds_h = array_vals[1];
                } else if (strcmp(prop_key, "x_range") == 0 && array_idx >= 2) {
                    uniforms.x_min = array_vals[0], uniforms.x_max = array_vals[1];
                } else if (strcmp(prop_key, "y_range") == 0 && array_idx >= 2) {
                    uniforms.y_min = array_vals[0], uniforms.y_max = array_vals[1];
                }
                in_array = 0;
            }
            expect_value = 0;
            break;
        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;
            if (in_array) {
                if (array_idx < 8) {
                    array_vals[array_idx++] = strtof(val, NULL);
                }
            } else if (in_func_item) {
                if (!expect_value) {
                    strncpy(prop_key, val, sizeof(prop_key) - 1);
                    expect_value = 1;
                } else {
                    if (strcmp(prop_key, "expr") == 0 && func_count < YPLOT_MAX_FUNCTIONS) {
                        strncpy(exprs[func_count], val, 255);
                    } else if (strcmp(prop_key, "color") == 0 && func_count < YPLOT_MAX_FUNCTIONS) {
                        uniforms.colors[func_count] = yplot_parse_color(val);
                    }
                    expect_value = 0;
                }
            } else if (!expect_value) {
                strncpy(prop_key, val, sizeof(prop_key) - 1);
                expect_value = 1;
            } else {
                if (strcmp(prop_key, "show_grid") == 0) {
                    uniforms.flags =
                        (uniforms.flags & ~0x01) | ((strcmp(val, "true") == 0) ? 0x01 : 0);
                } else if (strcmp(prop_key, "show_axes") == 0) {
                    uniforms.flags =
                        (uniforms.flags & ~0x02) | ((strcmp(val, "true") == 0) ? 0x02 : 0);
                } else if (strcmp(prop_key, "show_labels") == 0) {
                    uniforms.flags =
                        (uniforms.flags & ~0x04) | ((strcmp(val, "true") == 0) ? 0x04 : 0);
                }
                expect_value = 0;
            }
            break;
        }
        default:
            break;
        }
        yaml_event_delete(&event);
    }

    uniforms.function_count = (uint32_t)func_count;

    uint32_t bc_buf[1024] = {0};
    size_t bc_count = 0;

    if (func_count > 0) {
        char multi_expr[2048] = {0};
        size_t off = 0;
        for (int i = 0; i < func_count; i++) {
            if (i > 0 && off < sizeof(multi_expr) - 2) {
                multi_expr[off++] = ';';
                multi_expr[off++] = ' ';
            }
            size_t len = strlen(exprs[i]);
            if (off + len < sizeof(multi_expr)) {
                memcpy(multi_expr + off, exprs[i], len);
                off += len;
            }
        }

        struct yetty_yfsvm_program_result prog_res =
            yetty_yfsvm_compile_multi_expr(multi_expr, off);
        if (YETTY_IS_OK(prog_res)) {
            bc_count = yetty_yfsvm_program_serialize(&prog_res.value, bc_buf, 1024);
        }
    }

    struct yetty_yplot_buffers bufs = {
        .bytecode = bc_buf,
        .bytecode_len = bc_count,
    };

    size_t required = yetty_yplot_serialized_size(&uniforms, &bufs);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        return YETTY_ERR(yetty_ycore_void, "malloc failed");
    }

    struct yetty_ycore_size_result ser_res =
        yetty_yplot_serialize(&uniforms, &bufs, prim_buf, required);
    if (YETTY_IS_ERR(ser_res)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ycore_void, ser_res.error.msg);
    }

    struct yetty_ypaint_id_result id_res =
        yetty_ypaint_core_buffer_add_prim(buffer, prim_buf, required);
    free(prim_buf);

    if (id_res.error) {
        return YETTY_ERR(yetty_ycore_void, "add_prim failed");
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplot_register_yaml_factory(
    struct yetty_ypaint_yaml_parser *parser)
{
    return yetty_ypaint_yaml_parser_register(parser, "yplot", yplot_yaml_factory);
}
