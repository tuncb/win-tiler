# win-tiler

## App Definition

`win-tiler` is a hotkey-driven tiling window manager for Windows. It watches the windows on your desktop, groups them by monitor, and arranges them with a binary space partitioning layout. The main runtime mode is designed for everyday use, and the repository also includes diagnostic commands for inspecting discovered windows, generating a config file, and managing startup registration.

## Feature Summary

- Hotkey-driven tiling for existing Windows application windows.
- Binary space partitioning layout engine for splitting, navigating, moving, and exchanging tiles.
- Multi-monitor support based on monitor work areas, with reinitialization when monitor layouts change.
- Separate tiling state per virtual desktop.
- TOML-based configuration for hotkeys, ignore rules, gaps, loop timing, and visualization settings.
- Zen mode for focusing the selected tile by expanding it within the current monitor cluster.
- Config hot-reload while supported runtime modes are active.
- Ignore filters for processes, window titles, process/title pairs, child windows, and very small windows.
- Overlay and visualization support for understanding the current layout and selection state.
- Diagnostic support for logging discovered windows and inspecting runtime behavior.
- Per-user startup registration through the Windows `Run` registry entry.

## Configuration

`win-tiler` reads configuration from a TOML file. Use `--config <filepath>` to load a specific file, or place `win-tiler.toml` next to the executable for runtime commands such as `loop` and `track-windows`. You can generate a starter file with `win-tiler init-config [filepath]`.

Every configuration field is optional. Missing values fall back to built-in defaults. Keyboard bindings are merged with the default bindings when an action is omitted, and the ignore lists can either merge with or replace the built-in defaults through the `merge_*_with_defaults` flags. Invalid numeric values fall back to defaults; `visualization.render.zen_percentage` is clamped to the `0.1` to `1.0` range.

Top-level sections:

- `ignore`: ignored processes, ignored window titles, ignored process/title pairs, ignored child windows, and the minimum small-window size barrier.
- `keyboard`: maps actions to hotkeys.
- `gap`: horizontal and vertical spacing between tiled windows.
- `loop`: loop timing, automatic zen toggling on maximize, and mouse drag/drop behavior.
- `layout`: optional declarative tiling rules selected by managed window count.
- `visualization`: toast timing and overlay rendering settings, including zen mode sizing.
- `monitor_profiles`: optional per-monitor overrides for gap, layout, and zen mode sizing.

Default config written by `win-tiler init-config`:

```toml
[ignore]
merge_processes_with_defaults = true
merge_window_titles_with_defaults = true
merge_process_title_pairs_with_defaults = true
merge_ignore_children_of_processes_with_defaults = true
processes = [
  "TextInputHost.exe",
  "ApplicationFrameHost.exe",
  "Microsoft.CmdPal.UI.exe",
  "PowerToys.PowerLauncher.exe",
  "win-tiler.exe",
]
window_titles = []
process_title_pairs = [
  { process = "SystemSettings.exe", title = "Settings" },
  { process = "explorer.exe", title = "Program Manager" },
  { process = "explorer.exe", title = "System tray overflow window." },
  { process = "explorer.exe", title = "PopupHost" },
  { process = "claude.exe", title = "Title: Claude" },
  { process = "WidgetBoard.exe", title = "Windows Widgets" },
  { process = "msedgewebview2.exe", title = "MSN" },
]
ignore_children_of_processes = []
small_window_barrier = { width = 200, height = 150 }

[keyboard]
bindings = [
  { action = "NavigateLeft", hotkey = "super+shift+h" },
  { action = "NavigateDown", hotkey = "super+shift+j" },
  { action = "NavigateUp", hotkey = "super+shift+k" },
  { action = "NavigateRight", hotkey = "super+shift+l" },
  { action = "ToggleSplit", hotkey = "super+shift+y" },
  { action = "Exit", hotkey = "super+shift+escape" },
  { action = "CycleSplitMode", hotkey = "super+shift+;" },
  { action = "StoreCell", hotkey = "super+shift+[" },
  { action = "ClearStored", hotkey = "super+shift+]" },
  { action = "Exchange", hotkey = "super+shift+," },
  { action = "Move", hotkey = "super+shift+." },
  { action = "SplitIncrease", hotkey = "super+shift+pageup" },
  { action = "SplitDecrease", hotkey = "super+shift+pagedown" },
  { action = "ExchangeSiblings", hotkey = "super+shift+e" },
  { action = "ToggleZen", hotkey = "super+shift+'" },
  { action = "ResetSplitRatio", hotkey = "super+shift+home" },
  { action = "TogglePause", hotkey = "super+shift+\\" },
  { action = "DumpWindowManagement", hotkey = "super+shift+d" },
  { action = "RestartSystem", hotkey = "super+shift+r" },
  { action = "ToggleFloating", hotkey = "super+shift+f" },
]

[gap]
horizontal = 10.0
vertical = 10.0

[loop]
interval_ms = 100
config_refresh_interval_ms = 1000
toggle_zen_on_window_maximize = true
mouse_drag_drop = "exchange"

[layout]
enabled = true
rules = []

[visualization]
toast_duration_ms = 2000

[visualization.render]
normal_color = [255, 255, 255, 100]
selected_color = [0, 120, 255, 200]
stored_color = [255, 180, 0, 200]
border_width = 3.0
toast_font_size = 60.0
zen_percentage = 0.9
```

Per-monitor tiling overrides can target a monitor by device name, monitor index, primary status, or
a combination of those fields. Later matching profiles override earlier ones. Any field omitted from
a matching profile falls back to the global configuration.

```toml
[[monitor_profiles]]
name = "Laptop"
match = { device_name = "\\\\.\\DISPLAY1" }

[monitor_profiles.gap]
horizontal = 8
vertical = 8

[[monitor_profiles.layout.rules]]
window_count = 2
split = "vertical"
ratio = 0.50

[[monitor_profiles]]
name = "External"
match = { device_name = "\\\\.\\DISPLAY2" }

[monitor_profiles.gap]
horizontal = 16
vertical = 12

[monitor_profiles.visualization.render]
zen_percentage = 0.82

[[monitor_profiles.layout.rules]]
window_count = 3
split = "vertical"
ratio = 0.30
```

Declarative layout rules describe only the tiling structure, not specific apps. Rules are selected
by the number of managed windows on a monitor. A missing `first` or `second` child means that side is
a window leaf.

```toml
[[layout.rules]]
window_count = 2
split = "vertical"
ratio = 0.30

[[layout.rules]]
window_count = 3

[layout.rules.tree]
split = "vertical"
ratio = 0.30

[layout.rules.tree.second]
split = "horizontal"
ratio = 0.50
```

`vertical` splits left/right and `horizontal` splits top/bottom. The ratio belongs to the first
side of the split, so `ratio = 0.30` gives the first side 30% and the second side 70%. Rules whose
tree leaf count does not match `window_count` are ignored.

Mouse-based window drag/drop uses `loop.mouse_drag_drop` for the plain drag action. Set it to
`"exchange"` or `"split"`; Ctrl+drag/drop or right-button drag/drop performs the other action.

## Command Line Arguments

### Syntax

```text
win-tiler [options] [command] [command-args]
```

Global options are parsed before the command, so place `--logmode`, `--log-file`, and `--config` before commands such as `loop` or `startup`.

If no command is supplied, `win-tiler` defaults to `loop`.

### Global Options

| Option | Meaning |
| --- | --- |
| `--help`, `-h` | Print help text and exit immediately. |
| `--version`, `-v` | Print version information and exit immediately. |
| `--monitor-info` | Log monitor handle, device name, full rect, work area, and primary status, then exit immediately. |
| `--logmode <level>` | Set the log level. Valid values are `trace`, `debug`, `info`, `warn`, `err`, and `off`. |
| `--log-file <filepath>` | Write logs to a file instead of stdout. |
| `--config <filepath>` | Load configuration from a TOML file. For runtime commands, `win-tiler` otherwise looks for `win-tiler.toml` next to the executable and uses it if the file exists. When used with `startup enable`, the resolved config path is included in the registered startup command line. |
| `--perf-stats` | In `loop` mode, print periodic stage timing summaries for the active portion of the main loop so before/after optimization runs are easier to compare. |

If `--config` is explicitly provided and the file cannot be loaded, the program exits with an error.

### Commands

| Command | Meaning |
| --- | --- |
| `loop` | Start the main tiling loop. This mode registers hotkeys, tracks monitor and window changes, applies tiling, and renders the overlay. This is the default command. |
| `version` | Print version information. This is the command form of `--version`. |
| `monitor-info` | Log monitor handle, device name, full rect, work area, and primary status, then exit immediately. This is the command form of `--monitor-info`. |
| `track-windows` | Log the windows found on each monitor once per second until the configured exit hotkey is pressed. |
| `init-config [filepath]` | Write a default TOML config file. If `filepath` is omitted, the file is written as `win-tiler.toml` next to the executable. |
| `startup <action>` | Manage startup registration for the current user. Supported actions are `enable`, `disable`, and `status`. |

### `startup` Actions

| Action | Meaning |
| --- | --- |
| `startup enable` | Create or update the current-user startup entry so Windows launches `win-tiler loop` on sign-in. If `--config` or `--log-file` is supplied before the command, the resolved path is added to the stored startup command line. |
| `startup disable` | Remove the current-user startup entry. |
| `startup status` | Print whether startup is enabled and, if available, the exact command line stored in the registry. |

### Examples

```text
win-tiler
win-tiler --logmode debug
win-tiler --log-file C:\work\win-tiler\win-tiler.log
win-tiler --config C:\work\win-tiler\win-tiler.toml loop
win-tiler track-windows
win-tiler init-config
win-tiler init-config C:\work\win-tiler\custom-config.toml
win-tiler --config C:\work\win-tiler\custom-config.toml startup enable
win-tiler startup status
```
