# Agent Mode Manual

## Overview

`win-tiler agent stdio` starts a persistent JSON-lines control surface for local tools and agents.

- Input: one JSON request per line on `stdin`
- Output: one JSON response per line on `stdout`
- Default transport: `win-tiler agent` and `win-tiler agent stdio` are equivalent

This mode currently supports:

- `list_windows`
- `get_state`
- `focus_window`
- `send_action`
- `swap_windows`
- `move_window_to_monitor`
- `retile`

## Start

```text
win-tiler agent stdio
```

Example:

```powershell
Get-Content .\docs\agent_session_list_and_state.jsonl | .\x64\Debug\win-tiler.exe agent stdio
```

## Request Format

Every request is a JSON object with:

- `id`: caller-defined string used to correlate responses
- `command`: one of the supported command names

Example:

```json
{"id":"1","command":"get_state","include_layout":true}
```

## Window IDs

Window IDs use this format:

```text
hwnd:0000000000000000
```

They are stable for the lifetime of the native window handle.

## Commands

### `list_windows`

Returns the current state payload with window information.

Request fields:

- `managed_only`: optional boolean, default `false`

Example:

```json
{"id":"1","command":"list_windows","managed_only":true}
```

Notes:

- This currently reports the same filtered window set used by the normal runtime ignore rules.
- At the moment, `managed_only:false` does not expose additional ignored/system windows.

### `get_state`

Returns the current state payload.

Request fields:

- `include_layout`: optional boolean, default `true`

Example:

```json
{"id":"2","command":"get_state","include_layout":true}
```

When `include_layout` is `false`:

- `layout_rect` is omitted
- `cluster_index` and `cell_index` are omitted
- `selection` is omitted

### `focus_window`

Focuses a window. It can also update the engine selection.

Request fields:

- `window_id`: required window ID
- `select`: optional boolean, default `true`

Example:

```json
{"id":"3","command":"focus_window","window_id":"hwnd:000000000012ABCD","select":true}
```

Notes:

- `select:true` requires the window to exist in the managed layout.
- `select:false` only focuses the OS window.

### `send_action`

Runs a tiling action using the same action names as `HotkeyAction`.

Request fields:

- `action`: required string

Example:

```json
{"id":"4","command":"send_action","action":"ToggleZen"}
```

Supported action strings:

- `NavigateLeft`
- `NavigateDown`
- `NavigateUp`
- `NavigateRight`
- `ToggleSplit`
- `CycleSplitMode`
- `StoreCell`
- `ClearStored`
- `Exchange`
- `Move`
- `SplitIncrease`
- `SplitDecrease`
- `ExchangeSiblings`
- `ToggleZen`
- `ResetSplitRatio`

Parsed but rejected in agent mode:

- `Exit`
- `TogglePause`
- `RestartSystem`

### `swap_windows`

Swaps two managed windows.

Request fields:

- `first_window_id`: required window ID
- `second_window_id`: required window ID

Example:

```json
{"id":"5","command":"swap_windows","first_window_id":"hwnd:000000000012ABCD","second_window_id":"hwnd:0000000000456789"}
```

### `move_window_to_monitor`

Moves a managed window to another monitor.

Request fields:

- `window_id`: required window ID
- `target_monitor_index`: required integer, zero-based
- `anchor_window_id`: optional window ID on the target monitor

Example:

```json
{"id":"6","command":"move_window_to_monitor","window_id":"hwnd:000000000012ABCD","target_monitor_index":1}
```

Notes:

- Empty target monitors are supported.
- If `anchor_window_id` is provided, it must already be on the target monitor.
- If the target monitor already has managed windows and no anchor is provided, the move uses the first managed tile on that monitor as the insertion anchor.

### `retile`

Reapplies the current layout to managed windows.

Example:

```json
{"id":"7","command":"retile"}
```

## Response Format

Every response is a JSON object with:

- `id`: copied from the request when available
- `ok`: boolean
- `result`: present on success
- `error`: present on failure

Success example:

```json
{"id":"1","ok":true,"result":{"split_mode":"Zigzag","windows":[],"selection":null}}
```

Error example:

```json
{"id":"1","ok":false,"error":"window_id was not found in the managed layout"}
```

For malformed requests that cannot be correlated to an input request ID, the response uses an empty `id`.

## State Response

`list_windows` and `get_state` return this shape:

```json
{
  "desktop_id": "{optional-desktop-id-or-null}",
  "split_mode": "Zigzag",
  "windows": [],
  "selection": null
}
```

Fields:

- `desktop_id`: current desktop ID, or `null`
- `split_mode`: current split mode string
- `windows`: array of window snapshots
- `selection`: selected managed cell, or `null`

### Window Snapshot

Each item in `windows` contains:

- `window_id`: handle-based window ID
- `pid`: process ID, or `null`
- `process_name`: process executable name
- `title`: window title
- `class_name`: native class name
- `monitor_index`: zero-based monitor index
- `cluster_index`: managed cluster index, or `null`
- `cell_index`: managed cell index, or `null`
- `rect`: compatibility rect
- `actual_rect`: live OS rect, or `null`
- `layout_rect`: computed tile rect, or `null`
- `is_managed`: whether the window is in the managed layout
- `is_foreground`: whether the window is the current foreground window
- `is_maximized`: whether the OS window is maximized
- `is_fullscreen`: whether the OS window is fullscreen

### Geometry Semantics

Use these fields intentionally:

- `actual_rect`: the real OS window position and size when available
- `layout_rect`: the engine's intended tile geometry when layout data is requested
- `rect`: compatibility field; it prefers `actual_rect` and only falls back to `layout_rect` if the live rect cannot be queried

For mismatch detection, compare `actual_rect` and `layout_rect`.

## Mutation Response

`focus_window`, `send_action`, `swap_windows`, `move_window_to_monitor`, and `retile` return this shape:

```json
{
  "selection_changed": false,
  "layout_changed": true,
  "focused_window_id": null,
  "toast_message": null
}
```

Fields:

- `selection_changed`: whether engine selection changed
- `layout_changed`: whether layout state changed
- `focused_window_id`: focused window ID when the mutation focused a window
- `toast_message`: optional status message

## Related Docs

- [Agent Session Examples](./agent_sessions.md)
- [PowerShell Client Wrapper](./Invoke-WinTilerAgent.ps1)
- [Implementation Notes](./agent_implementation.md)
