// Emscripten --pre-js shim for tinyemu.wasm (the iframe VM).
//
// Runs after the runtime structures are created but before main().
// Sets ENV.YETTY_DATA_DIR to /yetty-vm so the bundled cfg's path
// expansion ($YETTY_DATA_DIR/yemu/...) finds the preloaded kernel /
// opensbi / rootfs.
Module.preRun = (Module.preRun || []);
Module.preRun.push(function () {
    if (typeof ENV !== 'undefined') {
        ENV.YETTY_DATA_DIR = '/yetty-vm';
    }
});
