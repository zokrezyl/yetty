# runtime/ — server-mode state

Holds the runit service tree and the log catch-all that
`./tools/yos.sh --server` uses. Everything under `runit/` is
checked in (the service scripts ARE the configuration); everything
under `logs/` is generated at runtime and gitignored.

## Layout

    runtime/
      runit/                runsvdir scans this on --server
        <svc>/run           service script (foreground; exec the program)
        <svc>/log/run       (optional) logger child — typically
                            `exec svlogd $LOG_DIR/<svc>`
        <svc>/supervise/    runsv's per-service control state (gitignored)
      logs/                 default --log-dir target
        yos-server.log        catch-all (only written when --daemon)
        yos-server.pid        daemon PID
        <svc>/current         per-service svlogd output
        <svc>/@<tai64n>.s     rotated previous logs

## Quick start

Foreground:

    ./tools/yos.sh --server                  # catch-all to terminal
    ./tools/yos.sh --server --log-dir /tmp/yoslogs

Daemonized:

    ./tools/yos.sh --server --daemon
    tail -F runtime/logs/yos-server.log
    kill $(cat runtime/logs/yos-server.pid)

Add a service:

    mkdir -p runtime/runit/myservice/log
    cat > runtime/runit/myservice/run <<'EOF'
    #!/bin/sh
    exec 2>&1
    exec /libexec/telnetd 12323
    EOF
    cat > runtime/runit/myservice/log/run <<'EOF'
    #!/bin/sh
    mkdir -p "$LOG_DIR/myservice"
    exec svlogd -tt "$LOG_DIR/myservice"
    EOF
    chmod +x runtime/runit/myservice/run runtime/runit/myservice/log/run

runsvdir picks the new dir up within 5 seconds (its rescan interval).
No restart of the server needed.

Control individual services:

    sv status myservice      # one-shot status
    sv up myservice          # start (if stopped)
    sv down myservice        # stop
    sv restart myservice
