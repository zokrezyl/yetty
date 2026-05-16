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
