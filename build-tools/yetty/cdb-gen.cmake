# CDB Font Generation
# Generates MSDF CDB files from TTF fonts using yetty-ymsdf-gen tool.
# Output dir matches the prebuilt fetch dir (3rdparty/cdb/) so
# shared.cmake's glob picks up local-gen and prebuilt artifacts uniformly.

set(CDB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/3rdparty/cdb")
set(FONT_DIR "${YETTY_ROOT}/assets/fonts")

set(FONT_FILES
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Regular.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Bold.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Oblique.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-BoldOblique.ttf"
)

set(CDB_STAMP_FILE "${CDB_OUTPUT_DIR}/.cdb_generated")

if(CMAKE_CROSSCOMPILING)
    # Cross-compiling (Emscripten): build host tools using standalone CMakeLists.txt
    set(HOST_TOOLS_DIR "${CMAKE_BINARY_DIR}/host-tools")
    set(HOST_MSDF_GEN "${HOST_TOOLS_DIR}/yetty-ymsdf-gen")

    # Configure host tools build using standalone CMakeLists.txt (no X11/GLFW deps)
    add_custom_command(
        OUTPUT "${HOST_TOOLS_DIR}/build.ninja"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HOST_TOOLS_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -S "${YETTY_ROOT}/build-tools/cmake/host-tools"
            -B "${HOST_TOOLS_DIR}"
            -G Ninja
            -DCMAKE_BUILD_TYPE=Release
        COMMENT "Configuring host tools for CDB generation"
        VERBATIM
    )

    # Build host msdf-gen tool
    add_custom_command(
        OUTPUT "${HOST_MSDF_GEN}"
        COMMAND ${CMAKE_COMMAND} --build "${HOST_TOOLS_DIR}" --target yetty-ymsdf-gen --parallel
        DEPENDS "${HOST_TOOLS_DIR}/build.ninja"
        COMMENT "Building host yetty-ymsdf-gen tool"
        VERBATIM
    )

    # Generate CDB files
    add_custom_command(
        OUTPUT ${CDB_STAMP_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Regular.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Bold.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Oblique.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-BoldOblique.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${CDB_STAMP_FILE}"
        DEPENDS "${HOST_MSDF_GEN}" ${FONT_FILES}
        COMMENT "Generating MSDF CDB files from TTF fonts"
        VERBATIM
    )
else()
    # Native build: use msdf-gen from this build
    add_custom_command(
        OUTPUT ${CDB_STAMP_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Regular.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Bold.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Oblique.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-BoldOblique.ttf" "${CDB_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${CDB_STAMP_FILE}"
        DEPENDS yetty-ymsdf-gen ${FONT_FILES}
        COMMENT "Generating MSDF CDB files from TTF fonts"
        VERBATIM
    )
endif()

add_custom_target(generate-cdb DEPENDS ${CDB_STAMP_FILE})
