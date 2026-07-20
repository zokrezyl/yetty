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
#
# This image is a STABLE THIN RUNTIME: base OS + the shared libraries the tools
# link against + a few terminal toys + the demo user. NOTHING yetty-specific is
# baked in. The yetty binaries/data/config AND the browsable source tree all
# live in host directories that yetty-demo-session.sh bind-mounts read-only at
# runtime, so shipping a newer yetty needs only a host-side `update-yetty.sh`
# run — never an image rebuild. Because the image no longer carries the ~1.4 GB
# yetty payload it is small and rarely changes.
FROM debian:stable-slim

# Runtime shared libraries the companion tools link against, plus a couple of
# small terminal toys. curl/ca-certificates are handy inside the shell too.
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

# Mount points for the host-provided yetty install and source tree. The demo
# user resolves the product via the normal /usr/local paths (profile.sh bridges
# them into the writable home), so the launcher mounts the host prefix's bin /
# share/yetty / etc/xdg/yetty subtrees over these, plus the source tree. Creating
# the empty dirs means the paths exist even when nothing is mounted. (/usr/local/bin
# already exists in the base image and is the binaries mount point.)
RUN mkdir -p /usr/local/share/yetty \
             /usr/local/etc/xdg/yetty \
             /usr/share/yetty/sources

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
