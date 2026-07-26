# Yetty Build System
# Usage: make <target>

# Use system tools for most builds (avoid nix)
SYSTEM_PATH := /usr/bin:/bin:/usr/local/bin:$(PATH)

# Android SDK
export JAVA_HOME ?= /usr/lib/jvm/java-17-openjdk-amd64
export ANDROID_HOME ?= $(HOME)/android-sdk
export ANDROID_SDK_ROOT ?= $(ANDROID_HOME)
export ANDROID_NDK_HOME ?= $(ANDROID_HOME)/ndk/26.1.10909125
export ANDROID_ABI ?= arm64-v8a
export ANDROID_PLATFORM ?= android-26

# Build directories (platform-loglevel-buildtype)
# ytrace = all logging enabled (ytrace, ydebug, yinfo, ywarn, yerror)
# yinfo  = only yinfo and above (yinfo, ywarn, yerror) - for release/perf testing
BUILD_DIR_DESKTOP_YTRACE_DEBUG := build-desktop-ytrace-debug
BUILD_DIR_DESKTOP_YTRACE_RELEASE := build-desktop-ytrace-release
BUILD_DIR_DESKTOP_YTRACE_ASAN := build-desktop-ytrace-asan
BUILD_DIR_DESKTOP_YINFO_RELEASE := build-desktop-yinfo-release

# Linux cross-compile (from x86_64 host using stock distro cross gcc)
BUILD_DIR_LINUX_AARCH64_YTRACE_RELEASE := build-linux-aarch64-ytrace-release
BUILD_DIR_LINUX_RISCV_YTRACE_RELEASE := build-linux-riscv-ytrace-release

BUILD_DIR_ANDROID_YTRACE_DEBUG := build-android-ytrace-debug
BUILD_DIR_ANDROID_YTRACE_RELEASE := build-android-ytrace-release
BUILD_DIR_ANDROID_YINFO_RELEASE := build-android-yinfo-release

# Android x86_64 (for emulator)
BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG := build-android_x86_64-ytrace-debug
BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE := build-android_x86_64-ytrace-release

# WebAssembly uses browser native WebGPU (Dawn in Chrome)
BUILD_DIR_WEBASM_YTRACE_DEBUG := build-webasm-ytrace-debug
BUILD_DIR_WEBASM_YTRACE_RELEASE := build-webasm-ytrace-release
BUILD_DIR_WEBASM_YINFO_DEBUG := build-webasm-yinfo-debug
BUILD_DIR_WEBASM_YINFO_RELEASE := build-webasm-yinfo-release

# Windows (MSVC via VS Build Tools)
BUILD_DIR_WINDOWS_YTRACE_DEBUG := build-windows-ytrace-debug
BUILD_DIR_WINDOWS_YTRACE_RELEASE := build-windows-ytrace-release
BUILD_DIR_WINDOWS_YINFO_RELEASE := build-windows-yinfo-release

# iOS (arm64 for device)
BUILD_DIR_IOS_YTRACE_DEBUG := build-ios-ytrace-debug
BUILD_DIR_IOS_YTRACE_RELEASE := build-ios-ytrace-release

# iOS x86_64 (for simulator)
BUILD_DIR_IOS_X86_64_YTRACE_DEBUG := build-ios_x86_64-ytrace-debug
BUILD_DIR_IOS_X86_64_YTRACE_RELEASE := build-ios_x86_64-ytrace-release

# tvOS x86_64 (Apple TV simulator only — no device build wired up yet)
BUILD_DIR_TVOS_X86_64_YTRACE_DEBUG := build-tvos_x86_64-ytrace-debug
BUILD_DIR_TVOS_X86_64_YTRACE_RELEASE := build-tvos_x86_64-ytrace-release

# Parallel jobs (override with: make build-... PARALLEL_JOBS=30)
PARALLEL_JOBS ?=
CMAKE_PARALLEL := $(if $(PARALLEL_JOBS),--parallel $(PARALLEL_JOBS),--parallel)

# Build acceleration options:
#   USE_DISTCC=1: use ccache + distcc (distributed build)
#   USE_CCACHE=1: use ccache only (local caching)
#   Neither set:  no caching/distribution
#
# Platform support for ccache:
#   - Desktop (Linux/macOS): ✓ fully supported
#   - WebAssembly (emcc):    ✓ fully supported
#   - Android (NDK clang):   ✓ fully supported
#   - Windows (MSVC):        ✗ not supported (use sccache instead)
USE_DISTCC ?= 0
USE_CCACHE ?= 0
# @nixem-remote-yetty-build uses distcc's SSH transport: the `@` prefix
# tells distcc to reach the host over ssh, resolving host/port/user/identity
# from ~/.ssh/config. That alias enables ControlMaster, so all the per-job
# ssh connections share one persistent master instead of re-handshaking.
DISTCC_HOSTS ?= localhost
export DISTCC_HOSTS

# Android builds run on the plain host toolchain — no nix.
# The nix-based path lives in Makefile.2 (`make -f Makefile.2 ... USE_NIX=yes`).
# PATH is rebuilt explicitly so a caller's nix-flavoured $PATH doesn't leak
# in (which would otherwise put nix-store clang/cmake ahead of the NDK ones
# and silently break the cross-build). JAVA_HOME / ANDROID_HOME /
# ANDROID_NDK_HOME default to standard system locations above and may be
# overridden from the environment (e.g. by GitHub-hosted runners).
ANDROID_PATH := $(JAVA_HOME)/bin:$(ANDROID_HOME)/cmdline-tools/latest/bin:$(ANDROID_HOME)/platform-tools:$(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin:/usr/local/bin:/usr/bin:/bin

# WebAssembly builds use the upstream emscripten SDK (emsdk), no nix.
# EMSDK defaults to ~/.local/emsdk (matches build-tools/install-emscripten.sh).
# Override from the environment if installed elsewhere — emsdk_env.sh already
# exports EMSDK after `./emsdk activate`. When emsdk isn't installed at all,
# the apt-shipped /usr/bin/emcc on the system path is still picked up
# (older but workable).
EMSDK ?= $(HOME)/.local/emsdk
WEBASM_PATH := $(EMSDK)/upstream/emscripten:$(EMSDK):/usr/local/bin:/usr/bin:/bin

ifeq ($(USE_DISTCC),1)
ifneq ($(shell which distcc 2>/dev/null),)
export CCACHE_PREFIX := distcc
endif
CMAKE_COMPILER_LAUNCHER := -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
else ifeq ($(USE_CCACHE),1)
CMAKE_COMPILER_LAUNCHER := -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
else
CMAKE_COMPILER_LAUNCHER :=
endif

# CMake options
CMAKE := cmake
CMAKE_GENERATOR := -G Ninja
CMAKE_RELEASE := -DCMAKE_BUILD_TYPE=Release
CMAKE_DEBUG := -DCMAKE_BUILD_TYPE=Debug
# mimalloc is force-disabled under ASAN: its MI_OVERRIDE archive defines
# malloc/free ahead of the sanitizer runtime, which routes every allocation
# around ASAN's interceptors — no redzones, no use-after-free detection, no
# leak tracking. The sanitizer must own the heap.
CMAKE_ASAN := -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DYETTY_ENABLE_LIB_MIMALLOC=OFF

# Desktop compiler — clang/clang++ by default (the FFI annotate attribute
# is Clang-only; using gcc here produces ~1000 -Wattributes warnings).
# Override on the command line with: DESKTOP_CC=gcc DESKTOP_CXX=g++
DESKTOP_CC ?= clang
DESKTOP_CXX ?= clang++
CMAKE_DESKTOP_COMPILER := -DCMAKE_C_COMPILER=$(DESKTOP_CC) -DCMAKE_CXX_COMPILER=$(DESKTOP_CXX)

# Logging level options (ytrace = all, yinfo = info and above)
CMAKE_LOGLEVEL_YTRACE :=
CMAKE_LOGLEVEL_YINFO := -DYTRACE_ENABLE_YTRACE=0 -DYTRACE_ENABLE_YDEBUG=0

# Gradle options (path relative to build-tools/android/)
GRADLE_OPTS_YTRACE_DEBUG := --project-cache-dir=../../$(BUILD_DIR_ANDROID_YTRACE_DEBUG)/.gradle
GRADLE_OPTS_YTRACE_RELEASE := --project-cache-dir=../../$(BUILD_DIR_ANDROID_YTRACE_RELEASE)/.gradle
GRADLE_OPTS_YINFO_RELEASE := --project-cache-dir=../../$(BUILD_DIR_ANDROID_YINFO_RELEASE)/.gradle

# Gradle options for x86_64 (emulator)
GRADLE_OPTS_X86_64_YTRACE_DEBUG := --project-cache-dir=../../$(BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG)/.gradle
GRADLE_OPTS_X86_64_YTRACE_RELEASE := --project-cache-dir=../../$(BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE)/.gradle

# Default target - show help
.PHONY: all
all: help

#=============================================================================
# yclass codegen — standalone, NOT part of any platform build
#=============================================================================
#
# Generates per-module model.yaml, methods.gen.{h,c}, rpc.gen.c,
# per-class <name>.gen.c, public include/yetty/<module>/{methods,rpc,
# <name>}.h from annotated .c sources.
#
# Output is committed to git. Platform builds compile what is in the
# tree — they NEVER invoke this. Re-run when annotated sources change.
#
# Module discovery: every .c under src/yetty/<module>/ (excluding
# *.gen.c) that contains a `class@<module>:` or `mixin@<module>:`
# clang annotation is included as a source for that module.
#
# Codegen derives its clang include paths from the include/ and src/ roots
# it is passed; its -fsyntax-only step tolerates missing third-party headers —
# it needs the annotation AST nodes, not a clean compile.

# A module entry is either a bare name (sources under src/yetty/<name>/)
# or <name>=<path> for yclass modules living elsewhere (yclass-based tools).
YCLASS_MODULES := yapp yetty yfigure ygrid ygit ygui yguiapp ymgui yrdawn yshadertoy yvterm yflame ymap ynotebook yjupyter yview yplatform ychrome ymusic ycircuit yai=tools/ai/yai yrich yzoo=tools/yzoo ymaze=tools/ymaze yjungle=tools/yjungle demoygui=demo/ygui ycompositor=tools/ycompositor yaudio=tools/yaudio ycompositorygui=tools/ycompositor-ygui ybrowser=tools/ybrowser yhello=tools/yhello ygreeter=tools/ygreeter ynet api_yplot=src/api/yplot ydummy yterminal

# Modules whose generated public headers are written NEXT TO their source instead
# of under include/yetty/<module>/ (codegen --headers-local). Used for modules
# whose sources live outside the shared include tree — the ygui demos under
# demo/ygui/, where each demo dir stays self-contained (main.c + main.gen.c +
# main.h + rpc.gen.c together) rather than scattering headers into include/.
YCLASS_LOCAL_HEADERS := demoygui

# Bare module names (strip any "=<srcdir>" suffix used by out-of-tree modules).
YCLASS_MODNAMES := $(foreach spec,$(YCLASS_MODULES),$(firstword $(subst =, ,$(spec))))
# Codegen fan-out width. Override: `make CODEGEN_JOBS=8 codegen`.
CODEGEN_JOBS ?= $(shell nproc 2>/dev/null || echo 4)

# Regenerate ONE module by name: resolve its spec (for the `yai=<dir>` form),
# its annotated sources, and the --headers-local flag, then run the generator.
# Feature guards a module keeps its annotated class/overrides behind. codegen
# can't see annotations under an #ifdef unless the macro is defined for its parse
# (the generated output is committed and compiled under the real CMake define).
# Space-separate multiple macros. Keep in sync with each tool's CMake define.
YCLASS_DEFINES_ybrowser := YETTY_YBROWSER_HAS_STANDALONE YETTY_YGUI_HAS_UV
YCLASS_DEFINES_yhello := YETTY_YHELLO_HAS_STANDALONE
YCLASS_DEFINES_ygreeter := YETTY_YGREETER_HAS_STANDALONE YETTY_YGUI_HAS_UV

# Two-generator rollout. Modules migrate to the role-split layout ONE BY
# ONE: a module marked YCLASS_SPLIT_<mod> := 1 runs the NEW generator
# (src/yetty/yclass/gen/codegen.py) and gets the split output — raw
# annotated source in src/yetty/<module>/, generated object API in
# src/yetty/gen/api/<mod>/ (+ header under include/yetty/api/<mod>/),
# generated impl glue in src/yetty/gen/impl/<module>/, remote slots
# resolved by canonical qualified name, local-only factory, no constructor
# skel, no module-level rpc.gen.c. Every UNMARKED module runs the FROZEN
# origin/main generator (src/yetty/yclass/gen/codegen-old.py — never edit
# it; it exists so legacy output cannot drift while the new generator
# evolves). When the last module is migrated, codegen-old.py and these
# markers are deleted.
YCLASS_SPLIT_ydummy := 1
YCLASS_SPLIT_yfigure := 1
YCLASS_SPLIT_ygrid := 1
YCLASS_SPLIT_yrdawn := 1
YCLASS_SPLIT_yshadertoy := 1
YCLASS_SPLIT_ymgui := 1
YCLASS_SPLIT_yvterm := 1
YCLASS_SPLIT_yterminal := 1

# The two rollout groups, derived from the YCLASS_SPLIT_<mod> markers:
# migrated modules run the NEW generator (codegen.py, role-split layout),
# everything else runs the FROZEN origin/main generator (codegen-old.py).
YCLASS_SPLIT_MODNAMES := $(strip $(foreach m,$(YCLASS_MODNAMES),$(if $(filter 1,$(YCLASS_SPLIT_$(m))),$(m))))
YCLASS_LEGACY_MODNAMES := $(filter-out $(YCLASS_SPLIT_MODNAMES),$(YCLASS_MODNAMES))

define codegen_one
mod="$(1)"; spec="$$mod"; \
for s in $(YCLASS_MODULES); do case "$$s" in "$$mod"=*) spec="$$s";; esac; done; \
case "$$spec" in *=*) src_dir=$${spec#*=};; *) src_dir="src/yetty/$$mod";; esac; \
sources=$$(grep -lrE '(clang::annotate|YETTY_ANNOTATE)\("(class|mixin)@'"$$mod"':' "$$src_dir" --include='*.c' --exclude='*.gen.c' | LC_ALL=C sort); \
if [ -z "$$sources" ]; then echo "ERROR: no annotated sources under $$src_dir"; exit 1; fi; \
local_headers=""; case " $(YCLASS_LOCAL_HEADERS) " in *" $$mod "*) local_headers="--headers-local";; esac; \
if [ "$(YCLASS_SPLIT_$(1))" = "1" ]; then generator="src/yetty/yclass/gen/codegen.py"; else generator="src/yetty/yclass/gen/codegen-old.py"; fi; \
echo "  codegen: $$mod ($${generator##*/})"; \
YCLASS_DEFINES="$(YCLASS_DEFINES_$(1))" YCLASS_INCLUDE_DIRS="$(YCLASS_INCLUDE_DIRS_$(1))" YCLASS_SPLIT="$(YCLASS_SPLIT_$(1))" YCLASS_COMPAT_HEADER="$(YCLASS_COMPAT_HEADER_$(1))" PYTHONHASHSEED=0 uv run $$generator "$$mod" "$(CURDIR)/include/yetty" "$(CURDIR)/$$src_dir" $$local_headers $$sources
endef

# NB: the per-module _cg1-%/_cg2-% targets are deliberately NOT .PHONY — GNU
# make skips pattern-rule (implicit) recipes for phony targets. They never
# correspond to real files, so the pattern rule runs them every time anyway.
.PHONY: codegen codegen-old codegen-new _codegen_old_pass1 _codegen_old_pass2 _codegen_new_pass1 _codegen_new_pass2

codegen: ## Run yclass codegen for ALL modules (legacy group + migrated group)
	@$(MAKE) --no-print-directory codegen-old
	@$(MAKE) --no-print-directory codegen-new

codegen-old: ## Run yclass codegen for the LEGACY modules only (frozen codegen-old.py)
	@# Each module writes only its OWN files, so within a pass the modules run
	@# in parallel. Two passes with a barrier between them: a header-destined
	@# type (exposed arg, callback typedef, vtable struct) becomes visible to
	@# consumers in OTHER modules only once pass 1 has written the owning header;
	@# pass 2 re-parses with all headers present. codegen writes atomically, so
	@# a consumer never reads a half-written header mid-pass. (A cross-module
	@# type chain deeper than one level would need a 3rd pass.)
	@$(MAKE) --no-print-directory -j$(CODEGEN_JOBS) _codegen_old_pass1
	@$(MAKE) --no-print-directory -j$(CODEGEN_JOBS) _codegen_old_pass2

codegen-new: ## Run yclass codegen for the MIGRATED role-split modules only (codegen.py)
	@# Same two-pass shape as codegen-old, restricted to the migrated group.
	@$(MAKE) --no-print-directory -j$(CODEGEN_JOBS) _codegen_new_pass1
	@$(MAKE) --no-print-directory -j$(CODEGEN_JOBS) _codegen_new_pass2

_codegen_old_pass1: $(foreach m,$(YCLASS_LEGACY_MODNAMES),_cg1-$(m))
	@echo "==> yclass codegen (legacy): pass 1 done ($(words $(YCLASS_LEGACY_MODNAMES)) modules)"
_codegen_old_pass2: $(foreach m,$(YCLASS_LEGACY_MODNAMES),_cg2-$(m))
	@echo "==> yclass codegen (legacy): pass 2 done"
_codegen_new_pass1: $(foreach m,$(YCLASS_SPLIT_MODNAMES),_cg1-$(m))
	@echo "==> yclass codegen (migrated): pass 1 done ($(words $(YCLASS_SPLIT_MODNAMES)) modules)"
_codegen_new_pass2: $(foreach m,$(YCLASS_SPLIT_MODNAMES),_cg2-$(m))
	@echo "==> yclass codegen (migrated): pass 2 done"

_cg1-%:
	@$(call codegen_one,$*)
_cg2-%:
	@$(call codegen_one,$*)

.PHONY: ffi
ffi: ## Generate FFI language bindings from the per-module model.yaml (run after codegen)
	uv run tools/ffi-codegen/python/ffigen.py
	uv run tools/ffi-codegen/lua/ffigen.py

.PHONY: format-code
format-code: ## clang-format all C/H sources under include/ src/ tools/ (parallel, in place)
	uv run qa-tools/refactoring/code-format/apply-format.py include src tools

#=============================================================================
# Desktop - ytrace (full logging)
#=============================================================================

.PHONY: config-desktop-ytrace-debug
config-desktop-ytrace-debug: ## Configure desktop ytrace debug build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YTRACE_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_DESKTOP_COMPILER) $(CMAKE_COMPILER_LAUNCHER)
	@ln -sfn $(BUILD_DIR_DESKTOP_YTRACE_DEBUG)/compile_commands.json compile_commands.json

.PHONY: config-desktop-ytrace-release
config-desktop-ytrace-release: ## Configure desktop ytrace release build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_DESKTOP_COMPILER) $(CMAKE_COMPILER_LAUNCHER) -DYETTY_BUILD_FFI_SHARED=ON
	@ln -sfn $(BUILD_DIR_DESKTOP_YTRACE_RELEASE)/compile_commands.json compile_commands.json

.PHONY: build-desktop-ytrace-debug
build-desktop-ytrace-debug: ## Build desktop ytrace debug
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-desktop-ytrace-debug; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YTRACE_DEBUG) $(CMAKE_PARALLEL)

.PHONY: build-desktop-ytrace-release
build-desktop-ytrace-release: ## Build desktop ytrace release (daily driver)
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-desktop-ytrace-release; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) $(CMAKE_PARALLEL)

.PHONY: run-desktop-ytrace-debug
run-desktop-ytrace-debug: build-desktop-ytrace-debug ## Run desktop ytrace debug build
	./$(BUILD_DIR_DESKTOP_YTRACE_DEBUG)/yetty

.PHONY: run-desktop-ytrace-release
run-desktop-ytrace-release: build-desktop-ytrace-release ## Run desktop ytrace release build
	./$(BUILD_DIR_DESKTOP_YTRACE_RELEASE)/yetty

#=============================================================================
# Test suite (CTest) — the deterministic gate. See docs/testing.md.
#
# `make test-fast` is the default local + CI gate: build the release tree, then
# run every ctest except the environment-dependent lanes (network/gpu/display/
# slow). The other targets select different label sets against the same tree.
#=============================================================================

# Label sets excluded from each lane (see docs/testing.md §4).
CTEST_EXCLUDE_FAST := network|gpu|display|slow
CTEST_EXCLUDE_ASAN := network|gpu|display|slow|asan-skip

.PHONY: test-fast
test-fast: build-desktop-ytrace-release ## Run the deterministic fast test gate (no network/gpu/display/slow)
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --output-on-failure -LE '$(CTEST_EXCLUDE_FAST)'

.PHONY: test-all
test-all: build-desktop-ytrace-release ## Run all local tests except live-network ones
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --output-on-failure -LE 'network'

.PHONY: test-network
test-network: build-desktop-ytrace-release ## Run only the network-labeled tests
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --output-on-failure -L 'network'

.PHONY: test-wpt
test-wpt: build-desktop-ytrace-release ## Run the ybrowser WPT geometry suite
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --output-on-failure -L 'wpt'

.PHONY: test-render
test-render: build-desktop-ytrace-release ## Run render/GPU tests (needs a display/GPU; nightly)
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --output-on-failure -L 'render'

.PHONY: test-e2e
test-e2e: build-desktop-ytrace-release ## Run launched-yetty / yctl E2E tests (needs a display; nightly)
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --output-on-failure -L 'e2e'

.PHONY: test-nightly
test-nightly: build-desktop-ytrace-release ## Run all non-fast lanes: network + wpt + render + e2e (E2E/render self-skip headless)
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --output-on-failure -L 'network|wpt|render|e2e'

.PHONY: test-asan
test-asan: build-desktop-ytrace-asan ## Run the deterministic test gate under ASAN
	# LSAN_OPTIONS points at test/lsan.supp so the documented third-party leak
	# families (Fontconfig, Lexbor, libcss) are suppressed narrowly while the
	# affected tests still run — see test/lsan.supp and issue #414.
	PATH="$(SYSTEM_PATH)" LSAN_OPTIONS="suppressions=$(CURDIR)/test/lsan.supp:print_suppressions=0" \
		ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_ASAN) --output-on-failure -LE '$(CTEST_EXCLUDE_ASAN)'

.PHONY: test-regression
test-regression: build-desktop-ytrace-release ## Check ycat/decode-ydraw output against the published regression corpus (see test/regression/README.md)
	mkdir -p tmp/regression
	PATH="$(SYSTEM_PATH)" python3 test/regression/regression.py fetch --dest tmp/regression/corpus.zip
	PATH="$(SYSTEM_PATH)" python3 test/regression/regression.py check --corpus tmp/regression/corpus.zip --evidence tmp/regression/evidence

.PHONY: regression-record
regression-record: build-desktop-ytrace-release ## Record a local regression reference corpus into tmp/regression/recorded
	PATH="$(SYSTEM_PATH)" python3 test/regression/regression.py record --out tmp/regression/recorded --stability-runs 2

.PHONY: test-desktop-ytrace-release
test-desktop-ytrace-release: test-fast ## Deprecated alias for `make test-fast`

.PHONY: test-desktop-ytrace-debug
test-desktop-ytrace-debug: build-desktop-ytrace-debug ## Run the deterministic test gate on the debug build
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YTRACE_DEBUG) --output-on-failure -LE '$(CTEST_EXCLUDE_FAST)'

#=============================================================================
# Desktop - ytrace with ASAN
#=============================================================================

.PHONY: config-desktop-ytrace-asan
config-desktop-ytrace-asan: ## Configure desktop ytrace ASAN build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YTRACE_ASAN) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_ASAN) $(CMAKE_DESKTOP_COMPILER) $(CMAKE_COMPILER_LAUNCHER)
	@ln -sfn $(BUILD_DIR_DESKTOP_YTRACE_ASAN)/compile_commands.json compile_commands.json

.PHONY: build-desktop-ytrace-asan
build-desktop-ytrace-asan: ## Build desktop ytrace ASAN
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YTRACE_ASAN)/build.ninja" ]; then $(MAKE) config-desktop-ytrace-asan; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YTRACE_ASAN) $(CMAKE_PARALLEL)

.PHONY: run-desktop-ytrace-asan
run-desktop-ytrace-asan: build-desktop-ytrace-asan ## Run desktop ytrace ASAN build
	@# Dawn WebGPU uses prebuilt Release binaries (no ASAN). When ASAN-instrumented code
	@# links against non-ASAN Dawn, memory allocated by Dawn's slab allocator isn't tracked.
	@# halt_on_error=0 allows continuing despite Dawn allocator false positives.
	ASAN_OPTIONS=halt_on_error=0:detect_leaks=1 ./$(BUILD_DIR_DESKTOP_YTRACE_ASAN)/yetty

#=============================================================================
# Desktop - yinfo (minimal logging for release/perf testing)
#=============================================================================

.PHONY: config-desktop-yinfo-release
config-desktop-yinfo-release: ## Configure desktop yinfo release build (minimal logging)
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YINFO_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YINFO) $(CMAKE_DESKTOP_COMPILER) $(CMAKE_COMPILER_LAUNCHER) -DYETTY_BUILD_FFI_SHARED=ON

.PHONY: build-desktop-yinfo-release
build-desktop-yinfo-release: ## Build desktop yinfo release (for perf testing)
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YINFO_RELEASE)/build.ninja" ]; then $(MAKE) config-desktop-yinfo-release; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YINFO_RELEASE) $(CMAKE_PARALLEL)

.PHONY: run-desktop-yinfo-release
run-desktop-yinfo-release: build-desktop-yinfo-release ## Run desktop yinfo release build
	./$(BUILD_DIR_DESKTOP_YINFO_RELEASE)/yetty

.PHONY: test-desktop-yinfo-release
test-desktop-yinfo-release: build-desktop-yinfo-release ## Run the deterministic test gate on the yinfo release build
	PATH="$(SYSTEM_PATH)" ctest --test-dir $(BUILD_DIR_DESKTOP_YINFO_RELEASE) --output-on-failure -LE '$(CTEST_EXCLUDE_FAST)'

#=============================================================================
# Linux cross-compile (aarch64 / riscv64) — host x86_64 → target Linux ARM64/RISC-V
# NO nix. Uses Ubuntu/Debian's stock cross toolchain + multiarch dev packages.
#
# One-time host setup (Ubuntu 24.04 / Debian 12):
#
#   sudo apt-get update
#   sudo apt-get install -y \
#       cmake ninja-build pkg-config \
#       gcc-aarch64-linux-gnu g++-aarch64-linux-gnu \
#       gcc-riscv64-linux-gnu g++-riscv64-linux-gnu
#
#   # Multiarch: pull arm64/riscv64 dev packages from ports.ubuntu.com
#   sudo dpkg --add-architecture arm64
#   sudo dpkg --add-architecture riscv64
#
#   # Restrict default sources to amd64 (so apt doesn't try to fetch
#   # arm64/riscv64 from us.archive — which doesn't have them):
#   sudo sed -i -E 's|^deb |deb [arch=amd64] |' /etc/apt/sources.list \
#       /etc/apt/sources.list.d/*.list 2>/dev/null || true
#
#   # Add ports for arm64/riscv64 (replace `noble` with your codename):
#   . /etc/os-release
#   echo "deb [arch=arm64,riscv64] http://ports.ubuntu.com/ubuntu-ports $${VERSION_CODENAME} main universe" \
#       | sudo tee /etc/apt/sources.list.d/ports.list
#   echo "deb [arch=arm64,riscv64] http://ports.ubuntu.com/ubuntu-ports $${VERSION_CODENAME}-updates main universe" \
#       | sudo tee -a /etc/apt/sources.list.d/ports.list
#
#   sudo apt-get update
#
#   # Cross dev packages (aarch64). Build of the main `yetty` exec needs
#   # X11/wayland/GL/fontconfig; the riscv64 target builds demos+tools only
#   # so it's a smaller list — but we install the same set for symmetry.
#   sudo apt-get install -y \
#       libx11-dev:arm64 libxrandr-dev:arm64 libxinerama-dev:arm64 \
#       libxcursor-dev:arm64 libxi-dev:arm64 libxext-dev:arm64 \
#       libxkbcommon-dev:arm64 libwayland-dev:arm64 wayland-protocols \
#       libgl-dev:arm64 libfontconfig-dev:arm64 \
#       libexpat1-dev:arm64 uuid-dev:arm64 \
#       \
#       libx11-dev:riscv64 libxrandr-dev:riscv64 libxinerama-dev:riscv64 \
#       libxcursor-dev:riscv64 libxi-dev:riscv64 libxext-dev:riscv64 \
#       libxkbcommon-dev:riscv64 libwayland-dev:riscv64 \
#       libgl-dev:riscv64 libfontconfig-dev:riscv64 \
#       libexpat1-dev:riscv64 uuid-dev:riscv64
#
# riscv64 note: the main `yetty` executable links Dawn/WebGPU which has no
# riscv64 prebuilt — `build-linux-riscv-ytrace-release` therefore builds
# only the demos + non-GPU tools (passes -DYETTY_ENABLE_LIB_WEBGPU=OFF and
# selects specific ninja targets), not `yetty` itself.
#=============================================================================

# Cross flags. CMAKE_LIBRARY_ARCHITECTURE makes find_library/find_package(X11)
# pick libs from /usr/lib/<arch>-linux-gnu/ first, ahead of the host /usr/lib.
CMAKE_CROSS_AARCH64 := \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_SYSTEM_PROCESSOR=aarch64 \
	-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
	-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
	-DCMAKE_LIBRARY_ARCHITECTURE=aarch64-linux-gnu

CMAKE_CROSS_RISCV := \
	-DCMAKE_SYSTEM_NAME=Linux \
	-DCMAKE_SYSTEM_PROCESSOR=riscv64 \
	-DCMAKE_C_COMPILER=riscv64-linux-gnu-gcc \
	-DCMAKE_CXX_COMPILER=riscv64-linux-gnu-g++ \
	-DCMAKE_LIBRARY_ARCHITECTURE=riscv64-linux-gnu \
	-DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
	-DYETTY_ENABLE_LIB_WEBGPU=OFF \
	-DYETTY_ENABLE_LIB_GLFW=OFF \
	-DYETTY_ENABLE_LIB_QEMU=OFF \
	-DYETTY_ENABLE_LIB_QEMU_BINARY=OFF \
	-DYETTY_ENABLE_TOOL_QA=OFF \
	-DYETTY_ENABLE_TOOL_YINSTALL=OFF

# Scope pkg-config to the multiarch .pc dir so `pkg_check_modules(fontconfig)`
# doesn't pick up host-x86_64 metadata. PKG_CONFIG_LIBDIR replaces the default
# search path (PKG_CONFIG_PATH only prepends).
PKG_CFG_AARCH64 := PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
PKG_CFG_RISCV := PKG_CONFIG_LIBDIR=/usr/lib/riscv64-linux-gnu/pkgconfig:/usr/share/pkgconfig

# riscv64 target list (no `yetty` — Dawn unavailable for riscv64).
# Tools/demos that link yrender, ywebgpu, or ydraw-core (which all
# include <webgpu/webgpu.h> directly) are excluded — that rules out
# ycat, yecho, yplot, ymesh, ymaze, ydoc, ysheet, yslide, yetty-ythorvg,
# yetty-ymsdf-gen-gpu, msdf-render-atlas, and every demo-ygui-*. What
# remains: pure CDB / msdf / ydraw-decode / ymgui-imgui_core / TinyEMU.
LINUX_RISCV_TARGETS := \
    decode-ydraw \
    yetty-ymsdf-gen yetty-msdf-gen \
    cdb-viewer cdb-diff \
    temu \
    demo-ymgui-01-demo-window \
    yplot ymesh yecho ycat \
    ygreeter

.PHONY: config-linux-aarch64-ytrace-release
config-linux-aarch64-ytrace-release: ## Configure Linux aarch64 cross-build (release)
	$(PKG_CFG_AARCH64) \
		$(CMAKE) -B $(BUILD_DIR_LINUX_AARCH64_YTRACE_RELEASE) $(CMAKE_GENERATOR) \
			$(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_CROSS_AARCH64) $(CMAKE_COMPILER_LAUNCHER)

.PHONY: build-linux-aarch64-ytrace-release
build-linux-aarch64-ytrace-release: ## Build Linux aarch64 cross-compiled (release)
	@if [ ! -f "$(BUILD_DIR_LINUX_AARCH64_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-linux-aarch64-ytrace-release; fi
	$(CMAKE) --build $(BUILD_DIR_LINUX_AARCH64_YTRACE_RELEASE) $(CMAKE_PARALLEL)

.PHONY: config-linux-riscv-ytrace-release
config-linux-riscv-ytrace-release: ## Configure Linux riscv64 cross-build (release, no GPU)
	$(PKG_CFG_RISCV) \
		$(CMAKE) -B $(BUILD_DIR_LINUX_RISCV_YTRACE_RELEASE) $(CMAKE_GENERATOR) \
			$(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_CROSS_RISCV) $(CMAKE_COMPILER_LAUNCHER)

.PHONY: build-linux-riscv-ytrace-release
build-linux-riscv-ytrace-release: ## Build Linux riscv64 demos+tools (NOT yetty exec — Dawn missing)
	@if [ ! -f "$(BUILD_DIR_LINUX_RISCV_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-linux-riscv-ytrace-release; fi
	$(CMAKE) --build $(BUILD_DIR_LINUX_RISCV_YTRACE_RELEASE) $(CMAKE_PARALLEL) --target $(LINUX_RISCV_TARGETS)

#=============================================================================
# Android - ytrace (full logging)
#=============================================================================

.PHONY: config-android-ytrace-debug
config-android-ytrace-debug: ## Configure Android ytrace debug build
	@$(MAKE) _android-ytrace-deps-debug

.PHONY: config-android-ytrace-release
config-android-ytrace-release: ## Configure Android ytrace release build
	@$(MAKE) _android-ytrace-deps-release

.PHONY: build-android-ytrace-debug
build-android-ytrace-debug: ## Build Android ytrace debug APK
	@$(MAKE) _android-ytrace-deps-debug
	PATH="$(ANDROID_PATH)" USE_CCACHE=$(USE_CCACHE) ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_YTRACE_DEBUG) bash -c "cd build-tools/android && ./gradlew $(GRADLE_OPTS_YTRACE_DEBUG) assembleDebug"

.PHONY: build-android-ytrace-release
build-android-ytrace-release: ## Build Android ytrace release APKs (yetty + ygreeter flavors)
	@$(MAKE) _android-ytrace-deps-release
	PATH="$(ANDROID_PATH)" USE_CCACHE=$(USE_CCACHE) ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_YTRACE_RELEASE) bash -c "cd build-tools/android && ./gradlew $(GRADLE_OPTS_YTRACE_RELEASE) assembleRelease"

.PHONY: test-android-ytrace-debug
test-android-ytrace-debug: build-android-ytrace-debug ## Install and run Android ytrace debug build
	adb install -r $(BUILD_DIR_ANDROID_YTRACE_DEBUG)/app/outputs/apk/yetty/debug/app-yetty-debug.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

.PHONY: test-android-ytrace-release
test-android-ytrace-release: build-android-ytrace-release ## Install and run Android ytrace release build
	adb install -r $(BUILD_DIR_ANDROID_YTRACE_RELEASE)/app/outputs/apk/yetty/release/app-yetty-release.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

#=============================================================================
# Android x86_64 - ytrace (for emulator)
#=============================================================================

.PHONY: build-android_x86_64-ytrace-debug
build-android_x86_64-ytrace-debug: ## Build Android x86_64 ytrace debug APK (emulator)
	@$(MAKE) _android_x86_64-ytrace-deps-debug
	PATH="$(ANDROID_PATH)" USE_CCACHE=$(USE_CCACHE) ANDROID_ABI=x86_64 ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG) bash -c "cd build-tools/android && ./gradlew $(GRADLE_OPTS_X86_64_YTRACE_DEBUG) assembleDebug"

.PHONY: build-android_x86_64-ytrace-release
build-android_x86_64-ytrace-release: ## Build Android x86_64 ytrace release APKs (yetty + ygreeter, emulator)
	@$(MAKE) _android_x86_64-ytrace-deps-release
	PATH="$(ANDROID_PATH)" USE_CCACHE=$(USE_CCACHE) ANDROID_ABI=x86_64 ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE) bash -c "cd build-tools/android && ./gradlew $(GRADLE_OPTS_X86_64_YTRACE_RELEASE) assembleRelease"

.PHONY: test-android_x86_64-ytrace-debug
test-android_x86_64-ytrace-debug: build-android_x86_64-ytrace-debug ## Install and run Android x86_64 ytrace debug (emulator)
	adb install -r $(BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG)/app/outputs/apk/yetty/debug/app-yetty-debug.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

.PHONY: test-android_x86_64-ytrace-release
test-android_x86_64-ytrace-release: build-android_x86_64-ytrace-release ## Install and run Android x86_64 ytrace release (emulator)
	adb install -r $(BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE)/app/outputs/apk/yetty/release/app-yetty-release.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

#=============================================================================
# WebAssembly (browser native WebGPU - Dawn in Chrome)
#=============================================================================

.PHONY: config-webasm-ytrace-debug
config-webasm-ytrace-debug: ## Configure WebAssembly ytrace debug build
	PATH="$(WEBASM_PATH)" bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YTRACE_DEBUG) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=Debug $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: config-webasm-ytrace-release
config-webasm-ytrace-release: ## Configure WebAssembly ytrace release build
	PATH="$(WEBASM_PATH)" bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YTRACE_RELEASE) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=MinSizeRel $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: build-webasm-ytrace-debug
build-webasm-ytrace-debug: ## Build WebAssembly ytrace debug
	@if [ ! -f "$(BUILD_DIR_WEBASM_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-webasm-ytrace-debug; fi
	PATH="$(WEBASM_PATH)" bash -c 'cmake --build $(BUILD_DIR_WEBASM_YTRACE_DEBUG) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/serve.py $(BUILD_DIR_WEBASM_YTRACE_DEBUG)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YTRACE_DEBUG)

.PHONY: build-webasm-ytrace-release
build-webasm-ytrace-release: ## Build WebAssembly ytrace release (CDB generation handled by CMake)
	@if [ ! -f "$(BUILD_DIR_WEBASM_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-webasm-ytrace-release; fi
	PATH="$(WEBASM_PATH)" bash -c 'cmake --build $(BUILD_DIR_WEBASM_YTRACE_RELEASE) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/serve.py $(BUILD_DIR_WEBASM_YTRACE_RELEASE)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YTRACE_RELEASE)

.PHONY: config-webasm-yinfo-debug
config-webasm-yinfo-debug: ## Configure WebAssembly yinfo debug build (minimal logging)
	PATH="$(WEBASM_PATH)" bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YINFO_DEBUG) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=Debug \
			-DYTRACE_ENABLE_YTRACE=0 -DYTRACE_ENABLE_YDEBUG=0 $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: build-webasm-yinfo-debug
build-webasm-yinfo-debug: ## Build WebAssembly yinfo debug (minimal logging)
	@if [ ! -f "$(BUILD_DIR_WEBASM_YINFO_DEBUG)/build.ninja" ]; then $(MAKE) config-webasm-yinfo-debug; fi
	PATH="$(WEBASM_PATH)" bash -c 'cmake --build $(BUILD_DIR_WEBASM_YINFO_DEBUG) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/serve.py $(BUILD_DIR_WEBASM_YINFO_DEBUG)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YINFO_DEBUG)

.PHONY: config-webasm-yinfo-release
config-webasm-yinfo-release: ## Configure WebAssembly yinfo release build (minimal logging)
	PATH="$(WEBASM_PATH)" bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YINFO_RELEASE) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=MinSizeRel \
			-DYTRACE_ENABLE_YTRACE=0 -DYTRACE_ENABLE_YDEBUG=0 $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: build-webasm-yinfo-release
build-webasm-yinfo-release: ## Build WebAssembly yinfo release (minimal logging)
	@if [ ! -f "$(BUILD_DIR_WEBASM_YINFO_RELEASE)/build.ninja" ]; then $(MAKE) config-webasm-yinfo-release; fi
	PATH="$(WEBASM_PATH)" bash -c 'cmake --build $(BUILD_DIR_WEBASM_YINFO_RELEASE) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/serve.py $(BUILD_DIR_WEBASM_YINFO_RELEASE)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YINFO_RELEASE)

.PHONY: verify-webasm
verify-webasm: ## Post-build verification that all webasm artifacts are present
	@echo "=== Post-build webasm verification ==="
	@FAIL=0; \
	for f in yetty.js yetty.wasm index.html terminal.html yos-iframe.html webgpu-health.js webgpu-health.html yos-web/engine/yos_proc.mjs yos-web/engine/yfs_client.mjs yos-web/yfs/current.json serve.py; do \
		if [ ! -e "$(BUILD_DIR)/$$f" ]; then \
			echo "MISSING: $$f"; \
			FAIL=1; \
		fi; \
	done; \
	if [ "$$FAIL" -ne 0 ]; then \
		echo "ERROR: Webasm build incomplete — missing artifacts above"; \
		exit 1; \
	fi; \
	echo "All webasm artifacts verified OK"

.PHONY: run-webasm-ytrace-debug
run-webasm-ytrace-debug: build-webasm-ytrace-debug ## Serve WebAssembly ytrace debug build
	python3 $(BUILD_DIR_WEBASM_YTRACE_DEBUG)/serve.py 8000 $(BUILD_DIR_WEBASM_YTRACE_DEBUG)

.PHONY: run-webasm-ytrace-release
run-webasm-ytrace-release: build-webasm-ytrace-release ## Serve WebAssembly ytrace release build
	python3 $(BUILD_DIR_WEBASM_YTRACE_RELEASE)/serve.py 8000 $(BUILD_DIR_WEBASM_YTRACE_RELEASE)

.PHONY: run-webasm-yinfo-debug
run-webasm-yinfo-debug: build-webasm-yinfo-debug ## Serve WebAssembly yinfo debug build
	python3 $(BUILD_DIR_WEBASM_YINFO_DEBUG)/serve.py 8000 $(BUILD_DIR_WEBASM_YINFO_DEBUG)

.PHONY: run-webasm-yinfo-release
run-webasm-yinfo-release: build-webasm-yinfo-release ## Serve WebAssembly yinfo release build
	python3 $(BUILD_DIR_WEBASM_YINFO_RELEASE)/serve.py 8000 $(BUILD_DIR_WEBASM_YINFO_RELEASE)


#=============================================================================
# Windows (MSVC via VS Build Tools)
#=============================================================================

.PHONY: config-windows-ytrace-debug
config-windows-ytrace-debug: ## Configure Windows ytrace debug build (MSVC)
	cmd.exe //c "build-tools\windows\build.bat debug configure"

.PHONY: config-windows-ytrace-release
config-windows-ytrace-release: ## Configure Windows ytrace release build (MSVC)
	cmd.exe //c "build-tools\windows\build.bat release configure"

.PHONY: build-windows-ytrace-debug
build-windows-ytrace-debug: ## Build Windows ytrace debug
	cmd.exe //c "build-tools\windows\build.bat debug"

.PHONY: build-windows-ytrace-release
build-windows-ytrace-release: ## Build Windows ytrace release
	cmd.exe //c "build-tools\windows\build.bat release"

.PHONY: run-windows-ytrace-debug
run-windows-ytrace-debug: build-windows-ytrace-debug ## Run Windows ytrace debug build
	./$(BUILD_DIR_WINDOWS_YTRACE_DEBUG)/yetty.exe

.PHONY: run-windows-ytrace-release
run-windows-ytrace-release: build-windows-ytrace-release ## Run Windows ytrace release build
	./$(BUILD_DIR_WINDOWS_YTRACE_RELEASE)/yetty.exe

#=============================================================================
# iOS (arm64 device) - requires macOS with Xcode
#=============================================================================

# Check for macOS (iOS builds only work on macOS)
UNAME_S := $(shell uname -s)
define CHECK_MACOS
	@if [ "$(UNAME_S)" != "Darwin" ]; then \
		echo "ERROR: iOS builds require macOS with Xcode. Current OS: $(UNAME_S)"; \
		exit 1; \
	fi
endef

# iOS CMake toolchain (requires Xcode)
CMAKE_IOS_TOOLCHAIN := -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
CMAKE_IOS_SIMULATOR_TOOLCHAIN := -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0

.PHONY: config-ios-ytrace-debug
config-ios-ytrace-debug: ## Configure iOS ytrace debug build (device, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_IOS_YTRACE_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_IOS_TOOLCHAIN) -DYETTY_IOS=ON

.PHONY: config-ios-ytrace-release
config-ios-ytrace-release: ## Configure iOS ytrace release build (device, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_IOS_YTRACE_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_IOS_TOOLCHAIN) -DYETTY_IOS=ON

.PHONY: build-ios-ytrace-debug
build-ios-ytrace-debug: ## Build iOS ytrace debug (device, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_IOS_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-ios-ytrace-debug; fi
	$(CMAKE) --build $(BUILD_DIR_IOS_YTRACE_DEBUG) $(CMAKE_PARALLEL)

.PHONY: build-ios-ytrace-release
build-ios-ytrace-release: ## Build iOS ytrace release (device, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_IOS_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-ios-ytrace-release; fi
	$(CMAKE) --build $(BUILD_DIR_IOS_YTRACE_RELEASE) $(CMAKE_PARALLEL)

#=============================================================================
# iOS x86_64 (simulator) - requires macOS with Xcode
#=============================================================================

.PHONY: config-ios_x86_64-ytrace-debug
config-ios_x86_64-ytrace-debug: ## Configure iOS x86_64 ytrace debug build (simulator, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_IOS_X86_64_YTRACE_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_IOS_SIMULATOR_TOOLCHAIN) -DYETTY_IOS=ON

.PHONY: config-ios_x86_64-ytrace-release
config-ios_x86_64-ytrace-release: ## Configure iOS x86_64 ytrace release build (simulator, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_IOS_X86_64_YTRACE_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_IOS_SIMULATOR_TOOLCHAIN) -DYETTY_IOS=ON

.PHONY: build-ios_x86_64-ytrace-debug
build-ios_x86_64-ytrace-debug: ## Build iOS x86_64 ytrace debug (simulator, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_IOS_X86_64_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-ios_x86_64-ytrace-debug; fi
	$(CMAKE) --build $(BUILD_DIR_IOS_X86_64_YTRACE_DEBUG) $(CMAKE_PARALLEL)

.PHONY: build-ios_x86_64-ytrace-release
build-ios_x86_64-ytrace-release: ## Build iOS x86_64 ytrace release (simulator, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_IOS_X86_64_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-ios_x86_64-ytrace-release; fi
	$(CMAKE) --build $(BUILD_DIR_IOS_X86_64_YTRACE_RELEASE) $(CMAKE_PARALLEL)

#=============================================================================
# tvOS x86_64 (Apple TV simulator) - requires macOS with Xcode
#=============================================================================

CMAKE_TVOS_SIMULATOR_TOOLCHAIN := -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_SYSROOT=appletvsimulator -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0

.PHONY: config-tvos_x86_64-ytrace-debug
config-tvos_x86_64-ytrace-debug: ## Configure tvOS x86_64 ytrace debug build (simulator, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_TVOS_X86_64_YTRACE_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_TVOS_SIMULATOR_TOOLCHAIN) -DYETTY_TVOS=ON

.PHONY: config-tvos_x86_64-ytrace-release
config-tvos_x86_64-ytrace-release: ## Configure tvOS x86_64 ytrace release build (simulator, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_TVOS_X86_64_YTRACE_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_TVOS_SIMULATOR_TOOLCHAIN) -DYETTY_TVOS=ON

.PHONY: build-tvos_x86_64-ytrace-debug
build-tvos_x86_64-ytrace-debug: ## Build tvOS x86_64 ytrace debug (simulator, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_TVOS_X86_64_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-tvos_x86_64-ytrace-debug; fi
	$(CMAKE) --build $(BUILD_DIR_TVOS_X86_64_YTRACE_DEBUG) $(CMAKE_PARALLEL)

.PHONY: build-tvos_x86_64-ytrace-release
build-tvos_x86_64-ytrace-release: ## Build tvOS x86_64 ytrace release (simulator, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_TVOS_X86_64_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-tvos_x86_64-ytrace-release; fi
	$(CMAKE) --build $(BUILD_DIR_TVOS_X86_64_YTRACE_RELEASE) $(CMAKE_PARALLEL)

#=============================================================================
# tvOS arm64 (real Apple TV device) - requires macOS with Xcode
# Note: appletvos SDK has no x86_64 slice, hence the device-only build dir.
# Deployment to a paired Apple TV uses `xcrun devicectl device install/launch`.
#=============================================================================

BUILD_DIR_TVOS_YTRACE_DEBUG := build-tvos-ytrace-debug
BUILD_DIR_TVOS_YTRACE_RELEASE := build-tvos-ytrace-release

CMAKE_TVOS_TOOLCHAIN := -DCMAKE_SYSTEM_NAME=tvOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT=appletvos -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0

.PHONY: config-tvos-ytrace-debug
config-tvos-ytrace-debug: ## Configure tvOS ytrace debug build (device, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_TVOS_YTRACE_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_TVOS_TOOLCHAIN) -DYETTY_TVOS=ON

.PHONY: config-tvos-ytrace-release
config-tvos-ytrace-release: ## Configure tvOS ytrace release build (device, macOS only)
	$(CHECK_MACOS)
	$(CMAKE) -B $(BUILD_DIR_TVOS_YTRACE_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_TVOS_TOOLCHAIN) -DYETTY_TVOS=ON

.PHONY: build-tvos-ytrace-debug
build-tvos-ytrace-debug: ## Build tvOS ytrace debug (device, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_TVOS_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-tvos-ytrace-debug; fi
	$(CMAKE) --build $(BUILD_DIR_TVOS_YTRACE_DEBUG) $(CMAKE_PARALLEL)

.PHONY: build-tvos-ytrace-release
build-tvos-ytrace-release: ## Build tvOS ytrace release (device, macOS only)
	$(CHECK_MACOS)
	@if [ ! -f "$(BUILD_DIR_TVOS_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-tvos-ytrace-release; fi
	$(CMAKE) --build $(BUILD_DIR_TVOS_YTRACE_RELEASE) $(CMAKE_PARALLEL)

#=============================================================================
# Clean
#=============================================================================

.PHONY: clean
clean: ## Clean all build directories
	rm -rf $(BUILD_DIR_DESKTOP_YTRACE_DEBUG) $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) \
	       $(BUILD_DIR_DESKTOP_YTRACE_ASAN) $(BUILD_DIR_DESKTOP_YINFO_RELEASE) \
	       $(BUILD_DIR_WINDOWS_YTRACE_DEBUG) $(BUILD_DIR_WINDOWS_YTRACE_RELEASE) \
	       $(BUILD_DIR_WINDOWS_YINFO_RELEASE) \
	       $(BUILD_DIR_ANDROID_YTRACE_DEBUG) $(BUILD_DIR_ANDROID_YTRACE_RELEASE) \
	       $(BUILD_DIR_ANDROID_YINFO_RELEASE) \
	       $(BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG) $(BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE) \
	       $(BUILD_DIR_IOS_YTRACE_DEBUG) $(BUILD_DIR_IOS_YTRACE_RELEASE) \
	       $(BUILD_DIR_IOS_X86_64_YTRACE_DEBUG) $(BUILD_DIR_IOS_X86_64_YTRACE_RELEASE) \
	       $(BUILD_DIR_WEBASM_YTRACE_DEBUG) $(BUILD_DIR_WEBASM_YTRACE_RELEASE) \
	       $(BUILD_DIR_WEBASM_YINFO_RELEASE) \
	       build-desktop build-android build-webasm build-windows build-ios \
	       build-desktop-debug build-desktop-release \
	       build-android-debug build-android-release \
	       build-android_x86_64-debug build-android_x86_64-release \
	       build-ios-debug build-ios-release \
	       build-ios_x86_64-debug build-ios_x86_64-release \
	       build-desktop-dawn-* \
	       build-android-dawn-* \
	       build-android_x86_64-dawn-* \
	       build-ios-dawn-* build-ios_x86_64-dawn-* \
	       build-webasm-dawn-* build-windows-dawn-*

#=============================================================================
# Internal targets (not shown in help)
#=============================================================================

# ARM64 (arm64-v8a) internal targets
.PHONY: _android-ytrace-deps-debug
_android-ytrace-deps-debug:
	@cd $(CURDIR) && ANDROID_ABI=arm64-v8a ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_YTRACE_DEBUG) bash build-tools/android/build-dawn.sh

.PHONY: _android-ytrace-deps-release
_android-ytrace-deps-release:
	@cd $(CURDIR) && ANDROID_ABI=arm64-v8a ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_YTRACE_RELEASE) bash build-tools/android/build-dawn.sh

# x86_64 internal targets (for emulator)
.PHONY: _android_x86_64-ytrace-deps-debug
_android_x86_64-ytrace-deps-debug:
	@cd $(CURDIR) && ANDROID_ABI=x86_64 ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG) bash build-tools/android/build-dawn.sh

.PHONY: _android_x86_64-ytrace-deps-release
_android_x86_64-ytrace-deps-release:
	@cd $(CURDIR) && ANDROID_ABI=x86_64 ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE) bash build-tools/android/build-dawn.sh

#=============================================================================
# Help
#=============================================================================

.PHONY: help
help:
	@echo "Yetty Build System"
	@echo ""
	@echo "Usage: make <target>"
	@echo ""
	@echo "Logging Levels:"
	@echo "  ytrace - Full logging (ytrace, ydebug, yinfo, ywarn, yerror) - daily driver"
	@echo "  yinfo  - Minimal logging (yinfo, ywarn, yerror) - for release/perf testing"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z0-9_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-30s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Build outputs:"
	@echo "  build-desktop-{ytrace,yinfo}-{debug,release}/yetty"
	@echo "  build-android-{ytrace,yinfo}-{debug,release}/app/outputs/apk/"
	@echo "  build-android_x86_64-ytrace-{debug,release}/app/outputs/apk/  (emulator)"
	@echo "  build-ios-ytrace-{debug,release}/yetty.app  (device)"
	@echo "  build-ios_x86_64-ytrace-{debug,release}/yetty.app  (simulator)"
	@echo "  build-webasm-{ytrace,yinfo}-{debug,release}/yetty.html"
