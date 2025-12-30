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

# Build directories (with debug/release suffix)
BUILD_DIR_DESKTOP_DEBUG := build-desktop-debug
BUILD_DIR_DESKTOP_RELEASE := build-desktop-release
BUILD_DIR_ANDROID_DEBUG := build-android-debug
BUILD_DIR_ANDROID_RELEASE := build-android-release
BUILD_DIR_WEBASM_DEBUG := build-webasm-debug
BUILD_DIR_WEBASM_RELEASE := build-webasm-release

# Legacy build directories (for backward compatibility)
BUILD_DIR_DESKTOP := build-desktop
BUILD_DIR_ANDROID := build-android
BUILD_DIR_WEBASM := build-webasm

# CMake options
CMAKE := cmake
CMAKE_GENERATOR := -G Ninja
CMAKE_RELEASE := -DCMAKE_BUILD_TYPE=Release
CMAKE_DEBUG := -DCMAKE_BUILD_TYPE=Debug

# Gradle options
GRADLE_OPTS := --project-cache-dir=../$(BUILD_DIR_ANDROID_DEBUG)/.gradle

# Default target - show help
.PHONY: all
all: help

#=============================================================================
# Desktop
#=============================================================================

.PHONY: config-desktop-debug
config-desktop-debug: ## Configure desktop debug build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_DEBUG)

.PHONY: config-desktop-release
config-desktop-release: ## Configure desktop release build
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DESKTOP_RELEASE) $(CMAKE_GENERATOR) $(CMAKE_RELEASE)

.PHONY: build-desktop-debug
build-desktop-debug: ## Build desktop debug
	@if [ ! -f "$(BUILD_DIR_DESKTOP_DEBUG)/build.ninja" ]; then $(MAKE) config-desktop-debug; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_DEBUG) --parallel

.PHONY: build-desktop-release
build-desktop-release: ## Build desktop release
	@if [ ! -f "$(BUILD_DIR_DESKTOP_RELEASE)/build.ninja" ]; then $(MAKE) config-desktop-release; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_RELEASE) --parallel

.PHONY: run-desktop-debug
run-desktop-debug: build-desktop-debug ## Run desktop debug build
	./$(BUILD_DIR_DESKTOP_DEBUG)/yetty

.PHONY: run-desktop-release
run-desktop-release: build-desktop-release ## Run desktop release build
	./$(BUILD_DIR_DESKTOP_RELEASE)/yetty

.PHONY: test-desktop-debug
test-desktop-debug: ## Run desktop debug tests
	@if [ ! -f "$(BUILD_DIR_DESKTOP_DEBUG)/build.ninja" ]; then $(MAKE) config-desktop-debug; fi
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DESKTOP_DEBUG) --target yetty_tests --parallel
	./$(BUILD_DIR_DESKTOP_DEBUG)/test/ut/yetty_tests

#=============================================================================
# Android
#=============================================================================

.PHONY: config-android-debug
config-android-debug: ## Configure Android debug build
	@$(MAKE) _android-wgpu

.PHONY: config-android-release
config-android-release: ## Configure Android release build
	@$(MAKE) _android-wgpu

.PHONY: build-android-debug
build-android-debug: ## Build Android debug APK
	@$(MAKE) _android-deps
	nix develop .#android --command bash -c "cd android && ./gradlew $(GRADLE_OPTS) assembleDebug"

.PHONY: build-android-release
build-android-release: ## Build Android release APK
	@$(MAKE) _android-deps
	nix develop .#android --command bash -c "cd android && ./gradlew $(GRADLE_OPTS) assembleRelease"

.PHONY: test-android-debug
test-android-debug: build-android-debug ## Install and run Android debug build
	adb install -r android/app/build/outputs/apk/debug/app-debug.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

.PHONY: test-android-release
test-android-release: build-android-release ## Install and run Android release build
	adb install -r android/app/build/outputs/apk/release/app-release.apk
	adb shell am start -n com.yetty.terminal/android.app.NativeActivity

#=============================================================================
# WebAssembly
#=============================================================================

.PHONY: config-webasm-debug
config-webasm-debug: ## Configure WebAssembly debug build
	nix develop .#web --command bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_DEBUG) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=Debug'

.PHONY: config-webasm-release
config-webasm-release: ## Configure WebAssembly release build
	nix develop .#web --command bash -c '\
		export EMCC_SKIP_SANITY_CHECK=1 && \
		emcmake cmake -B $(BUILD_DIR_WEBASM_RELEASE) $(CMAKE_GENERATOR) \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_EXE_LINKER_FLAGS="-O2"'

.PHONY: build-webasm-debug
build-webasm-debug: ## Build WebAssembly debug
	@if [ ! -f "$(BUILD_DIR_WEBASM_DEBUG)/build.ninja" ]; then $(MAKE) config-webasm-debug; fi
	nix develop .#web --command bash -c 'cmake --build $(BUILD_DIR_WEBASM_DEBUG) --parallel'
	@cp build-tools/web/index.html build-tools/web/serve.py $(BUILD_DIR_WEBASM_DEBUG)/

.PHONY: build-webasm-release
build-webasm-release: ## Build WebAssembly release
	@if [ ! -f "$(BUILD_DIR_WEBASM_RELEASE)/build.ninja" ]; then $(MAKE) config-webasm-release; fi
	nix develop .#web --command bash -c 'cmake --build $(BUILD_DIR_WEBASM_RELEASE) --parallel'
	@cp build-tools/web/index.html build-tools/web/serve.py $(BUILD_DIR_WEBASM_RELEASE)/

.PHONY: run-webasm-debug
run-webasm-debug: build-webasm-debug ## Serve WebAssembly debug build
	python3 $(BUILD_DIR_WEBASM_DEBUG)/serve.py 8000 $(BUILD_DIR_WEBASM_DEBUG)

.PHONY: run-webasm-release
run-webasm-release: build-webasm-release ## Serve WebAssembly release build
	python3 $(BUILD_DIR_WEBASM_RELEASE)/serve.py 8000 $(BUILD_DIR_WEBASM_RELEASE)

#=============================================================================
# Convenience aliases (default to release for run, debug for test)
#=============================================================================

.PHONY: config-desktop build-desktop run-desktop test-desktop
config-desktop: config-desktop-release ## Alias for config-desktop-release
build-desktop: build-desktop-release ## Alias for build-desktop-release
run-desktop: run-desktop-release ## Alias for run-desktop-release
test-desktop: test-desktop-debug ## Alias for test-desktop-debug

.PHONY: config-android build-android test-android
config-android: config-android-debug ## Alias for config-android-debug
build-android: build-android-debug ## Alias for build-android-debug
test-android: test-android-debug ## Alias for test-android-debug

.PHONY: config-webasm build-webasm run-webasm test-webasm
config-webasm: config-webasm-release ## Alias for config-webasm-release
build-webasm: build-webasm-release ## Alias for build-webasm-release
run-webasm: run-webasm-release ## Alias for run-webasm-release
test-webasm: run-webasm-debug ## Alias for run-webasm-debug

#=============================================================================
# Clean
#=============================================================================

.PHONY: clean
clean: ## Clean all build directories
	rm -rf $(BUILD_DIR_DESKTOP_DEBUG) $(BUILD_DIR_DESKTOP_RELEASE) \
	       $(BUILD_DIR_ANDROID_DEBUG) $(BUILD_DIR_ANDROID_RELEASE) \
	       $(BUILD_DIR_WEBASM_DEBUG) $(BUILD_DIR_WEBASM_RELEASE) \
	       $(BUILD_DIR_DESKTOP) $(BUILD_DIR_ANDROID) $(BUILD_DIR_WEBASM)

#=============================================================================
# Internal targets (not shown in help)
#=============================================================================

.PHONY: _android-wgpu
_android-wgpu:
	@cd $(CURDIR) && bash build-tools/android/build-wgpu.sh

.PHONY: _android-busybox
_android-busybox:
	@nix develop .#android --command bash build-tools/android/build-busybox.sh

.PHONY: _android-assets
_android-assets:
	@mkdir -p $(BUILD_DIR_ANDROID_DEBUG)/assets
	@cp -f assets/*.ttf assets/*.png assets/*.json $(BUILD_DIR_ANDROID_DEBUG)/assets/ 2>/dev/null || true

.PHONY: _android-deps
_android-deps: _android-wgpu _android-busybox _android-assets

#=============================================================================
# Help
#=============================================================================

.PHONY: help
help:
	@echo "Yetty Build System"
	@echo ""
	@echo "Usage: make <target>"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-24s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Build outputs:"
	@echo "  build-desktop-{debug,release}/yetty"
	@echo "  android/app/build/outputs/apk/{debug,release}/"
	@echo "  build-webasm-{debug,release}/yetty.html"
