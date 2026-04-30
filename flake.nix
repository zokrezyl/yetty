{
  description = "Yetty - WebGPU Terminal Emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            android_sdk.accept_license = true;
          };
        };

        # Use Clang with libstdc++ (GCC's C++ library) for Dawn ABI compatibility
        clangStdenv = pkgs.clangStdenv;

        # Yetty package (nix build .#yetty)
        yetty = pkgs.callPackage ./build-tools/nix/default.nix {
          inherit clangStdenv;
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
          abiVersions = [ "arm64-v8a" "x86_64" ];
        };

        androidSdk = androidComposition.androidsdk;
        androidNdk = "${androidSdk}/libexec/android-sdk/ndk/26.1.10909125";

        # Common build dependencies
        commonDeps = with pkgs; [
          cmake
          ninja
          pkg-config
          git
          llvmPackages_21.clang
          llvmPackages_21.clang-unwrapped.dev
          llvmPackages_21.lld
          llvmPackages_21.llvm
          llvmPackages_21.libclang
          meson  # For dav1d (AV1 decoder)
          nasm   # For dav1d assembly optimizations
        ];

        # Static library packages (for static linking)
        fontconfigStatic = pkgs.fontconfig.overrideAttrs (old: {
          configureFlags = (old.configureFlags or []) ++ [ "--enable-static" "--disable-shared" ];
          postInstall = (old.postInstall or "") + ''
            rm -f $out/lib/*.so*
          '';
        });
        expatStatic = pkgs.expat.overrideAttrs (old: {
          configureFlags = (old.configureFlags or []) ++ [ "--enable-static" "--disable-shared" ];
          postInstall = (old.postInstall or "") + ''
            rm -f $out/lib/*.so*
          '';
        });
        libuuidStatic = pkgs.libuuid.overrideAttrs (old: {
          configureFlags = (old.configureFlags or []) ++ [ "--enable-static" ];
        });

        # Buck2 build dependencies
        buck2Deps = with pkgs; [
          buck2
          # Build tools buck2 genrules will call
          cmake
          ninja
          pkg-config
          curl
          gnutar
          gzip
          gperf  # For fontconfig
          # Compilers
          llvmPackages_21.clang
          llvmPackages_21.lld
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
          expat  # Required by fontconfig

          # Other
          glfw
          zlib
          openssl
          brotli  # For asset compression in incbin

          # Static libraries (referenced via *_STATIC_LIB env vars, not linked globally)
          zlib.static
          fontconfigStatic
          expatStatic
          libuuidStatic
        ];

        # Android build dependencies
        androidDeps = [
          androidSdk
          pkgs.llvmPackages.libclang
          pkgs.llvmPackages.clang
          pkgs.qemu  # For ARM emulation
        ];

        # meson wrapped so `packaging` is importable from its python.
        # glib's meson.build:2422 does `find_installation(modules: ['packaging'])`
        # with no name, which resolves to meson's sys.executable. The stock
        # meson's shebang points at a bare python3 that has no `packaging`,
        # so wrap meson with a shell script that prepends packaging's
        # site-packages to PYTHONPATH before exec'ing.
        mesonWithPackaging = pkgs.writeShellScriptBin "meson" ''
          export PYTHONPATH="${pkgs.python3.pkgs.packaging}/${pkgs.python3.sitePackages}''${PYTHONPATH:+:$PYTHONPATH}"
          exec ${pkgs.meson}/bin/meson "$@"
        '';

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

            # Static library paths for cmake
            FONTCONFIG_STATIC_LIB = "${fontconfigStatic.lib}/lib/libfontconfig.a";
            EXPAT_STATIC_LIB = "${expatStatic}/lib/libexpat.a";
            ZLIB_STATIC_LIB = "${pkgs.zlib.static}/lib/libz.a";
            UUID_STATIC_LIB = "${libuuidStatic.lib}/lib/libuuid.a";

            # LLVM/Clang paths for QA tools
            LLVM_DIR = "${pkgs.llvmPackages_21.llvm.dev}/lib/cmake/llvm";
            Clang_DIR = "${pkgs.llvmPackages_21.clang-unwrapped.dev}/lib/cmake/clang";

            shellHook = ''
              echo "Yetty development environment (desktop)"
              echo "  Run: make release"
            '';
          };

          # Android build shell
          android = pkgs.mkShell {
            buildInputs = commonDeps ++ androidDeps ++ [ pkgs.zlib pkgs.openssl pkgs.brotli armEmulatorScript ];

            ANDROID_HOME = "${androidSdk}/libexec/android-sdk";
            ANDROID_SDK_ROOT = "${androidSdk}/libexec/android-sdk";
            ANDROID_NDK_HOME = androidNdk;
            NDK_HOME = androidNdk;
            JAVA_HOME = "${pkgs.jdk17}";

            shellHook = ''
              # Add NDK toolchain to PATH for cross-compilation
              export PATH="${androidNdk}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"

              echo "Yetty Android build environment"
              echo "  Android SDK: $ANDROID_HOME"
              echo "  Android NDK: $ANDROID_NDK_HOME"
              echo "  NDK clang: $(which aarch64-linux-android26-clang 2>/dev/null || echo 'not in PATH')"
              echo ""
              echo "Run: make android"
              echo "Run ARM emulator: run-arm-emulator"
            '';
          };

          # Asset-build shell: RISC-V cross-toolchain for OpenSBI + Linux kernel.
          # Used by build-tools/assets/{opensbi,linux}/build.sh.
          # Override CROSS_COMPILE when invoking scripts outside nix if needed.
          assets-riscv = pkgs.mkShell {
            buildInputs = with pkgs; [
              # RISC-V cross-toolchain
              pkgsCross.riscv64.buildPackages.gcc
              pkgsCross.riscv64.buildPackages.binutils

              # Build drivers + fetch tools
              gnumake
              bc
              bison
              flex
              cpio
              rsync
              curl
              gnutar
              gzip

              # Kernel deps (host-side, for scripts/)
              openssl
              elfutils
              perl
              python3
              pkg-config
            ];

            shellHook = ''
              export CROSS_COMPILE="riscv64-unknown-linux-gnu-"
              echo "Yetty asset-build environment (RISC-V cross)"
              echo "  CROSS_COMPILE=$CROSS_COMPILE"
              echo "  gcc:    $(''${CROSS_COMPILE}gcc --version | head -1)"
              echo ""
              echo "Run: bash build-tools/assets/opensbi/build.sh"
              echo "Run: bash build-tools/assets/linux/build.sh"
            '';
          };

          # Asset-build shell for build-tools/3rdparty/alpine-extended-disk:
          # Builds a raw ext4 RISC-V Alpine rootfs *with the JSLinux package
          # set baked in*, by booting the freshly-laid-down disk in
          # qemu-system-riscv64 and letting Alpine's own apk run inside the
          # VM. So the shell needs:
          #   - disk plumbing (mkfs.ext4, losetup, mount) + brotli + curl
          #   - qemu-system-riscv64 to run the install VM
          # The kernel for the install VM is fetched as a prebuilt artifact
          # from this repo's `lib-linux-<ver>` releases — no cross-toolchain
          # needed here (that's `assets-riscv`).
          assets-alpine-extended = pkgs.mkShell {
            buildInputs = with pkgs; [
              curl
              brotli
              e2fsprogs
              util-linux
              gnutar
              gzip
              qemu
            ];
            shellHook = ''
              echo "Yetty asset-build environment (alpine-extended-disk)"
              echo "  qemu-system-riscv64: $(qemu-system-riscv64 --version | head -1)"
            '';
          };

          # Asset-build shell: host toolchain for building yetty-ymsdf-gen
          # and generating MSDF CDB font databases from TTFs.
          # Used by build-tools/assets/cdb/build.sh.
          assets-cdb = pkgs.mkShell {
            buildInputs = with pkgs; [
              cmake
              ninja
              pkg-config
              git
              gcc
              curl
              gnutar
              gzip
              zlib
              zlib.static
              brotli
            ];
            shellHook = ''
              echo "Yetty asset-build environment (cdb / host tools)"
            '';
          };

          # Asset-build shells: one per QEMU target platform.
          # Each carries the target-arch glib/pixman/pkg-config so QEMU's
          # meson configure can find them via the cross pkg-config wrapper.

          assets-qemu-linux-x86_64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja pkg-config python3 bison flex gnumake perl
              curl gnutar xz gzip
              gcc glib glib.dev pixman zlib zlib.static
            ];
            shellHook = "echo 'Yetty asset-build (qemu linux-x86_64)'";
          };

          # Use pkgsCross.*.mkShell so nativeBuildInputs (host-side build
          # tools) stay out of PKG_CONFIG_PATH_FOR_TARGET. Otherwise native
          # zlib from curl/python leaks into the cross link and the
          # aarch64 linker gets fed x86_64 libz.
          assets-qemu-linux-aarch64 = pkgs.pkgsCross.aarch64-multiplatform.mkShell {
            nativeBuildInputs = with pkgs; [
              meson ninja python3 bison flex gnumake perl
              curl gnutar xz gzip
              pkgsCross.aarch64-multiplatform.buildPackages.pkg-config
            ];
            buildInputs = with pkgs.pkgsCross.aarch64-multiplatform; [
              glib pixman zlib
            ];
            shellHook = ''
              # QEMU configure defers pkg-config lookups to $PKG_CONFIG
              # when set; make it the aarch64 cross wrapper.
              export PKG_CONFIG=aarch64-unknown-linux-gnu-pkg-config
              echo "Yetty asset-build (qemu linux-aarch64)"
            '';
          };

          # windows-x86_64: glib is not available in nixpkgs' mingwW64 cross.
          # Shell provides just the toolchain + zlib so the configure
          # failure lands deterministically; CI can supply glib via a
          # downloaded binary package when we're ready to support it.
          assets-qemu-windows-x86_64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja python3 bison flex gnumake perl
              curl gnutar xz gzip
              pkgsCross.mingwW64.buildPackages.gcc
              pkgsCross.mingwW64.buildPackages.pkg-config
              pkgsCross.mingwW64.zlib
            ];
            shellHook = "echo 'Yetty asset-build (qemu windows-x86_64)'";
          };

          # android targets: use the Android NDK directly (same toolchain
          # yetty's own Android build uses via gradle). Sidesteps
          # pkgsCross.*-android, whose compiler-rt rebuild has been broken
          # across clang-19/20/21. We build glib + pixman from source into
          # a sysroot with NDK clang — see build-tools/assets/qemu/
          # platforms/android-*.sh.
          #
          # This shell only supplies the build-side tooling; the NDK comes
          # in via the `ANDROID_NDK_HOME` env and PATH additions below.
          # glib's meson.build does `python.find_installation('python3', modules: ['packaging'])`
          # and meson prefers its own bundled python over $PATH.
          # `mesonWithPackaging` (defined in the outer let block) ships a
          # meson whose site-packages includes `packaging`, and we prepend
          # its bin dir to PATH because commonDeps already contributes a
          # plain `meson` that would otherwise shadow it.
          assets-qemu-android-arm64-v8a = pkgs.mkShell {
            buildInputs = commonDeps ++ androidDeps ++ (with pkgs; [
              mesonWithPackaging ninja bison flex gnumake perl
              curl gnutar xz gzip pkg-config
              (python3.withPackages (ps: [ ps.packaging ]))
            ]);
            ANDROID_NDK_HOME = androidNdk;
            # API 28 — bionic gained iconv here, glib requires it.
            ANDROID_API = "28";
            ANDROID_TARGET_ABI = "arm64-v8a";
            shellHook = ''
              export PATH="${mesonWithPackaging}/bin:${androidNdk}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
              echo "Yetty asset-build (qemu android-arm64-v8a) — NDK direct"
            '';
          };

          assets-qemu-android-x86_64 = pkgs.mkShell {
            buildInputs = commonDeps ++ androidDeps ++ (with pkgs; [
              mesonWithPackaging ninja bison flex gnumake perl
              curl gnutar xz gzip pkg-config
              (python3.withPackages (ps: [ ps.packaging ]))
            ]);
            ANDROID_NDK_HOME = androidNdk;
            # API 28 — bionic gained iconv here, glib requires it.
            ANDROID_API = "28";
            ANDROID_TARGET_ABI = "x86_64";
            shellHook = ''
              export PATH="${mesonWithPackaging}/bin:${androidNdk}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
              echo "Yetty asset-build (qemu android-x86_64) — NDK direct"
            '';
          };

          # Darwin targets (macos/ios/tvos): run on a macOS host with nix
          # installed. Build tools come from nix; the Xcode SDKs for
          # iOS/tvOS still come from xcrun at script time.
          # macos native uses the host's clang + nix-provided glib/pixman.
          # iOS/tvOS use xcrun -sdk switches — they don't need glib/pixman
          # cross-builds because the resulting binary is embedded (no
          # dynamic glib dep) — but QEMU's configure still wants glib at
          # build time. Supplying a native glib and pointing configure at
          # it works because meson uses the same compiler for both host
          # and target on darwin cross (the kludge path used in poc/qemu
          # ios scripts).
          assets-qemu-macos-x86_64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja pkg-config python3 bison flex gnumake perl
              curl gnutar xz gzip
              glib pixman
            ];
            shellHook = "echo 'Yetty asset-build (qemu macos-x86_64)'";
          };

          assets-qemu-macos-arm64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja pkg-config python3 bison flex gnumake perl
              curl gnutar xz gzip
              glib pixman
            ];
            shellHook = "echo 'Yetty asset-build (qemu macos-arm64)'";
          };

          assets-qemu-ios-arm64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja pkg-config python3 bison flex gnumake perl
              curl gnutar xz gzip
              glib pixman
            ];
            shellHook = "echo 'Yetty asset-build (qemu ios-arm64)'";
          };

          assets-qemu-ios-x86_64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja pkg-config python3 bison flex gnumake perl
              curl gnutar xz gzip
              glib pixman
            ];
            shellHook = "echo 'Yetty asset-build (qemu ios-x86_64 simulator)'";
          };

          assets-qemu-tvos-arm64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja pkg-config python3 bison flex gnumake perl
              curl gnutar xz gzip
              glib pixman
            ];
            shellHook = "echo 'Yetty asset-build (qemu tvos-arm64)'";
          };

          assets-qemu-tvos-x86_64 = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja pkg-config python3 bison flex gnumake perl
              curl gnutar xz gzip
              glib pixman
            ];
            shellHook = "echo 'Yetty asset-build (qemu tvos-x86_64 simulator)'";
          };

          # 3rdparty library build shells — one per target platform.
          # Used by build-tools/3rdparty/<lib>/build.sh wrappers, which
          # invoke nix develop .#3rdparty-<target> before re-execing into
          # the per-lib _build.sh. Lean: meson/ninja/gnumake/perl/nasm
          # plus the per-target compiler/SDK. Add deps here as new libs land.

          "3rdparty-linux-x86_64" = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja gnumake perl nasm python3
              gcc binutils
              curl gnutar xz gzip
              pkg-config
              # X11 dev headers — glfw 3.4 build probes for these via
              # find_package(X11). Without them GLFW_BUILD_X11=ON fails.
              # Wayland is intentionally OFF in the glfw producer.
              xorg.libX11 xorg.libXrandr xorg.libXinerama xorg.libXcursor
              xorg.libXi xorg.libXext xorg.xorgproto
            ];
            shellHook = "echo 'Yetty 3rdparty-build (linux-x86_64)'";
          };

          # Cross to aarch64 Linux: pkgsCross.*.mkShell so nativeBuildInputs
          # (host-side build tools) stay out of the cross link path.
          # Cross-X11 (libX11/Xrandr/etc.) goes via buildInputs so glfw's
          # find_package(X11) resolves when targeting Raspberry-Pi-style
          # boards.
          "3rdparty-linux-aarch64" = pkgs.pkgsCross.aarch64-multiplatform.mkShell {
            nativeBuildInputs = with pkgs; [
              meson ninja gnumake perl nasm python3
              curl gnutar xz gzip
              pkgsCross.aarch64-multiplatform.buildPackages.gcc
              pkgsCross.aarch64-multiplatform.buildPackages.binutils
              pkgsCross.aarch64-multiplatform.buildPackages.pkg-config
            ];
            buildInputs = with pkgs.pkgsCross.aarch64-multiplatform; [
              xorg.libX11 xorg.libXrandr xorg.libXinerama
              xorg.libXcursor xorg.libXi xorg.libXext xorgproto
            ];
            shellHook = ''
              export CROSS_PREFIX="aarch64-unknown-linux-gnu-"
              echo "Yetty 3rdparty-build (linux-aarch64) — CROSS_PREFIX=$CROSS_PREFIX"
            '';
          };

          # Cross to riscv64 Linux: same shape as linux-aarch64. Host runs
          # demo clients inside a RISC-V VM, so every link-target lib needs
          # a riscv64 build alongside x86_64/aarch64. nasm stays in the kit
          # for symmetry — RISC-V has no NASM target, individual _build.sh
          # branches gate hand-rolled asm with USE_ASM=No / WITH_SIMD=OFF /
          # enable_asm=false depending on the upstream build system.
          "3rdparty-linux-riscv64" = pkgs.pkgsCross.riscv64.mkShell {
            nativeBuildInputs = with pkgs; [
              meson ninja gnumake perl nasm python3
              curl gnutar xz gzip
              pkgsCross.riscv64.buildPackages.gcc
              pkgsCross.riscv64.buildPackages.binutils
              pkgsCross.riscv64.buildPackages.pkg-config
            ];
            buildInputs = with pkgs.pkgsCross.riscv64; [
              xorg.libX11 xorg.libXrandr xorg.libXinerama
              xorg.libXcursor xorg.libXi xorg.libXext xorgproto
            ];
            shellHook = ''
              export CROSS_PREFIX="riscv64-unknown-linux-gnu-"
              echo "Yetty 3rdparty-build (linux-riscv64) — CROSS_PREFIX=$CROSS_PREFIX"
            '';
          };

          # macOS native — clang comes from Xcode on the host runner.
          # autoconf/automake/libtool are needed by libmagic (file 5.x):
          # its `make` rules regenerate Makefile.in from Makefile.am even
          # in --disable-maintainer-mode (file's configure.ac doesn't gate
          # the regen rules with AM_MAINTAINER_MODE).
          "3rdparty-macos-x86_64" = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja gnumake perl nasm python3
              autoconf automake libtool
              curl gnutar xz gzip
              pkg-config
            ];
            shellHook = "echo 'Yetty 3rdparty-build (macos-x86_64)'";
          };

          "3rdparty-macos-arm64" = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja gnumake perl nasm python3
              autoconf automake libtool
              curl gnutar xz gzip
              pkg-config
            ];
            shellHook = "echo 'Yetty 3rdparty-build (macos-arm64)'";
          };

          # iOS — compilation goes through `/usr/bin/xcrun -sdk <iphoneos|iphonesimulator>`
          # at build time; nasm is for the simulator (x86_64) only.
          "3rdparty-ios-arm64" = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja gnumake perl python3
              curl gnutar xz gzip
              pkg-config
            ];
            shellHook = "echo 'Yetty 3rdparty-build (ios-arm64)'";
          };

          "3rdparty-ios-x86_64" = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja gnumake perl nasm python3
              curl gnutar xz gzip
              pkg-config
            ];
            shellHook = "echo 'Yetty 3rdparty-build (ios-x86_64 simulator)'";
          };

          # tvOS — same toolchain shape as iOS; x86_64 is the appletvsimulator,
          # arm64 is the Apple TV device (appletvos SDK).
          "3rdparty-tvos-arm64" = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja gnumake perl python3
              curl gnutar xz gzip
              pkg-config
            ];
            shellHook = "echo 'Yetty 3rdparty-build (tvos-arm64)'";
          };

          "3rdparty-tvos-x86_64" = pkgs.mkShell {
            buildInputs = with pkgs; [
              meson ninja gnumake perl nasm python3
              curl gnutar xz gzip
              pkg-config
            ];
            shellHook = "echo 'Yetty 3rdparty-build (tvos-x86_64 simulator)'";
          };

          # Android — NDK-direct cross. The NDK comes in via $ANDROID_NDK_HOME
          # and PATH additions below.
          "3rdparty-android-arm64-v8a" = pkgs.mkShell {
            buildInputs = commonDeps ++ androidDeps ++ (with pkgs; [
              meson ninja gnumake perl python3
              curl gnutar xz gzip
              pkg-config
            ]);
            ANDROID_NDK_HOME = androidNdk;
            ANDROID_API = "26";
            shellHook = ''
              export PATH="${androidNdk}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
              echo "Yetty 3rdparty-build (android-arm64-v8a) — NDK direct"
            '';
          };

          "3rdparty-android-x86_64" = pkgs.mkShell {
            buildInputs = commonDeps ++ androidDeps ++ (with pkgs; [
              meson ninja gnumake perl nasm python3
              curl gnutar xz gzip
              pkg-config
            ]);
            ANDROID_NDK_HOME = androidNdk;
            ANDROID_API = "26";
            shellHook = ''
              export PATH="${androidNdk}/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
              echo "Yetty 3rdparty-build (android-x86_64) — NDK direct"
            '';
          };

          # WebAssembly via emscripten. emmake/emcc wrap the toolchain.
          "3rdparty-webasm" = pkgs.mkShell {
            buildInputs = with pkgs; [
              emscripten meson ninja gnumake perl python3 nodejs
              curl gnutar xz gzip
              pkg-config
            ];
            EMSDK = "${pkgs.emscripten}/share/emscripten";
            EM_CONFIG = "${pkgs.emscripten}/share/emscripten/.emscripten";
            EM_CACHE = "/tmp/emscripten-cache";
            shellHook = "echo 'Yetty 3rdparty-build (webasm)'";
          };

          # Web/Emscripten build shell
          web = pkgs.mkShell {
            buildInputs = commonDeps ++ [
              pkgs.emscripten
              pkgs.python3
              pkgs.nodejs
              pkgs.gcc  # For building host tools (yecho-static for VM)
              pkgs.brotli  # For asset compression in incbin
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

          # Buck2 build shell
          buck2 = pkgs.mkShell {
            buildInputs = buck2Deps ++ desktopDeps;

            LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath desktopDeps;

            # Ensure clang is used
            CC = "clang";
            CXX = "clang++";

            shellHook = ''
              echo "Yetty Buck2 build environment"
              echo "  Buck2: $(buck2 --version 2>/dev/null || echo 'available')"
              echo "  Clang: $(clang --version | head -1)"
              echo ""
              echo "Build commands:"
              echo "  buck2 build //:yetty           # Build main executable"
              echo "  buck2 build //...              # Build everything"
              echo "  buck2 targets //...            # List all targets"
              echo ""
              echo "Cache info:"
              echo "  Cache dir: ~/.cache/buck2"
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
          inherit yetty;
          default = yetty;
          arm-emulator = armEmulatorScript;
        };
      }
    );
}
