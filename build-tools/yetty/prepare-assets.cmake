# prepare-assets.cmake
# Auto-generates MSDF CDB fonts at configure time if they don't exist
# This runs BEFORE incbin so the GLOB finds the CDB files

# Local-gen output goes alongside the prebuilt fetch dir so shared.cmake's
# glob picks them up. Note: local-gen produces raw .cdb (uncompressed);
# the embed pipeline globs *.cdb.br first — to use local-gen for embed
# you'd need to brotli the output. Today this dir is mainly for the
# YETTY_ENABLE_FEATURE_CDB_GEN path used by ymsdf-gen tooling, not the
# default embed path.
set(PREBUILT_CDB_DIR "${CMAKE_BINARY_DIR}/3rdparty/cdb")
set(FONT_DIR "${YETTY_ROOT}/assets/fonts")

set(CDB_FILES
    "DejaVuSansMNerdFontMono-Regular.cdb"
    "DejaVuSansMNerdFontMono-Bold.cdb"
    "DejaVuSansMNerdFontMono-Oblique.cdb"
    "DejaVuSansMNerdFontMono-BoldOblique.cdb"
)

set(TTF_FILES
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Regular.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Bold.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-Oblique.ttf"
    "${FONT_DIR}/DejaVuSansMNerdFontMono-BoldOblique.ttf"
)

# Check if all CDB files exist
set(ALL_CDBS_EXIST TRUE)
foreach(CDB_FILE ${CDB_FILES})
    if(NOT EXISTS "${PREBUILT_CDB_DIR}/${CDB_FILE}")
        set(ALL_CDBS_EXIST FALSE)
        break()
    endif()
endforeach()

if(ALL_CDBS_EXIST)
    message(STATUS "MSDF CDB fonts found in ${PREBUILT_CDB_DIR}")
else()
    message(STATUS "MSDF CDB fonts not found - generating at configure time...")

    # Build directory for host tools
    set(HOST_TOOLS_BUILD_DIR "${CMAKE_BINARY_DIR}/_host-tools-bootstrap")
    if(WIN32)
        set(HOST_MSDF_GEN "${HOST_TOOLS_BUILD_DIR}/yetty-ymsdf-gen.exe")
    else()
        set(HOST_MSDF_GEN "${HOST_TOOLS_BUILD_DIR}/yetty-ymsdf-gen")
    endif()

    # Determine generator and compiler args for host tools
    # When cross-compiling (e.g., Emscripten, Android NDK), we must use native compilers
    set(HOST_CMAKE_ARGS "")

    if(CMAKE_CROSSCOMPILING OR DEFINED ENV{EMSCRIPTEN} OR DEFINED EMSCRIPTEN)
        # Cross-compiling: find and use native compilers
        find_program(HOST_CC NAMES gcc clang cc)
        find_program(HOST_CXX NAMES g++ clang++ c++)

        if(HOST_CC)
            list(APPEND HOST_CMAKE_ARGS "-DCMAKE_C_COMPILER=${HOST_CC}")
        endif()
        if(HOST_CXX)
            list(APPEND HOST_CMAKE_ARGS "-DCMAKE_CXX_COMPILER=${HOST_CXX}")
        endif()

        # Clear cross-compilation settings
        list(APPEND HOST_CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=")
        list(APPEND HOST_CMAKE_ARGS "-DCMAKE_CROSSCOMPILING=OFF")
        list(APPEND HOST_CMAKE_ARGS "-G" "Ninja")
        list(APPEND HOST_CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release")

        message(STATUS "  Cross-compilation detected, using native compilers:")
        message(STATUS "    CC=${HOST_CC} CXX=${HOST_CXX}")
    elseif(WIN32)
        # Windows: use Ninja generator
        list(APPEND HOST_CMAKE_ARGS "-G" "Ninja")
        list(APPEND HOST_CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release")
        message(STATUS "  Windows detected, using Ninja generator")
    else()
        # Native build on Linux/macOS: use Ninja with current compilers
        list(APPEND HOST_CMAKE_ARGS "-G" "Ninja")
        list(APPEND HOST_CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release")
        message(STATUS "  Native build, using Ninja generator")
    endif()

    # Configure host tools
    message(STATUS "  Configuring host tools...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S "${YETTY_ROOT}/build-tools/cmake/host-tools"
            -B "${HOST_TOOLS_BUILD_DIR}"
            ${HOST_CMAKE_ARGS}
        RESULT_VARIABLE CONFIG_RESULT
        OUTPUT_VARIABLE CONFIG_OUTPUT
        ERROR_VARIABLE CONFIG_ERROR
    )
    if(NOT CONFIG_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to configure host tools:\nSTDOUT:\n${CONFIG_OUTPUT}\nSTDERR:\n${CONFIG_ERROR}")
    endif()

    # Build yetty-ymsdf-gen
    message(STATUS "  Building yetty-ymsdf-gen...")
    set(HOST_BUILD_ARGS --build "${HOST_TOOLS_BUILD_DIR}" --target yetty-ymsdf-gen --parallel)
    # Note: using Ninja (single-config) on all platforms, no --config needed

    execute_process(
        COMMAND ${CMAKE_COMMAND} ${HOST_BUILD_ARGS}
        RESULT_VARIABLE BUILD_RESULT
        OUTPUT_VARIABLE BUILD_OUTPUT
        ERROR_VARIABLE BUILD_ERROR
    )
    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build yetty-ymsdf-gen:\nSTDOUT:\n${BUILD_OUTPUT}\nSTDERR:\n${BUILD_ERROR}")
    endif()

    # Generate CDB files
    file(MAKE_DIRECTORY "${PREBUILT_CDB_DIR}")

    foreach(TTF_FILE ${TTF_FILES})
        get_filename_component(TTF_NAME "${TTF_FILE}" NAME)
        message(STATUS "  Generating CDB for ${TTF_NAME}...")
        execute_process(
            COMMAND "${HOST_MSDF_GEN}" --all "${TTF_FILE}" "${PREBUILT_CDB_DIR}"
            RESULT_VARIABLE GEN_RESULT
            OUTPUT_VARIABLE GEN_OUTPUT
            ERROR_VARIABLE GEN_ERROR
        )
        if(NOT GEN_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to generate CDB for ${TTF_NAME}:\nSTDOUT:\n${GEN_OUTPUT}\nSTDERR:\n${GEN_ERROR}")
        endif()
    endforeach()

    message(STATUS "MSDF CDB fonts generated in ${PREBUILT_CDB_DIR}")
endif()
