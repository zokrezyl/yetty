# yview — yclass client emitter for server-side scrollable content surfaces.
# Migrated to the role-split codegen layout:
#
#   yetty_api_yview   compiles src/yetty/gen/api/yview/view.c and exports
#                     <yetty/api/yview/view.h> — the object API (typed stubs).
#                     GPU-free (yclass + ycore only).
#   yetty_yview       compiles the hand-written view.c (which #includes the
#                     generated impl glue src/yetty/gen/impl/yview/view.c).
#                     Builds the figure-tree wire records from a ydraw-core
#                     drawable list and drives the host container over RPC.

add_library(yetty_api_yview STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/yview/view.c
)

target_include_directories(yetty_api_yview PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_api_yview PUBLIC
    yetty_ycore
    yetty_yclass
)

add_library(yetty_yview STATIC
    view.c
)

target_include_directories(yetty_yview PUBLIC
    ${YETTY_ROOT}/include
)
# view.c #includes the generated impl glue via "yetty/gen/impl/...".
target_include_directories(yetty_yview PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_yview PUBLIC
    yetty_api_yview
    yetty_ycore
    yetty_yclass
    yetty_api_yfigure   # typed container stubs only (pure remote-driver client)
    yetty_api_yterminal # figure_root_container navigation from the session root
    yetty_ydraw_core
    yetty_yplot_core    # set_plot: render a yplot expression to a drawable list (GPU-less)
)

# view.c carries `[[clang::annotate(...)]]` (C23 attribute syntax); see
# ygrid/CMakeLists.txt for the rationale.
if(NOT MSVC)
    set_target_properties(yetty_yview PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
