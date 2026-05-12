include(${YETTY_ROOT}/build-tools/yetty/libs/cdb.cmake)

add_library(yetty_ycdb STATIC
    ycdb.c
)

target_include_directories(yetty_ycdb PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ycdb PUBLIC cdb)

if(YETTY_USE_HOWERJ_CDB)
    target_compile_definitions(yetty_ycdb PUBLIC YETTY_USE_HOWERJ_CDB=1)
endif()
