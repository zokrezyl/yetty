# ymcat tool — pure C. Ships RAW file bytes in a YETTY_DCS_MIME_FILE
# envelope; the terminal detects the type and renders it terminal-side.
# Deliberately thin: no renderer libraries, just ymime (detection +
# prologue) and yface (envelope codec). ytrace.c is pulled in directly the
# same way ycat does — it lives outside any exported static library.

add_executable(ymcat
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ymcat PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ymcat PRIVATE
    yetty_ymime
    yetty_yface
    yetty_ycore
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(ymcat PRIVATE -Wall -Wextra)
endif()
