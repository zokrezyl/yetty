# yecho tool — pure C frontend for the yecho parser/renderer.

add_executable(yecho
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(yecho PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Threads::Threads pulls -lpthread on linux/macos and nothing on android
# (bionic has pthread in libc). Same pattern as tools/ycat.
find_package(Threads REQUIRED)
target_link_libraries(yecho PRIVATE
    yetty_yecho
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(yecho PRIVATE -Wall -Wextra)
endif()
