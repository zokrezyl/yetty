#!/usr/bin/env bash
# Refresh the mounted yetty install (binaries + data + config) AND the source
# tree on the remote host FROM your workstation.
#
# Runs LOCALLY (no root needed here): it copies update-yetty.sh to the remote
# demo host and runs it there under sudo, so the host directories each demo
# container bind-mounts (/var/lib/yetty-demo/prefix and /var/lib/yetty-demo/sources)
# get the latest release. No image rebuild, no redeploy. The script is
# standalone, so only that one file is copied.
#
# Usage:
#   ./remote-update-yetty.sh                    # newest yetty-X.Y.Z
#   ./remote-update-yetty.sh yetty-0.2.71       # a specific tag
#   YETTY_DEMO_SSH=ubuntu@1.2.3.4 ./remote-update-yetty.sh
#
# Environment:
#   YETTY_DEMO_SSH   ssh target (default ubuntu@ws.yetty.dev)

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SSH_TARGET="${YETTY_DEMO_SSH:-ubuntu@ws.yetty.dev}"
TAG="${1:-}"

log() { printf 'remote-update-yetty: %s\n' "$1" >&2; }

log "target host: ${SSH_TARGET}"

log "copying update-yetty.sh to the remote"
remote_script="$(ssh "${SSH_TARGET}" 'mktemp /tmp/yetty-update.XXXXXX')"
scp -q "${HERE}/update-yetty.sh" "${SSH_TARGET}:${remote_script}"

log "running updater on the remote (sudo)${TAG:+ for tag ${TAG}}"
# $rc/$? are escaped so they evaluate on the REMOTE shell.
remote_cmd="chmod +x '${remote_script}' \
    && sudo '${remote_script}' ${TAG}; rc=\$?; rm -f '${remote_script}'; exit \$rc"
ssh -t "${SSH_TARGET}" "${remote_cmd}"

log "done"
