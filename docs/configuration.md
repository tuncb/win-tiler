# Configuration

`win-tiler` reads configuration from a TOML file. Use `--config <filepath>` to load a specific file,
or place `win-tiler.toml` next to the executable for runtime commands such as `loop` and
`track-windows`.

Generate a starter file with:

```text
win-tiler init-config [filepath]
```

If `filepath` is omitted, `init-config` writes `win-tiler.toml` next to the executable.

## Loading Rules

Every configuration field is optional. Missing values fall back to built-in defaults.

Keyboard bindings are merged with default bindings when an action is omitted. Set a hotkey to an
empty string to disable that action.

Ignore lists merge with the built-in defaults by default. Set the relevant
`merge_*_with_defaults` flag to `false` to use only the list in the config file.

Invalid numeric values fall back to defaults unless noted otherwise. `layout` ratios are clamped to
the `0.1` to `0.9` range. `visualization.render.zen_percentage` is clamped to the `0.1` to `1.0`
range.

## Sections

| Section | Purpose |
| --- | --- |
| `ignore` | Ignored processes, ignored window titles, ignored process/title pairs, ignored child windows, and the small-window size barrier. |
| `keyboard` | Action-to-hotkey bindings. |
| `gap` | Horizontal and vertical spacing between tiled windows. |
| `loop` | Main loop timing, automatic zen toggling on maximize, and mouse drag/drop behavior. |
| `layout` | Split mode and optional declarative tiling rules selected by managed window count. |
| `visualization` | Toast timing and overlay rendering settings. |
| `monitor_profiles` | Optional per-monitor overrides for gap, layout, and zen mode sizing. |

## Keyboard

Hotkeys join key names with `+`, for example `super+shift+h` or `ctrl+alt+left`. `super` is the
Windows key.

```toml
[keyboard]
bindings = [
  { action = "NavigateLeft", hotkey = "super+shift+h" },
]
```

Supported actions:

```text
NavigateLeft, NavigateDown, NavigateUp, NavigateRight, ToggleSplit, Exit,
CycleSplitMode, StoreCell, ClearStored, Exchange, Move, SplitIncrease,
SplitDecrease, ExchangeSiblings, ToggleZen, ResetSplitRatio, TogglePause,
DumpWindowManagement, RestartSystem, ToggleFloating, ToggleVerboseLogging
```

## Ignore Rules

```toml
[ignore]
merge_processes_with_defaults = true
merge_window_titles_with_defaults = true
merge_process_title_pairs_with_defaults = true
merge_ignore_children_of_processes_with_defaults = true
processes = ["MyPopupApp.exe"]
window_titles = ["Settings"]
process_title_pairs = [{ process = "app.exe", title = "Tool Window" }]
ignore_children_of_processes = ["launcher.exe"]
small_window_barrier = { width = 200, height = 150 }
```

| Option | Meaning |
| --- | --- |
| `merge_*_with_defaults` | `true` merges your list with built-in ignored windows. `false` uses only the list in this file. |
| `processes` | Executable names to ignore. Matching is case-insensitive. |
| `window_titles` | Exact window titles to ignore. |
| `process_title_pairs` | Ignore only when process and title both match. |
| `ignore_children_of_processes` | Ignore child windows spawned by these processes. |
| `small_window_barrier` | Ignore windows at or below this width and height. |

## Gaps

```toml
[gap]
horizontal = 10.0
vertical = 10.0
```

Both values are pixels and must be non-negative.

## Loop

```toml
[loop]
interval_ms = 100
config_refresh_interval_ms = 1000
toggle_zen_on_window_maximize = true
mouse_drag_drop = "exchange"
```

| Option | Meaning |
| --- | --- |
| `interval_ms` | Main loop polling interval in milliseconds. Must be non-negative. |
| `config_refresh_interval_ms` | How often the config file is checked for changes. Must be non-negative. |
| `toggle_zen_on_window_maximize` | `true` toggles zen mode when a tiled window is maximized. |
| `mouse_drag_drop` | Plain drag/drop action. Use `"exchange"` to swap windows or `"split"` to insert as a split. Ctrl+drag/drop or right-button drag/drop performs the other action. |

## Layout

```toml
[layout]
enabled = true
split_mode = "dwindle"
split_width_multiplier = 1.0
rules = []
```

`layout.split_mode` controls how new or moved windows choose the split direction when no
declarative layout rule is applied. Supported values are `dwindle`, `vertical`, and `horizontal`.

`dwindle` splits wide target cells left/right and tall target cells top/bottom. The
`split_width_multiplier` value defaults to `1.0` and is applied to the target cell width before
`dwindle` compares width and height.

Declarative layout rules describe tiling structure, not specific apps. Rules are selected by the
number of managed windows on a monitor. A missing `first` or `second` child means that side is a
window leaf.

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

`vertical` splits left/right and `horizontal` splits top/bottom. The ratio belongs to the first side
of the split, so `ratio = 0.30` gives the first side 30% and the second side 70%. Rules whose tree
leaf count does not match `window_count` are ignored.

Fully explicit rule example:

```toml
[[layout.rules]]
window_count = 3

[layout.rules.tree]
split = "vertical"
ratio = 0.35
first = "window"

[layout.rules.tree.second]
split = "horizontal"
ratio = 0.50
first = "window"
second = "window"
```

## Visualization

```toml
[visualization]
toast_duration_ms = 2000

[visualization.render]
normal_color = [255, 255, 255, 100]
selected_color = [0, 120, 255, 200]
stored_color = [255, 180, 0, 200]
border_width = 3.0
toast_font_size = 60.0
zen_percentage = 0.90
hide_rectangles_when_processes_open = ["ScreenShare.exe"]
```

| Option | Meaning |
| --- | --- |
| `toast_duration_ms` | How long status toast messages stay visible. Must be non-negative. |
| `normal_color` | Overlay rectangle color for normal cells. |
| `selected_color` | Overlay rectangle color for the selected cell. |
| `stored_color` | Overlay rectangle color for the stored cell. |
| `border_width` | Overlay border width in pixels. Must be non-negative. |
| `toast_font_size` | Toast text size. Must be at least `1.0`. |
| `zen_percentage` | Zen cell size from `0.1` to `1.0` of the monitor cluster. |
| `hide_rectangles_when_processes_open` | Executable names that hide overlay rectangles while a visible top-level window from that process exists. Matching is case-insensitive. |

Color values are `[red, green, blue, alpha]`, each in the `0` to `255` range. Alpha controls
opacity: `0` is transparent and `255` is opaque.

## Monitor Profiles

Per-monitor overrides can target a monitor by device name, monitor index, primary status, or a
combination of those fields. Later matching profiles override earlier ones. Any field omitted from a
matching profile falls back to the global configuration.

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

| Option | Meaning |
| --- | --- |
| `name` | Optional label for your own reference. |
| `match.primary` | `true` matches the primary display, `false` matches non-primary displays. |
| `match.index` | Zero-based monitor index from the Windows monitor list. |
| `match.device_name` | Windows monitor device name such as `\\\\.\\DISPLAY1`. |
| `gap` | Optional per-monitor horizontal and/or vertical gap override. |
| `layout` | Optional per-monitor layout override using the same layout fields as the global `layout` section. |
| `visualization.render.zen_percentage` | Optional per-monitor zen size override. |

At least one match field is required for a monitor profile.

## Default Generated Config

This is the uncommented TOML body written by `win-tiler init-config` for the current defaults:

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
  { action = "ToggleVerboseLogging", hotkey = "super+shift+v" },
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
split_mode = "dwindle"
split_width_multiplier = 1.0
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
hide_rectangles_when_processes_open = []
```
