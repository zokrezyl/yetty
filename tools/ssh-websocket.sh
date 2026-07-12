#!/bin/bash
# WebSocket proxy for an SSH daemon.
# Bridges WebSocket connections from the browser to the sshd TCP socket.
#
# The webasm yetty client runs a full SSH client (libssh2) in the browser and
# terminates the crypto there; this proxy only relays the *encrypted* SSH byte
# stream between the browser WebSocket and sshd's TCP port. It needs no SSH
# awareness at all — it is a transparent WebSocket <-> TCP relay, exactly like
# telnet-websocket.sh and vnc-websocket.sh.
#
# For the public yetty.dev showcase this listens on loopback only and TLS is
# terminated by nginx in front (wss://) — see build-tools/deploy/ssh-demo/.

set -e

WS_PORT="${WS_PORT:-8025}"
SSH_PORT="${SSH_PORT:-22}"
SSH_HOST="${SSH_HOST:-127.0.0.1}"
WS_BIND="${WS_BIND:-127.0.0.1}"

usage() {
    echo "Usage: $0 [HOST:PORT] [WS_PORT]"
    echo "       $0 [options]"
    echo ""
    echo "Sets up a WebSocket-to-TCP proxy for sshd."
    echo "The browser connects via WebSocket, this proxy forwards the"
    echo "encrypted SSH stream to the sshd TCP socket."
    echo ""
    echo "Positional arguments:"
    echo "  HOST:PORT    sshd address (default: 127.0.0.1:22)"
    echo "  WS_PORT      WebSocket listen port (default: 8025)"
    echo ""
    echo "Options:"
    echo "  -w, --ws-port PORT   WebSocket listen port (default: 8025)"
    echo "  -s, --ssh-port PORT  sshd TCP port (default: 22)"
    echo "  -H, --ssh-host HOST  sshd host (default: 127.0.0.1)"
    echo "  -b, --ws-bind ADDR   WebSocket bind address (default: 127.0.0.1)"
    echo "  -h, --help           Show this help"
    echo ""
    echo "Environment variables:"
    echo "  WS_PORT   WebSocket listen port"
    echo "  SSH_PORT  sshd TCP port"
    echo "  SSH_HOST  sshd host"
    echo "  WS_BIND   WebSocket bind address"
    echo ""
    echo "Examples:"
    echo "  # Default: proxy localhost:22 -> ws://127.0.0.1:8025"
    echo "  $0"
    echo ""
    echo "  # Bind the WebSocket on all interfaces (no TLS — dev only)"
    echo "  $0 --ws-bind 0.0.0.0"
    echo ""
    echo "  # Proxy to a remote sshd on a custom port"
    echo "  $0 192.168.1.100:2222 8080"
    exit 0
}

# Parse arguments - support both positional and named options.
# Positional: [HOST:PORT] [WS_PORT]
if [[ $# -gt 0 && ! "$1" =~ ^- ]]; then
    if [[ "$1" =~ : ]]; then
        SSH_HOST="${1%:*}"
        SSH_PORT="${1#*:}"
    else
        SSH_HOST="$1"
    fi
    shift
    if [[ $# -gt 0 && ! "$1" =~ ^- ]]; then
        WS_PORT="$1"
        shift
    fi
fi

while [[ $# -gt 0 ]]; do
    case $1 in
        -w|--ws-port)
            WS_PORT="$2"
            shift 2
            ;;
        -s|--ssh-port)
            SSH_PORT="$2"
            shift 2
            ;;
        -H|--ssh-host)
            SSH_HOST="$2"
            shift 2
            ;;
        -b|--ws-bind)
            WS_BIND="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

echo "SSH WebSocket Proxy"
echo "==================="
echo "WebSocket: ws://$WS_BIND:$WS_PORT"
echo "SSH Server: $SSH_HOST:$SSH_PORT"
echo ""

# Prefer websocat (lightweight, static Rust binary).
if command -v websocat &> /dev/null; then
    echo "Using websocat..."
    exec websocat --binary "ws-l:$WS_BIND:$WS_PORT" "tcp:$SSH_HOST:$SSH_PORT"
fi

# Fall back to websockify (Python, ships with noVNC).
if command -v websockify &> /dev/null; then
    echo "Using websockify..."
    exec websockify "$WS_BIND:$WS_PORT" "$SSH_HOST:$SSH_PORT"
fi

echo "Error: No WebSocket proxy tool found!"
echo ""
echo "Please install one of:"
echo "  - websocat:   https://github.com/vi/websocat/releases (static binary)"
echo "  - websockify: pip install websockify"
exit 1
