if(YETTY_ENABLE_TOOL_GPU_INFO)
    add_subdirectory(gpu-info)
endif()

if(YETTY_ENABLE_TOOL_CARD_RUNNER)
    add_subdirectory(card-runner)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_GENERATOR)
    add_subdirectory(ydraw-generator)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_BENCH)
    add_subdirectory(ydraw-bench)
endif()

if(YETTY_ENABLE_TOOL_YCAT)
    add_subdirectory(ycat)
endif()

if(YETTY_ENABLE_TOOL_YECHO)
    add_subdirectory(yecho)
endif()

if(YETTY_ENABLE_TOOL_YWASM_DEMO)
    add_subdirectory(ywasm-demo)
endif()

if(YETTY_ENABLE_TOOL_YPLOT)
    add_subdirectory(yplot)
endif()

if(YETTY_ENABLE_TOOL_YVIDEO AND YETTY_ENABLE_FEATURE_YVIDEO)
    add_subdirectory(yvideo)
endif()

if(YETTY_ENABLE_TOOL_YTOP)
    add_subdirectory(ytop)
endif()

if(YETTY_ENABLE_TOOL_YGREETER)
    add_subdirectory(ygreeter)
endif()

if(YETTY_ENABLE_TOOL_YMESH)
    add_subdirectory(ymesh)
endif()

if(YETTY_ENABLE_TOOL_YMAZE)
    add_subdirectory(ymaze)
endif()

if(YETTY_ENABLE_TOOL_YZOO)
    add_subdirectory(yzoo)
endif()

if(YETTY_ENABLE_TOOL_YJUNGLE)
    add_subdirectory(yjungle)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_MAZE)
    add_subdirectory(ydraw-maze)
endif()

if(YETTY_ENABLE_TOOL_YMUX)
    add_subdirectory(ymux)
endif()

if(YETTY_ENABLE_TOOL_YFLAME)
    add_subdirectory(yflame)
endif()

if(YETTY_ENABLE_TOOL_PDF2YDRAW)
    add_subdirectory(pdf2ydraw)
endif()

if(YETTY_ENABLE_TOOL_HTML2YDRAW)
    add_subdirectory(html2ydraw)
endif()

if(YETTY_ENABLE_TOOL_YHTML_MACHINE)
    add_subdirectory(yhtml-machine)
endif()

if(YETTY_ENABLE_TOOL_YBROWSER)
    add_subdirectory(ybrowser)
endif()

if(YETTY_ENABLE_TOOL_YDOC OR YETTY_ENABLE_TOOL_YSHEET OR YETTY_ENABLE_TOOL_YSLIDE)
    add_subdirectory(yrich-runner)
endif()

if(YETTY_ENABLE_TOOL_YDOC)
    add_subdirectory(ydoc)
endif()

if(YETTY_ENABLE_TOOL_YSHEET)
    add_subdirectory(ysheet)
endif()

if(YETTY_ENABLE_TOOL_YSLIDE)
    add_subdirectory(yslide)
endif()

if(YETTY_ENABLE_TOOL_YTHORVG AND YETTY_ENABLE_FEATURE_YTHORVG AND YETTY_ENABLE_LIB_THORVG)
    add_subdirectory(ythorvg)
endif()

if(YETTY_ENABLE_TOOL_DECODE_YDRAW)
    add_subdirectory(decode-ydraw)
endif()

# ydiagram CLI links the yetty_ydiagram static lib (built in
# src/yetty/ydiagram) — that lib only exists when FEATURE_YDIAGRAM is ON.
# Without this AND clause, default builds (TOOL=ON, FEATURE=OFF) try to
# link `-lyetty_ydiagram` against a non-existent target → ld error.
if(YETTY_ENABLE_TOOL_YDIAGRAM AND YETTY_ENABLE_FEATURE_YDIAGRAM)
    add_subdirectory(ydiagram)
endif()

# MSDF toolbox — CDB viewers/diff/generators/atlas-viewer. Pure C
# tooling, always built (the bits that need WGPU bring it in themselves).
add_subdirectory(msdf)

if(YETTY_ENABLE_TOOL_YNETSURF)
    add_subdirectory(ynetsurf)
endif()

if(YETTY_ENABLE_TOOL_YLEXBOR)
    add_subdirectory(ylexbor)
endif()
