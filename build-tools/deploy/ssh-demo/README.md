# yetty SSH-over-WebSocket demo (ephemeral container per connection)

Serves a public SSH playground for the [yetty.dev](https://yetty.dev)
showcase. Each visitor lands in a **fresh, disposable Docker container** that
is destroyed the moment they disconnect.

## How it works

The webasm yetty client runs a **full SSH client (libssh2) inside the
browser** — it does the handshake, key exchange, password auth, and shell
request itself (`src/yetty/yssh/ssh-websocket-pty.c`). The WebSocket carries
nothing but the **raw encrypted SSH byte stream** as binary frames
(`src/yetty/ytransport/websocket-transport.c`).

So the server side needs **no SSH awareness** — it is a transparent
WebSocket↔TCP relay in front of a normal `sshd`. Per-connection isolation
comes from sshd itself: the throwaway `yetty` user has a `ForceCommand` that
`exec`s a locked-down `docker run --rm` for every session.

```
Browser (yetty.dev, https)
  │  wss://ssh.yetty.dev/     binary frames = raw SSH wire bytes
  ▼
nginx :443  (TLS termination, Let's Encrypt)              ── on the host
  │  proxy the Upgrade to
  ▼
websocat  ws-l:127.0.0.1:8025  →  tcp:127.0.0.1:22        ── on the host
  ▼
sshd :22   (Match User yetty → ForceCommand)
  ▼
docker run --rm  ← one fresh, sandboxed container per connection
```

One WebSocket → one SSH session → one ephemeral container.

## Files

| File | Purpose |
|---|---|
| `install.sh` | One-shot provisioner for a fresh Ubuntu host |
| `Dockerfile` | The disposable demo image (`yetty-demo:latest`) |
| `motd`, `profile.sh` | Banner, prompt, and idle-timeout inside the container |
| `yetty-demo-session.sh` | sshd `ForceCommand`: spawns the sandboxed container |
| `sshd-yetty-demo.conf` | Scoped sshd config (password auth + ForceCommand for `yetty` only) |
| `yetty-ssh-ws.service` | systemd unit for the websocat relay |
| `nginx-ssh.conf` | TLS reverse-proxy vhost |

The relay itself lives at `tools/ssh-websocket.sh` (sibling of
`telnet-websocket.sh` / `vnc-websocket.sh`) for local/manual use; the systemd
unit calls `websocat` directly.

## Quick start (on the EC2 host)

You need a real domain — Let's Encrypt won't issue for
`*.compute.amazonaws.com`. Point a subdomain (e.g. `ssh.yetty.dev`) at the
instance's public IP, open TCP **80** and **443** in the security group, then:

```sh
# copy this directory to the host and run:
sudo ./install.sh ssh.yetty.dev
```

That installs docker, websocat, nginx and certbot; creates the throwaway
`yetty` user; builds the image; wires up sshd, the relay, the vhost, and the
TLS cert. It prints the exact client URL at the end.

Set a non-default password with `DEMO_PASSWORD=... sudo -E ./install.sh <domain>`.

## Pointing the client at it

The webasm shell reads its session config from query params
(`build-tools/web/terminal.html`):

```
https://yetty.dev/terminal.html?mode=ssh&url=wss://ssh.yetty.dev/&user=yetty&password=yetty
```

- `url=` maps to `--websocket-url` and overrides the default `ws://…:8025`.
  It **must** be `wss://` (yetty.dev is HTTPS; a plain `ws://` is blocked as
  mixed content).
- `user`/`password` are the demo credentials. Only **password auth** works on
  webasm; the host part of `user@host` is informational — the relay owns the
  TCP endpoint.

## Tuning

`yetty-demo-session.sh` reads these env vars (set them in the sshd service
environment, or edit the script):

| Var | Default | Meaning |
|---|---|---|
| `YETTY_DEMO_IMAGE` | `yetty-demo:latest` | container image |
| `YETTY_DEMO_MAX_SESSIONS` | `25` | global concurrency cap |
| `YETTY_DEMO_TIMEOUT` | `1800` | hard per-session wall-clock limit (s) |
| `YETTY_DEMO_NETWORK` | `none` | `none` (isolated) or `bridge` (outbound net) |
| `YETTY_DEMO_MEMORY` | `256m` | per-container memory cap |
| `YETTY_DEMO_CPUS` | `0.5` | per-container CPU cap |
| `YETTY_DEMO_PIDS` | `128` | per-container process cap |

Watch it live:

```sh
docker ps --filter label=yetty-demo     # active sessions
journalctl -u yetty-ssh-ws -f           # relay logs
```

## Security notes

This exposes a shell to the whole internet — treat it as hostile input.

- Each session is a **throwaway container**: non-root user, all caps dropped,
  `no-new-privileges`, read-only root fs, tmpfs home/tmp, no network by
  default, and memory/cpu/pid caps. It is wiped on disconnect.
- The `yetty` user has **no shell escape**: sshd's `ForceCommand` runs the
  launcher no matter what the client asks for, and TCP/agent/X11 forwarding
  are all disabled.
- Password auth is enabled **only for `yetty`** (scoped `Match User` block);
  every other account stays key-only. Keep your admin access on your key.
- Port 22 can stay **closed to the internet** — the browser reaches sshd over
  the loopback relay. Only 80/443 need to be public.
- The demo password travels in the URL query string (browser history). That is
  acceptable for a canned public demo; don't reuse it anywhere.
- The browser client logs the SSH host-key fingerprint but does **not** verify
  it — fine for a disposable demo, not for real access.
- For stronger container isolation, consider a sandboxed runtime
  (gVisor/`runsc` or sysbox) and set it as the docker runtime in
  `yetty-demo-session.sh`.
