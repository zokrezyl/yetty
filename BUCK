# Yetty - WebGPU Terminal
# Root BUCK file

load("@prelude//rules.bzl", "cxx_library", "cxx_binary", "genrule")

#=============================================================================
# Common configuration
#=============================================================================

YETTY_C_FLAGS = [
    "-std=c11",
    "-D_POSIX_C_SOURCE=200809L",
    "-D_GNU_SOURCE",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-fPIC",
]

YETTY_INCLUDES = [
    "include",
    "src",
]

#=============================================================================
# Embedded shaders - convert .wgsl to C arrays
#=============================================================================

genrule(
    name = "gblend_shader_gen",
    out = "gblend_shader.c",
    srcs = ["src/yetty/yrender/blend.wgsl"],
    bash = """
        echo '#include <stddef.h>' > "$OUT"
        echo 'const unsigned char gblend_shaderData[] = {' >> "$OUT"
        xxd -i < "$SRCS" >> "$OUT"
        echo '};' >> "$OUT"
        echo "const unsigned int gblend_shaderSize = sizeof(gblend_shaderData);" >> "$OUT"
    """,
    visibility = ["PUBLIC"],
)

genrule(
    name = "gyplot_shader_gen",
    out = "gyplot_shader.c",
    srcs = ["src/yetty/yplot/yplot.wgsl"],
    bash = """
        echo '#include <stddef.h>' > "$OUT"
        echo 'const unsigned char gyplot_shaderData[] = {' >> "$OUT"
        xxd -i < "$SRCS" >> "$OUT"
        echo '};' >> "$OUT"
        echo "const unsigned int gyplot_shaderSize = sizeof(gyplot_shaderData);" >> "$OUT"
    """,
    visibility = ["PUBLIC"],
)

genrule(
    name = "gyfsvm_shader_gen",
    out = "gyfsvm_shader.c",
    srcs = ["src/yetty/yfsvm/yfsvm.gen.wgsl"],
    bash = """
        echo '#include <stddef.h>' > "$OUT"
        echo 'const unsigned char gyfsvm_shaderData[] = {' >> "$OUT"
        xxd -i < "$SRCS" >> "$OUT"
        echo '};' >> "$OUT"
        echo "const unsigned int gyfsvm_shaderSize = sizeof(gyfsvm_shaderData);" >> "$OUT"
    """,
    visibility = ["PUBLIC"],
)

#=============================================================================
# libvterm (local customized version) - build genrule here to access src/
#=============================================================================

genrule(
    name = "vterm_build",
    out = "install",
    srcs = glob(["src/libvterm-0.3.3/**"]),
    bash = """
        set -e
        VTERM="$SRCDIR/src/libvterm-0.3.3"
        mkdir -p "$OUT/lib" "$OUT/include"
        for f in encoding keyboard mouse parser pen screen state unicode vterm; do
            clang -c -fPIC -O2 -w -std=c99 -I"$VTERM/include" "$VTERM/src/$f.c" -o "$BUCK_SCRATCH_PATH/$f.o"
        done
        ar rcs "$OUT/lib/libvterm.a" "$BUCK_SCRATCH_PATH"/*.o
        cp "$VTERM/include/"*.h "$OUT/include/"
    """,
    cacheable = True,
    visibility = ["PUBLIC"],
)

#=============================================================================
# ytrace - tracing/logging infrastructure
#=============================================================================

cxx_library(
    name = "ytrace",
    srcs = ["src/yetty/ytrace.c"],
    exported_headers = glob(["include/yetty/**/*.h", "src/yetty/**/*.h"]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        "//buck-build-tools/third_party:spdlog",
    ],
    visibility = ["PUBLIC"],
)

#=============================================================================
# Core libraries (platform-independent)
#=============================================================================

cxx_library(
    name = "ycore",
    srcs = glob(["src/yetty/ycore/**/*.c"]),
    headers = glob(["include/yetty/ycore/**/*.h"]),
    header_namespace = "",
    exported_headers = glob(["include/yetty/**/*.h", "src/yetty/**/*.h"]),
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ytrace",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yui_core",
    srcs = glob(["src/yetty/yui-core/**/*.c"]),
    headers = glob([
        "include/yetty/yui-core/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yterm",
    srcs = glob(["src/yetty/yterm/**/*.c"]),
    headers = glob([
        "include/yetty/yterm/**/*.h",
        "src/yetty/yterm/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":ypaint_yaml",
        ":yui_core",
        "//buck-build-tools/third_party:vterm",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yfont",
    srcs = glob(["src/yetty/yfont/**/*.c"]),
    headers = glob([
        "include/yetty/yfont/**/*.h",
        "src/yetty/yfont/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":ycdb",
        "//buck-build-tools/third_party:freetype",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yrender",
    srcs = glob(["src/yetty/yrender/**/*.c"]) + [":gblend_shader_gen"],
    headers = glob([
        "include/yetty/yrender/**/*.h",
        "src/yetty/yrender/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "ywebgpu",
    srcs = glob(["src/yetty/ywebgpu/**/*.c"]),
    headers = glob([
        "include/yetty/webgpu/**/*.h",
        "src/yetty/ywebgpu/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "ypaint",
    srcs = glob(["src/yetty/ypaint/**/*.c"]),
    headers = glob([
        "include/yetty/ypaint/**/*.h",
        "src/yetty/ypaint/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":yrender",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "ypaint_yaml",
    srcs = glob(["src/yetty/ypaint-yaml/**/*.c"]),
    headers = glob([
        "include/yetty/ypaint-yaml/**/*.h",
        "src/yetty/ypaint-yaml/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":ypaint",
        "//buck-build-tools/third_party:libyaml",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "ypaint_core",
    srcs = glob(["src/yetty/ypaint-core/**/*.c"]),
    headers = glob([
        "include/yetty/ypaint-core/**/*.h",
        "src/yetty/ypaint-core/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yplot",
    srcs = glob(["src/yetty/yplot/**/*.c"]) + [":gyplot_shader_gen"],
    headers = glob([
        "include/yetty/yplot/**/*.h",
        "src/yetty/yplot/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":ypaint",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yimage",
    srcs = glob(["src/yetty/yimage/**/*.c"]),
    headers = glob([
        "include/yetty/yimage/**/*.h",
        "src/yetty/yimage/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        "//buck-build-tools/third_party:libjpeg_turbo",
        "//buck-build-tools/third_party:libpng",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yrpc",
    srcs = glob(["src/yetty/yrpc/**/*.c"]),
    headers = glob([
        "include/yetty/yrpc/**/*.h",
        "src/yetty/yrpc/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        "//buck-build-tools/third_party:msgpack",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yvnc",
    srcs = glob(["src/yetty/yvnc/**/*.c"]),
    headers = glob([
        "include/yetty/yvnc/**/*.h",
        "src/yetty/yvnc/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        "//buck-build-tools/third_party:libjpeg_turbo",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "ycdb",
    srcs = glob(["src/yetty/ycdb/**/*.c"]),
    headers = glob([
        "include/yetty/ycdb/**/*.h",
        "src/yetty/ycdb/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS + ["-DYETTY_USE_HOWERJ_CDB"],
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        "//buck-build-tools/third_party:howerj_cdb",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yexpr",
    srcs = glob(["src/yetty/yexpr/**/*.c"]),
    headers = glob([
        "include/yetty/yexpr/**/*.h",
        "src/yetty/yexpr/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yfsvm",
    srcs = glob(["src/yetty/yfsvm/**/*.c"]) + [":gyfsvm_shader_gen"],
    headers = glob([
        "include/yetty/yfsvm/**/*.h",
        "src/yetty/yfsvm/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "ymsdf_gen",
    srcs = glob(["src/yetty/ymsdf-gen/**/*.c"]),
    headers = glob([
        "include/yetty/ymsdf-gen/**/*.h",
        "src/yetty/ymsdf-gen/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":yfont",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "ysdf",
    srcs = glob(["src/yetty/ysdf/**/*.c"]),
    headers = glob([
        "include/yetty/ysdf/**/*.h",
        "src/yetty/ysdf/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":ypaint_yaml",
        "//buck-build-tools/third_party:dawn",
        "//buck-build-tools/third_party:libyaml",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yui",
    srcs = glob(["src/yetty/yui/**/*.c"]),
    headers = glob([
        "include/yetty/yui/**/*.h",
        "src/yetty/yui/**/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":yui_core",
        ":yterm",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

#=============================================================================
# Platform-specific code
#=============================================================================

cxx_library(
    name = "yplatform_shared",
    srcs = glob(["src/yetty/yplatform/shared/**/*.c"]),
    headers = glob(["include/yetty/yplatform/**/*.h"]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ycore",
        ":incbin_assets",
        "//buck-build-tools/third_party:libuv",
        "//buck-build-tools/third_party:glfw",
        "//buck-build-tools/third_party:dawn",
        "//buck-build-tools/third_party:glfw3webgpu",
    ],
    visibility = ["PUBLIC"],
)

cxx_library(
    name = "yplatform_linux",
    srcs = glob(["src/yetty/yplatform/linux/**/*.c"]),
    headers = glob(["include/yetty/yplatform/**/*.h"]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":yplatform_shared",
    ],
    visibility = ["PUBLIC"],
)

#=============================================================================
# Main yetty library (core app logic)
#=============================================================================

cxx_library(
    name = "yetty_main",
    srcs = [
        "src/yetty/yetty.c",
        "src/yetty/config.c",
    ],
    headers = glob([
        "include/yetty/*.h",
    ]),
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ytrace",
        ":ycore",
        ":yterm",
        ":yfont",
        ":yrender",
        ":ywebgpu",
        ":ypaint",
        ":ypaint_core",
        ":yplot",
        ":yimage",
        ":yrpc",
        ":yvnc",
        ":yui",
        ":yexpr",
        ":yfsvm",
        ":ymsdf_gen",
        ":ysdf",
        "//buck-build-tools/third_party:libyaml",
        "//buck-build-tools/third_party:dawn",
    ],
    visibility = ["PUBLIC"],
)

#=============================================================================
# Embedded assets (incbin)
#=============================================================================

cxx_library(
    name = "incbin_assets",
    srcs = ["src/yetty/incbin-assets.c"],
    header_namespace = "",
    compiler_flags = YETTY_C_FLAGS,
    include_directories = YETTY_INCLUDES,
    preferred_linkage = "static",
    deps = [
        ":ytrace",
        "//buck-build-tools/third_party:brotli",
    ],
    visibility = ["PUBLIC"],
)

#=============================================================================
# Main executable (Linux desktop)
#=============================================================================

cxx_binary(
    name = "yetty",
    srcs = [
        "src/yetty/yplatform/shared/glfw-main.c",
    ],
    compiler_flags = YETTY_C_FLAGS + [
        "-DYETTY_WEB=0",
        "-DYETTY_ANDROID=0",
        "-DYETTY_USE_FONTCONFIG=1",
        "-DYETTY_USE_FORKPTY=1",
        "-DYETTY_HAS_VNC=1",
    ],
    include_directories = YETTY_INCLUDES,
    link_style = "static",
    deps = [
        ":yetty_main",
        ":ytrace",
        ":ycore",
        ":yterm",
        ":yfont",
        ":yrender",
        ":ywebgpu",
        ":ypaint",
        ":ypaint_core",
        ":yplot",
        ":yimage",
        ":yrpc",
        ":yvnc",
        ":ycdb",
        ":yexpr",
        ":yfsvm",
        ":ymsdf_gen",
        ":ysdf",
        ":yui",
        ":yplatform_shared",
        ":yplatform_linux",
        "//buck-build-tools/third_party:dawn",
        "//buck-build-tools/third_party:spdlog",
        "//buck-build-tools/third_party:libuv",
        "//buck-build-tools/third_party:libyaml",
        "//buck-build-tools/third_party:glfw",
        "//buck-build-tools/third_party:vterm",
        "//buck-build-tools/third_party:freetype",
        "//buck-build-tools/third_party:msgpack",
        "//buck-build-tools/third_party:libjpeg_turbo",
        "//buck-build-tools/third_party:libpng",
        "//buck-build-tools/third_party:zlib",
        "//buck-build-tools/third_party:fontconfig",
        "//buck-build-tools/third_party:expat",
        "//buck-build-tools/third_party:libuuid",
        "//buck-build-tools/third_party:brotli",
    ],
    linker_flags = [
        "-lrt",
        "-lutil",
        "-lm",
        "-lpthread",
        "-ldl",
    ],
    visibility = ["PUBLIC"],
)
