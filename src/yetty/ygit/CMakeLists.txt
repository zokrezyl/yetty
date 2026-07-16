# ygit — read-only Git history/inspection component (yclass module).
#
# The `repo` class (repo.c) is the reusable object surface; git-backend.c wraps
# libgit2 for the actual repository access; commit-graph.c computes DAG lane
# layout. libgit2 is vendored under build-tools/3rdparty/libgit2 and imported
# by the stub below; it is PRIVATE so <git2.h> never leaks past this library —
# consumers see only the plain-C structs in include/yetty/ygit/.

include(${YETTY_ROOT}/build-tools/yetty/libs/libgit2.cmake)

add_library(yetty_ygit STATIC
    repo.c
    git-backend.c
    commit-graph.c
    rpc.gen.c
)

target_include_directories(yetty_ygit PUBLIC
    ${YETTY_ROOT}/include
)
# Codegen-emitted internal sources (*.gen.c) sit under src/yetty/ygit/; repo.c
# #includes its own repo.gen.c. PRIVATE so the path does not leak to consumers —
# the public API is the per-class header under include/yetty/ygit/.
target_include_directories(yetty_ygit PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ygit
    PUBLIC
        yetty_ycore
        yetty_yclass
    PRIVATE
        libgit2
)

# repo.c carries `[[clang::annotate(...)]]` on its data struct (codegen reads it
# off the AST). The C2x attribute syntax needs C23 to parse; host clang/gcc
# accept it by default. Off-MSVC only. (Mirrors ygrid/CMakeLists.txt.)
if(NOT MSVC)
    set_target_properties(yetty_ygit PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
