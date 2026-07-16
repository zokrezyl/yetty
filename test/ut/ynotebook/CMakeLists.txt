# ynotebook MIME bundle contract test — nbformat mimebundle load/emit round-trip
# across the text / binary / json payload variants. Pure, headless.

include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)

yetty_add_c_test(
    NAME    ynotebook_mime_bundle
    SOURCES mime-bundle-test.c
    LIBS    yetty_ynotebook yetty_yclass yetty_ycore yyjson::yyjson
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

yetty_add_c_test(
    NAME    ynotebook_document
    SOURCES notebook-test.c
    LIBS    yetty_ynotebook yetty_yclass yetty_ycore yyjson::yyjson
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)

yetty_add_c_test(
    NAME    ynotebook_execution
    SOURCES exec-test.c
    LIBS    yetty_ynotebook yetty_yclass yetty_ycore yyjson::yyjson
    SHIMS   trace platform_thread platform_term
    LABELS  contract
    TIMEOUT 30)
