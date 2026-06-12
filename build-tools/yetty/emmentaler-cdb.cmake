# emmentaler-cdb.cmake
# Generates the Emmentaler music-font MSDF CDB at configure time and drops it
# into the cdb dir next to the fetched default-font CDBs, so the embed glob
# (shared.cmake: ${YETTY_3RDPARTY_cdb_DIR}/*.cdb.br) ships it like any other
# installed font. ymusic then references the font by name ("Emmentaler") and
# the receiver resolves msdf-fonts/Emmentaler.cdb from the install — no score
# ever carries the ~200 KB font.
#
# Unlike the four DejaVu faces (shipped via the prebuilt `cdb` 3rdparty
# tarball), Emmentaler is generated locally here: it is a single static music
# face, cheap to engrave once, and keeping it out of the tarball avoids a
# release-artifact round-trip just to add one font.
#
# Idempotent: skips entirely once Emmentaler.cdb.br exists in the cdb dir.

set(_EMM_CDB_DIR "${YETTY_3RDPARTY_cdb_DIR}")
set(_EMM_OTF "${YETTY_ROOT}/assets/fonts/Emmentaler-20.otf")
set(_EMM_NAME "Emmentaler")
set(_EMM_RAW "${_EMM_CDB_DIR}/${_EMM_NAME}.cdb")
set(_EMM_BR "${_EMM_CDB_DIR}/${_EMM_NAME}.cdb.br")

if(EXISTS "${_EMM_BR}")
    # Already generated for this build tree.
elseif(NOT EXISTS "${_EMM_OTF}")
    message(WARNING "emmentaler-cdb: source font not found: ${_EMM_OTF} — music will render without glyphs")
else()
    find_program(BROTLI_EXECUTABLE brotli)
    if(NOT BROTLI_EXECUTABLE)
        message(WARNING "emmentaler-cdb: brotli not found — skipping music font CDB; music glyphs unavailable")
    else()
        # Build the host yetty-ymsdf-gen tool (native, even when cross-compiling
        # the main target — the generator runs on the build host). Reuses the
        # bootstrap tree prepare-assets.cmake uses, so a prior bootstrap is not
        # rebuilt.
        set(_EMM_HOST_DIR "${CMAKE_BINARY_DIR}/_host-tools-bootstrap")
        if(WIN32)
            set(_EMM_GEN "${_EMM_HOST_DIR}/yetty-ymsdf-gen.exe")
        else()
            set(_EMM_GEN "${_EMM_HOST_DIR}/yetty-ymsdf-gen")
        endif()

        if(NOT EXISTS "${_EMM_GEN}")
            set(_EMM_HOST_ARGS "")
            if(CMAKE_CROSSCOMPILING OR DEFINED ENV{EMSCRIPTEN} OR DEFINED EMSCRIPTEN)
                find_program(_EMM_HOST_CC NAMES gcc clang cc)
                find_program(_EMM_HOST_CXX NAMES g++ clang++ c++)
                if(_EMM_HOST_CC)
                    list(APPEND _EMM_HOST_ARGS "-DCMAKE_C_COMPILER=${_EMM_HOST_CC}")
                endif()
                if(_EMM_HOST_CXX)
                    list(APPEND _EMM_HOST_ARGS "-DCMAKE_CXX_COMPILER=${_EMM_HOST_CXX}")
                endif()
                list(APPEND _EMM_HOST_ARGS "-DCMAKE_TOOLCHAIN_FILE=" "-DCMAKE_CROSSCOMPILING=OFF")
            endif()
            list(APPEND _EMM_HOST_ARGS "-G" "Ninja" "-DCMAKE_BUILD_TYPE=Release")

            message(STATUS "emmentaler-cdb: configuring host yetty-ymsdf-gen...")
            execute_process(
                COMMAND ${CMAKE_COMMAND}
                    -S "${YETTY_ROOT}/build-tools/yetty/host-tools"
                    -B "${_EMM_HOST_DIR}"
                    ${_EMM_HOST_ARGS}
                RESULT_VARIABLE _EMM_CFG_RC
                OUTPUT_VARIABLE _EMM_CFG_OUT
                ERROR_VARIABLE _EMM_CFG_OUT)
            if(NOT _EMM_CFG_RC EQUAL 0)
                message(FATAL_ERROR "emmentaler-cdb: host tool configure failed:\n${_EMM_CFG_OUT}")
            endif()

            message(STATUS "emmentaler-cdb: building host yetty-ymsdf-gen...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${_EMM_HOST_DIR}" --target yetty-ymsdf-gen --parallel
                RESULT_VARIABLE _EMM_BLD_RC
                OUTPUT_VARIABLE _EMM_BLD_OUT
                ERROR_VARIABLE _EMM_BLD_OUT)
            if(NOT _EMM_BLD_RC EQUAL 0)
                message(FATAL_ERROR "emmentaler-cdb: host tool build failed:\n${_EMM_BLD_OUT}")
            endif()
        endif()

        # Engrave into a scratch dir (the tool names the output <stem>.cdb, i.e.
        # Emmentaler-20.cdb), then settle it as Emmentaler.cdb so the install
        # name matches the "Emmentaler" reference ymusic emits.
        set(_EMM_WORK "${CMAKE_BINARY_DIR}/_emmentaler-cdb")
        file(MAKE_DIRECTORY "${_EMM_WORK}")
        file(MAKE_DIRECTORY "${_EMM_CDB_DIR}")
        message(STATUS "emmentaler-cdb: generating ${_EMM_NAME}.cdb from Emmentaler-20.otf...")
        execute_process(
            COMMAND "${_EMM_GEN}" --all "${_EMM_OTF}" "${_EMM_WORK}"
            RESULT_VARIABLE _EMM_GEN_RC
            OUTPUT_VARIABLE _EMM_GEN_OUT
            ERROR_VARIABLE _EMM_GEN_OUT)
        if(NOT _EMM_GEN_RC EQUAL 0)
            message(FATAL_ERROR "emmentaler-cdb: CDB generation failed:\n${_EMM_GEN_OUT}")
        endif()

        get_filename_component(_EMM_STEM "${_EMM_OTF}" NAME_WE)
        set(_EMM_GENERATED "${_EMM_WORK}/${_EMM_STEM}.cdb")
        if(NOT EXISTS "${_EMM_GENERATED}")
            message(FATAL_ERROR "emmentaler-cdb: expected output ${_EMM_GENERATED} not produced")
        endif()

        # Raw copy (runtime-path mode) + brotli copy (embed/incbin path), matching
        # how the fetched CDBs are staged.
        file(COPY "${_EMM_GENERATED}" DESTINATION "${_EMM_CDB_DIR}")
        if(NOT _EMM_STEM STREQUAL _EMM_NAME)
            file(RENAME "${_EMM_CDB_DIR}/${_EMM_STEM}.cdb" "${_EMM_RAW}")
        endif()
        execute_process(
            COMMAND "${BROTLI_EXECUTABLE}" -q 11 -f -o "${_EMM_BR}" "${_EMM_RAW}"
            RESULT_VARIABLE _EMM_BR_RC
            OUTPUT_VARIABLE _EMM_BR_OUT
            ERROR_VARIABLE _EMM_BR_OUT)
        if(NOT _EMM_BR_RC EQUAL 0)
            message(FATAL_ERROR "emmentaler-cdb: brotli compression failed:\n${_EMM_BR_OUT}")
        endif()
        message(STATUS "emmentaler-cdb: ${_EMM_NAME}.cdb ready in ${_EMM_CDB_DIR}")
    endif()
endif()
