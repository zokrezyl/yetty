// ypaint-yaml - YAML parser for ypaint primitives
//
// Two layers:
// - High level: yetty_ypaint_yaml_parse() - just call with YAML, get buffer
// - Low level: parser create/register/parse/destroy - for factory registration

#pragma once

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ypaint-core/yaml-factory.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// High level API - ypaint-layer uses this
//=============================================================================

struct yetty_ypaint_core_buffer_result yetty_ypaint_yaml_parse(const char *yaml, size_t len);

//=============================================================================
// Low level API - for factory registration
//=============================================================================

struct yetty_ypaint_yaml_parser;

struct yetty_ypaint_yaml_parser *yetty_ypaint_yaml_parser_create(void);
void yetty_ypaint_yaml_parser_destroy(struct yetty_ypaint_yaml_parser *parser);

struct yetty_ycore_void_result yetty_ypaint_yaml_parser_register(
    struct yetty_ypaint_yaml_parser *parser, const char *primitive_type_name,
    yetty_ypaint_yaml_factory_fn factory);

struct yetty_ycore_void_result yetty_ypaint_yaml_parser_parse(
    struct yetty_ypaint_yaml_parser *parser, struct yetty_ypaint_core_buffer *buffer,
    const char *yaml, size_t len);

#ifdef __cplusplus
}
#endif
