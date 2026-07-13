# yconfig — YAML config + command line behind one path-keyed tree

`yconfig` merges the platform config file, environment, and the full yetty
command line into a single tree of nodes, exposed through an ops vtable with
slash-separated paths (`"rendering/show-fps"`, `"rpc/port"`). One instance is
created during bootstrap by the per-OS `yplatform/yplatform/*.c` runtime and
travels down the context chain; consumers only see
`struct yetty_yconfig_config`. Parsing uses libyaml; directory resolution
comes from the [yplatform](../yplatform/README.md) paths abstraction.

## Load order (later wins)

1. **Config file** — first hit of: `-c/--config <file>` →
   `$XDG_CONFIG_HOME/yetty/config.yaml` → `~/.config/yetty/config.yaml` →
   `<platform config dir>/config.yaml` (webasm `/config`, Android app files).
2. **Platform paths** — resolved via `yetty_yplatform_paths_create()`, stored
   under `paths/*` in the tree, and exported as `YETTY_*_DIR` env vars
   *before* file loading so YAML values can reference them.
3. **`$SHELL`** — folded into `shell/default`.
4. **Command line** — `parse_cmdline` maps every flag onto a tree path, so
   CLI always overrides YAML: `-r <port>` → `rpc/port`, `-e <cmd>` →
   `shell/command`, `--ssh` / `--telnet` / `--websocket` / `--temu` /
   `--qemu` / `--wsl` (mutually exclusive session modes), `--log-floor` /
   `--log-all` / `--log-level` / … → the `log/*` subtree.

After the merge, `apply_ytrace_config` pushes any present `log/*` keys into
ytrace (see [ytrace](../ytrace/README.md)) — absent keys leave ytrace's own
defaults and `YTRACE_*` env handling untouched.

A YAML `import: other.yaml` key (scalar or sequence) splices another file
into the current node, resolved relative to the importing file's directory.
Sequences are stored as children with numeric keys (`"0"`, `"1"`, …); scalar
nodes cache an int/bool interpretation at load time.

## Public API sketch

```c
struct yetty_yconfig_result cfg_res = yetty_yconfig_create(argc, argv);
struct yetty_yconfig_config *cfg = cfg_res.value;

int port        = cfg->ops->get_int(cfg, YETTY_YCONFIG_KEY_RPC_PORT, 0);
const char *fam = cfg->ops->get_string(cfg, YETTY_YCONFIG_KEY_FONT_FAMILY, "monospace");
int n           = cfg->ops->get_array_count(cfg, "plugins/path");
struct yetty_yconfig_config *node = cfg->ops->get_node(cfg, "vnc");

struct yetty_yconfig_shell_argv shell;         /* execvp-ready argv */
cfg->ops->get_shell_argv(cfg, &shell);         /* -e cmd, else $SHELL > shell/default > /bin/bash */
```

`get_node` returns a lightweight sub-config view (same ops surface, rooted at
the child), so subsystems can be handed just their subtree. The well-known
key catalog lives as `YETTY_YCONFIG_KEY_*` macros in
`include/yetty/yconfig/config.h` — scrollback tier budgets, GPU device
limits, RPC host/port, session-mode endpoints, log controls.

## File map

| file | role |
|------|------|
| `config.c` | node tree, libyaml loader (+`import`), CLI parser, shell-argv resolution, ytrace push, sub-config views |

There is no per-module CMakeLists: `config.c` is compiled directly into the
application source set (`YETTY_SOURCES` in `src/yetty/CMakeLists.txt`), the
public header is `include/yetty/yconfig/config.h`.

## Consumers

- **Bootstrap** — `yplatform/yplatform/{glfw,android,ios-tvos,webasm}.c`
  create the config and stash it in the yinit runtime
  ([contexts.md](../../../docs/contexts.md)).
- **yframework** — `rpc/host` + `rpc/port` gate the
  [yctl](../yctl/README.md) server; VNC/recording keys drive
  [yvnc](../yvnc/README.md).
- **ywebgpu** — `gpu/*` requested device limits (clamped to the adapter).
- **yterminal / yvterm** — `scrollback/*` tier budgets, font render method
  ([yvterm](../yvterm/README.md)).
