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
DISTCC_HOSTS ?= localhost 192.168.1.10
export DISTCC_HOSTS

# Android builds run on the plain host toolchain — no nix.
# The nix-based path lives in Makefile.2 (`make -f Makefile.2 ... USE_NIX=yes`).
# PATH is rebuilt explicitly so a caller's nix-flavoured $PATH doesn't leak
# in (which would otherwise put nix-store clang/cmake ahead of the NDK ones
# and silently break the cross-build). JAVA_HOME / ANDROID_HOME /
# ANDROID_NDK_HOME default to standard system locations above and may be
# overridden from the environment (e.g. by GitHub-hosted runners).
ANDROID_PATH := $(JAVA_HOME)/bin:$(ANDROID_HOME)/cmdline-tools/latest/bin:$(ANDROID_HOME)/platform-tools:$(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin:/usr/local/bin:/usr/bin:/bin

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
CMAKE_ASAN := -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"

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
# Desktop - ytrace (full logging)
#=============================================================================

.PHONY: config-desktop-ytrace-debug
config-desktop-ytrace-debug: ## Configure desktop ytrace debug build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YTRACE_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_COMPILER_LAUNCHER)
	@ln -sfn $(BUILD_DIR_DESKTOP_YTRACE_DEBUG)/compile_commands.json compile_commands.json

.PHONY: config-desktop-ytrace-release
config-desktop-ytrace-release: ## Configure desktop ytrace release build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_COMPILER_LAUNCHER)
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

.PHONY: test-desktop-ytrace-release
test-desktop-ytrace-release: ## Run desktop ytrace release tests
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-desktop-ytrace-release; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YTRACE_RELEASE) --target yetty_tests $(CMAKE_PARALLEL)
	./$(BUILD_DIR_DESKTOP_YTRACE_RELEASE)/test/ut/yetty_tests

.PHONY: test-desktop-ytrace-debug
test-desktop-ytrace-debug: ## Run desktop ytrace debug tests
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-desktop-ytrace-debug; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YTRACE_DEBUG) --target yetty_tests $(CMAKE_PARALLEL)
	./$(BUILD_DIR_DESKTOP_YTRACE_DEBUG)/test/ut/yetty_tests

#=============================================================================
# Desktop - ytrace with ASAN
#=============================================================================

.PHONY: config-desktop-ytrace-asan
config-desktop-ytrace-asan: ## Configure desktop ytrace ASAN build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YTRACE_ASAN) $(CMAKE_GENERATOR) $(CMAKE_DEBUG) $(CMAKE_LOGLEVEL_YTRACE) $(CMAKE_ASAN) $(CMAKE_COMPILER_LAUNCHER)
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
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_YINFO_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE) $(CMAKE_LOGLEVEL_YINFO) $(CMAKE_COMPILER_LAUNCHER)

.PHONY: build-desktop-yinfo-release
build-desktop-yinfo-release: ## Build desktop yinfo release (for perf testing)
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YINFO_RELEASE)/build.ninja" ]; then $(MAKE) config-desktop-yinfo-release; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YINFO_RELEASE) $(CMAKE_PARALLEL)

.PHONY: run-desktop-yinfo-release
run-desktop-yinfo-release: build-desktop-yinfo-release ## Run desktop yinfo release build
	./$(BUILD_DIR_DESKTOP_YINFO_RELEASE)/yetty

.PHONY: test-desktop-yinfo-release
test-desktop-yinfo-release: ## Run desktop yinfo release tests
	@if [ ! -f "$(BUILD_DIR_DESKTOP_YINFO_RELEASE)/build.ninja" ]; then $(MAKE) config-desktop-yinfo-release; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_YINFO_RELEASE) --target yetty_tests $(CMAKE_PARALLEL)
	./$(BUILD_DIR_DESKTOP_YINFO_RELEASE)/test/ut/yetty_tests

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
build-android-ytrace-release: ## Build Android ytrace release APK
	@$(MAKE) _android-ytrace-deps-release
	PATH="$(ANDROID_PATH)" USE_CCACHE=$(USE_CCACHE) ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_YTRACE_RELEASE) bash -c "cd build-tools/android && ./gradlew $(GRADLE_OPTS_YTRACE_RELEASE) assembleRelease"

.PHONY: test-android-ytrace-debug
test-android-ytrace-debug: build-android-ytrace-debug ## Install and run Android ytrace debug build
	adb install -r $(BUILD_DIR_ANDROID_YTRACE_DEBUG)/app/outputs/apk/debug/app-debug.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

.PHONY: test-android-ytrace-release
test-android-ytrace-release: build-android-ytrace-release ## Install and run Android ytrace release build
	adb install -r $(BUILD_DIR_ANDROID_YTRACE_RELEASE)/app/outputs/apk/release/app-release.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

#=============================================================================
# Android x86_64 - ytrace (for emulator)
#=============================================================================

.PHONY: build-android_x86_64-ytrace-debug
build-android_x86_64-ytrace-debug: ## Build Android x86_64 ytrace debug APK (emulator)
	@$(MAKE) _android_x86_64-ytrace-deps-debug
	PATH="$(ANDROID_PATH)" USE_CCACHE=$(USE_CCACHE) ANDROID_ABI=x86_64 ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG) bash -c "cd build-tools/android && ./gradlew $(GRADLE_OPTS_X86_64_YTRACE_DEBUG) assembleDebug"

.PHONY: build-android_x86_64-ytrace-release
build-android_x86_64-ytrace-release: ## Build Android x86_64 ytrace release APK (emulator)
	@$(MAKE) _android_x86_64-ytrace-deps-release
	PATH="$(ANDROID_PATH)" USE_CCACHE=$(USE_CCACHE) ANDROID_ABI=x86_64 ANDROID_BUILD_DIR=$(CURDIR)/$(BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE) bash -c "cd build-tools/android && ./gradlew $(GRADLE_OPTS_X86_64_YTRACE_RELEASE) assembleRelease"

.PHONY: test-android_x86_64-ytrace-debug
test-android_x86_64-ytrace-debug: build-android_x86_64-ytrace-debug ## Install and run Android x86_64 ytrace debug (emulator)
	adb install -r $(BUILD_DIR_ANDROID_X86_64_YTRACE_DEBUG)/app/outputs/apk/debug/app-debug.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

.PHONY: test-android_x86_64-ytrace-release
test-android_x86_64-ytrace-release: build-android_x86_64-ytrace-release ## Install and run Android x86_64 ytrace release (emulator)
	adb install -r $(BUILD_DIR_ANDROID_X86_64_YTRACE_RELEASE)/app/outputs/apk/release/app-release.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

#=============================================================================
# WebAssembly (browser native WebGPU - Dawn in Chrome)
#=============================================================================

.PHONY: config-webasm-ytrace-debug
config-webasm-ytrace-debug: ## Configure WebAssembly ytrace debug build
	nix develop .#web --command bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YTRACE_DEBUG) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=Debug $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: config-webasm-ytrace-release
config-webasm-ytrace-release: ## Configure WebAssembly ytrace release build
	nix develop .#web --command bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YTRACE_RELEASE) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=MinSizeRel $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: build-webasm-ytrace-debug
build-webasm-ytrace-debug: ## Build WebAssembly ytrace debug
	@if [ ! -f "$(BUILD_DIR_WEBASM_YTRACE_DEBUG)/build.ninja" ]; then $(MAKE) config-webasm-ytrace-debug; fi
	nix develop .#web --command bash -c 'cmake --build $(BUILD_DIR_WEBASM_YTRACE_DEBUG) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/index.html build-tools/web/serve.py $(BUILD_DIR_WEBASM_YTRACE_DEBUG)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YTRACE_DEBUG)

.PHONY: build-webasm-ytrace-release
build-webasm-ytrace-release: ## Build WebAssembly ytrace release (CDB generation handled by CMake)
	@if [ ! -f "$(BUILD_DIR_WEBASM_YTRACE_RELEASE)/build.ninja" ]; then $(MAKE) config-webasm-ytrace-release; fi
	nix develop .#web --command bash -c 'cmake --build $(BUILD_DIR_WEBASM_YTRACE_RELEASE) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/index.html build-tools/web/serve.py $(BUILD_DIR_WEBASM_YTRACE_RELEASE)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YTRACE_RELEASE)

.PHONY: config-webasm-yinfo-debug
config-webasm-yinfo-debug: ## Configure WebAssembly yinfo debug build (minimal logging)
	nix develop .#web --command bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YINFO_DEBUG) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=Debug \
			-DYTRACE_ENABLE_YTRACE=0 -DYTRACE_ENABLE_YDEBUG=0 $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: build-webasm-yinfo-debug
build-webasm-yinfo-debug: ## Build WebAssembly yinfo debug (minimal logging)
	@if [ ! -f "$(BUILD_DIR_WEBASM_YINFO_DEBUG)/build.ninja" ]; then $(MAKE) config-webasm-yinfo-debug; fi
	nix develop .#web --command bash -c 'cmake --build $(BUILD_DIR_WEBASM_YINFO_DEBUG) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/index.html build-tools/web/serve.py $(BUILD_DIR_WEBASM_YINFO_DEBUG)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YINFO_DEBUG)

.PHONY: config-webasm-yinfo-release
config-webasm-yinfo-release: ## Configure WebAssembly yinfo release build (minimal logging)
	nix develop .#web --command bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_YINFO_RELEASE) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=MinSizeRel \
			-DYTRACE_ENABLE_YTRACE=0 -DYTRACE_ENABLE_YDEBUG=0 $(CMAKE_COMPILER_LAUNCHER)'

.PHONY: build-webasm-yinfo-release
build-webasm-yinfo-release: ## Build WebAssembly yinfo release (minimal logging)
	@if [ ! -f "$(BUILD_DIR_WEBASM_YINFO_RELEASE)/build.ninja" ]; then $(MAKE) config-webasm-yinfo-release; fi
	nix develop .#web --command bash -c 'cmake --build $(BUILD_DIR_WEBASM_YINFO_RELEASE) --target yetty $(CMAKE_PARALLEL)'
	@cp build-tools/web/index.html build-tools/web/serve.py $(BUILD_DIR_WEBASM_YINFO_RELEASE)/
	@$(MAKE) verify-webasm BUILD_DIR=$(BUILD_DIR_WEBASM_YINFO_RELEASE)

.PHONY: verify-webasm
verify-webasm: ## Post-build verification that all webasm artifacts are present
	@echo "=== Post-build webasm verification ==="
	@FAIL=0; \
	for f in yetty.js yetty.wasm yetty.data index.html serve.py; do \
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
