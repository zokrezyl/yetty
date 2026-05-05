add_executable(msdf-render-atlas main.cpp)

target_link_libraries(msdf-render-atlas PRIVATE
    msdf-wgsl
    glfw
    glfw3webgpu
    webgpu
    ${FREETYPE_ALL_LIBS}
)

target_include_directories(msdf-render-atlas PRIVATE
    ${YETTY_ROOT}/include
)

# Copy shader next to executable
add_custom_command(TARGET msdf-render-atlas POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/msdf_gen.wgsl
        $<TARGET_FILE_DIR:msdf-render-atlas>/msdf_gen.wgsl
)
