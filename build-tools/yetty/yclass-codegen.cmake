# yclass codegen as build-graph targets — `make codegen` delegates to the
# `yclass-codegen` target of the configured desktop build.
#
# Ninja re-runs a module's generator only when that module's annotated
# sources (or the generator/driver) changed — pass 1. Pass 2 re-parses a
# module against the other modules' regenerated headers (a header-destined
# type exposed by module A is visible to module B only after A's header is
# written); its driver invocation exits immediately unless some generated
# header is actually newer than the module's pass-2 stamp. The generator
# skips content-identical writes, so header mtimes move only on real
# changes: an unchanged tree is a no-op, and a leaf .c edit costs one
# clang parse plus cheap pass-2 short-circuits.
#
# The module table (names, source dirs, per-module parse defines,
# compat-header flags) lives in src/yetty/yclass/gen/driver.py — the one
# source of truth, shared with hand invocations.
#
# The codegen clang parse takes its flags from THIS build's
# compile_commands.json, so the target only exists in builds that export
# it (the desktop builds do).

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "yclass-codegen target disabled: no python3 interpreter found")
    return()
endif()

set(yclass_codegen_driver ${YETTY_ROOT}/src/yetty/yclass/gen/driver.py)
set(yclass_codegen_generator ${YETTY_ROOT}/src/yetty/yclass/gen/codegen.py)
set(yclass_codegen_stamp_dir ${CMAKE_BINARY_DIR}/yclass-codegen)
set(yclass_codegen_compile_db ${CMAKE_BINARY_DIR}/compile_commands.json)
file(MAKE_DIRECTORY ${yclass_codegen_stamp_dir})

execute_process(
    COMMAND ${Python3_EXECUTABLE} ${yclass_codegen_driver} --list-modules
    OUTPUT_VARIABLE yclass_codegen_module_lines
    RESULT_VARIABLE yclass_codegen_list_result
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT yclass_codegen_list_result EQUAL 0)
    message(FATAL_ERROR "yclass codegen driver --list-modules failed")
endif()
string(REPLACE "\n" ";" yclass_codegen_module_lines "${yclass_codegen_module_lines}")

set(yclass_codegen_pass1_stamps "")
foreach(module_line IN LISTS yclass_codegen_module_lines)
    string(REPLACE "|" ";" module_parts "${module_line}")
    list(GET module_parts 0 module_name)
    list(GET module_parts 1 module_source_dir)
    # Over-approximate dependency set: every .c under the module dir. The
    # driver re-greps for the annotated subset at run time, so a new
    # annotated file is picked up without reconfiguring (CONFIGURE_DEPENDS
    # re-globs on any file-set change anyway).
    file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS
         ${YETTY_ROOT}/${module_source_dir}/*.c)
    add_custom_command(
        OUTPUT ${yclass_codegen_stamp_dir}/${module_name}.pass1
        COMMAND ${Python3_EXECUTABLE} ${yclass_codegen_driver}
                --run ${module_name}
                --repo-root ${YETTY_ROOT}
                --compile-db ${yclass_codegen_compile_db}
        COMMAND ${CMAKE_COMMAND} -E touch
                ${yclass_codegen_stamp_dir}/${module_name}.pass1
        DEPENDS ${module_sources} ${yclass_codegen_driver} ${yclass_codegen_generator}
        COMMENT "yclass codegen (pass 1): ${module_name}"
        VERBATIM)
    list(APPEND yclass_codegen_pass1_stamps
         ${yclass_codegen_stamp_dir}/${module_name}.pass1)
endforeach()

set(yclass_codegen_pass2_stamps "")
foreach(module_line IN LISTS yclass_codegen_module_lines)
    string(REPLACE "|" ";" module_parts "${module_line}")
    list(GET module_parts 0 module_name)
    add_custom_command(
        OUTPUT ${yclass_codegen_stamp_dir}/${module_name}.pass2
        COMMAND ${Python3_EXECUTABLE} ${yclass_codegen_driver}
                --run-if-headers-changed ${module_name}
                --since ${yclass_codegen_stamp_dir}/${module_name}.pass2
                --repo-root ${YETTY_ROOT}
                --compile-db ${yclass_codegen_compile_db}
        COMMAND ${CMAKE_COMMAND} -E touch
                ${yclass_codegen_stamp_dir}/${module_name}.pass2
        DEPENDS ${yclass_codegen_pass1_stamps} ${yclass_codegen_driver}
        COMMENT "yclass codegen (pass 2): ${module_name}"
        VERBATIM)
    list(APPEND yclass_codegen_pass2_stamps
         ${yclass_codegen_stamp_dir}/${module_name}.pass2)
endforeach()

add_custom_target(yclass-codegen DEPENDS ${yclass_codegen_pass2_stamps})
