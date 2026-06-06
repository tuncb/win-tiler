# Save Layout Feature Spec

## Goal

Add a notification-area context menu action that saves the current tiling layout for one monitor or
all monitors into the active TOML configuration file as per-monitor layout rules.

Saving a layout records only the binary split structure and split ratios for the number of managed
windows currently tiled on the selected monitor. It does not record window handles, process names,
titles, positions, gaps, hotkeys, or any other settings.

## Existing App Context

- Runtime state is monitor-cluster based. `winapi::LoopInputState.monitors` and
  `windows_per_monitor` are gathered in `src/winapi.*`.
- The engine owns the live tiling tree in `Engine::system.clusters`. Each cluster has a
  `BinaryTree<ctrl::CellData>` where split nodes store `split_dir` and `split_ratio`, and leaves
  store the window-backed `leaf_id`.
- Declarative layout rules already exist as `LayoutRule` and `LayoutTreeNode` in `src/options.h`.
  Rules are selected by `window_count`.
- Per-monitor configuration already exists through `[[monitor_profiles]]`. A matching monitor
  profile can override `layout`, and later matching profiles override earlier profiles.
- `write_options_toml()` currently writes a complete normalized configuration document. This is not
  suitable for this feature because the requirement is to update only the relevant rules and leave
  unrelated options and unrelated rules unchanged.

## User Experience

The existing notification-area context menu gains a parent item:

```text
Save layout
  <monitor label 1>
  <monitor label 2>
  ...
  Save All
```

Monitor items are sorted from left to right by full monitor rectangle `rect.left`. Ties should be
broken by `rect.top`, then by original Windows monitor index. The command must still carry the
original monitor index, because that index maps to engine clusters and `windows_per_monitor`.

Each monitor label should include enough identifying information to avoid ambiguity:

```text
DISPLAY2 - 2560x1440 at 1920,0 - 3 windows
```

If available, include primary status:

```text
DISPLAY1 - Primary - 1920x1080 at 0,0 - 2 windows
```

If no configuration file is active, the entire `Save layout` parent item is disabled. "Active"
means the loop was started with a resolved config path in `LoopRunOptions.config_path`; the app
should not create a new config file from this menu action.

If a config path is active but the file cannot currently be read or written, keep the menu enabled
and show/log a failure when the user invokes the command.

## Save Semantics

For a selected monitor:

1. Resolve the monitor by original monitor index.
2. Count managed tiled windows for that monitor from the current loop input or from the live cluster
   leaves. The saved `window_count` must match the number of leaves with `leaf_id`.
3. Build a `LayoutRule` from `Engine::system.clusters[index].tree`.
4. Update only that monitor profile's `layout.rules` entry with the same `window_count`.
5. Preserve every unrelated config option, comment, ordering choice, and rule for other
   `window_count` values as much as practical.

For `Save All`, repeat the same operation for every monitor that has at least two managed tiled
windows. Save All should be one logical file update: either all selected monitor rules are written,
or none are written.

Monitors with zero or one tiled managed window should not create a layout rule because the current
layout rule format requires a split tree with two leaves or more. For a single-monitor save, show a
toast such as `Need at least 2 tiled windows`. For Save All, skip those monitors and include the
saved count in the result toast.

Fullscreen and zen state are not saved. The tree topology saved should be the normal cluster tree;
if a cluster is fullscreen or zen, the layout tree is still read from the underlying cluster.

## Monitor Profile Targeting

Saved rules should be written under `[[monitor_profiles]]`, not under global `[layout]`, because the
feature is explicitly monitor-specific.

Profile matching policy:

1. Prefer an existing monitor profile whose `match.device_name` equals the monitor's `deviceName`.
2. If none exists, prefer an existing profile whose match criteria uniquely match the monitor among
   the current monitor set.
3. If none exists, append a new monitor profile:

```toml
[[monitor_profiles]]
name = "DISPLAY2"
match = { device_name = "\\\\.\\DISPLAY2" }

[[monitor_profiles.layout.rules]]
window_count = 3
...
```

New profiles should match by `device_name` because it is more stable than the current enumeration
index. Do not create index-only profiles unless the device name is empty.

If multiple existing profiles match the same monitor and would affect layout, update the last
matching profile because runtime resolution applies matching profiles in file order and later
profiles override earlier ones.

## Rule Serialization

Convert the current cluster tree to `LayoutTreeNode` recursively:

- Root is node `0`.
- A leaf node becomes a `"window"` child in TOML and does not include the leaf ID.
- A split node becomes a table with:
  - `split = "vertical"` for left/right splits.
  - `split = "horizontal"` for top/bottom splits.
  - `ratio = <split_ratio>`.
  - `first` and `second`, each either `"window"` or a nested split table.

The generated rule must satisfy `count_layout_windows(rule.tree) == window_count`.

Use the existing supported TOML shape. For a nested rule:

```toml
[[monitor_profiles.layout.rules]]
window_count = 3

[monitor_profiles.layout.rules.tree]
split = "vertical"
ratio = 0.35
first = "window"

[monitor_profiles.layout.rules.tree.second]
split = "horizontal"
ratio = 0.50
first = "window"
second = "window"
```

For a two-window rule, the compact shape is acceptable:

```toml
[[monitor_profiles.layout.rules]]
window_count = 2
split = "vertical"
ratio = 0.50
```

## Config File Update Requirements

Implement a targeted TOML update helper instead of calling `write_options_toml()` for the active
config file.

The helper should:

- Read and parse the active TOML file.
- Locate or create the target `monitor_profiles` array item.
- In that profile's `layout.rules`, replace only the rule with the saved `window_count`.
- Preserve rules for other window counts.
- Preserve global `[layout]` rules.
- Preserve other monitor profiles and their rules.
- Preserve unrelated options.
- Write via a temporary file and atomic replace where practical.
- Return a structured success/failure result for toast/log output.

Because `toml++` does not preserve formatting and comments when re-emitting a whole document, the
implementation should avoid full-document normalization. If a fully comment-preserving edit proves
too costly, the acceptable fallback is to surgically replace only the relevant `[[...layout.rules]]`
block and append new blocks at the end of the target profile. The fallback must not rewrite the
entire configuration.

After a successful write, update the in-memory `GlobalOptionsProvider` state immediately or force a
config refresh so the saved rule participates in subsequent tiling without waiting for the refresh
interval.

## Runtime Architecture

Keep with the existing loop/engine split:

- `src/winapi.*`
  - Builds the menu.
  - Stores a pending `SaveLayoutRequest` from the selected menu item.
  - Exposes `consume_notification_area_save_layout_request()`.
  - Does not inspect or mutate engine state.
- `src/loop.cpp`
  - Consumes the pending save request in the main loop after input state and current desktop engine
    are available.
  - Builds `SavedLayoutRule` values from `engine.system`.
  - Calls the config update helper.
  - Shows a toast and logs the result.
- `src/engine.*`
  - May expose a pure helper to convert a cluster tree to `LayoutRule`.
  - Should not perform file I/O.
- `src/options.*` or a new small config-edit module
  - Owns TOML persistence for saved monitor layout rules.

Do not add direct `engine.system` mutations or `ctrl::*` mutator calls to `src/loop.cpp`. Reading the
current tree to serialize a rule is acceptable.

## Menu Command IDs

The existing menu uses fixed command IDs in `src/winapi.cpp`. Reserve a command ID range for monitor
save commands, for example:

```cpp
constexpr UINT ID_SAVE_LAYOUT_MONITOR_BASE = 1100;
constexpr UINT ID_SAVE_LAYOUT_MONITOR_MAX = 1199;
constexpr UINT ID_SAVE_LAYOUT_ALL = 1200;
```

When building the menu, map sorted menu rows back to original monitor indices. If there are more
monitors than the reserved range supports, show the first supported set and log a warning.

## Error Handling And Toasts

Recommended toast messages:

- `Saved layout for DISPLAY2`
- `Saved layouts for 2 monitors`
- `No config file active`
- `Need at least 2 tiled windows`
- `Failed to save layout`

Detailed errors should be logged with the config path, monitor identity, window count, and parse or
I/O failure.

## Test Plan

Add unit tests because this changes behavior.

Recommended coverage:

- Converts a live cluster tree to a `LayoutRule` with the expected split directions, ratios, and
  window count.
- Replaces only the selected monitor profile rule for the current `window_count`.
- Preserves other rules for the same monitor with different `window_count`.
- Preserves other monitor profiles and global `[layout]` rules.
- Appends a new device-name monitor profile when no existing profile matches.
- Updates the last matching profile when multiple profiles match the monitor.
- Notification menu availability disables Save layout when no config path is active.
- Monitor menu ordering is left-to-right while preserving original monitor index in the request.
- Save All skips monitors with fewer than two tiled windows and commits the remaining monitor rules.

Verification commands:

```powershell
.\build-run.bat build-run --Test-Debug
.\build-run.bat build --Debug
```

Both commands should complete without warnings.

## Open Questions

- Should the action be available while manually paused? The current menu can still request actions
  while paused. Recommended behavior: allow saving while paused if a current engine/input snapshot is
  available; otherwise show `No active layout`.
- Should saved rules include `layout.enabled`, `split_mode`, or `split_width_multiplier` in new
  profiles? Recommended behavior: write only the `rules` array and let omitted fields fall back to
  global options.
- Should Save All fail if any selected monitor cannot be serialized, or save the serializable
  monitors? Recommended behavior: all-or-nothing for I/O/parse failures, but skip zero/one-window
  monitors by design.
