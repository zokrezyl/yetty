# decode-ydraw — diagnostic CLI that takes a captured OSC stream from ycat
# (or any other ydraw-bin emitter) and re-runs the wire decode through
# yface, printing the meta header and decompressed payload size. Useful for
# isolating wire-format bugs from receiver-side rendering bugs.

add_executable(decode-ydraw
    main.c
    # ytrace stubs used by libraries we pull in.
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(decode-ydraw PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(decode-ydraw PRIVATE
    yetty_yface
    yetty_ycore
    yetty_ydraw_list
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(decode-ydraw PRIVATE -Wall -Wextra)
endif()
