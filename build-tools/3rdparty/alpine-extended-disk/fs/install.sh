#!/bin/sh
# One-shot init for the install VM. The kernel boots us with
# init=/install.sh; we apk add the package set, create the yetty user,
# self-erase, and poweroff so the host-side _build.sh continues.
#
# Files this script used to write inline now live as static files under
# the host-side fs/ overlay:
#   /etc/resolv.conf                — DNS server (10.0.2.3, qemu slirp)
#   /etc/apk/repositories           — alpine main + community
#   /etc/sudoers.d/wheel-nopasswd   — passwordless sudo for the wheel group
#   /etc/service/telnetd/run        — runit-supervised telnetd
#
# The pre-VM overlay pass in _build.sh has already laid them down, so we
# only need to bring up the kernel-side state (mounts, network) and run
# apk + adduser.

set -e

mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev 2>/dev/null || true

ip link set lo up
ip link set eth0 up
ip addr add 10.0.2.15/24 dev eth0
ip route add default via 10.0.2.2

echo "==> apk update"
apk update

echo "==> apk add (no toolchain — nvim/git/python/ssh/etc.)"
# Drop the heavy build toolchain (build-base, cmake, linux-headers — they
# alone took ~330 MB on the previous build) so the image fits the 300 MB
# target. Keep general-purpose CLI + editor + python + ssh + tracing.
# runit + busybox-extras supervise the in-guest telnetd that yetty
# --telnet attaches to.
apk add --no-cache \
    bash \
    zsh \
    coreutils \
    findutils \
    grep \
    sed \
    gawk \
    diffutils \
    patch \
    less \
    file \
    util-linux \
    neovim \
    git \
    openssh \
    python3 \
    py3-pip \
    net-tools \
    curl \
    wget \
    tar \
    gzip \
    xz \
    zip \
    unzip \
    man-pages \
    mandoc \
    strace \
    ncurses-terminfo-base \
    tree \
    htop \
    runit \
    busybox-extras \
    sudo \
    shadow

# yetty user/group — uid/gid 1000 so host-mounted shares (9p) line up
# with the typical desktop user. wheel group + NOPASSWD sudoers makes
# this a friction-free dev VM. Locked password (-D) is fine because
# the only entry point (telnetd) auto-logins as yetty (see
# /usr/local/sbin/yetty-telnet-login from the fs/ overlay).
echo "==> creating yetty user (uid/gid 1000, wheel/sudo)"
addgroup -g 1000 yetty
adduser -D -u 1000 -G yetty -s /bin/bash yetty
addgroup yetty wheel

# Drop apk's download cache + tmp scratch; it's just bytes the brotli pass
# would carry around. --no-cache already keeps /var/cache/apk empty, but
# in case any earlier `apk add` fell back to caching, drop the lot before
# we pack the image.
rm -rf /var/cache/apk/* /etc/apk/cache /tmp/* /var/tmp/* 2>/dev/null || true

# Self-erase so the produced image only carries the runtime /init.
# (Belt + braces — host-side _build.sh also rms /install.sh after the
# second fs/ overlay pass, which would otherwise re-stage this file.)
rm -f /install.sh

sync
echo "==> install complete, powering off"
# Older busybox poweroff sometimes hangs without -f; force it.
poweroff -f
