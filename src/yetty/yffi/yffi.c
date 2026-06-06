/*
 * yffi.c — aggregation unit for the FFI shared library (libyetty_ffi.so).
 *
 * The shared library is built ONLY in the dedicated PIC "double build" tree
 * (YETTY_BUILD_FFI_SHARED=ON, CMAKE_POSITION_INDEPENDENT_CODE=ON). The static
 * application build never links it, so the fast non-PIC archives stay
 * uncompromised. The CMake target force-includes the yclass module archives
 * whose `yetty_<module>_*` stubs the language bindings (bindings/<lang>/) call
 * via dlopen; this TU just gives the library a source + a version probe.
 */

/* Version probe the bindings can call to confirm they loaded the expected
 * library. Bumped alongside the binding ABI. */
const char *yetty_ffi_version(void)
{
    return "0.0.1";
}
