# libyaml — YAML 1.1 parser/emitter (C).
#
# Consumes a prebuilt static lib + headers from the 3rdparty release
# tarball published by build-3rdparty-libyaml.yml. The from-source build
# (CMake driver, per-platform handling) lives in
# build-tools/3rdparty/libyaml/_build.sh.
#
# Exposed target: `yaml` (matches what shared.cmake links against).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

if(TARGET yaml)
    return()
endif()

yetty_3rdparty_fetch(libyaml _LIBYAML_DIR)

# Tarball layout: lib/libyaml.a (or yaml.lib on native MSVC; under MSYS2
# CLANG64 it's libyaml.a) + include/yaml.h
if(WIN32 AND EXISTS "${_LIBYAML_DIR}/lib/yaml.lib")
    set(_LIBYAML_LIB "${_LIBYAML_DIR}/lib/yaml.lib")
elseif(EXISTS "${_LIBYAML_DIR}/lib/libyaml.a")
    set(_LIBYAML_LIB "${_LIBYAML_DIR}/lib/libyaml.a")
else()
    message(FATAL_ERROR
        "libyaml: no static lib found in ${_LIBYAML_DIR}/lib/ — \
tarball layout changed? (check build-tools/3rdparty/libyaml/_build.sh)")
endif()
if(NOT EXISTS "${_LIBYAML_DIR}/include/yaml.h")
    message(FATAL_ERROR
        "libyaml: yaml.h not found in ${_LIBYAML_DIR}/include/ — tarball layout changed?")
endif()

add_library(yaml STATIC IMPORTED GLOBAL)
set_target_properties(yaml PROPERTIES
    IMPORTED_LOCATION "${_LIBYAML_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_LIBYAML_DIR}/include"
    INTERFACE_COMPILE_DEFINITIONS "YAML_DECLARE_STATIC"
)

message(STATUS "libyaml: prebuilt v${YETTY_3RDPARTY_libyaml_VERSION} (${_LIBYAML_LIB})")
