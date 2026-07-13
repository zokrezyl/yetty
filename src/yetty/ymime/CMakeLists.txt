# ymime — file-type detection + DCS file-envelope prologue codec. Shared by
# the terminal (YETTY_DCS_MIME_FILE receive path) and thin clients
# (ycat --raw). Deliberately leaf: ycore only.
add_library(yetty_ymime STATIC
    mime.c
)

target_include_directories(yetty_ymime PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ymime PUBLIC
    yetty_ycore
)
