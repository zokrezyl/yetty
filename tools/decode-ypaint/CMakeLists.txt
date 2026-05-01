# decode-ypaint — diagnostic CLI that takes a captured OSC stream from ycat
# (or any other ypaint-bin emitter) and re-runs the wire decode through
# yface, printing the meta header and decompressed payload size. Useful for
# isolating wire-format bugs from receiver-side rendering bugs.

add_executable(decode-ypaint
    main.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    # ytrace stubs used by libraries we pull in.
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(decode-ypaint PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(decode-ypaint PRIVATE
    yetty_yface
    yetty_ycore
)

if(NOT MSVC)
    target_compile_options(decode-ypaint PRIVATE -Wall -Wextra)
endif()
