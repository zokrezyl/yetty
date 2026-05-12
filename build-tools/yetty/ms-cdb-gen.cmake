# MS-CDB Font Generation
# Generates monospace MSDF .ms-cdb files from TTF fonts using yetty-ymsdf-gen.
# Output dir lives under 3rdparty/ alongside the cdb fetch dir; consumers
# glob it explicitly (no prebuilt fetch yet for ms-msdf-fonts — generated
# locally only).

set(MS_CDB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/3rdparty/ms-msdf-fonts")
set(FONT_DIR "${YETTY_ROOT}/assets/fonts")

set(MS_FONT_FILES
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Regular.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Bold.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Oblique.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-BoldOblique.ttf"
)

set(MS_CDB_STAMP_FILE "${MS_CDB_OUTPUT_DIR}/.ms_cdb_generated")

if(CMAKE_CROSSCOMPILING)
    set(HOST_TOOLS_DIR "${CMAKE_BINARY_DIR}/host-tools")
    set(HOST_MS_MSDF_GEN "${HOST_TOOLS_DIR}/yetty-ymsdf-gen")

    add_custom_command(
        OUTPUT "${HOST_TOOLS_DIR}/build.ninja"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HOST_TOOLS_DIR}"
        COMMAND ${CMAKE_COMMAND}
            -S "${YETTY_ROOT}/build-tools/cmake/host-tools"
            -B "${HOST_TOOLS_DIR}"
            -G Ninja
            -DCMAKE_BUILD_TYPE=Release
        COMMENT "Configuring host tools for MS-CDB generation"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${HOST_MS_MSDF_GEN}"
        COMMAND ${CMAKE_COMMAND} --build "${HOST_TOOLS_DIR}" --target yetty-ymsdf-gen --parallel
        DEPENDS "${HOST_TOOLS_DIR}/build.ninja"
        COMMENT "Building host yetty-ymsdf-gen tool"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${MS_CDB_STAMP_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MS_CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MS_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Regular.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MS_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Bold.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MS_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Oblique.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND "${HOST_MS_MSDF_GEN}" --all "${FONT_DIR}/DejaVuSansMNerdFontMono-BoldOblique.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${MS_CDB_STAMP_FILE}"
        DEPENDS "${HOST_MS_MSDF_GEN}" ${MS_FONT_FILES}
        COMMENT "Generating monospace MSDF .ms-cdb files from TTF fonts"
        VERBATIM
    )
else()
    add_custom_command(
        OUTPUT ${MS_CDB_STAMP_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MS_CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Regular.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Bold.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-Oblique.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND $<TARGET_FILE:yetty-ymsdf-gen> --all "${FONT_DIR}/DejaVuSansMNerdFontMono-BoldOblique.ttf" "${MS_CDB_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${MS_CDB_STAMP_FILE}"
        DEPENDS yetty-ymsdf-gen ${MS_FONT_FILES}
        COMMENT "Generating monospace MSDF .ms-cdb files from TTF fonts"
        VERBATIM
    )
endif()

add_custom_target(generate-ms-cdb DEPENDS ${MS_CDB_STAMP_FILE})
