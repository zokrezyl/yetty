# yjungle tool — incremental scene-canvas test scene.
# Pure C frontend over the yetty_yjungle library.

add_executable(yjungle
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yjungle PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(yjungle PRIVATE
    yetty_yjungle
    yetty_yface
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yjungle PRIVATE -Wall -Wextra)
endif()
