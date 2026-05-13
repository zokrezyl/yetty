# yxml — small streaming XML parser (Yorhel/yxml).
#
# Single .c + .h pair, MIT-licensed, pulled via CPM. yxml.c is pre-
# generated from yxml-states + yxml-gen.pl and committed upstream, so
# we just compile the .c as-is — no codegen step needed at build time.
#
# Used by yetty_ysvg to parse SVG documents.

if(TARGET yxml)
    return()
endif()

CPMAddPackage(
    NAME yxml
    GIT_REPOSITORY https://g.blicky.net/yxml.git
    # Upstream doesn't tag releases — pin to a commit. This SHA is post-
    # the v1.0 release and includes a handful of post-release fixes.
    GIT_TAG 66507906673bc6159d5d620414479954c9c21c24
    DOWNLOAD_ONLY YES
)

if(yxml_ADDED)
    add_library(yxml STATIC ${yxml_SOURCE_DIR}/yxml.c)
    target_include_directories(yxml SYSTEM PUBLIC ${yxml_SOURCE_DIR})
    if(MSVC)
        target_compile_options(yxml PRIVATE /w)
    else()
        target_compile_options(yxml PRIVATE -w)
    endif()
    set_target_properties(yxml PROPERTIES POSITION_INDEPENDENT_CODE ON)
    message(STATUS "yxml: using Yorhel/yxml v1.0 via CPM")
endif()
