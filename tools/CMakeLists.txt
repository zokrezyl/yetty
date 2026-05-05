if(YETTY_ENABLE_TOOL_GPU_INFO)
    add_subdirectory(gpu-info)
endif()

if(YETTY_ENABLE_TOOL_CARD_RUNNER)
    add_subdirectory(card-runner)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_GENERATOR)
    add_subdirectory(ydraw-generator)
endif()

if(YETTY_ENABLE_TOOL_YPAINT_BENCH)
    add_subdirectory(ypaint-bench)
endif()

if(YETTY_ENABLE_TOOL_YCAT)
    add_subdirectory(ycat)
endif()

if(YETTY_ENABLE_TOOL_YECHO)
    add_subdirectory(yecho)
endif()

if(YETTY_ENABLE_TOOL_YPLOT)
    add_subdirectory(yplot)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_MAZE)
    add_subdirectory(ydraw-maze)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_ZOO)
    add_subdirectory(ydraw-zoo)
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

if(YETTY_ENABLE_TOOL_DECODE_YPAINT)
    add_subdirectory(decode-ypaint)
endif()

# MSDF toolbox — CDB viewers/diff/generators/atlas-viewer. Pure C
# tooling, always built (the bits that need WGPU bring it in themselves).
add_subdirectory(msdf)
