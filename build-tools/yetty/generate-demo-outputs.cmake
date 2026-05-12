# Generate pre-computed outputs from demo scripts
# This mirrors demo/scripts/* to ${OUTPUT_DIR}/demo-output/* with .out files

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED YETTY_ROOT)
    message(FATAL_ERROR "YETTY_ROOT must be defined")
endif()

if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "OUTPUT_DIR must be defined")
endif()

set(SCRIPTS_DIR "${YETTY_ROOT}/demo/scripts")
set(DEMO_OUTPUT_DIR "${OUTPUT_DIR}/demo-output")

# Scripts to skip (aggregators, interactive, etc.)
set(SKIP_SCRIPTS "all.sh" "demo.sh" "effects-demo.sh")

# Find all .sh files
file(GLOB_RECURSE DEMO_SCRIPTS "${SCRIPTS_DIR}/*.sh")

foreach(SCRIPT ${DEMO_SCRIPTS})
    # Get relative path from scripts dir
    file(RELATIVE_PATH REL_PATH "${SCRIPTS_DIR}" "${SCRIPT}")
    get_filename_component(SCRIPT_FILENAME "${SCRIPT}" NAME)

    # Skip aggregator scripts
    if(SCRIPT_FILENAME IN_LIST SKIP_SCRIPTS)
        message(STATUS "Skipping aggregator: ${REL_PATH}")
        continue()
    endif()

    # Get directory and filename
    get_filename_component(REL_DIR "${REL_PATH}" DIRECTORY)
    get_filename_component(SCRIPT_NAME "${REL_PATH}" NAME_WE)

    # Create output path
    if(REL_DIR)
        set(OUT_DIR "${DEMO_OUTPUT_DIR}/${REL_DIR}")
    else()
        set(OUT_DIR "${DEMO_OUTPUT_DIR}")
    endif()
    set(OUT_FILE "${OUT_DIR}/${SCRIPT_NAME}.out")

    # Create output directory
    file(MAKE_DIRECTORY "${OUT_DIR}")

    # Run script with timeout (5 seconds max)
    execute_process(
        COMMAND timeout 5 bash "${SCRIPT}"
        WORKING_DIRECTORY "${YETTY_ROOT}"
        OUTPUT_FILE "${OUT_FILE}"
        ERROR_QUIET
        RESULT_VARIABLE RESULT
    )

    if(RESULT EQUAL 0)
        message(STATUS "Generated: ${REL_PATH} -> ${SCRIPT_NAME}.out")
    else()
        message(STATUS "Skipped (error/timeout): ${REL_PATH}")
        file(REMOVE "${OUT_FILE}")
    endif()
endforeach()

message(STATUS "Demo outputs generated in ${DEMO_OUTPUT_DIR}")
