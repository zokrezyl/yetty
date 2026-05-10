add_library(yetty_ypaint_yaml STATIC
    ypaint-yaml.c
)

target_include_directories(yetty_ypaint_yaml PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_ypaint_yaml PUBLIC
    yaml
    yetty_ypaint_core
    yetty_ysdf
    yetty_yplot
    yetty_yfsvm
)

# Optional: fontconfig for the `font: <name>` lookup in text elements.
# Linux/desktop only — webasm/android/ios resolve fonts via different
# mechanisms. We need both the headers AND a transitive link of the
# static libraries so the final executable picks up FcInit / FcFontMatch
# AFTER yetty_ypaint_yaml in the link line (linker resolves left-to-right).
if(NOT EMSCRIPTEN AND NOT YETTY_ANDROID AND NOT YETTY_IOS AND NOT YETTY_TVOS AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FONTCONFIG QUIET fontconfig)
        if(FONTCONFIG_FOUND)
            target_include_directories(yetty_ypaint_yaml PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
            target_compile_definitions(yetty_ypaint_yaml PRIVATE YETTY_HAS_FONTCONFIG=1)
            # Resolve to the same static archives the executable uses (see
            # build-tools/cmake/targets/linux.cmake), then attach them as
            # INTERFACE link deps so anything pulling yetty_ypaint_yaml
            # also pulls fontconfig + expat + uuid in the right order.
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
                target_link_libraries(yetty_ypaint_yaml INTERFACE
                    ${_FC_STATIC} ${_EXPAT_STATIC} ${_UUID_STATIC})
                # Homebrew/MacPorts fontconfig is built with NLS, so the
                # static archive references libintl_dgettext from GNU gettext.
                if(APPLE)
                    find_library(_INTL_LIB intl
                        PATHS /usr/local/lib /opt/local/lib /opt/homebrew/lib)
                    if(_INTL_LIB)
                        target_link_libraries(yetty_ypaint_yaml INTERFACE ${_INTL_LIB})
                    endif()
                endif()
            else()
                target_link_libraries(yetty_ypaint_yaml INTERFACE
                    ${FONTCONFIG_LIBRARIES})
            endif()
        endif()
    endif()
endif()
