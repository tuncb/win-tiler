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
movement_mode = "swap"
bindings = [
  { action = "NavigateLeft", hotkey = "super+shift+h" },
]
```

Supported actions:

```text
NavigateLeft, NavigateDown, NavigateUp, NavigateRight, ToggleSplit, Exit,
CycleSplitMode, MoveLeft, MoveDown, MoveUp, MoveRight, ToggleMovementMode, SplitIncrease,
SplitDecrease, ExchangeSiblings, ToggleZen, ResetSplitRatio, TogglePause,
DumpWindowManagement, RestartSystem, ToggleFloating, ToggleVerboseLogging
```

`keyboard.movement_mode` sets the initial mode: `"swap"` (default) exchanges the selected
window with its directional neighbor; `"insert"` removes it from its old position and splits
the neighbor's space, placing it on the requested side. Invalid values fall back to `"swap"`.

`MoveLeft`, `MoveDown`, `MoveUp`, and `MoveRight` default to `Win+Alt+Shift+H/J/K/L`.
`ToggleMovementMode` defaults to `Win+Alt+Shift+,`; a toast and the tray menu show the current mode.
The toggle is session-only and tracked per virtual desktop. A changed configured mode takes
effect on the desktop's next frame; unrelated config reloads preserve the toggled mode.

Insertion orientation follows the direction: left/right means side by side, up/down means
stacked. This operation overrides `layout.split_mode` and does not reapply layout templates.
Both modes retain selection/focus on the moved window and can target windows on other monitors.
No neighbor means no movement; empty monitors are not directional targets.

The former `StoreCell`, `ClearStored`, `Exchange`, and `Move` bindings are no longer supported.
The former `visualization.render.stored_color` setting has also been removed.

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
split_target = "pointer"
split_width_multiplier = 1.0
rules = []
```

`layout.split_mode` controls the split direction for new windows and mouse insertions when no
declarative layout rule is applied. Supported values are `dwindle`, `vertical`, and `horizontal`.
Directional keyboard insertion instead uses the direction pressed.

`dwindle` splits wide target cells left/right and tall target cells top/bottom. The
`split_width_multiplier` value defaults to `1.0` and is applied to the target cell width before
`dwindle` compares width and height.

`layout.split_target` chooses the cell for automatic new-window insertion:

- `pointer` (default): use the monitor and cell under the pointer, preserving existing behavior.
- `focused`: split the last focused tiled window, independently of the pointer.
- `largest`: split the cell with the largest area on the last focused tiled window's monitor.

For example, set `split_target = "largest"` with `split_mode = "dwindle"` for automatic
placement into large cells with an aspect-based split direction. `vertical` and `horizontal`
also work with any target policy.

Focused and largest placement remember tiled focus when a new window or an untiled window
takes OS focus. If no tiled focus is known, windows keep their incoming monitor; focused
placement falls back to the first leaf there. Hovering does not affect either policy.
With monitor profiles, the focused monitor's target policy determines whether placement follows
focus or the pointer; without known focus, the incoming monitor's policy is used.

Largest uses the allocated cell area before gaps or zen expansion, including manually adjusted
split ratios. Equal areas use first-child tree traversal order. Each new split is 50/50, and
the largest cell is recalculated after each insertion, including startup and batches.
Other cells retain their geometry. Explicit moves and drag/drop keep their chosen target,
and matching declarative layout rules take precedence over automatic target selection.

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
show_rectangles = true
show_only_active_window = false
normal_color = [255, 255, 255, 100]
selected_color = [0, 120, 255, 200]
border_width = 3.0
toast_font_size = 60.0
zen_percentage = 0.90
hide_rectangles_when_processes_open = ["ScreenShare.exe"]
```

| Option | Meaning |
| --- | --- |
| `toast_duration_ms` | How long status toast messages stay visible. Must be non-negative. |
| `show_rectangles` | Whether overlay rectangles are visible. Defaults to `true`; toast messages are unaffected. |
| `show_only_active_window` | Draw only the foreground tiled window using `selected_color`. Defaults to `false`. |
| `normal_color` | Overlay rectangle color for normal cells. |
| `selected_color` | Overlay rectangle color for the selected cell. |
| `border_width` | Overlay border width in pixels. Must be non-negative; `0` hides rectangles even when `show_rectangles = true`. |
| `toast_font_size` | Toast text size. Must be at least `1.0`. |
| `zen_percentage` | Zen cell size from `0.1` to `1.0` of the monitor cluster. |
| `hide_rectangles_when_processes_open` | Executable names that hide overlay rectangles while a visible top-level window from that process exists. Matching is case-insensitive. |

Color values are `[red, green, blue, alpha]`, each in the `0` to `255` range. Alpha controls
opacity: `0` is transparent and `255` is opaque.

To hide all overlay rectangles, set `show_rectangles = false` in `[visualization.render]`.
Setting `border_width = 0` also hides them. To show rectangles again, use
`show_rectangles = true` with a positive `border_width`. These settings apply to normal,
selected, stored, and zen cells and preserve toast messages. Process-based suppression
still hides rectangles when a matching window is open.

To draw a rectangle only around the active window, set `show_only_active_window = true`
with `show_rectangles = true`. This follows Windows focus, including zen windows.
Moving the mouse over a tiled window focuses it, regardless of this setting. A stationary
pointer does not override keyboard focus changes such as Alt+Tab. Hover focus pauses while
an untiled dialog is active, until it closes or you explicitly switch away (for example,
by clicking another window or using Alt+Tab). Hovering a tiled window disabled by a modal
dialog brings its visible blocking dialog forward instead. Standard dialogs and custom
dialogs with disabled owners are recognized. No rectangle is drawn
when the foreground window is floating, ignored, or otherwise outside the tiled layout.
Fullscreen and process-based suppression still apply; toast messages remain visible.

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
movement_mode = "swap"
bindings = [
  { action = "NavigateLeft", hotkey = "super+shift+h" },
  { action = "NavigateDown", hotkey = "super+shift+j" },
  { action = "NavigateUp", hotkey = "super+shift+k" },
  { action = "NavigateRight", hotkey = "super+shift+l" },
  { action = "ToggleSplit", hotkey = "super+shift+y" },
  { action = "Exit", hotkey = "super+shift+escape" },
  { action = "CycleSplitMode", hotkey = "super+shift+;" },
  { action = "MoveLeft", hotkey = "super+alt+shift+h" },
  { action = "MoveDown", hotkey = "super+alt+shift+j" },
  { action = "MoveUp", hotkey = "super+alt+shift+k" },
  { action = "MoveRight", hotkey = "super+alt+shift+l" },
  { action = "ToggleMovementMode", hotkey = "super+alt+shift+," },
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
split_target = "pointer"
split_width_multiplier = 1.0
rules = []

[visualization]
toast_duration_ms = 2000

[visualization.render]
show_rectangles = true
show_only_active_window = false
normal_color = [255, 255, 255, 100]
selected_color = [0, 120, 255, 200]
border_width = 3.0
toast_font_size = 60.0
zen_percentage = 0.9
hide_rectangles_when_processes_open = []
```
