# yplot demos — small C tools that exercise the buffer-mode + math-expression
# combination by emitting yplot SCENE_BIN OSC envelopes a yetty terminal can
# consume. The demos use the new CMD_UPDATE wire command to stream sample
# chunks into a live yplot instance without rebuilding the envelope.

add_executable(demo-yplot-audio-source
    audio-source.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(demo-yplot-audio-source PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(demo-yplot-audio-source PRIVATE
    yetty_yplot_core
    yetty_ydraw_core
    yetty_yface
    yetty_yfsvm_core
    yetty_yexpr
    yetty_ycore
    Threads::Threads
    yetty_yplatform_core
)

target_compile_features(demo-yplot-audio-source PRIVATE c_std_11)

if(NOT WIN32)
    target_link_libraries(demo-yplot-audio-source PRIVATE m)
endif()
