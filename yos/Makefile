# yos — Makefile wrapper around meson/ninja.
#
# All real build logic lives in meson; this file just exposes the
# meaningful targets behind short, memorable names. Every recipe runs
# inside `nix develop` so the wasm cross-compile shim, apple-sdk_13,
# and matching wasm-ld are on PATH.
#
# Run `make help` for the menu.

# ─── Configuration ─────────────────────────────────────────────────────

# Build directory. Pick per-host so darwin and linux can co-exist on the
# same checkout without stomping each other's compile_commands.json.
UNAME_S  := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
BUILD    ?= build-darwin
else
BUILD    ?= build-linux
endif

# Optional ENV. Override on the command line as `make BUILD=foo …`.
JOBS     ?= $(shell command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu || nproc)

# All recipes go through `nix develop --command` so the toolchain shims
# (wasm-clang, lld, apple-sdk_13, …) are on PATH. Override with
# `make NIX_DEV=` to run with whatever is currently on the user's PATH.
NIX_DEV  ?= nix develop $(CURDIR) --command

MESON    := $(NIX_DEV) meson
NINJA    := $(NIX_DEV) ninja -C $(BUILD)

# ─── Phony bookkeeping ─────────────────────────────────────────────────

.PHONY: help all setup reconfigure clean distclean wipe \
        bootstrap toolchain mimalloc sysroot \
        codegen api api-compare api-analyse api-generate api-bridge api-struct-convert \
        libs wasm3 yos-api-bridge \
        libc-pure yos run \
        test test-libc test-freebsd test-list testlog \
        test-browser test-browser-chrome test-browser-engine test-browser-libc \
        test-browser-parity test-browser-tmux-render test-browser-tmux-xterm serve-zsh \
        browser-host-phase0 test-browser-host-phase0 \
        browser-host-phase1a test-browser-host-phase1a \
        browser-host-phase1b test-browser-host-phase1b \
        browser-host-runner test-browser-host-parity browser-host-perf \
        test-browser-ls test-browser-callbacks browser-liblua test-browser-nvim \
        test-browser-top test-browser-fullscreen test-browser-nested test-browser-fzy \
        nvim-build nvim-deps \
        format check

.DEFAULT_GOAL := help

# ─── Help ──────────────────────────────────────────────────────────────

help:
	@printf '\nyos build targets (BUILD=$(BUILD), JOBS=$(JOBS))\n\n'
	@printf '  Top-level\n'
	@printf '    make all            — host yos + libc-pure.wasm (default deliverable)\n'
	@printf '    make yos            — just the host executable\n'
	@printf '    make libc-pure      — just build-tools/freebsd-libc/libc-pure.wasm\n'
	@printf '    make run ARGS=…     — run the host yos binary (forwards ARGS)\n'
	@printf '\n  Configuration / lifecycle\n'
	@printf '    make setup          — initial meson setup (no-op if BUILD/ exists)\n'
	@printf '    make reconfigure    — meson setup --reconfigure (re-evaluates flake)\n'
	@printf '    make clean          — delete build artifacts but keep config\n'
	@printf '    make distclean      — wipe BUILD/ entirely\n'
	@printf '    make wipe           — alias for distclean + clean fetched deps\n'
	@printf '\n  Bootstrap (one-time fetches; idempotent)\n'
	@printf '    make bootstrap      — freebsd source + headers + mimalloc + sysroot\n'
	@printf '    make toolchain      — alias for bootstrap (legacy name)\n'
	@printf '    make mimalloc       — fetch + build mimalloc only\n'
	@printf '    make sysroot        — assemble guest sysroot from freebsd headers\n'
	@printf '\n  Codegen (api-extract → compare → analyse → generate → bridge)\n'
	@printf '    make codegen        — run the whole pipeline (= make api)\n'
	@printf '    make api            — alias for codegen\n'
	@printf '    make api-compare    — guest vs host signature compare\n'
	@printf '    make api-analyse    — bucket each call into mechanical/policy/etc.\n'
	@printf '    make api-generate   — emit yos_remap.{c,h}\n'
	@printf '    make api-bridge     — emit yos_bridge.{c,h}\n'
	@printf '    make api-struct-convert — emit yos_struct_convert.{c,h}\n'
	@printf '\n  Static libs\n'
	@printf '    make wasm3          — libwasm3.a\n'
	@printf '    make yos-api-bridge — libyos_api_bridge.a (depends on codegen)\n'
	@printf '    make libs           — all of the above\n'
	@printf '\n  Tests (depend on yos + libc-pure.wasm)\n'
	@printf '    make test           — full test suite\n'
	@printf '    make test-libc      — yos:libc tests only (ut/libc/test_*.c)\n'
	@printf '    make test-freebsd   — yos:freebsd-libc tests only (FreeBSD libc/tests)\n'
	@printf '    make test-list      — list available test names\n'
	@printf '    make testlog        — tail meson-logs/testlog.txt\n'
	@printf '\n  Browser-runtime tests (headless; issue #21 — needs node + google-chrome-stable)\n'
	@printf '    make test-browser-libc   — run the ENTIRE yos:libc unit-test suite through the browser engine\n'
	@printf '    make test-browser-parity — run the shared command case table (echo/cat/grep/wc/sed/sort/tr/cut) on native + browser, diff libvterm grids (BROWSER-GAP vs BOTH-FAIL)\n'
	@printf '    make test-browser        — in-page browser suite (headless Chrome + node engine)\n'
	@printf '    make test-browser-chrome — headless-Chrome page tests only\n'
	@printf '    make test-browser-engine — node process-engine page tests only (no Chrome)\n'
	@printf '    make test-browser-tmux-render — assess tmux rendering via libvterm (faithful 80x24 grid)\n'
	@printf '    make test-browser-tmux-xterm  — assess tmux in real xterm.js at non-24 height (pins status-bar/size bug)\n'
	@printf '    make serve-zsh           — serve the interactive browser zsh at http://127.0.0.1:8099/zsh.html (PORT=… to override)\n'
	@printf '\n  Browser convergence host (epic #33 — yos C runtime → wasm via emscripten)\n'
	@printf '    make browser-host-phase0      — build the Phase 0 wasm3-in-wasm host (#34)\n'
	@printf '    make test-browser-host-phase0 — build + smoke-test the Phase 0 host\n'
	@printf '    make test-browser-host-phase1a — build + smoke-test the Phase 1a bridge path (#35)\n'
	@printf '    make test-browser-host-phase1b — build + smoke-test the generated bridge + impl/vfs host (#36)\n'
	@printf '    make test-browser-host-parity  — run desktop tool artifacts on native + host wasm, classify gaps (#37)\n'
	@printf '\n  External wasm packages\n'
	@printf '    make nvim-build     — neovim 0.10.4 → wasm32 (needs nvim-deps first)\n'
	@printf '    make nvim-deps      — fetch + build all 9 nvim deps\n'
	@printf '\n  Misc\n'
	@printf '    make format         — clang-format on src/yos\n'
	@printf '    make check          — quick compile-only sanity (no tests)\n'
	@printf '\n'

# ─── Setup / reconfigure ───────────────────────────────────────────────

$(BUILD)/build.ninja:
	@$(MESON) setup $(BUILD)

setup: $(BUILD)/build.ninja

reconfigure:
	@$(MESON) setup --reconfigure $(BUILD)

clean: setup
	@$(NINJA) clean

distclean:
	rm -rf $(BUILD)

wipe: distclean
	rm -rf $(BUILD)-darwin $(BUILD)-linux build build-darwin build-linux

# ─── Top-level deliverable ─────────────────────────────────────────────

all: yos libc-pure

# ─── Bootstrap ─────────────────────────────────────────────────────────

bootstrap toolchain: setup
	@$(NINJA) freebsd-src-all freebsd-headers host-libc-headers \
	          build-tools/mimalloc/mimalloc.stamp build-tools/sysroot/sysroot.stamp

mimalloc: setup
	@$(NINJA) build-tools/mimalloc/mimalloc.stamp build-tools/mimalloc/libmimalloc.a

sysroot: setup
	@$(NINJA) build-tools/sysroot/sysroot.stamp

# ─── Codegen ───────────────────────────────────────────────────────────

codegen api: setup
	@$(NINJA) api

api-compare: setup
	@$(NINJA) api-compare

api-analyse: setup
	@$(NINJA) api-analyse

api-generate: setup
	@$(NINJA) api-generate

api-bridge: setup
	@$(NINJA) api-bridge

api-struct-convert: setup
	@$(NINJA) api-struct-convert

# ─── Static libs ───────────────────────────────────────────────────────

libs: wasm3 yos-api-bridge mimalloc

wasm3: setup
	@$(NINJA) src/wasm3/libwasm3.a

yos-api-bridge: setup
	@$(NINJA) src/yos/codegen/libyos_api_bridge.a

# ─── Top-level binaries ────────────────────────────────────────────────

yos: setup
	@$(NINJA) src/yos/yos

libc-pure: setup
	@$(NINJA) build-tools/freebsd-libc/libc-pure.wasm

run: yos
	@$(BUILD)/src/yos/yos $(ARGS)

# ─── Tests ─────────────────────────────────────────────────────────────

test: yos libc-pure
	@$(NIX_DEV) meson test -C $(BUILD)

test-libc: yos libc-pure
	@$(NIX_DEV) meson test -C $(BUILD) --suite yos:libc

test-freebsd: yos libc-pure
	@$(NIX_DEV) meson test -C $(BUILD) --suite yos:freebsd-libc

test-list:
	@$(NIX_DEV) meson test -C $(BUILD) --list

testlog:
	@cat $(BUILD)/meson-logs/testlog.txt

# ─── Browser-runtime tests (issue #21) ─────────────────────────────────
# Headless-Chrome + node-engine tests for the browser process engine in
# src/yos/platform/web. These are prototypes OUTSIDE the meson build graph:
# they run with the SYSTEM `node` + `google-chrome-stable` against the
# prebuilt wasm already in the web dir, so they need neither `make all` nor
# nix. Override the browser with YOS_CHROME, per-test timeout with
# YOS_TEST_TIMEOUT (seconds). The Chrome tests bind fixed debug ports, so the
# runner is strictly sequential.
WEB := src/yos/platform/web

# Run the ENTIRE yos:libc unit-test corpus (the same tests `make test-libc`
# runs on the native host binary) against the BROWSER process engine
# (yos_proc.mjs), via a CLI shim. Discovers the test set + each test's
# (src, wasm) from `meson introspect`, runs each through the browser engine
# with the exact `Expected:`-header pass criterion, and classifies any failure
# against the native binary (BROWSER-GAP = native passes but browser fails).
#
# Runs against the ALREADY-BUILT suite (it does not rebuild — a broken native
# build shouldn't block the browser run, and the driver reports any unbuilt
# wasm). Build the corpus first if needed: `make test-libc` (or `make all`).
# Needs system `node` + `meson` on PATH.
test-browser-libc:
	@node $(WEB)/browser-libc-suite.mjs

# Run the shared command case table (tests/integration/cases/*.json) on BOTH
# backends and diff libvterm grids (issue #25). Each case's tool wasm is run
# through the native `yos` binary AND the browser process engine; both raw
# streams are rendered through vterm_grid.c (the tmux oracle) and asserted
# against the case's `expect`. Classifies failures native-vs-browser:
# BROWSER-GAP (native passes, browser fails → engine bug; fails CI) vs
# BOTH-FAIL (native fails too → guest/libc gap; does not fail CI). Runs against
# the ALREADY-BUILT tools + native yos (build the corpus first: `make all`).
# Needs system `node`; compiles the libvterm grid tool on first run.
test-browser-parity:
	@node $(WEB)/browser-parity-suite.mjs

# Directory-listing regression (epic #32): ls / ls -l / ls -alrt through the
# browser engine — guards the opendir DIR*/dd_fd fix that unbroke fts.
test-browser-ls:
	@node $(WEB)/ls_dir_test.mjs

# Callback regression (epic #32): ps/qsort/bsearch through the browser engine —
# guards the wasm table-export patch (wasm_patch.mjs).
test-browser-callbacks:
	@node $(WEB)/callback_test.mjs

# Lua library for nvim (epic #33): build liblua.wasm from source (C++ + wasm
# exceptions, yos sysroot) so the browser gives nvim the same Lua as desktop.
browser-liblua:
	@$(NIX_DEV) bash $(WEB)/lua/build-liblua.sh

# nvim in the browser (Lua via shared-memory liblua.wasm). Needs `make all`.
# Batch (--version) + full interactive editor (TUI client + embedded server,
# runtime files mounted from result/share, insert round-trip, :q! teardown)
# + the deployed-site configuration (async yfs bodies; Lua-driven cold opens
# must take the sync fallback instead of asyncify-suspending, issue #724).
test-browser-nvim: browser-liblua
	@node $(WEB)/lua/nvim_test.mjs
	@node $(WEB)/lua/nvim_interactive_test.mjs
	@node $(WEB)/lua/nvim_yfs_async_test.mjs

# Interactive top in the browser — guards the asyncify instrumentation of the
# freebsd-tools binaries (a tool that blocks in select() must suspend, not trap).
test-browser-top:
	@node $(WEB)/top_test.mjs

# Full-screen tools launched FROM the browser zsh (fork+exec+tty handover):
# top and nvim, ending back at a working prompt. Guards empty-path ENOENT
# (netrw hijack) and the liblua heap-window reservation.
test-browser-fullscreen:
	@node $(WEB)/fullscreen_from_zsh_test.mjs

# The deepest interactive chain: zsh → tmux → nvim in the pane → :terminal
# (forkpty) → live shell → command → unwind to the outer prompt. 8 processes.
test-browser-nested:
	@node $(WEB)/nested_chain_test.mjs

# fzy fuzzy finder from the browser zsh: batch -e filter through a pipe,
# then the interactive picker (/dev/tty UI + pselect + raw-mode Enter).
test-browser-fzy:
	@node $(WEB)/fzy_from_zsh_test.mjs

test-browser:
	@node $(WEB)/browser-test-runner.mjs

test-browser-chrome:
	@node $(WEB)/browser-test-runner.mjs --chrome

test-browser-engine:
	@node $(WEB)/browser-test-runner.mjs --engine

# Assess tmux's RENDERED screen on the browser engine using libvterm as an
# independent oracle: capture the exact byte stream xterm.js receives, render it
# into a faithful 80x24 grid via the bundled libvterm sources (src/libvterm), and
# assert the layout (status bar, straight centred pane divider, splits, windows).
# Compiles the libvterm grid tool on first run.
test-browser-tmux-render:
	@node $(WEB)/tmux_render_test.mjs

# Real-xterm.js assessment of tmux in headless Chrome: boots zsh.html at a
# non-24 height, launches tmux, and checks WHERE the status bar lands in the
# xterm.js buffer. Guards the variadic-ioctl winsize fix (tmux must size to the
# real terminal, status bar on the last row). Needs google-chrome-stable.
test-browser-tmux-xterm:
	@node $(WEB)/tmux_xterm_render_test.mjs

# ─── Browser convergence host (epic #33) ───────────────────────────────
# Compile the yos C runtime to a browser-targeted wasm module with
# Emscripten (Architecture A: wasm3-in-wasm). Unlike the JS-engine browser
# tests above, the BUILD needs a working `emcc`, so it runs under
# `nix develop` (the flake dev shell provides nixpkgs' emscripten); the
# SMOKE test then runs with system `node`, like the other browser tests.
#
# Phase 0 (issue #34): prove wasm3, itself compiled to wasm, can execute a
# trivial guest — no JS libc. See $(WEB)/host/README.md.
browser-host-phase0:
	@$(NIX_DEV) bash $(WEB)/host/build-phase0.sh

test-browser-host-phase0: browser-host-phase0
	@node $(WEB)/host/phase0_smoke.mjs

# Phase 1a (issue #35): serve a guest's env.write/getpid/exit through C bridge
# wrappers (pointer translation), not JS. See $(WEB)/host/README.md.
browser-host-phase1a:
	@$(NIX_DEV) bash $(WEB)/host/build-phase1a.sh

test-browser-host-phase1a: browser-host-phase1a
	@node $(WEB)/host/phase1a_smoke.mjs

# Phase 1b (issue #36): the REAL generated bridge + impl/vfs compiled to the
# host wasm. echo + cat run through yos C code with no JS libc. Needs the
# generated bridge from `make codegen` (build-$(host)/src/yos/codegen).
browser-host-phase1b: codegen
	@$(NIX_DEV) bash $(WEB)/host/build-phase1b.sh

test-browser-host-phase1b: browser-host-phase1b
	@node $(WEB)/host/phase1b_smoke.mjs

# Phase 2 (issue #37): general yos-host runner + parity harness. yos-host.wasm
# runs an ARBITRARY desktop tool artifact (result/libexec); the harness runs
# each command through native yos AND the host wasm and classifies gaps. Needs
# `make codegen` (generated bridge) and `make all` (native yos + libexec tools).
browser-host-runner: codegen
	@$(NIX_DEV) bash $(WEB)/host/build-runner.sh

test-browser-host-parity: browser-host-runner
	@node $(WEB)/host/host_parity.mjs

# Architecture A perf (issue #41): native yos vs. wasm3-in-wasm host, ms/run.
browser-host-perf: browser-host-runner
	@node $(WEB)/host/arch_a_perf.mjs

# Serve the web dir over HTTP so you can open the interactive zsh terminal
# (zsh.html → zsh_main.mjs → the long-lived browser zsh) in a real browser.
# Foreground, Ctrl-C to stop. Override the port: `make serve-zsh PORT=8137`.
# Runs against the prebuilt wasm already in the web dir — no nix, no `make all`.
PORT ?= 8099
serve-zsh:
	@printf '\n  open  http://127.0.0.1:$(PORT)/zsh.html\n\n'
	@cd $(WEB) && ./serve.sh $(PORT)

# ─── External wasm packages ────────────────────────────────────────────

# Each entry below shells out to tools/wasm-pkg.sh, which sources the
# matching build-tools/wasm-pkg/configs/<dep>/build.sh with the env
# it expects (ROOT/PREFIX/WASM_CC/WASM_SYSROOT, plus DEP_PREFIXES for
# deps that have transitive deps). The PREFIX path is derived from
# the build.sh's VERSION string and lives at
# build-linux/wasm-pkgs/<name>-<version>/out (kept on `build-linux/`
# even on darwin to match nvim's hardcoded WORK path).

# Common env for every wasm-pkg invocation. Set once, exported.
# WASM_CFLAGS / WASM_LDFLAGS are the base compile/link flags every dep
# starts from — each build.sh prepends its own target-specific bits
# (e.g. `--target=wasm32-unknown-unknown -nostdlib`) before appending
# package-specific ones. Mirror what tests/freebsd-libc uses so deps
# share an ABI with the test harness.
WASM_BASE_CFLAGS = -target wasm32-unknown-unknown -nostdlib -O2 \
                   -fno-builtin -ffreestanding \
                   --sysroot=$(CURDIR)/$(BUILD)/sysroot \
                   -isystem $(CURDIR)/$(BUILD)/sysroot/usr/include \
                   -D__i386__=1 -D__yos__=1 -Wno-unused-parameter
WASM_BASE_LDFLAGS = -Wl,--no-entry -Wl,--export-all -Wl,--allow-undefined

WASM_PKG_ENV = \
    ROOT=$(CURDIR) \
    WASM_CC=$(CURDIR)/build-tools/wasm-clang \
    WASM_SYSROOT=$(CURDIR)/$(BUILD)/sysroot \
    WASM_CFLAGS='$(WASM_BASE_CFLAGS)' \
    WASM_LDFLAGS='$(WASM_BASE_LDFLAGS)'

# Each dep target depends on `sysroot` (so wasm headers + crt1.o exist)
# and on its own transitive deps. Stamp files live next to the install
# prefix so re-runs are no-ops once a dep has been built.
.PHONY: pkg-lua pkg-libuv pkg-msgpack-c pkg-unibilium pkg-libvterm \
        pkg-tree-sitter pkg-lpeg pkg-lua-mpack pkg-luv nvim-deps nvim-build

pkg-lua: sysroot
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh lua

pkg-libuv: sysroot
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh libuv

pkg-msgpack-c: sysroot
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh msgpack-c

pkg-unibilium: sysroot
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh unibilium

pkg-libvterm: sysroot
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh libvterm

pkg-tree-sitter: sysroot
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh tree-sitter

pkg-lpeg: pkg-lua
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh lpeg lua

pkg-lua-mpack: pkg-lua
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh lua-mpack lua

pkg-luv: pkg-lua pkg-libuv
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh luv lua libuv

# All 9 nvim deps, in build-order.
nvim-deps: pkg-lua pkg-libuv pkg-msgpack-c pkg-unibilium pkg-libvterm \
           pkg-tree-sitter pkg-lpeg pkg-lua-mpack pkg-luv

nvim-build: yos libc-pure nvim-deps
	@$(NIX_DEV) env $(WASM_PKG_ENV) bash tools/wasm-pkg.sh nvim \
	    lua libuv msgpack-c unibilium libvterm tree-sitter lpeg lua-mpack luv

# ─── Misc ──────────────────────────────────────────────────────────────

check: setup
	@$(NIX_DEV) ninja -C $(BUILD) src/yos/yos

format:
	@find src/yos -name '*.c' -o -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \
	    | xargs $(NIX_DEV) clang-format -i
