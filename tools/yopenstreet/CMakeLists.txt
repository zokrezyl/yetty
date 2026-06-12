# yopenstreet tool — OpenStreetMap snapshot → YDRAW_BIN OSC envelope.
# Pure C frontend over the yetty_yopenstreet library.

add_executable(yopenstreet
    main.c
    interactive.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yopenstreet PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# yview ships the map as a positioned figure for --interactive (same
# pattern as tools/yflame); yface splits the input stream.
find_package(Threads REQUIRED)
target_link_libraries(yopenstreet PRIVATE
    yetty_yopenstreet
    yetty_yview
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yopenstreet PRIVATE -Wall -Wextra)
endif()
