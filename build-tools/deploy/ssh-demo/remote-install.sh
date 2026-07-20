#!/usr/bin/env bash
# Provision the demo on the remote host FROM your workstation.
#
# Runs LOCALLY (no root needed here): it copies this whole ssh-demo deploy
# directory to the remote demo host and runs install.sh there under sudo. The
# provisioner needs its sibling files (Dockerfile, session launcher,
# sshd/nginx/systemd configs, motd, profile, update-yetty.sh), so the entire
# directory is staged on the remote in a throwaway temp dir that is removed
# afterwards.
#
# Usage:
#   ./remote-install.sh                       # host+domain default to ws.yetty.dev
#   ./remote-install.sh ssh.yetty.dev         # override the domain
#   YETTY_DEMO_SSH=ubuntu@1.2.3.4 ./remote-install.sh ws.yetty.dev
#   DEMO_PASSWORD=hunter2 ./remote-install.sh  # forwarded to install.sh
#
# Environment:
#   YETTY_DEMO_SSH   ssh target (default ubuntu@ws.yetty.dev)
#   DEMO_PASSWORD, DEMO_USER, WS_PORT, CERTBOT_EMAIL
#                    forwarded to install.sh on the remote if set (avoid single
#                    quotes in the values — they are wrapped in single quotes).

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SSH_TARGET="${YETTY_DEMO_SSH:-ubuntu@ws.yetty.dev}"
# Domain defaults to the host part of the ssh target (ubuntu@ws.yetty.dev -> ws.yetty.dev).
DOMAIN="${1:-${SSH_TARGET#*@}}"

log() { printf 'remote-install: %s\n' "$1" >&2; }

# Forward a small allowlist of install.sh knobs if the caller exported them.
remote_env=""
for var in DEMO_PASSWORD DEMO_USER WS_PORT CERTBOT_EMAIL; do
    if [ -n "${!var:-}" ]; then
        remote_env+="${var}='${!var}' "
    fi
done

log "target host: ${SSH_TARGET}   domain: ${DOMAIN}"

log "staging deploy directory on the remote"
remote_dir="$(ssh "${SSH_TARGET}" 'mktemp -d /tmp/yetty-ssh-demo.XXXXXX')"
scp -rq "${HERE}"/* "${SSH_TARGET}:${remote_dir}/"

log "running provisioner on the remote (sudo)"
# $rc/$? are escaped so they evaluate on the REMOTE shell; the rest interpolates
# here. sudo applies only to install.sh; staging is cleaned up on any exit path.
remote_cmd="cd '${remote_dir}' \
    && chmod +x install.sh update-yetty.sh yetty-demo-session.sh \
    && sudo ${remote_env}./install.sh '${DOMAIN}'; rc=\$?; rm -rf '${remote_dir}'; exit \$rc"
ssh -t "${SSH_TARGET}" "${remote_cmd}"

log "done"
