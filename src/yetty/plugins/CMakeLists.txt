# Plugin shared libraries - discovered at runtime via dlopen

# Common function to create a plugin shared library
function(add_yetty_plugin NAME)
    cmake_parse_arguments(PLUGIN "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})

    add_library(${NAME}_plugin SHARED ${PLUGIN_SOURCES})

    target_include_directories(${NAME}_plugin PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${PLUGIN_INCLUDES}
    )

    target_link_libraries(${NAME}_plugin PRIVATE
        webgpu
        spdlog::spdlog
        ${PLUGIN_LIBS}
    )

    # Output to plugins directory
    set_target_properties(${NAME}_plugin PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        OUTPUT_NAME "${NAME}"
        PREFIX ""  # No 'lib' prefix
    )

    # Ensure position independent code
    set_target_properties(${NAME}_plugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

# shader-toy plugin
add_yetty_plugin(shader
    SOURCES shader-toy/shader-toy.cpp
)

# image plugin
add_yetty_plugin(image
    SOURCES image/image.cpp
    LIBS stb
)

# markdown plugin
add_yetty_plugin(markdown
    SOURCES markdown/markdown.cpp
)

# pdf plugin
add_yetty_plugin(pdf
    SOURCES pdf/pdf.cpp
    LIBS mupdf freetype msdfgen::msdfgen-core msdfgen::msdfgen-ext
)

# plot plugin
add_yetty_plugin(plot
    SOURCES plot/plot.cpp
)

# piano plugin
add_yetty_plugin(piano
    SOURCES piano/piano.cpp
)

# musical-score plugin
add_yetty_plugin(musical-score
    SOURCES musical-score/musical-score.cpp
)

# sdf-primitives plugin
add_yetty_plugin(sdf-primitives
    SOURCES sdf-primitives/sdf-primitives.cpp
    LIBS yaml-cpp
)

# video plugin
add_yetty_plugin(video
    SOURCES video/video.cpp
    LIBS ffmpeg
)

# rich-text plugin
add_yetty_plugin(rich-text
    SOURCES rich-text/rich-text-plugin.cpp
    LIBS yaml-cpp msdfgen::msdfgen-core msdfgen::msdfgen-ext freetype
)

# ymery plugin (optional) - disabled for now due to API changes
# if(YETTY_YMERY_ENABLED)
#     add_yetty_plugin(ymery
#         SOURCES ymery/ymery.cpp
#         LIBS ymery_lib imgui implot
#     )
#     target_include_directories(ymery_plugin PRIVATE
#         ${ymery_SOURCE_DIR}/src
#         ${imgui_SOURCE_DIR}
#         ${imgui_SOURCE_DIR}/backends
#         ${implot_SOURCE_DIR}
#     )
# endif()
