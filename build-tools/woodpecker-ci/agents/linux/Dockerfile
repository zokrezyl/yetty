FROM ubuntu:latest

ENV DEBIAN_FRONTEND=noninteractive

# Chrome .deb fetched at build time so we don't need curl/gnupg pre-install
# (which is the only reason Chrome would force a second apt-get update cycle).
ADD https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb /tmp/chrome.deb

# All apt installs in one shot — single update, single install, single cleanup.
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    # LLVM/Clang — unversioned meta-packages provide /usr/bin/clang etc.
    # On Ubuntu 24.04 these resolve to clang-18, matching the dev libs below.
    # Several 3rdparty/_build.sh scripts call /usr/bin/clang directly.
    clang \
    clang-format \
    clang-tidy \
    # Dev libs needed by qa-tools/.../CMakeLists.txt:
    #   /usr/lib/llvm-18/lib/cmake/llvm  (llvm-18-dev)
    #   /usr/lib/cmake/clang-18          (libclang-18-dev)
    llvm-18-dev \
    libclang-18-dev \
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
    # NVIDIA driver userspace
    nvidia-utils-580 \
    libnvidia-gl-580 \
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
    # Browser for webasm/web tests (local .deb fetched via ADD above; apt
    # resolves its dependencies from Ubuntu repos in the same install).
    /tmp/chrome.deb \
    && rm /tmp/chrome.deb && rm -rf /var/lib/apt/lists/*

# Android SDK and NDK
ENV ANDROID_HOME=/opt/android-sdk
ENV ANDROID_SDK_ROOT=$ANDROID_HOME
ENV ANDROID_NDK_VERSION=26.1.10909125
ENV ANDROID_NDK_HOME=$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION

RUN mkdir -p $ANDROID_HOME/cmdline-tools && \
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
        "cmake;3.22.1" \
        "emulator" \
        "system-images;android-34;google_apis;x86_64" && \
    chown -R 1000:1000 $ANDROID_HOME

# Emscripten SDK
ENV EMSDK=/opt/emsdk
ENV EMSDK_NODE=/opt/emsdk/node/20.18.0_64bit/bin/node
ENV PATH=$EMSDK:$EMSDK/upstream/emscripten:$PATH

RUN git clone https://github.com/emscripten-core/emsdk.git $EMSDK && \
    cd $EMSDK && \
    ./emsdk install latest && \
    ./emsdk activate latest && \
    chown -R 1000:1000 $EMSDK

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
ENV PATH=$EMSDK:$EMSDK/upstream/emscripten:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/emulator:$ANDROID_HOME/platform-tools:$PATH

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

CMD ["zsh"]
