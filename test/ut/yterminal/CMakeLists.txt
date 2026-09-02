# yterminal ydraw-ingest round trip (#728) — headless, no GPU device, no
# PTY. The ingest-harness terminal slice (terminal.c) wires ONLY the
# content grid + drawable registry + an instrumented complex factory, and
# the test feeds the exact serialized envelope bytes a producer ships
# through the real terminal_ydraw_apply_body pipeline. yetty_yterminal
# PUBLIC-links the GPU libs for the full terminal, but the harness path
# never initializes a device.

yetty_add_c_test(
    NAME    yterminal_ingest_roundtrip
    SOURCES ingest-roundtrip-test.c
            ingest-roundtrip-stubs.c
    LIBS    yetty_yterminal yetty_yvterm yetty_ygui2 yetty_ysdf yetty_ydraw_list yetty_yface
            yetty_yclass yetty_ycore
            freetype
    SHIMS   trace platform_thread platform_term
    LABELS  contract terminal ingest
    TIMEOUT 30)

# Static fontconfig's fcfreetype.o pulls FT_Get_PS_Font_Info etc. —
# freetype must follow the archive on the link line; the rescan group
# pins that order (same fix as ydraw-yaml / ypdf).
find_library(_INGEST_FC_STATIC libfontconfig.a
    PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}
          /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/lib
          /usr/local/lib /opt/local/lib /opt/homebrew/lib)
if(_INGEST_FC_STATIC)
    target_link_libraries(yterminal_ingest_roundtrip-test PRIVATE
        "$<LINK_GROUP:RESCAN,${_INGEST_FC_STATIC},freetype>")
endif()
