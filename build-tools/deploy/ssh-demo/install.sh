#!/usr/bin/env bash
# Provision the yetty SSH-over-WebSocket demo on a fresh Ubuntu host (e.g. an
# EC2 instance). Run as root from this directory:
#
#     sudo ./install.sh ssh.yetty.dev
#
# Prerequisites:
#   - A DNS A/AAAA record for the domain pointing at this host.
#   - Inbound TCP 80 and 443 open in the security group (80 is needed for the
#     Let's Encrypt HTTP-01 challenge; 443 serves wss://).
#   - Port 22 does NOT need to be open to the internet: the browser reaches
#     sshd through the loopback relay, so you can keep 22 closed and admin over
#     your key on a separate path.
#
# Idempotent-ish: safe to re-run to pick up changes to the image or configs.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DOMAIN="${1:-}"
DEMO_USER="${DEMO_USER:-yetty}"
DEMO_PASSWORD="${DEMO_PASSWORD:-yetty}"
WS_PORT="${WS_PORT:-8025}"
CERTBOT_EMAIL="${CERTBOT_EMAIL:-admin@${DOMAIN#*.}}"

if [ "$(id -u)" -ne 0 ]; then
    echo "error: run as root (sudo ./install.sh <domain>)" >&2
    exit 1
fi
if [ -z "${DOMAIN}" ]; then
    echo "usage: sudo ./install.sh <domain>   (e.g. ssh.yetty.dev)" >&2
    exit 1
fi

echo "==> installing packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    docker.io nginx curl ca-certificates certbot python3-certbot-nginx

echo "==> installing websocat"
if ! command -v websocat >/dev/null 2>&1; then
    curl -fsSL -o /usr/local/bin/websocat \
        https://github.com/vi/websocat/releases/latest/download/websocat.x86_64-unknown-linux-musl
    chmod +x /usr/local/bin/websocat
fi

echo "==> enabling docker"
systemctl enable --now docker

echo "==> creating throwaway demo user '${DEMO_USER}'"
if ! id -u "${DEMO_USER}" >/dev/null 2>&1; then
    useradd --create-home --shell /bin/bash "${DEMO_USER}"
fi
echo "${DEMO_USER}:${DEMO_PASSWORD}" | chpasswd
# The forced command needs to run docker; ForceCommand prevents any other use.
usermod -aG docker "${DEMO_USER}"

echo "==> building demo container image (yetty-demo:latest)"
docker build -t yetty-demo:latest "${HERE}"

echo "==> installing session launcher"
install -m 0755 "${HERE}/yetty-demo-session.sh" /usr/local/bin/yetty-demo-session

echo "==> configuring sshd (scoped password auth + ForceCommand for ${DEMO_USER})"
sed "s/@DEMO_USER@/${DEMO_USER}/g" "${HERE}/sshd-yetty-demo.conf" \
    > /etc/ssh/sshd_config.d/yetty-demo.conf
sshd -t
systemctl reload ssh 2>/dev/null || systemctl reload sshd

echo "==> installing websocat relay service (ws 127.0.0.1:${WS_PORT} -> tcp 127.0.0.1:22)"
sed "s/@WS_PORT@/${WS_PORT}/g" "${HERE}/yetty-ssh-ws.service" \
    > /etc/systemd/system/yetty-ssh-ws.service
systemctl daemon-reload
systemctl enable --now yetty-ssh-ws

echo "==> installing nginx vhost for ${DOMAIN}"
sed -e "s/@DOMAIN@/${DOMAIN}/g" -e "s/@WS_PORT@/${WS_PORT}/g" \
    "${HERE}/nginx-ssh.conf" > /etc/nginx/sites-available/yetty-ssh
ln -sf /etc/nginx/sites-available/yetty-ssh /etc/nginx/sites-enabled/yetty-ssh
nginx -t
systemctl reload nginx

echo "==> obtaining TLS certificate (Let's Encrypt)"
if certbot --nginx -d "${DOMAIN}" --non-interactive --agree-tos \
        -m "${CERTBOT_EMAIL}" --redirect; then
    echo "    certificate installed"
else
    echo "    certbot failed — check that DNS for ${DOMAIN} points here and"
    echo "    that TCP 80 is open, then re-run: certbot --nginx -d ${DOMAIN} --redirect"
fi

echo
echo "Done."
echo
echo "Point the client at:"
echo "  https://yetty.dev/terminal.html?mode=ssh&url=wss://${DOMAIN}/&user=${DEMO_USER}&password=${DEMO_PASSWORD}"
echo
echo "Watch active sessions:  docker ps --filter label=yetty-demo"
echo "Relay logs:             journalctl -u yetty-ssh-ws -f"
