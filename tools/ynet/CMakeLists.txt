# ynet — network capture and observability (thin frontend over yetty_ynet).
#
# Headless dump mode in this milestone: loads a pcap/pcapng file and prints a
# packet table + flow summary. The interactive pane (ygui) and live capture
# land next. Skips itself when the module is absent (no libpcap) or off-Linux.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()
if(NOT TARGET yetty_ynet)
    # libpcap missing → yetty_ynet was not created; nothing to build against.
    return()
endif()

# ytrace is compiled per-final-target (not a standalone library), matching the
# other tool frontends.
add_executable(ynet
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ynet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# yetty_ynet PUBLIC-links yetty_ycore + yetty_yclass, pulled in transitively.
# yetty_yplatform_core provides the ytrace mutex/timestamp backend; Threads is
# its pthread dependency (same pattern as tools/yflame).
find_package(Threads REQUIRED)
target_link_libraries(ynet PRIVATE
    yetty_ynet
    yetty_yplatform_core
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(ynet PRIVATE -Wall -Wextra)
endif()
