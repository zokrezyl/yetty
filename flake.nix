{
  description = "Yetty - WebGPU Terminal Emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";
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
          includeNDK = true;
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
        ];

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
            buildInputs = commonDeps ++ androidDeps ++ [ pkgs.zlib pkgs.openssl ];

            ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
            ANDROID_SDK_ROOT = "${androidSdk}/libexec/android-sdk";
            ANDROID_NDK_HOME = androidNdk;
            NDK_HOME = androidNdk;
            JAVA_HOME = "${pkgs.jdk17}";
            LIBCLANG_PATH = "${pkgs.llvmPackages.libclang.lib}/lib";

            shellHook = ''
              echo "Yetty Android build environment"
              echo "  Rust: $(rustc --version)"
              echo "  Android SDK: $ANDROID_HOME"
              echo "  Android NDK: $ANDROID_NDK_HOME"
              echo ""
              echo "Run: ./android/build-wgpu.sh"
              echo "Then: make android"
            '';
          };
        };
      }
    );
}
