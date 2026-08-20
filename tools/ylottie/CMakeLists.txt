# yetty-ylottie — CLI that plays a Lottie animation as ydraw OSC (one frame
# per envelope, paced at the composition fps). Pure SDF/MSDF via yetty_ylottie.

add_executable(yetty-ylottie
    main.c
    # ytrace stubs used by libraries we pull in (ydraw-list, ylottie).
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yetty-ylottie PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty-ylottie PRIVATE
    yetty_ylottie
    yetty_ydraw_list
    yetty_yface
    yetty_ycore
    yetty_yplatform_core
)

# Math functions live in the C runtime on MSVC — there is no separate libm.
if(NOT WIN32)
    target_link_libraries(yetty-ylottie PRIVATE m)
endif()
