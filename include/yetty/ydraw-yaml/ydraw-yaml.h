// ydraw-yaml - YAML parser for ydraw primitives
//
// Two layers:
// - High level: yetty_ydraw_yaml_parse() - just call with YAML, get buffer
// - Low level: parser create/register/parse/destroy - for factory registration

#pragma once

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/buffer.h>
#include <yetty/ydraw-core/yaml-factory.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// High level API - ydraw-layer uses this
//=============================================================================

struct yetty_ydraw_core_buffer_result yetty_ydraw_yaml_parse(const char *yaml, size_t len);

//=============================================================================
// Low level API - for factory registration
//=============================================================================

struct yetty_ydraw_yaml_parser;
YETTY_YRESULT_DECLARE(yetty_ydraw_yaml_parser_ptr, struct yetty_ydraw_yaml_parser *);

struct yetty_ydraw_yaml_parser_ptr_result yetty_ydraw_yaml_parser_create(void);
void yetty_ydraw_yaml_parser_destroy(struct yetty_ydraw_yaml_parser *parser);

struct yetty_ycore_void_result yetty_ydraw_yaml_parser_register(
    struct yetty_ydraw_yaml_parser *parser, const char *primitive_type_name,
    yetty_ydraw_yaml_factory_fn factory);

struct yetty_ycore_void_result yetty_ydraw_yaml_parser_parse(
    struct yetty_ydraw_yaml_parser *parser, struct yetty_ydraw_core_buffer *buffer,
    const char *yaml, size_t len);

#ifdef __cplusplus
}
#endif
