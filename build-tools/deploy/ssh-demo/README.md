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
| `install.sh` | One-shot provisioner for a fresh Ubuntu host (runs **on** the host, as root) |
| `remote-install.sh` | Run from your workstation: copies this dir to the host and runs `install.sh` there over ssh |
| `Dockerfile` | The disposable demo image (`yetty-demo:latest`) — a stable thin runtime, no yetty baked in |
| `update-yetty.sh` | Downloads the latest yetty release — binaries **and** source tree — to the host dirs the container mounts (no image rebuild) |
| `remote-update-yetty.sh` | Run from your workstation: copies `update-yetty.sh` to the host and runs it there over ssh |
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

### From your workstation (over ssh)

You don't have to copy files up by hand or ssh in yourself — the two
`remote-*` wrappers do it for you. They run **locally** (no root on your
machine), `scp` the scripts to the host, and run them there under `sudo`:

```sh
# full provision — copies this whole dir to ubuntu@ws.yetty.dev and runs install.sh
./remote-install.sh

# refresh the mounted yetty (binaries + sources) to the latest release (no image rebuild)
./remote-update-yetty.sh
# ...or pin a tag:
./remote-update-yetty.sh yetty-0.2.71
```

Both default the ssh target to `ubuntu@ws.yetty.dev`; override with
`YETTY_DEMO_SSH=ubuntu@<host> ./remote-install.sh`. `remote-install.sh` derives
the TLS domain from the host part of the ssh target (override it with a
positional arg) and forwards `DEMO_PASSWORD` / `CERTBOT_EMAIL` / `DEMO_USER` /
`WS_PORT` if you export them.

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
| `YETTY_DEMO_PREFIX` | `/var/lib/yetty-demo/prefix` | host yetty install; its `bin`, `share/yetty`, `etc/xdg/yetty` are mounted read-only into `/usr/local/…` |
| `YETTY_DEMO_SOURCES` | `/var/lib/yetty-demo/sources` | host source tree, bind-mounted read-only at `/usr/share/yetty/sources` |

Watch it live:

```sh
docker ps --filter label=yetty-demo     # active sessions
journalctl -u yetty-ssh-ws -f           # relay logs
```

## yetty is mounted, not baked

**Nothing yetty-specific is baked into the image.** The image
(`yetty-demo:latest`) is a stable thin runtime: base OS + the shared libraries
the tools link against + a few terminal toys + the demo user. Everything yetty
— the binaries and their data/config, and the browsable source tree — lives in
host directories that each session bind-mounts **read-only**:

| Host dir (default) | Mounted read-only at | Holds |
|---|---|---|
| `/var/lib/yetty-demo/prefix/bin` | `/usr/local/bin` | the companion tools (`ycat`, `yplot`, …) |
| `/var/lib/yetty-demo/prefix/share/yetty` | `/usr/local/share/yetty` | shaders, fonts, demos |
| `/var/lib/yetty-demo/prefix/etc/xdg/yetty` | `/usr/local/etc/xdg/yetty` | config |
| `/var/lib/yetty-demo/sources` | `/usr/share/yetty/sources` | the browsable source tree |

The payoff: shipping a newer yetty **never rebuilds the image**. You refresh the
host directories in place and the next session picks them up. Because the image
no longer carries the ~1.4 GB yetty payload it is small and rarely changes.

Refresh with the updater the provisioner installs:

```sh
# pull the newest desktop release — binaries AND sources — in place (no rebuild)
sudo yetty-demo-update-yetty

# or pin a specific tag
sudo yetty-demo-update-yetty yetty-0.2.71
```

`update-yetty.sh` resolves "latest" the same way `https://yetty.dev/install.sh`
does — the repo publishes several release families (`yetty-*`, `yos-web-*`,
`yetty-rootfs-riscv-*`) and the repo-wide "latest release" is whichever
published most recently, so it lists releases and picks the highest
`yetty-X.Y.Z` rather than trusting the pointer. For that one tag it runs the
canonical installer into a host staging prefix (dropping the ~470 MB RISC-V VM
runtime) and unpacks the matching source archive, then swaps both into place
with same-filesystem renames. Binaries and sources are always the **same tag**,
so they never drift. A marker file in each tree skips the download when already
current, so it is cheap to run from cron or a systemd timer.

The **only** time you rebuild the image is to change the runtime itself (bump
the base OS, add a shared library, add a terminal toy) — not to ship new yetty.

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
