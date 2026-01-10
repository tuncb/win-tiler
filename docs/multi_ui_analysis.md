# multi_ui.cpp Execution Flow Analysis

This document analyzes the `src/multi_ui.cpp` file and explains the sequence of execution, focusing on how it uses the `cells` namespace from `src/multi_cells.h`.

## Overview

`multi_ui.cpp` provides a Raylib-based interactive visualization for testing the tiling system. It serves as a sandbox for experimenting with cell operations without needing actual Windows windows.

## Execution Flow

### 1. Initialization Phase (`run_raylib_ui_multi_cluster`)

```
Entry Point: run_raylib_ui_multi_cluster(infos, options_provider)
```

**Cells functions used:**
- `cells::create_system(infos, gap_h, gap_v)` - Creates the multi-cluster system from `ClusterInitInfo` data

The initialization also scans all clusters to find the maximum `leaf_id` to avoid ID collisions when adding new cells.

### 2. Main Loop Structure

The loop runs at 60 FPS and follows this sequence each frame:

```
+-------------------------------------------------------------+
|  1. Config Hot-Reload Check                                  |
|     - cells::recompute_rects() if config changed            |
+-------------------------------------------------------------+
|  2. Mouse Hover Selection                                    |
|     - find_cell_at_global_point() [local helper]            |
|     - Updates system.selection                              |
+-------------------------------------------------------------+
|  3. Keyboard Input Processing                                |
|     - Non-HotkeyAction keys (SPACE, D, I, C)                |
|     - HotkeyAction enum keys (H,J,K,L, etc.)                |
+-------------------------------------------------------------+
|  4. Drawing                                                  |
|     - Cluster backgrounds                                    |
|     - Cell rectangles                                        |
|     - Zen overlays                                           |
+-------------------------------------------------------------+
```

## Key Operations and Cells Functions Used

### A. Navigation (HotkeyAction::Navigate*)

Keys: `H`, `J`, `K`, `L`

```cpp
cells::move_selection(system, Direction::{Left|Down|Up|Right})
```

Returns `MoveSelectionResult` with `center` point for mouse repositioning.

### B. Adding/Deleting Cells

**SPACE key - Add new cell:**
```cpp
cells::get_cluster_leaf_ids(cluster)  // via build_current_state()
cells::update(system, state, selection, coords, zen%, foreground, gap_h, gap_v)
```

**D key - Delete selected cell:**
```cpp
cells::get_cluster_leaf_ids(cluster)  // via build_current_state()
cells::update(system, state, nullopt, coords, zen%, foreground, gap_h, gap_v)
```

### C. Split Operations

| Key | Action | Function |
|-----|--------|----------|
| `Y` | Toggle split direction | `cells::toggle_selected_split_dir(system, gap_h, gap_v)` |
| `PAGE_UP` | Increase split ratio by 5% | `cells::adjust_selected_split_ratio(system, 0.05f, gap_h, gap_v)` |
| `PAGE_DOWN` | Decrease split ratio by 5% | `cells::adjust_selected_split_ratio(system, -0.05f, gap_h, gap_v)` |
| `HOME` | Reset split ratio to 50% | `cells::set_selected_split_ratio(system, 0.5f, gap_h, gap_v)` |
| `;` | Cycle split mode | `cells::cycle_split_mode(system)` |

### D. Cell Exchange/Move Operations

| Key | Action | Function |
|-----|--------|----------|
| `[` | Store current cell | Stores `{cluster_index, leaf_id}` locally |
| `]` | Clear stored cell | Clears stored reference |
| `,` | Exchange stored with selected | `cells::swap_cells(system, cluster1, leaf1, cluster2, leaf2, gap_h, gap_v)` |
| `.` | Move stored to selected position | `cells::move_cell(system, src_cluster, src_leaf, dst_cluster, dst_leaf, gap_h, gap_v)` |
| `E` | Exchange with sibling | `cells::get_selected_sibling_leaf_id(system)` then `cells::swap_cells(...)` |

### E. Zen Mode

| Key | Action | Function |
|-----|--------|----------|
| `'` | Toggle zen mode | `cells::toggle_selected_zen(system)` |

### F. Debug/Validation

| Key | Action | Function |
|-----|--------|----------|
| `I` | Print system state | `cells::debug_print_system(system)` |
| `C` | Validate system | `cells::validate_system(system)` |

## Query Functions Used for Rendering

```cpp
// Hit testing for mouse hover:
cells::is_leaf(cluster, cell_index)
cells::get_cell_global_rect(pc, cell_index)

// Building current state for updates:
cells::get_cluster_leaf_ids(cluster)

// Finding stored cell for highlighting:
cells::find_cell_by_leaf_id(cluster, leaf_id)

// Zen overlay rendering:
cells::get_cell_display_rect(pc, cell_index, is_zen, zen_percentage)
```

## Data Flow Summary

```
+------------------+     +-----------------------------+
|  User Input      |---->|  HotkeyAction enum          |
+------------------+     +-------------+---------------+
                                       |
                                       v
+----------------------------------------------------------+
|                    cells:: Functions                      |
|  +------------------+  +------------------+               |
|  | Query Functions  |  | Mutation Funcs   |               |
|  | - is_leaf()      |  | - update()       |               |
|  | - find_cell_*()  |  | - move_*()       |               |
|  | - get_*_rect()   |  | - swap_cells()   |               |
|  | - get_*_leaf_id  |  | - toggle_*()     |               |
|  +------------------+  +------------------+               |
+----------------------------------------------------------+
                                       |
                                       v
+----------------------------------------------------------+
|  cells::System                                            |
|  - clusters: vector<PositionedCluster>                   |
|  - selection: optional<CellIndicatorByIndex>             |
|  - split_mode: SplitMode                                 |
+----------------------------------------------------------+
```

## Key Observations

1. **Selection is system-wide**: The `System.selection` tracks one selected cell across all clusters

2. **Mouse drives selection**: Hover updates selection via local `find_cell_at_global_point()`

3. **`cells::update()` is the main sync function**: Used when adding/deleting cells, takes current state and produces updates

4. **Gap parameters propagate everywhere**: Most mutation functions require `gap_horizontal` and `gap_vertical`

5. **Local helpers wrap coordinate transforms**: `ViewTransform` struct handles world-to-screen conversion, while `cells::` functions work in global/cluster coordinates

6. **Stored cell pattern**: The `[`, `]`, `,`, `.` keys implement a "yank and paste" workflow for cell operations across clusters

## File References

- `src/multi_ui.cpp` - Implementation
- `src/multi_ui.h` - Header with `run_raylib_ui_multi_cluster()` declaration
- `src/multi_cells.h` - Cell system interface
- `src/multi_cells.cpp` - Cell system implementation
