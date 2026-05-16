# QA/Static analysis tools
# These tools require LLVM/Clang development libraries

if(DEFINED ENV{NIX_CC})
    # Nix: use LLVM/Clang from flake.nix env vars
    set(LLVM_DIR "$ENV{LLVM_DIR}")
    set(Clang_DIR "$ENV{Clang_DIR}")
else()
    # System: use LLVM 20
    if(NOT DEFINED LLVM_DIR)
        set(LLVM_DIR "/usr/lib/llvm-20/lib/cmake/llvm")
    endif()
    if(NOT DEFINED Clang_DIR)
        set(Clang_DIR "/usr/lib/cmake/clang-20")
    endif()
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)


message(STATUS "QA tools: Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "QA tools: Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "QA tools: Using ClangConfig.cmake in: ${Clang_DIR}")

# naming-convention: enforces yetty_<module>_<...> naming on external symbols
add_executable(naming-convention
    naming-convention.cpp
)

target_include_directories(naming-convention PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

target_compile_definitions(naming-convention PRIVATE
    ${LLVM_DEFINITIONS}
)

target_link_libraries(naming-convention PRIVATE
    yetty-qa-naming-core
    clangTooling
    clangBasic
    clangFrontend
    clangAST
    clangASTMatchers
)
