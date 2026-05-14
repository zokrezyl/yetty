## 0. Install build dependencies

One-time, in an *elevated* PowerShell. Installs Chocolatey, cmake / ninja
/ nasm / make / git / git-lfs / python / Strawberry Perl / VS 2022 Build
Tools / meson, and sets `MSYS2_PATH_TYPE=inherit` machine-wide plus
`git config --global safe.directory '*'` — the baseline that GitHub's
`windows-latest` hosted runner provides.

```
Start-Process powershell -Verb RunAs
Set-ExecutionPolicy -Scope Process Bypass -Force
.\install-dependencies.ps1
```

Idempotent — re-running skips packages already installed.

## 1. Install winsw

```
winget install WinsW

```

1. copy config to C:\Services\woodpecker-agent\woodpecker-agent.xml
2. install the service

```
cd C:\Services\woodpecker-agent
.\woodpecker-agent.exe install
```


3.  start the service
```
.\woodpecker-agent.exe start
```

