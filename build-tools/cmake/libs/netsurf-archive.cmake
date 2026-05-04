# Helper invoked by netsurf.cmake to ar all NetSurf core .o files
# (everything under ${BUILDDIR}, EXCEPT frontends/*) into a single
# libnetsurf_core.a. Frontends/* are excluded so we can plug our own
# ynetsurf frontend tables in place of monkey's.
#
# Args (passed via -D):
#   BUILDPARENT — netsurf/build/  (we glob *-monkey* under it)
#   OUTPUT      — absolute path of the .a to produce
#   AR          — archiver binary (CMAKE_AR)

if(NOT BUILDPARENT OR NOT OUTPUT OR NOT AR)
    message(FATAL_ERROR "netsurf-archive: BUILDPARENT/OUTPUT/AR required")
endif()

file(GLOB _CANDS LIST_DIRECTORIES true "${BUILDPARENT}/*-monkey*")
set(BUILDDIR "")
foreach(d IN LISTS _CANDS)
    if(IS_DIRECTORY "${d}")
        set(BUILDDIR "${d}")
        break()
    endif()
endforeach()
if(NOT BUILDDIR)
    message(FATAL_ERROR
        "netsurf-archive: no *-monkey* build dir under ${BUILDPARENT}")
endif()

file(GLOB_RECURSE _OBJS
    LIST_DIRECTORIES false
    RELATIVE "${BUILDDIR}"
    "${BUILDDIR}/*.o")

set(_INCLUDE)
foreach(o IN LISTS _OBJS)
    # Match both path form (frontends/...) and netsurf's flattened object
    # naming (frontends_monkey_main.o).
    if(NOT o MATCHES "^frontends" AND NOT o MATCHES "^build_.*_frontends_")
        list(APPEND _INCLUDE "${BUILDDIR}/${o}")
    endif()
endforeach()

if(NOT _INCLUDE)
    message(FATAL_ERROR "netsurf-archive: no .o files under ${BUILDDIR}")
endif()

# Write argfile to keep the command line short — netsurf has hundreds
# of objects.
set(_ARGFILE "${BUILDDIR}/.netsurf-core.objs")
file(WRITE "${_ARGFILE}" "")
foreach(o IN LISTS _INCLUDE)
    file(APPEND "${_ARGFILE}" "${o}\n")
endforeach()

file(REMOVE "${OUTPUT}")
execute_process(
    COMMAND ${AR} rcs "${OUTPUT}" "@${_ARGFILE}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "ar rcs ${OUTPUT} failed (rc=${_rc})")
endif()
message(STATUS "netsurf-archive: ${OUTPUT} (${_INCLUDE} objects)")
