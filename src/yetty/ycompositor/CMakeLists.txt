add_library(yetty_ycompositor STATIC
    compositor.c
)

target_include_directories(yetty_ycompositor PUBLIC
    ${YETTY_ROOT}/include
)

# yfigure for the base figure type; ycore for result/types; ygrid for
# the per-group ygrid figures the compositor creates from CMD_GROUP
# wire records; ywire for the OSC-statemachine drawable iterator used
# by yetty_ycompositor_process_input; ydraw_core for the flyweight
# registry + drawable command parser used by both
# process_input and process_records.
#
# No terminal_layer base, no yterm dependency — the compositor is a
# standalone module with its own direct API (add_figure / process_input /
# process_records). The yetty/yetty.h include in compositor.c pulls in
# the runtime context type; webgpu types arrive transitively through
# yetty_ygrid (the only consumer that actually touches WGPU).
target_link_libraries(yetty_ycompositor PUBLIC
    yetty_ycore
    yetty_yfigure
    yetty_ygrid
    yetty_ywire
    yetty_ydraw_core
)
