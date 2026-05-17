# Shared core library for the naming-convention QA tools
# (analysis + refactoring frontends both link against this).

if(DEFINED ENV{NIX_CC})
    set(LLVM_DIR "$ENV{LLVM_DIR}")
    set(Clang_DIR "$ENV{Clang_DIR}")
else()
    if(NOT DEFINED LLVM_DIR)
        set(LLVM_DIR "/usr/lib/llvm-20/lib/cmake/llvm")
    endif()
    if(NOT DEFINED Clang_DIR)
        set(Clang_DIR "/usr/lib/cmake/clang-20")
    endif()
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

add_library(yetty-qa-naming-core STATIC
    core.cpp
)

# Pin to C++20: LLVM 20's <llvm/Support/AlignOf.h> uses std::aligned_union_t,
# which libstdc++ marks deprecated in C++23. The project default is C++23
# (top-level CMakeLists.txt), so we explicitly set the target property to
# override — `target_compile_features(... cxx_std_20)` would only set a
# *minimum* and leaves the inherited C++23 in place.
set_target_properties(yetty-qa-naming-core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(yetty-qa-naming-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

target_compile_definitions(yetty-qa-naming-core PUBLIC
    ${LLVM_DEFINITIONS}
)

# The frontends link clangTooling/Basic/Frontend/AST themselves; the lib
# only uses headers so we don't drag those into every consumer here.
