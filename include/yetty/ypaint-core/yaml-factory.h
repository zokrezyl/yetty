// ypaint-core YAML factory callback type

#pragma once

#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/buffer.h>

// Forward declare libyaml parser (avoids yaml.h dependency in ypaint-core)
struct yaml_parser_s;
typedef struct yaml_parser_s yaml_parser_t;

#ifdef __cplusplus
extern "C" {
#endif

// Factory callback: reads from yaml_parser, writes to buffer
// primitive_type_name: e.g., "circle", "box", "text"
// yaml_parser: positioned to read the value (mapping content)
typedef struct yetty_ycore_void_result (*yetty_ypaint_yaml_factory_fn)(
    struct yetty_ypaint_core_buffer *buffer, yaml_parser_t *yaml_parser,
    const char *primitive_type_name);

#ifdef __cplusplus
}
#endif
