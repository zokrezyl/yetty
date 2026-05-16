# Pinned to 24.04 (noble) — qa-tools' cmake hardcodes /usr/lib/cmake/clang-20
# and /usr/lib/llvm-20/lib/cmake/llvm, both supplied by noble-updates/universe
# (clang-20 / llvm-20-dev / libclang-20-dev). `ubuntu:latest` rolls forward
# across releases and would silently shift available clang versions; re-pin
# this FROM only when intentionally bumping LLVM in qa-tools.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# All apt installs in one shot — single update, single install, single cleanup.
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    # LLVM/Clang 20 explicitly. Versioned packages only — never the
    # unversioned `clang` meta-package, which can pull a different llvm
    # version and collide with the qa-tools' hardcoded /usr/lib/llvm-20
    # cmake paths (find_package(Clang) latches onto the first match).
    # Hardcoded paths in qa-tools/.../CMakeLists.txt:
    #   /usr/lib/llvm-20/lib/cmake/llvm  (llvm-20-dev)
    #   /usr/lib/cmake/clang-20          (libclang-20-dev)
    # update-alternatives below provides /usr/bin/clang for 3rdparty scripts.
    clang-20 \
    clang-format-20 \
    clang-tidy-20 \
    llvm-20-dev \
    libclang-20-dev \
    git \
    git-lfs \
    pkg-config \
    python3 \
    python3-pip \
    ccache \
    curl \
    wget \
    unzip \
    zsh \
    # Graphics / X11 / GPU
    libx11-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libxext-dev \
    libxxf86vm-dev \
    libxrender-dev \
    libwayland-dev \
    libxkbcommon-dev \
    libgl-dev \
    libvulkan-dev \
    mesa-utils \
    # GLFW dependencies
    libglfw3-dev \
    # FreeType
    libfreetype-dev \
    # libuv
    libuv1-dev \
    # msgpack
    libmsgpack-dev \
    # Misc libs
    libfontconfig-dev \
    libexpat1-dev \
    uuid-dev \
    libharfbuzz-dev \
    libpng-dev \
    libjpeg-dev \
    libwebp-dev \
    zlib1g-dev \
    # Build systems
    meson \
    nasm \
    # NetSurf libcss / libwapcaplet / etc. generate parser tables at build
    # time — needed by .woodpecker/libcss-linux-*.yml.
    perl \
    flex \
    bison \
    gperf \
    # Wayland protocol XMLs + wayland-scanner — needed by glfw's wayland
    # backend (.woodpecker/glfw-linux-*.yml).
    wayland-protocols \
    libwayland-bin \
    # Misc tools
    file \
    tmux \
    scrcpy \
    libpulse0 \
    # Docker CLI
    docker.io \
    libsm-dev libice-dev \
    # Android build - JDK
    openjdk-17-jdk-headless \
    && rm -rf /var/lib/apt/lists/* && \
    # Provide unversioned /usr/bin/clang etc. pointing at the -20 binaries
    # (3rdparty/_build.sh scripts hardcode `/usr/bin/clang` / `clang++`).
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 200 \
        --slave /usr/bin/clang++ clang++ /usr/bin/clang++-20 \
        --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-20 \
        --slave /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-20

# Multi-arch build (linux/amd64 + linux/arm64). The android NDK + emsdk
# ship only x86_64 binaries; skip both on non-amd64 hosts. The android &
# webasm woodpecker workflows are pinned to platform: linux/amd64 anyway,
# so a slimmer arm64 image is the correct outcome.
ARG TARGETARCH

# Android SDK and NDK (amd64 only)
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=$ANDROID_HOME
ENV ANDROID_NDK_VERSION=26.1.10909125
ENV ANDROID_NDK_HOME=$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION

RUN if [ "$TARGETARCH" = "amd64" ]; then \
        mkdir -p $ANDROID_HOME/cmdline-tools && \
        curl -L -o /tmp/cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip && \
        unzip -q /tmp/cmdline-tools.zip -d $ANDROID_HOME/cmdline-tools && \
        mv $ANDROID_HOME/cmdline-tools/cmdline-tools $ANDROID_HOME/cmdline-tools/latest && \
        rm /tmp/cmdline-tools.zip && \
        yes | $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager --licenses && \
        $ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager \
            "ndk;$ANDROID_NDK_VERSION" \
            "platform-tools" \
            "platforms;android-34" \
            "build-tools;34.0.0" \
            "cmake;3.22.1" && \
        chown -R 1000:1000 $ANDROID_HOME ; \
    fi

# Emscripten SDK (amd64 only)
ENV EMSDK=/opt/emsdk
ENV EMSDK_NODE=/opt/emsdk/node/20.18.0_64bit/bin/node
ENV PATH=$EMSDK:$EMSDK/upstream/emscripten:$PATH

RUN if [ "$TARGETARCH" = "amd64" ]; then \
        git clone https://github.com/emscripten-core/emsdk.git $EMSDK && \
        cd $EMSDK && \
        ./emsdk install latest && \
        ./emsdk activate latest && \
        chown -R 1000:1000 $EMSDK ; \
    fi

# Run as host user
RUN userdel -r ubuntu 2>/dev/null; \
    groupadd -g 1000 misi && \
    useradd -u 1000 -g 1000 -m -s /bin/zsh misi && \
    groupmod -g 138 docker && \
    groupadd -g 1005 vglusers && \
    groupadd -g 110 render 2>/dev/null; \
    groupadd -g 108 kvm 2>/dev/null; \
    usermod -aG docker,video,render,vglusers,kvm misi && \
    mkdir -m 0755 /nix && chown 1000:1000 /nix
USER misi
ENV HOME=/home/misi
ENV USER=misi
ENV PATH=$EMSDK:$EMSDK/upstream/emscripten:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH

# Android debug keystore for APK signing
RUN mkdir -p ~/.android && \
    keytool -genkey -v -keystore ~/.android/debug.keystore \
        -storepass android -alias androiddebugkey -keypass android \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Android Debug,O=Android,C=US"

# Nix (single-user install, flakes enabled)
RUN curl --proto '=https' --tlsv1.2 -sSf -L https://nixos.org/nix/install \
        | sh -s -- --no-daemon && \
    mkdir -p ~/.config/nix && \
    echo "experimental-features = nix-command flakes" > ~/.config/nix/nix.conf
ENV PATH=/home/misi/.nix-profile/bin:$PATH

# Woodpecker's clone container runs as root, build container as misi (uid
# 1000) — the workspace ends up root-owned. Both git and libgit2 (used by
# nix to evaluate `git+file://` flake inputs) refuse repos owned by a
# different uid. Opt out of that check for misi's gitconfig.
RUN git config --global --add safe.directory '*'

CMD ["zsh"]
