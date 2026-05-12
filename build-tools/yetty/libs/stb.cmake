# stb - single-file public domain libraries
if(TARGET stb)
    return()
endif()

CPMAddPackage(
    NAME stb
    GITHUB_REPOSITORY nothings/stb
    GIT_TAG master
    DOWNLOAD_ONLY YES
)

if(stb_ADDED)
    # Create implementation file
    file(WRITE ${CMAKE_BINARY_DIR}/stb_impl.cpp
"#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include \"stb_image.h\"
#include \"stb_image_write.h\"
")

    add_library(stb STATIC ${CMAKE_BINARY_DIR}/stb_impl.cpp)
    target_include_directories(stb PUBLIC ${stb_SOURCE_DIR})
endif()
