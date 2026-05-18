# Demo programs — gated by YETTY_ENABLE_FEATURE_DEMO.
# Each subdirectory is a self-contained family of demos.

if(YETTY_ENABLE_FEATURE_YGUI)
    add_subdirectory(ygui)
endif()

if(YETTY_ENABLE_FEATURE_YMGUI)
    add_subdirectory(ymgui)
endif()

# yplot demos — pure CPU client tools that emit yplot OSC envelopes.
# Linked against yetty_yplot_core only (no Dawn / WebGPU), so they build
# wherever the wire-format core builds.
add_subdirectory(yplot)
