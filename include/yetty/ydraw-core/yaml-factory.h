// ydraw-core YAML factory callback type

#pragma once

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/draw-list.h>

// Forward declare libyaml parser (avoids yaml.h dependency in ydraw-core)
struct yaml_parser_s;

#ifdef __cplusplus
extern "C" {
#endif

// Factory callback: reads from yaml_parser, writes to buffer
// primitive_type_name: e.g., "circle", "box", "text"
// yaml_parser: positioned to read the value (mapping content)
typedef struct yetty_ycore_void_result (*yetty_ydraw_yaml_factory_fn)(
    struct yetty_ydraw_draw_list *buffer, struct yaml_parser_s *yaml_parser,
    const char *primitive_type_name);

#ifdef __cplusplus
}
#endif
