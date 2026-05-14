#!/usr/bin/env bash
set -euo pipefail

: "${WOODPECKER_SERVER:?WOODPECKER_SERVER must be set (e.g. 192.168.1.10:9000)}"
: "${WOODPECKER_AGENT_SECRET:?WOODPECKER_AGENT_SECRET must be set}"

NAME=woodpecker-agent

if docker ps -a --format '{{.Names}}' | grep -qx "$NAME"; then
    docker rm -f "$NAME" >/dev/null
fi

docker run -d --restart always --name "$NAME" \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -v woodpecker-agent-config:/etc/woodpecker \
    -e WOODPECKER_SERVER="$WOODPECKER_SERVER" \
    -e WOODPECKER_AGENT_SECRET="$WOODPECKER_AGENT_SECRET" \
    -e WOODPECKER_BACKEND=docker \
    -e WOODPECKER_MAX_WORKFLOWS="${WOODPECKER_MAX_WORKFLOWS:-1}" \
    woodpeckerci/woodpecker-agent:v3 agent
