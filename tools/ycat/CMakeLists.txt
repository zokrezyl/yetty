# ycat tool — pure C.
#
# Several sources live outside any exported static library target (they are
# appended to YETTY_SOURCES for the yetty executable only): ytrace.c,
# yplatform/getopt.c, and the yplatform/shared helpers ytrace depends on
# (thread for ymutex_*, term for yplatform_format_timestamp). Pull them in
# directly.

add_executable(ycat
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(ycat PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Use Threads::Threads so we get the right thing per platform:
# -lpthread on linux/macos, nothing on android (bionic has pthread in libc).
find_package(Threads REQUIRED)
target_link_libraries(ycat PRIVATE
    yetty_ycat
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(ycat PRIVATE -Wall -Wextra)
endif()
