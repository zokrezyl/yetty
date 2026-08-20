# yvideo demos — pure CPU client tools that emit yvideo SCENE_BIN
# envelopes (INIT + a stream of CMD_UPDATE NAL chunks). Linked against
# yetty_yvideo_core only — no Dawn / WebGPU, no openh264 (decode runs
# on the receiving terminal).

add_executable(demo-yvideo-source
    video-source.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(demo-yvideo-source PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(demo-yvideo-source PRIVATE
    yetty_yvideo_core
    yetty_ydraw_list
    yetty_yface
    yetty_ycore
    Threads::Threads
    yetty_yplatform_core
)

target_compile_features(demo-yvideo-source PRIVATE c_std_11)
