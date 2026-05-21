# yaudio-intervals - CLI driver around the yaudio interval detector.

add_executable(yaudio-intervals
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yaudio-intervals PRIVATE
    ${YETTY_ROOT}/include
)

target_link_libraries(yaudio-intervals PRIVATE
    yetty_yaudio
    yetty_ycore
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yaudio-intervals PRIVATE -Wall -Wextra)
endif()
