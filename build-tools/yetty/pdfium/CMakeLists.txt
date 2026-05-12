# CMake wrapper for PDFium (prebuilt binaries from bblanchon/pdfium-binaries)
# This uses prebuilt static libraries with BSD-3-Clause license (MIT compatible)

cmake_minimum_required(VERSION 3.16)

# PDFium source directory (set by CPMAddPackage)
if(NOT DEFINED pdfium_SOURCE_DIR)
    message(FATAL_ERROR "pdfium_SOURCE_DIR must be set by CPMAddPackage")
endif()

set(PDFIUM_DIR ${pdfium_SOURCE_DIR})

message(STATUS "Using PDFium from: ${PDFIUM_DIR}")

# The prebuilt package structure is:
#   lib/libpdfium.a (or .so/.dylib)
#   include/pdfium/*.h

# Check for static library
if(EXISTS "${PDFIUM_DIR}/lib/libpdfium.a")
    set(PDFIUM_LIBRARY "${PDFIUM_DIR}/lib/libpdfium.a")
    set(PDFIUM_LIBRARY_TYPE STATIC)
elseif(EXISTS "${PDFIUM_DIR}/lib/libpdfium.so")
    set(PDFIUM_LIBRARY "${PDFIUM_DIR}/lib/libpdfium.so")
    set(PDFIUM_LIBRARY_TYPE SHARED)
elseif(EXISTS "${PDFIUM_DIR}/lib/libpdfium.dylib")
    set(PDFIUM_LIBRARY "${PDFIUM_DIR}/lib/libpdfium.dylib")
    set(PDFIUM_LIBRARY_TYPE SHARED)
else()
    message(FATAL_ERROR "PDFium library not found in ${PDFIUM_DIR}/lib")
endif()

# Check for headers
if(NOT EXISTS "${PDFIUM_DIR}/include/fpdfview.h")
    message(FATAL_ERROR "PDFium headers not found in ${PDFIUM_DIR}/include")
endif()

# Create imported library target
add_library(pdfium_lib ${PDFIUM_LIBRARY_TYPE} IMPORTED GLOBAL)

set_target_properties(pdfium_lib PROPERTIES
    IMPORTED_LOCATION "${PDFIUM_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${PDFIUM_DIR}/include"
)

# On Linux, PDFium needs some system libraries
if(UNIX AND NOT APPLE)
    # PDFium static lib needs pthread and dl
    set_property(TARGET pdfium_lib APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES pthread dl
    )
endif()

# On macOS, link against required frameworks
if(APPLE)
    set_property(TARGET pdfium_lib APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreText"
    )
endif()

message(STATUS "PDFium library: ${PDFIUM_LIBRARY} (${PDFIUM_LIBRARY_TYPE})")
