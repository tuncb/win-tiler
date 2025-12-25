# runLoopMode Function Analysis

## Overview

The `runLoopMode` function (`src/loop.cpp:367-607`) is the main tiling loop that manages window positions, handles keyboard hotkeys, and keeps the tile layout synchronized with actual window state.

---

## Phase 1: Initialization (lines 371-408)

### Data Flow

```
get_monitors() → vector<MonitorInfo>
    ↓
For each monitor:
    get_hwnds_for_monitor() → vector<HWND>
        ↓
    Convert HWNDs to size_t cellIds
        ↓
    Build ClusterInitInfo{id, x, y, w, h, cellIds}
        ↓
createSystem(clusterInfos) → multi_cell_logic::System
```

### Key Transformations

1. **Monitor work areas** → `ClusterInitInfo` structs (float coordinates)
2. **HWND pointers** → `size_t` leaf IDs via `reinterpret_cast`
3. **ClusterInitInfo vector** → `System` (BSP tree structure)

### Actions

- Print initial tile layout (`printTileLayout`)
- Apply initial window positions (`applyTileLayout`)
- Register keyboard hotkeys (`registerNavigationHotkeys`)

---

## Phase 2: Main Loop (lines 425-602)

Each iteration performs these sequential steps:

### Step 2.1: Hotkey Processing (lines 431-506)

```
winapi::check_keyboard_action() → optional<int> hotkeyId
    ↓
idToHotkeyAction(hotkeyId) → optional<HotkeyAction>
```

**Actions by HotkeyAction type:**

| Action | Data Transformation |
|--------|---------------------|
| Navigate* | `hotkeyActionToDirection()` → `Direction` → `moveSelection()` updates `system.selection` |
| ToggleSplit | Mutates selected cell's `splitDir` in system |
| Exit | Breaks loop |
| ToggleGlobal | Mutates cluster's `globalSplitDir` |
| StoreCell | Copies `(clusterId, leafId)` to `storedCell` variable |
| ClearStored | Resets `storedCell` |
| Exchange | `swapCells()` swaps leaf IDs between two cells |
| Move | `moveCell()` relocates a leaf from source to target cell |

### Step 2.2: Window State Gathering (lines 509-513)

```
gatherCurrentWindowState(ignoreOptions)
    ↓
vector<ClusterCellIds> = [{clusterId, vector<leafIds>}, ...]
    ↓
redirectNewWindowsToSelection(system, currentState)
    - Filters new windows (not in any cluster)
    - Removes them from detected clusters
    - Appends them to selected cluster's leafIds
```

### Step 2.3: System Synchronization (lines 516-518)

```
updateSystem(system, currentState, nullopt)
    ↓
UpdateResult {
    deletedLeafIds: vector<size_t>,  // Windows that closed
    addedLeafIds: vector<size_t>     // New windows detected
}
```

**Internal mutations to `system`:**
- Dead cells marked for closed windows
- New cells split from selected cell for new windows

### Step 2.4: Selection Update (lines 521-557)

```
get_foreground_window() → HWND
    ↓
isHwndInSystem() check
    ↓
get_cursor_pos() → {x, y}
    ↓
findCellAtPoint(system, x, y) → optional<(clusterId, cellIndex)>
    ↓
Update system.selection if changed
    ↓
set_foreground_window() for the cell under cursor
```

### Step 2.5: Layout Application (lines 560-596)

**On changes detected:**
```
For each added window:
    Find cluster containing leafId
        ↓
    getCellGlobalRect() → Rect
        ↓
    Calculate center point
        ↓
    set_cursor_pos(centerX, centerY)
```

**Always:**
```
applyTileLayout(system)
    ↓
For each cluster, for each leaf cell:
    getCellGlobalRect(pc, cellIndex) → Rect
        ↓
    Convert to WindowPosition{x, y, width, height}
        ↓
    update_window_position(TileInfo{hwnd, pos})
```

---

## Phase 3: Cleanup (lines 604-606)

```
unregisterNavigationHotkeys(keyboardOptions)
```

---

## Data Structure Summary

| Structure | Purpose |
|-----------|---------|
| `System` | Top-level container with `clusters` vector and `selection` |
| `PositionedCluster` | Cluster + global offset (x, y) |
| `CellCluster` | BSP tree of `Cell` objects |
| `Cell` | Node with optional `leafId`, `splitDir`, child indices |
| `Selection` | Current `{clusterId, cellIndex}` |
| `storedCell` | Temporary `{clusterId, leafId}` for swap/move ops |

---

## External Function Calls by Unit

### winapi (Windows API Wrapper)

| Function | Use Case |
|----------|----------|
| `get_monitors()` | Enumerate all monitors and their work areas during initialization |
| `get_hwnds_for_monitor()` | Get all tiling-eligible window handles for a specific monitor |
| `create_hotkey()` | Parse hotkey string and create hotkey registration info |
| `register_hotkey()` | Register a global hotkey with Windows |
| `unregister_hotkey()` | Unregister a global hotkey during cleanup |
| `check_keyboard_action()` | Poll for hotkey press events (returns hotkey ID if pressed) |
| `get_foreground_window()` | Get the currently focused window handle |
| `set_foreground_window()` | Bring a window to foreground after navigation |
| `get_cursor_pos()` | Get current mouse cursor position for selection tracking |
| `set_cursor_pos()` | Move cursor to center of cell after navigation or new window |
| `get_window_info()` | Get window title for debug logging |
| `update_window_position()` | Apply calculated tile position/size to a window |

### multi_cell_logic (Multi-Cluster System Logic)

| Function | Use Case |
|----------|----------|
| `createSystem()` | Build initial BSP tree system from cluster init infos |
| `updateSystem()` | Sync system state with current window list (add/remove cells) |
| `getCluster()` | Retrieve a specific cluster by ID |
| `getSelectedCell()` | Get the currently selected cell's cluster ID and index |
| `findCellByLeafId()` | Look up a cell by its window handle (leaf ID) |
| `findCellAtPoint()` | Find which cell contains a given screen coordinate |
| `getCellGlobalRect()` | Convert cell's local rect to global screen coordinates |
| `countTotalLeaves()` | Count total windows across all clusters (for logging) |
| `moveSelection()` | Navigate selection in a direction (left/right/up/down) |
| `toggleSelectedSplitDir()` | Toggle the selected cell's split direction |
| `toggleClusterGlobalSplitDir()` | Toggle the cluster's default split direction |
| `swapCells()` | Exchange leaf IDs between two cells |
| `moveCell()` | Move a leaf from one cell to another (with deletion) |

### cell_logic (Single Cluster Logic)

| Function | Use Case |
|----------|----------|
| `Direction` enum | Used for navigation direction (Left, Right, Up, Down) |
| `SplitDir` enum | Used for split direction (Horizontal, Vertical) |
| `Rect` struct | Rectangle type for cell bounds |

### spdlog (Logging)

| Function | Use Case |
|----------|----------|
| `trace()` | Performance timing and detailed debug info |
| `debug()` | Window change details and layout dumps |
| `info()` | User-visible status messages (hotkey registration, changes) |
| `error()` | Error conditions (failed foreground set, missing cells) |

### std::chrono (Timing)

| Function | Use Case |
|----------|----------|
| `high_resolution_clock::now()` | Measure function execution times |
| `duration_cast<microseconds>()` | Convert duration for logging |

### std::this_thread (Threading)

| Function | Use Case |
|----------|----------|
| `sleep_for()` | 100ms delay between loop iterations |

---

## Key Invariants

1. Only leaf cells (no children) hold window handles
2. Selection is system-wide (one cell across all monitors)
3. New windows always split from the currently selected cell
4. Layout is reapplied every 100ms loop iteration
