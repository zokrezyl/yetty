# QA/Static analysis tool: yclass navigation checker.
# Requires LLVM/Clang development libraries (same setup as result-checker).

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

message(STATUS "QA tools: yclass-checker using LLVM ${LLVM_PACKAGE_VERSION}")

# yclass-checker: flags hand-rolled object<->slice pointer arithmetic in
# files that use the yclass object model.
add_executable(yclass-checker
    yclass-checker.cpp
)

# See qa-tools/lib/naming-convention/CMakeLists.txt for why C++20.
set_target_properties(yclass-checker PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(yclass-checker PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

target_compile_definitions(yclass-checker PRIVATE
    ${LLVM_DEFINITIONS}
)

target_link_libraries(yclass-checker PRIVATE
    clangTooling
    clangBasic
    clangFrontend
    clangAST
    clangASTMatchers
)
