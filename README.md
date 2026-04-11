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
- Persistent agent mode over stdio JSON lines for programmatic window inspection and control.
- Per-user startup registration through the Windows `Run` registry entry.

## Configuration

`win-tiler` reads configuration from a TOML file. Use `--config <filepath>` to load a specific file, or place `win-tiler.toml` next to the executable for runtime commands such as `loop` and `track-windows`. You can generate a starter file with `win-tiler init-config [filepath]`.

Every configuration field is optional. Missing values fall back to built-in defaults. Keyboard bindings are merged with the default bindings when an action is omitted, and the ignore lists can either merge with or replace the built-in defaults through the `merge_*_with_defaults` flags. Invalid numeric values fall back to defaults; `visualization.render.zen_percentage` is clamped to the `0.1` to `1.0` range.

Top-level sections:

- `ignore`: ignored processes, ignored window titles, ignored process/title pairs, ignored child windows, and the minimum small-window size barrier.
- `keyboard`: maps actions to hotkeys.
- `gap`: horizontal and vertical spacing between tiled windows.
- `loop`: loop timing and automatic zen toggling on maximize.
- `visualization`: toast timing and overlay rendering settings, including zen mode sizing.

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
]

[gap]
horizontal = 10.0
vertical = 10.0

[loop]
interval_ms = 100
toggle_zen_on_window_maximize = true

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
| `--perf-stats` | In `loop` mode, print periodic stage timing summaries for the active portion of the main loop so before/after optimization runs are easier to compare. |

If `--config` is explicitly provided and the file cannot be loaded, the program exits with an error.

### Commands

| Command | Meaning |
| --- | --- |
| `loop` | Start the main tiling loop. This mode registers hotkeys, tracks monitor and window changes, applies tiling, and renders the overlay. This is the default command. |
| `version` | Print version information. This is the command form of `--version`. |
| `track-windows` | Log the windows found on each monitor once per second until the configured exit hotkey is pressed. |
| `agent [stdio]` | Start persistent agent mode over stdio JSON lines. The current implementation supports `list_windows`, `get_state`, `focus_window`, `send_action`, `swap_windows`, `move_window_to_monitor`, and `retile`. |
| `init-config [filepath]` | Write a default TOML config file. If `filepath` is omitted, the file is written as `win-tiler.toml` next to the executable. |
| `startup <action>` | Manage startup registration for the current user. Supported actions are `enable`, `disable`, and `status`. |

### `startup` Actions

| Action | Meaning |
| --- | --- |
| `startup enable` | Create or update the current-user startup entry so Windows launches `win-tiler loop` on sign-in. If `--config` is supplied before the command, that config path is added to the stored startup command line. |
| `startup disable` | Remove the current-user startup entry. |
| `startup status` | Print whether startup is enabled and, if available, the exact command line stored in the registry. |

### `agent` Mode

`agent stdio` reads one JSON request per input line and writes one JSON response per output line. The default transport is also `stdio`, so `win-tiler agent` and `win-tiler agent stdio` are equivalent.

Window IDs use the `hwnd:0000000000000000` format. Responses currently report the filtered window set produced by the normal runtime ignore rules. In state responses, `actual_rect` reports the live OS window rect when it is available, `layout_rect` reports the engine's computed tile geometry when `include_layout` is enabled, and `rect` is kept as a compatibility field that prefers the live OS rect and only falls back to `layout_rect` if the live rect cannot be queried. `move_window_to_monitor` supports empty target monitors; when the target already has managed windows, `anchor_window_id` can be used to choose the insertion point.

Full manual: [Agent Mode Manual](./docs/agent_mode.md)

Example session:

```text
{"id":"1","command":"list_windows"}
{"id":"2","command":"get_state","include_layout":true}
{"id":"3","command":"focus_window","window_id":"hwnd:000000000012ABCD","select":true}
{"id":"4","command":"send_action","action":"ToggleZen"}
{"id":"5","command":"swap_windows","first_window_id":"hwnd:000000000012ABCD","second_window_id":"hwnd:0000000000456789"}
{"id":"6","command":"move_window_to_monitor","window_id":"hwnd:000000000012ABCD","target_monitor_index":1}
{"id":"7","command":"retile"}
```

### Examples

```text
win-tiler
win-tiler --logmode debug
win-tiler --config C:\work\win-tiler\win-tiler.toml loop
win-tiler track-windows
win-tiler agent stdio
win-tiler init-config
win-tiler init-config C:\work\win-tiler\custom-config.toml
win-tiler --config C:\work\win-tiler\custom-config.toml startup enable
win-tiler startup status
```
