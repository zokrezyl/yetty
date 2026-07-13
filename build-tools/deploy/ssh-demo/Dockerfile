# Ephemeral demo environment served over the yetty SSH-over-WebSocket bridge.
#
# One fresh, disposable instance is started per visitor (see
# yetty-demo-session.sh) and destroyed on disconnect. It holds no persistent
# state: the home directory and /tmp are tmpfs at runtime and the whole root
# filesystem is mounted read-only.
#
# The point of the container is the yetty companion tools (ycat, yplot,
# ychart-cli, ydiagram-cli, ymesh, yflame, ydoc, ysheet, yslide, ...). They
# emit rich-content OSC/ydraw envelopes over the SSH stream, which the
# browser-side yetty (yetty.dev, webasm) renders as figures. That is the demo.
FROM debian:stable-slim

# Runtime shared libraries the companion tools link against, plus a couple of
# small terminal toys and the tools the installer bootstrap needs.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        tar \
        libfontconfig1 \
        libfreetype6 \
        libpng16-16 \
        libwebp7 \
        libxml2 \
        less \
        nano \
        neovim \
        vim-tiny \
        htop \
        tree \
        cmatrix \
    && rm -rf /var/lib/apt/lists/*

# Install the yetty product into system paths (not $HOME, which is a tmpfs at
# runtime and would be wiped). yinstall resolves its target dirs from the XDG
# variables, so we set them ONLY for this install step — deliberately NOT as
# persistent ENV. Leaking XDG_DATA_HOME=/usr/local/share into the runtime
# environment would hijack every other XDG-aware program (nvim, for one, would
# try to create /usr/local/share/nvim on the read-only rootfs). At runtime the
# shell keeps the normal per-user XDG defaults; profile.sh symlinks the yetty
# data/config into the writable home so the tools still find their install.

# "latest" currently points at a yos-web release with no Linux desktop asset,
# so pin a release that ships yetty-linux.tar.gz. Override with
# --build-arg YETTY_VERSION=yetty-X.Y.Z to bump.
ARG YETTY_VERSION=yetty-0.2.55

RUN curl -fsSL https://yetty.dev/install.sh \
    | XDG_BIN_HOME=/usr/local/bin \
      XDG_DATA_HOME=/usr/local/share \
      XDG_CONFIG_HOME=/usr/local/etc/xdg \
      YETTY_VERSION="${YETTY_VERSION}" bash \
    # Drop the RISC-V VM runtime — ~470 MB, irrelevant to the CLI demos.
    && rm -rf /usr/local/share/yetty/yemu /usr/local/share/yetty/qemu \
    # yinstall lays the demo scripts down non-executable (0644); make them
    # runnable so visitors can `./all.sh` instead of `bash all.sh`.
    && find /usr/local/share/yetty/demos -type f -name '*.sh' -exec chmod 0755 {} + \
    && ls /usr/local/bin/ycat /usr/local/bin/yplot >/dev/null

# Bundle the full source tree of the SAME release into the image so visitors can
# browse and grep the exact code that produced the installed binaries. The
# GitHub tag archive unpacks under a "yetty-${YETTY_VERSION}" top dir, which we
# strip so the tree sits directly under /usr/share/yetty/sources.
RUN mkdir -p /usr/share/yetty/sources \
    && curl -fsSL "https://github.com/zokrezyl/yetty/archive/refs/tags/${YETTY_VERSION}.tar.gz" \
       | tar -xz -C /usr/share/yetty/sources --strip-components=1 \
    && test -f /usr/share/yetty/sources/CMakeLists.txt

# Non-root demo user. uid/gid 1000 matches the runtime tmpfs home mount so the
# shell can write to its own home even under --read-only.
RUN useradd --create-home --uid 1000 --shell /bin/bash yetty

COPY motd /etc/motd
COPY profile.sh /etc/profile.d/zz-yetty-demo.sh

USER yetty
WORKDIR /home/yetty
ENV TERM=xterm-256color \
    TERM_PROGRAM=yetty

# Login shell so /etc/profile.d/zz-yetty-demo.sh runs (banner + idle timeout).
CMD ["/bin/bash", "-l"]
