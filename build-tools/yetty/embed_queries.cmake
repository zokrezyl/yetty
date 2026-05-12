# embed_queries.cmake — invoked at build time to embed highlights.scm as C strings
#
# Expected variables:
#   GRAMMAR_NAMES  — semicolon-separated list of grammar names
#   QUERY_DIRS     — semicolon-separated list of query directories (parallel to names)
#   OUTPUT_FILE    — path to write the generated header

set(CONTENT "// Auto-generated — do not edit\n#pragma once\n\n")

list(LENGTH GRAMMAR_NAMES COUNT)
math(EXPR LAST "${COUNT} - 1")

foreach(IDX RANGE ${LAST})
    list(GET GRAMMAR_NAMES ${IDX} NAME)
    list(GET QUERY_DIRS ${IDX} QDIR)

    set(QUERY_FILE "${QDIR}/highlights.scm")
    if(EXISTS "${QUERY_FILE}")
        file(READ "${QUERY_FILE}" QUERY_CONTENT)
        # Escape backslashes, then double-quotes, then newlines
        string(REPLACE "\\" "\\\\" QUERY_CONTENT "${QUERY_CONTENT}")
        string(REPLACE "\"" "\\\"" QUERY_CONTENT "${QUERY_CONTENT}")
        string(REPLACE "\n" "\\n\"\n\"" QUERY_CONTENT "${QUERY_CONTENT}")
        string(APPEND CONTENT "static const char TS_QUERY_${NAME}[] =\n\"${QUERY_CONTENT}\";\n\n")
    else()
        # Empty query fallback
        string(APPEND CONTENT "static const char TS_QUERY_${NAME}[] = \"\";\n\n")
    endif()
endforeach()

file(WRITE "${OUTPUT_FILE}" "${CONTENT}")
