# yguiapp2 — the generic ygui2 terminal application host (plain C, no
# yclass class of its own: it hosts the framework object).
add_library(yetty_yguiapp2 STATIC run.c)
target_include_directories(yetty_yguiapp2 PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(yetty_yguiapp2 PUBLIC yetty_ygui2)
