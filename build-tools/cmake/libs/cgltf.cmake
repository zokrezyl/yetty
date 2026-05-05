# cgltf - single-header glTF 2.0 parser (MIT)
if(TARGET cgltf)
    return()
endif()

CPMAddPackage(
    NAME cgltf
    GITHUB_REPOSITORY jkuhlmann/cgltf
    GIT_TAG v1.14
    DOWNLOAD_ONLY YES
)

if(cgltf_ADDED)
    file(WRITE ${CMAKE_BINARY_DIR}/cgltf_impl.c
"#define CGLTF_IMPLEMENTATION
#include \"cgltf.h\"
")

    add_library(cgltf STATIC ${CMAKE_BINARY_DIR}/cgltf_impl.c)
    target_include_directories(cgltf PUBLIC ${cgltf_SOURCE_DIR})
endif()
