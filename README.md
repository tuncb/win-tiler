# win-tiler

## App Definition

`win-tiler` is a hotkey-driven tiling window manager for Windows. It watches the windows on your desktop, groups them by monitor, and arranges them with a binary space partitioning layout. The main runtime mode is designed for everyday use, and the repository also includes UI and diagnostic commands for testing layouts, inspecting discovered windows, generating a config file, and managing startup registration.

## Feature Summary

- Hotkey-driven tiling for existing Windows application windows.
- Binary space partitioning layout engine for splitting, navigating, moving, and exchanging tiles.
- Multi-monitor support based on monitor work areas, with reinitialization when monitor layouts change.
- Separate tiling state per virtual desktop.
- TOML-based configuration for hotkeys, ignore rules, gaps, loop timing, and visualization settings.
- Config hot-reload while supported runtime modes are active.
- Ignore filters for processes, window titles, process/title pairs, child windows, and very small windows.
- Overlay and visualization support for understanding the current layout and selection state.
- Diagnostic commands for live monitor visualization, synthetic cluster visualization, and window tracking.
- Per-user startup registration through the Windows `Run` registry entry.

## Command Line Arguments

### Syntax

```text
win-tiler [options] [command] [command-args]
```

Global options are parsed before the command, so place `--logmode` and `--config` before commands such as `loop` or `startup`.

If no command is supplied, `win-tiler` defaults to `loop`.

### Global Options

| Option | Meaning |
| --- | --- |
| `--help`, `-h` | Print help text and exit immediately. |
| `--version`, `-v` | Print version information and exit immediately. |
| `--logmode <level>` | Set the log level. Valid values are `trace`, `debug`, `info`, `warn`, `err`, and `off`. |
| `--config <filepath>` | Load configuration from a TOML file. For runtime commands, `win-tiler` otherwise looks for `win-tiler.toml` next to the executable and uses it if the file exists. When used with `startup enable`, the resolved config path is included in the registered startup command line. |

If `--config` is explicitly provided and the file cannot be loaded, the program exits with an error.

### Commands

| Command | Meaning |
| --- | --- |
| `loop` | Start the main tiling loop. This mode registers hotkeys, tracks monitor and window changes, applies tiling, and renders the overlay. This is the default command. |
| `version` | Print version information. This is the command form of `--version`. |
| `ui-test-monitor` | Launch the UI visualizer using the current monitor work areas and the windows discovered on each monitor. |
| `ui-test-multi [x y width height]...` | Launch the UI visualizer with custom cluster rectangles. Arguments must be passed in groups of four numbers. If no cluster definitions are provided, the command uses two default `1920x1080` clusters side by side. |
| `track-windows` | Log the windows found on each monitor once per second until the configured exit hotkey is pressed. |
| `init-config [filepath]` | Write a default TOML config file. If `filepath` is omitted, the file is written as `win-tiler.toml` next to the executable. |
| `startup <action>` | Manage startup registration for the current user. Supported actions are `enable`, `disable`, and `status`. |

### `startup` Actions

| Action | Meaning |
| --- | --- |
| `startup enable` | Create or update the current-user startup entry so Windows launches `win-tiler loop` on sign-in. If `--config` is supplied before the command, that config path is added to the stored startup command line. |
| `startup disable` | Remove the current-user startup entry. |
| `startup status` | Print whether startup is enabled and, if available, the exact command line stored in the registry. |

### Examples

```text
win-tiler
win-tiler --logmode debug
win-tiler --config C:\work\win-tiler\win-tiler.toml loop
win-tiler ui-test-monitor
win-tiler ui-test-multi 0 0 1920 1080 1920 0 1920 1080
win-tiler track-windows
win-tiler init-config
win-tiler init-config C:\work\win-tiler\custom-config.toml
win-tiler --config C:\work\win-tiler\custom-config.toml startup enable
win-tiler startup status
```
