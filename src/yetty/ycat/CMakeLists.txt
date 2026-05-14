# ycat - MIME-dispatched cat library

set(YCAT_SOURCES
    ycat.c
    detect.c
    handler-markdown.c
    handler-pdf.c
    handler-image.c
    handler-svg.c
    osc.c
    fetch.c
    ts-grammars.c
    ts-highlight.c
)

# Mermaid handler is optional — only built when ydiagram is enabled. The
# detect.c content sniffer also uses the ydiagram parser, so detect.c
# guards that call on the same compile-time flag.
if(YETTY_ENABLE_FEATURE_DIAGRAM)
    list(APPEND YCAT_SOURCES handler-mermaid.c)
endif()

#=============================================================================
# Tree-sitter highlight queries are embedded at configure time. Each grammar
# exports TS_QUERIES_DIR_<name> (see build-tools/yetty/libs/tree-sitter.cmake).
# Some child grammars (cpp, typescript) only add rules; inherit from parent.
#=============================================================================

set(TS_GRAMMAR_NAMES
    c cpp python javascript typescript rust go java bash json yaml toml html xml markdown)
set(TS_QUERY_PARENT_cpp "c")
set(TS_QUERY_PARENT_typescript "javascript")
set(TS_GENERATED_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ts_queries_generated.h")

if(YETTY_ENABLE_LIB_TREESITTER)
    set(_TS_HEADER "// Auto-generated — do not edit\n#pragma once\n\n")
    foreach(NAME ${TS_GRAMMAR_NAMES})
        set(_QUERY_FILE "${TS_QUERIES_DIR_${NAME}}/highlights.scm")
        if(EXISTS "${_QUERY_FILE}")
            set(_QUERY_CONTENT "")
            if(DEFINED TS_QUERY_PARENT_${NAME})
                set(_PARENT "${TS_QUERY_PARENT_${NAME}}")
                set(_PARENT_FILE "${TS_QUERIES_DIR_${_PARENT}}/highlights.scm")
                if(EXISTS "${_PARENT_FILE}")
                    file(READ "${_PARENT_FILE}" _QUERY_CONTENT)
                    string(APPEND _QUERY_CONTENT "\n")
                endif()
            endif()
            file(READ "${_QUERY_FILE}" _CHILD_CONTENT)
            string(APPEND _QUERY_CONTENT "${_CHILD_CONTENT}")
            string(REPLACE "\\" "\\\\" _QUERY_CONTENT "${_QUERY_CONTENT}")
            string(REPLACE "\"" "\\\"" _QUERY_CONTENT "${_QUERY_CONTENT}")
            string(REPLACE "\n" "\\n\"\n\"" _QUERY_CONTENT "${_QUERY_CONTENT}")
            string(APPEND _TS_HEADER
                "static const char TS_QUERY_${NAME}[] =\n\"${_QUERY_CONTENT}\";\n\n")
        else()
            string(APPEND _TS_HEADER
                "static const char TS_QUERY_${NAME}[] = \"\";\n\n")
        endif()
    endforeach()
    file(WRITE "${TS_GENERATED_HEADER}" "${_TS_HEADER}")
else()
    # Emit empty stubs so ts-grammars.c compiles even without the library.
    set(_TS_HEADER "// Auto-generated — tree-sitter disabled\n#pragma once\n\n")
    foreach(NAME ${TS_GRAMMAR_NAMES})
        string(APPEND _TS_HEADER
            "static const char TS_QUERY_${NAME}[] = \"\";\n")
    endforeach()
    file(WRITE "${TS_GENERATED_HEADER}" "${_TS_HEADER}")
endif()

#=============================================================================
# Library target
#=============================================================================

add_library(yetty_ycat STATIC ${YCAT_SOURCES})

target_include_directories(yetty_ycat PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_include_directories(yetty_ycat PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}   # for ts_queries_generated.h
)

target_link_libraries(yetty_ycat PUBLIC
    yetty_ycore
    yetty_yface
    yetty_ypaint_core
    yetty_ymarkdown
    yetty_ypdf
    yetty_ysvg
    yetty_yfont_core
    yetty_yimage_core
    pdfio_lib
)

if(YETTY_ENABLE_FEATURE_DIAGRAM)
    target_link_libraries(yetty_ycat PUBLIC yetty_ydiagram)
    target_compile_definitions(yetty_ycat PUBLIC YETTY_YCAT_HAS_DIAGRAM=1)
endif()

if(YETTY_ENABLE_LIB_LIBMAGIC AND TARGET magic)
    target_link_libraries(yetty_ycat PUBLIC magic)
    target_compile_definitions(yetty_ycat PUBLIC YETTY_YCAT_HAS_LIBMAGIC=1)
    if(DEFINED LIBMAGIC_MGC_PATH)
        target_compile_definitions(yetty_ycat PRIVATE
            YETTY_YCAT_MAGIC_MGC_PATH="${LIBMAGIC_MGC_PATH}")
    endif()
endif()

if(YETTY_ENABLE_LIB_LIBCURL)
    if(TARGET CURL::libcurl)
        message(STATUS "ycat: CURL::libcurl target visible, linking")
    else()
        message(WARNING "ycat: YETTY_ENABLE_LIB_LIBCURL=ON but CURL::libcurl not visible")
    endif()
endif()
if(YETTY_ENABLE_LIB_LIBCURL AND TARGET CURL::libcurl)
    target_link_libraries(yetty_ycat PUBLIC CURL::libcurl)
    target_compile_definitions(yetty_ycat PUBLIC YETTY_YCAT_HAS_LIBCURL=1)
endif()

if(YETTY_ENABLE_LIB_TREESITTER AND TARGET tree-sitter-core)
    target_compile_definitions(yetty_ycat PUBLIC YETTY_YCAT_HAS_TREESITTER=1)
    target_link_libraries(yetty_ycat PUBLIC
        tree-sitter-core
        ts-grammar-c
        ts-grammar-cpp
        ts-grammar-python
        ts-grammar-javascript
        ts-grammar-typescript
        ts-grammar-rust
        ts-grammar-go
        ts-grammar-java
        ts-grammar-bash
        ts-grammar-json
        ts-grammar-yaml
        ts-grammar-toml
        ts-grammar-html
        ts-grammar-xml
        ts-grammar-markdown
    )
endif()
