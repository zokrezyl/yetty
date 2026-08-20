# yvideo tool — stream a raw H.264 Annex-B file as a yvideo OSC envelope
# sequence (INIT + N × CMD_UPDATE) on stdout. Pure CPU; no Dawn / WebGPU
# / openh264 needed at this side — decode happens on the receiving terminal.

add_executable(yvideo
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yvideo PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# minimp4 — header-only MP4 demuxer. main.c #defines MINIMP4_IMPLEMENTATION
# so the symbols land in this translation unit; the cmake target just
# provides the include path.
include(${YETTY_ROOT}/build-tools/yetty/minimp4.cmake)

find_package(Threads REQUIRED)
target_link_libraries(yvideo PRIVATE
    yetty_yvideo_core
    yetty_ydraw_list
    yetty_yface
    yetty_ycore
    Threads::Threads
    yetty_yplatform_core
    minimp4
)

target_compile_features(yvideo PRIVATE c_std_11)

if(NOT MSVC)
    target_compile_options(yvideo PRIVATE -Wall -Wextra)
endif()
