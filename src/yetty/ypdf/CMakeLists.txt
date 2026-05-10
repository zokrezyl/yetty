# ypdf - PDF → ypaint buffer rendering
add_library(yetty_ypdf STATIC
    pdf-content-parser.c
    pdf-renderer.c
)

target_include_directories(yetty_ypdf PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ypdf PUBLIC
    yetty_ycore
    yetty_ypaint_core
    yetty_yfont
    yetty_ysdf
    pdfio_lib
)

# Optional: fontconfig for system-font substitution when a referenced PDF
# font isn't embedded (very common for "standard" fonts like Arial / Times
# / Helvetica). Mirrors the wiring in src/yetty/ypaint-yaml/CMakeLists.txt.
if(NOT EMSCRIPTEN AND NOT YETTY_ANDROID AND NOT YETTY_IOS AND NOT YETTY_TVOS AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FONTCONFIG QUIET fontconfig)
        if(FONTCONFIG_FOUND)
            target_include_directories(yetty_ypdf PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
            target_compile_definitions(yetty_ypdf PRIVATE YETTY_HAS_FONTCONFIG=1)
            find_library(_FC_STATIC libfontconfig.a
                PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
                      /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib
                      /usr/local/lib /opt/local/lib /opt/homebrew/lib)
            find_library(_EXPAT_STATIC libexpat.a
                PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
                      /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib
                      /usr/local/lib /opt/local/lib /opt/homebrew/lib)
            find_library(_UUID_STATIC libuuid.a
                PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
                      /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib
                      /usr/local/lib /opt/local/lib /opt/homebrew/lib)
            if(_FC_STATIC AND _EXPAT_STATIC AND _UUID_STATIC)
                target_link_libraries(yetty_ypdf INTERFACE
                    ${_FC_STATIC} ${_EXPAT_STATIC} ${_UUID_STATIC})
                # Homebrew/MacPorts fontconfig is built with NLS, so the
                # static archive references libintl_dgettext from GNU gettext.
                if(APPLE)
                    find_library(_INTL_LIB intl
                        PATHS /usr/local/lib /opt/local/lib /opt/homebrew/lib)
                    if(_INTL_LIB)
                        target_link_libraries(yetty_ypdf INTERFACE ${_INTL_LIB})
                    endif()
                endif()
            else()
                target_link_libraries(yetty_ypdf INTERFACE ${FONTCONFIG_LIBRARIES})
            endif()
        endif()
    endif()
endif()
