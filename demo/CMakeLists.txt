# Demo programs — gated by YETTY_ENABLE_FEATURE_DEMO.
# Each subdirectory is a self-contained family of demos.

if(YETTY_ENABLE_FEATURE_YGUI)
    add_subdirectory(ygui)
endif()

if(YETTY_ENABLE_FEATURE_YMGUI)
    add_subdirectory(ymgui)
endif()

if(YETTY_ENABLE_FEATURE_YRDAWN)
    add_subdirectory(yrdawn)
endif()

# yplot demos — pure CPU client tools that emit yplot OSC envelopes.
# Linked against yetty_yplot_core only (no Dawn / WebGPU), so they build
# wherever the wire-format core builds.
add_subdirectory(yplot)

# yvideo demos — same pattern: client-side tool emits yvideo SCENE_BIN
# envelopes (INIT then a stream of CMD_UPDATE NAL chunks).
if(YETTY_ENABLE_FEATURE_YVIDEO)
    add_subdirectory(yvideo)
endif()

# ydummy demos — client/server halves of the yclass codegen split pilot.
# The client half is GPU-free and builds everywhere; the server half
# self-gates on YETTY_ENABLE_LIB_WEBGPU.
add_subdirectory(ydummy)
