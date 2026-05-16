# Refactoring frontend that emits Replacements for the auto-fixable
# subset of naming-convention violations. Shares the AST-visitor logic
# with the analysis tool via qa-tools/lib/naming-convention.

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

add_executable(naming-convention-fix
    naming-convention-fix.cpp
)

target_include_directories(naming-convention-fix PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

target_compile_definitions(naming-convention-fix PRIVATE
    ${LLVM_DEFINITIONS}
)

target_link_libraries(naming-convention-fix PRIVATE
    yetty-qa-naming-core
    clangTooling
    clangBasic
    clangFrontend
    clangAST
    clangASTMatchers
    clangIndex
)
