# ymsdf — the generate-unless-cached atlas front (ensure.c), driven through the
# CPU backend so the test stays headless (the gpu backend needs a WebGPU
# device). The two module sources are compiled in directly rather than linking
# yetty_ymsdf, which would drag the gpu backend and Dawn into a unit test.
yetty_add_c_test(
    NAME    ymsdf_ensure
    SOURCES ensure-test.c
            ${YETTY_ROOT}/src/yetty/ymsdf/ensure.c
            ${YETTY_ROOT}/src/yetty/ymsdf/cpu-generator.c
    LIBS    yetty_ymsdf_gen yetty_yplatform_core yetty_ycore Threads::Threads
    SHIMS   trace
    LABELS  contract
    TIMEOUT 60)
target_compile_definitions(ymsdf_ensure-test PRIVATE
    YMSDF_TEST_FONT=\"${YETTY_ROOT}/assets/fonts/Ahem.ttf\")
