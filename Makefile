# Yetty Build System
# Usage: make <target>

# Use system tools for most builds (avoid nix)
SYSTEM_PATH := /usr/bin:/bin:/usr/local/bin:$(PATH)

# Android SDK (adjust paths as needed)
export JAVA_HOME ?= /usr/lib/jvm/java-17-openjdk-amd64
export ANDROID_HOME ?= $(HOME)/android-sdk
export ANDROID_SDK_ROOT ?= $(ANDROID_HOME)

# Android NDK (for BusyBox cross-compilation)
export ANDROID_NDK_HOME ?= $(ANDROID_HOME)/ndk/26.1.10909125
export ANDROID_ABI ?= arm64-v8a
export ANDROID_PLATFORM ?= android-26

# Directories
BUILD_DIR := build
BUILD_DIR_DEBUG := build-debug
BUILD_DIR_WEB := web-build
BUILD_DIR_ANDROID := build-android
BUSYBOX_VERSION := 1.36.1
BUSYBOX_DIR := external/busybox
BUSYBOX_SRC := $(BUSYBOX_DIR)/busybox-$(BUSYBOX_VERSION)

# CMake options
CMAKE := cmake
CMAKE_GENERATOR := -G Ninja
CMAKE_COMMON :=
CMAKE_RELEASE := -DCMAKE_BUILD_TYPE=Release
CMAKE_DEBUG := -DCMAKE_BUILD_TYPE=Debug

# Android toolchain
ANDROID_TOOLCHAIN := $(ANDROID_NDK_HOME)/build/cmake/android.toolchain.cmake
ANDROID_CMAKE_OPTS := -DCMAKE_TOOLCHAIN_FILE=$(ANDROID_TOOLCHAIN) \
	-DANDROID_ABI=$(ANDROID_ABI) \
	-DANDROID_PLATFORM=$(ANDROID_PLATFORM) \
	-DANDROID_STL=c++_shared

# Default target
.PHONY: all
all: release

# === Desktop Builds ===

.PHONY: release
release: ## Build Release (WebGPU, native)
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR) $(CMAKE_GENERATOR) $(CMAKE_COMMON) $(CMAKE_RELEASE)
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR) --parallel

.PHONY: debug
debug: ## Build Debug (WebGPU, native)
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_DEBUG) $(CMAKE_GENERATOR) $(CMAKE_COMMON) $(CMAKE_DEBUG)
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_DEBUG) --parallel

# === Web/Emscripten Build ===

.PHONY: web
web: ## Build for Web with Emscripten
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR_WEB) $(CMAKE_GENERATOR) $(CMAKE_COMMON) $(CMAKE_RELEASE) \
		-DCMAKE_TOOLCHAIN_FILE=$(EMSDK)/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
		-DYETTY_WEB=ON
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR_WEB) --parallel

# === Android Build ===

.PHONY: android-wgpu
android-wgpu: ## Download pre-built wgpu-native for Android
	cd android && bash ./build-wgpu.sh

.PHONY: android
android: android-wgpu ## Build Android APK (Debug)
	nix develop .#android --command bash -c "cd android && ./gradlew assembleDebug"

.PHONY: android-release
android-release: android-wgpu ## Build Android APK (Release)
	nix develop .#android --command bash -c "cd android && ./gradlew assembleRelease"

.PHONY: android-clean
android-clean: ## Clean Android build
	rm -f android/app/libs/arm64-v8a/libwgpu_native.so
	nix develop .#android --command bash -c "cd android && ./gradlew clean" || true
	rm -rf $(BUILD_DIR_ANDROID)

# === BusyBox for Android ===

.PHONY: busybox-download
busybox-download: ## Download BusyBox source
	@mkdir -p $(BUSYBOX_DIR)
	@if [ ! -d "$(BUSYBOX_SRC)" ]; then \
		echo "Downloading BusyBox $(BUSYBOX_VERSION)..."; \
		curl -L https://busybox.net/downloads/busybox-$(BUSYBOX_VERSION).tar.bz2 | tar -xj -C $(BUSYBOX_DIR); \
	else \
		echo "BusyBox source already exists"; \
	fi

.PHONY: busybox-android
busybox-android: busybox-download ## Cross-compile BusyBox for Android (requires ANDROID_NDK_HOME)
	@if [ -z "$(ANDROID_NDK_HOME)" ] || [ ! -d "$(ANDROID_NDK_HOME)" ]; then \
		echo "Note: ANDROID_NDK_HOME not set. Run 'make android' first to download NDK via Gradle,"; \
		echo "then set ANDROID_NDK_HOME to the downloaded NDK path."; \
		exit 1; \
	fi
	@echo "Building BusyBox for Android ($(ANDROID_ABI))..."
	@cd $(BUSYBOX_SRC) && \
		make defconfig && \
		sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config && \
		sed -i 's/CONFIG_FEATURE_PREFER_APPLETS=y/# CONFIG_FEATURE_PREFER_APPLETS is not set/' .config && \
		sed -i 's/CONFIG_FEATURE_SH_STANDALONE=y/# CONFIG_FEATURE_SH_STANDALONE is not set/' .config && \
		yes "" | make oldconfig && \
		make CROSS_COMPILE=$(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android$(subst android-,,$(ANDROID_PLATFORM))- \
			LDFLAGS="--static" \
			CFLAGS="--static -fPIE" \
			LDFLAGS="-pie" \
			-j$$(nproc) && \
		make install
	@echo "BusyBox built. Copy to android/app/src/main/assets/busybox"

.PHONY: busybox-clean
busybox-clean: ## Clean BusyBox build
	rm -rf $(BUSYBOX_DIR)

# === Run Targets ===

.PHONY: run
run: release ## Run yetty
	./$(BUILD_DIR)/yetty

.PHONY: run-debug
run-debug: debug ## Run yetty (debug)
	./$(BUILD_DIR_DEBUG)/yetty

# === Test Targets ===

.PHONY: test
test: ## Build and run tests
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR) $(CMAKE_GENERATOR) $(CMAKE_COMMON) $(CMAKE_DEBUG)
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR) --target yetty_tests --parallel
	./$(BUILD_DIR)/test/ut/yetty_tests

.PHONY: coverage
coverage: ## Run tests with coverage
	PATH="$(SYSTEM_PATH)" $(CMAKE) -B $(BUILD_DIR) $(CMAKE_GENERATOR) $(CMAKE_COMMON) $(CMAKE_DEBUG) -DYETTY_COVERAGE=ON
	PATH="$(SYSTEM_PATH)" $(CMAKE) --build $(BUILD_DIR) --target yetty_tests --parallel
	./$(BUILD_DIR)/test/ut/yetty_tests
	~/.local/bin/uv run gcovr --root . --filter 'test/ut/harness/' -s

# === Clean Targets ===

.PHONY: clean
clean: ## Clean build directories
	rm -rf $(BUILD_DIR) $(BUILD_DIR_DEBUG) $(BUILD_DIR_WEB) $(BUILD_DIR_ANDROID)

.PHONY: clean-all
clean-all: clean busybox-clean ## Clean everything including external deps
	-cd android && PATH="$(SYSTEM_PATH)" ./gradlew clean 2>/dev/null || true

# === Help ===

.PHONY: help
help: ## Show this help
	@echo "Yetty Build System"
	@echo ""
	@echo "Usage: make <target>"
	@echo ""
	@echo "Targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-18s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "Environment variables:"
	@echo "  ANDROID_NDK_HOME  - Android NDK path (default: ~/Android/Sdk/ndk/...)"
	@echo "  ANDROID_ABI       - Target ABI (default: arm64-v8a)"
	@echo "  ANDROID_PLATFORM  - Min API level (default: android-26)"
	@echo "  EMSDK             - Emscripten SDK path (for web target)"
