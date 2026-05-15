// ysdf YAML factory registration

#pragma once

#include <yetty/ydraw-yaml/ypaint-yaml.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register all SDF primitive type factories with parser
void yetty_ysdf_register_yaml_factories(struct yetty_ydraw_yaml_parser *parser);

#ifdef __cplusplus
}
#endif
