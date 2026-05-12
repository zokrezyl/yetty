#!/usr/bin/env bash
# ngtcp2 3rdparty wrapper. See nghttp2/build.sh for context. ngtcp2 is
# the QUIC transport library; this build fetches the openssl-new prebuilt
# tarball at build time and links its libngtcp2_crypto_ossl.a against
# OpenSSL's QUIC TLS API (SSL_set_quic_tls_cbs, available in OpenSSL ≥
# 3.5 — openssl-new is 4.0+).
set -euo pipefail
: "${TARGET_PLATFORM:?TARGET_PLATFORM is required}"
exec bash "$(dirname "$0")/_build.sh" "$@"
