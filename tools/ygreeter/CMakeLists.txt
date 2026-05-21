# ygreeter — the "first contact" tool shown on the RISCV Linux VM (also
# loadable as a regular yetty tool). Demos the ygui tabbar + rich widgets:
# four tabs (Welcome / Plots / Images / Code), each with a small tree
# nav on the left and a rich content surface on the right.
#
# ytrace + the per-platform yplatform impls (thread/term/fs/time) are
# compiled per-final-target — they're not standalone libraries. Use
# YETTY_YPLATFORM_THREAD_SOURCES which shared.cmake populates with the
# correct {default,windows}.c picks so the build stays portable. (The
# variable name predates fs/ being lumped in; same shape as the main
# yetty target.)

add_executable(ygreeter
    main.c
    embedded-assets.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ygreeter PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

# ygui carries everything ygreeter needs: ydraw-core, ysdf, ycore,
# yface, yfont_core, ydraw-yaml (rich's create_from_yaml), plus the
# yplot / yimage / yvideo prim serializers used by the embedded
# ygui_yplot / ygui_yimage / ygui_yvideo widgets. yplatform_core
# stays explicit — ygreeter calls yetty_yplatform_* directly.
target_link_libraries(ygreeter PRIVATE ygui yetty_yplatform_core)

# Video tab — drives the ygui_yvideo widget. The widget lives inside
# ygui_core now (yetty_yvideo_core + yvideo-mp4.c are PUBLIC link
# transitives via ygui), so ygreeter doesn't need its own minimp4 /
# yvideo linkage. The compile define just gates the Video tab code
# in main.c on the feature flag.
if(YETTY_ENABLE_FEATURE_YVIDEO)
    target_compile_definitions(ygreeter PRIVATE YGREETER_HAS_YVIDEO)
endif()

# Optional showcase widgets — each gated on the feature flag that turns
# the underlying backend on. Missing flag → tab silently absent.
if(YETTY_ENABLE_FEATURE_YMARKDOWN)
    target_link_libraries(ygreeter PRIVATE ygui_ymarkdown)
    target_compile_definitions(ygreeter PRIVATE YGREETER_HAS_YMARKDOWN)
endif()
if(YETTY_ENABLE_FEATURE_YBROWSER)
    target_link_libraries(ygreeter PRIVATE ygui_ybrowser)
    target_compile_definitions(ygreeter PRIVATE YGREETER_HAS_YBROWSER)
endif()
if(YETTY_ENABLE_FEATURE_YPDF)
    target_link_libraries(ygreeter PRIVATE ygui_ypdf)
    target_compile_definitions(ygreeter PRIVATE YGREETER_HAS_YPDF)
endif()
if(YETTY_ENABLE_FEATURE_YZOO)
    target_link_libraries(ygreeter PRIVATE ygui_yzoo)
    target_compile_definitions(ygreeter PRIVATE YGREETER_HAS_YZOO)
endif()
if(YETTY_ENABLE_FEATURE_YJUNGLE)
    target_link_libraries(ygreeter PRIVATE ygui_yjungle)
    target_compile_definitions(ygreeter PRIVATE YGREETER_HAS_YJUNGLE)
endif()

if(NOT MSVC)
    target_compile_options(ygreeter PRIVATE -Wall -Wextra)
endif()

# Self-contained ygreeter: embed the assets that drive the structurally
# required tabs (Images, Video) plus the small repo samples that feed
# the optional Markdown / Browser / PDF tabs. Mirrors yetty_embed_assets
# at a much smaller scale — a single "data/" section, extracted on first
# run into <data_dir>/ (i.e. ~/.local/share/yetty on Linux). The
# YETTY_ENABLE_LIB_INCBIN guard lets the dev build skip embedding;
# embedded-assets.c becomes a no-op then and main.c falls back to the
# argv0-relative lookup.
if(YETTY_ENABLE_LIB_INCBIN)
    include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)

    yetty_compute_build_version()
    target_compile_definitions(ygreeter PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")

    set(_YGREETER_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embed-data")
    file(REMOVE_RECURSE "${_YGREETER_EMBED_DIR}")
    file(MAKE_DIRECTORY "${_YGREETER_EMBED_DIR}")

    # Structurally required by the Images / Video tabs.
    foreach(_F
            "assets/logo-1.jpeg"
            "assets/logo-2.jpeg"
            "assets/logo-3.jpeg"
            "assets/logo-4.jpeg"
            "assets/yetty-unchained-2.mp4")
        if(EXISTS "${YETTY_ROOT}/${_F}")
            get_filename_component(_NAME "${_F}" NAME)
            configure_file("${YETTY_ROOT}/${_F}"
                "${_YGREETER_EMBED_DIR}/${_NAME}" COPYONLY)
        endif()
    endforeach()

    # Repo samples for the optional widget tabs. Flat names — main.c
    # probes <data_dir>/<basename> for these, not their original repo path.
    foreach(_PAIR
            "README.md|README.md"
            "demo/ygui/26_ybrowser/sample.html|sample.html"
            "test/ut/ypdf/test-comprehensive.pdf|pdf-sample.pdf")
        string(REPLACE "|" ";" _PARTS "${_PAIR}")
        list(GET _PARTS 0 _SRC)
        list(GET _PARTS 1 _DST)
        if(EXISTS "${YETTY_ROOT}/${_SRC}")
            configure_file("${YETTY_ROOT}/${_SRC}"
                "${_YGREETER_EMBED_DIR}/${_DST}" COPYONLY)
        endif()
    endforeach()

    incbin_add_directory(ygreeter "data" "${_YGREETER_EMBED_DIR}" "*" TRUE)

    # incbin_add_directory only adds the brotli headers on the include
    # path via the brotlidec IMPORTED target; the .a goes here.
    include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)
    target_link_libraries(ygreeter PRIVATE brotlidec brotlicommon)
endif()
