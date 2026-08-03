# yclass codegen as build-graph targets — `make codegen` delegates to the
# `yclass-codegen` target of the configured desktop build.
#
# Modules REGISTER THEMSELVES: a yclass module's own CMakeLists.txt calls
#
#   yetty_yclass_module(<name> [COMPAT_HEADER] [SOURCE_DIR <dir>]
#                              [DEFINES <macro>...])
#
# and the deferred finalizer below turns the registrations into the
# two-pass target graph at the end of the top-level configure. There is
# no central module list anywhere — the module's CMakeLists is the single
# registration point, and the calling directory is the module's source
# dir. SOURCE_DIR (repo-root-relative) overrides that for a module whose
# annotated sources are compiled by another module's target (yapp lives
# in yplatform's build). DEFINES are the feature-guard macros the codegen clang parse needs
# so annotations behind #ifdef are visible (they sit next to the module's
# own target_compile_definitions, where they cannot drift). COMPAT_HEADER
# additionally emits the legacy-path header for modules whose old
# include/yetty/<module>/ headers still have un-flipped consumers.
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
# The codegen clang parse takes its flags from THIS build's
# compile_commands.json, so the target only exists in builds that export
# it (the desktop builds do); registrations elsewhere are inert.

function(yetty_yclass_module module_name)
    cmake_parse_arguments(module_arg "COMPAT_HEADER" "SOURCE_DIR" "DEFINES" ${ARGN})
    if(module_arg_SOURCE_DIR)
        set(module_source_dir ${module_arg_SOURCE_DIR})
    else()
        file(RELATIVE_PATH module_source_dir ${YETTY_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    set(module_compat 0)
    if(module_arg_COMPAT_HEADER)
        set(module_compat 1)
    endif()
    string(JOIN " " module_defines ${module_arg_DEFINES})
    set_property(GLOBAL APPEND PROPERTY YETTY_YCLASS_MODULES
                 "${module_name}|${module_source_dir}|${module_defines}|${module_compat}")
endfunction()

if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
    return()
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "yclass-codegen target disabled: no python3 interpreter found")
    return()
endif()

function(yetty_yclass_codegen_finalize)
    set(yclass_codegen_driver ${YETTY_ROOT}/src/yetty/yclass/gen/driver.py)
    set(yclass_codegen_generator ${YETTY_ROOT}/src/yetty/yclass/gen/codegen.py)
    set(yclass_codegen_stamp_dir ${CMAKE_BINARY_DIR}/yclass-codegen)
    set(yclass_codegen_compile_db ${CMAKE_BINARY_DIR}/compile_commands.json)
    file(MAKE_DIRECTORY ${yclass_codegen_stamp_dir})

    get_property(yclass_codegen_module_lines GLOBAL PROPERTY YETTY_YCLASS_MODULES)
    if(NOT yclass_codegen_module_lines)
        message(STATUS "yclass-codegen target disabled: no modules registered")
        return()
    endif()

    set(yclass_codegen_pass1_stamps "")
    foreach(module_line IN LISTS yclass_codegen_module_lines)
        string(REPLACE "|" ";" module_parts "${module_line}")
        list(GET module_parts 0 module_name)
        list(GET module_parts 1 module_source_dir)
        list(GET module_parts 2 module_defines)
        list(GET module_parts 3 module_compat)
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
                    --source-dir ${module_source_dir}
                    --defines "${module_defines}"
                    --compat-header ${module_compat}
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
        list(GET module_parts 1 module_source_dir)
        list(GET module_parts 2 module_defines)
        list(GET module_parts 3 module_compat)
        add_custom_command(
            OUTPUT ${yclass_codegen_stamp_dir}/${module_name}.pass2
            COMMAND ${Python3_EXECUTABLE} ${yclass_codegen_driver}
                    --run-if-headers-changed ${module_name}
                    --source-dir ${module_source_dir}
                    --defines "${module_defines}"
                    --compat-header ${module_compat}
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
endfunction()

cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL yetty_yclass_codegen_finalize)
