# yscene-demo tool — minimal DIRECT producer for the `yscene` retained-
# scene figure kind (#691): connects over yclass RPC, navigates to the
# host's root figure container, CREATE_CHILDs a scene, and drives the
# retained model (create / re-emit / scroll) through the typed container
# stubs — the same path the ychromium paint bridge uses. No yview. The
# reference producer shape and the live smoke test for the scene.

add_executable(yscene-demo
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yscene-demo PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(yscene-demo PRIVATE
    yetty_api_yfigure
    yetty_api_yterminal
    yetty_yclass
    yetty_ydraw_core
    yetty_ysdf
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yscene-demo PRIVATE -Wall -Wextra)
endif()
