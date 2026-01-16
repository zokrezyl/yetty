{
  description = "Yetty - WebGPU Terminal Emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
    rust-overlay.url = "github:oxalica/rust-overlay";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, rust-overlay, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        overlays = [ (import rust-overlay) ];
        pkgs = import nixpkgs {
          inherit system overlays;
          config = {
            allowUnfree = true;
            android_sdk.accept_license = true;
          };
        };

        # Android SDK/NDK setup
        androidComposition = pkgs.androidenv.composeAndroidPackages {
          platformVersions = [ "34" ];
          buildToolsVersions = [ "34.0.0" ];
          ndkVersions = [ "26.1.10909125" ];
          cmakeVersions = [ "3.22.1" ];
          includeNDK = true;
          # Emulator support
          includeEmulator = true;
          includeSystemImages = true;
          systemImageTypes = [ "google_apis" ];
          abiVersions = [ "arm64-v8a" ];
        };

        androidSdk = androidComposition.androidsdk;
        androidNdk = "${androidSdk}/libexec/android-sdk/ndk/26.1.10909125";

        # Rust with Android target
        rustToolchain = pkgs.rust-bin.stable.latest.default.override {
          targets = [ "aarch64-linux-android" ];
        };

        # Common build dependencies
        commonDeps = with pkgs; [
          cmake
          ninja
          pkg-config
          git
        ];

        # Desktop build dependencies
        desktopDeps = with pkgs; [
          # Graphics
          xorg.libX11
          xorg.libXcursor
          xorg.libXrandr
          xorg.libXi
          xorg.libXinerama
          libxkbcommon
          wayland
          libGL
          vulkan-loader
          vulkan-headers

          # Fonts
          freetype
          fontconfig

          # Other
          glfw
          zlib
          openssl
        ];

        # Android build dependencies
        androidDeps = [
          rustToolchain
          androidSdk
          pkgs.llvmPackages.libclang
          pkgs.llvmPackages.clang
          pkgs.qemu  # For ARM emulation
        ];

        # ARM emulator script using QEMU
        armEmulatorScript = pkgs.writeShellScriptBin "run-arm-emulator" ''
          SYSTEM_IMG="${androidSdk}/libexec/android-sdk/system-images/android-34/google_apis/arm64-v8a"

          echo "Starting ARM64 Android emulator (software emulation - will be slow)"
          echo "System image: $SYSTEM_IMG"

          # Create temp directory for runtime files
          WORKDIR=$(mktemp -d)
          trap "rm -rf $WORKDIR" EXIT

          echo "Copying images to writable location..."
          cp $SYSTEM_IMG/system.img $WORKDIR/system.img
          cp $SYSTEM_IMG/vendor.img $WORKDIR/vendor.img
          cp $SYSTEM_IMG/userdata.img $WORKDIR/userdata.img
          chmod 644 $WORKDIR/*.img

          # Resize userdata for more space
          ${pkgs.qemu}/bin/qemu-img resize -f raw $WORKDIR/userdata.img 2G

          echo "Starting QEMU with GUI..."
          # Run QEMU with Android system image and graphical display
          ${pkgs.qemu}/bin/qemu-system-aarch64 \
            -machine virt,gic-version=2 \
            -cpu cortex-a57 \
            -smp 4 \
            -m 3072 \
            -kernel $SYSTEM_IMG/kernel-ranchu \
            -initrd $SYSTEM_IMG/ramdisk.img \
            -drive file=$WORKDIR/system.img,format=raw,if=none,id=system,readonly=off \
            -device virtio-blk-device,drive=system \
            -drive file=$WORKDIR/vendor.img,format=raw,if=none,id=vendor,readonly=off \
            -device virtio-blk-device,drive=vendor \
            -drive file=$WORKDIR/userdata.img,format=raw,if=none,id=userdata \
            -device virtio-blk-device,drive=userdata \
            -append "androidboot.hardware=ranchu androidboot.selinux=permissive console=ttyAMA0 androidboot.console=ttyAMA0" \
            -netdev user,id=net0,hostfwd=tcp::5555-:5555 \
            -device virtio-net-device,netdev=net0 \
            -device virtio-gpu-pci,xres=720,yres=1280 \
            -display sdl \
            -device qemu-xhci \
            -device usb-kbd \
            -device usb-tablet
        '';

      in {
        devShells = {
          # Default shell - desktop development
          default = pkgs.mkShell {
            buildInputs = commonDeps ++ desktopDeps;

            LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath desktopDeps;

            shellHook = ''
              echo "Yetty development environment (desktop)"
              echo "  Run: make release"
            '';
          };

          # Android build shell
          android = pkgs.mkShell {
            buildInputs = commonDeps ++ androidDeps ++ [ pkgs.zlib pkgs.openssl armEmulatorScript ];

            ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
            ANDROID_SDK_ROOT = "${androidSdk}/libexec/android-sdk";
            ANDROID_NDK_HOME = androidNdk;
            NDK_HOME = androidNdk;
            JAVA_HOME = "${pkgs.jdk17}";
            LIBCLANG_PATH = "${pkgs.llvmPackages.libclang.lib}/lib";

            shellHook = ''
              # Add NDK toolchain to PATH for cross-compilation
              export PATH="${androidNdk}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"

              echo "Yetty Android build environment"
              echo "  Rust: $(rustc --version)"
              echo "  Android SDK: $ANDROID_HOME"
              echo "  Android NDK: $ANDROID_NDK_HOME"
              echo "  NDK clang: $(which aarch64-linux-android26-clang 2>/dev/null || echo 'not in PATH')"
              echo ""
              echo "Run: make android"
              echo "Run ARM emulator: run-arm-emulator"
            '';
          };

          # Web/Emscripten build shell
          web = pkgs.mkShell {
            buildInputs = commonDeps ++ [
              pkgs.emscripten
              pkgs.python3
              pkgs.nodejs
            ];

            # Emscripten environment
            EMSDK = "${pkgs.emscripten}/share/emscripten";
            EM_CONFIG = "${pkgs.emscripten}/share/emscripten/.emscripten";
            EM_CACHE = "/tmp/emscripten-cache";

            shellHook = ''
              echo "Yetty Web/Emscripten build environment"
              echo "  Emscripten: $(emcc --version | head -1)"
              echo "  EMSDK: $EMSDK"
              echo ""
              echo "Run: make web"
            '';
          };
        };

        # Apps - runnable outputs
        apps = {
          emulator = {
            type = "app";
            program = "${armEmulatorScript}/bin/run-arm-emulator";
          };
        };

        # Packages
        packages = {
          arm-emulator = armEmulatorScript;
        };
      }
    );
}
