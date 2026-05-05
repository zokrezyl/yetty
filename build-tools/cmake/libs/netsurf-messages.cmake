# Helper invoked by netsurf.cmake to generate a per-language Messages
# file from NetSurf's combined FatMessages source, using the host-built
# split-messages tool that `make TARGET=monkey` builds as a side effect.
#
# Args (-D):
#   BUILDPARENT  — netsurf/build/ (we glob *-monkey* under it for tools/split-messages)
#   FATMESSAGES  — path to resources/FatMessages
#   OUTPUT       — destination file (single-language Messages)

if(NOT BUILDPARENT OR NOT FATMESSAGES OR NOT OUTPUT)
    message(FATAL_ERROR "netsurf-messages: BUILDPARENT/FATMESSAGES/OUTPUT required")
endif()

file(GLOB _CANDS LIST_DIRECTORIES true "${BUILDPARENT}/*-monkey*")
set(_SPLIT "")
foreach(d IN LISTS _CANDS)
    if(IS_DIRECTORY "${d}" AND EXISTS "${d}/tools/split-messages")
        set(_SPLIT "${d}/tools/split-messages")
        break()
    endif()
endforeach()
if(NOT _SPLIT)
    message(FATAL_ERROR
        "netsurf-messages: tools/split-messages not found under ${BUILDPARENT}/*-monkey*")
endif()

execute_process(
    COMMAND "${_SPLIT}" -l en -p any -f messages -o "${OUTPUT}" "${FATMESSAGES}"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "split-messages failed (rc=${_rc})")
endif()
message(STATUS "netsurf-messages: ${OUTPUT}")
