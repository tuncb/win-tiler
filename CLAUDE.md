# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```batch
build-run.bat build --Debug           # Build debug version
build-run.bat build --Release         # Build release version
build-run.bat build-run --Test-Debug  # Build and run unit tests
```

The build script auto-detects MSBuild via vswhere.exe and copies required DLLs (raylib.dll, glfw3.dll) to the output directory `x64\{Configuration}\`.

The development is going on windows OS, so use \\ for bash commands path separation.
Ie. cmd //c "C:\\work\\win-tiler\\build-run.bat build --Debug"

## Running the Application

```batch
build-run.bat build-run --Debug apply              # Apply tiling to actual windows
build-run.bat build-run --Debug apply-test         # Preview tiling layout in console
build-run.bat build-run --Debug ui-test-monitor 0  # Launch UI visualizer for monitor 0
build-run.bat build-run --Debug ui-test-multi 800 600 1920 1080  # UI with custom cluster dimensions
```

## Architecture

Win-tiler is a Windows window tiling manager using a binary space partition (BSP) tree algorithm.

### Three-Layer Design

1. **Cell Logic** (`cell_logic` namespace in `src/multi_cells.h/cpp`)
   - BSP tree implementation where each `Cell` is either a leaf or has two children
   - Core operations: split, delete, toggle split direction, navigate
   - `CellCluster` manages a tree of cells for one logical area

2. **Multi-Cluster System** (`multi_cell_logic` namespace in `src/multi_cells.h/cpp`)
   - `System` manages multiple `CellCluster` instances (one per monitor)
   - System-wide selection tracking across all clusters
   - Cross-cluster navigation (move selection between monitors)
   - Coordinate conversion between local cluster coords and global screen coords

3. **Application Layer**
   - `src/multi_ui.h/cpp` - Raylib-based interactive visualization
   - `src/winapi.h/cpp` - Windows API wrapper for monitor/window enumeration
   - `src/main.cpp` - Main entry point with command dispatch

### Key Concepts

- **Selection Model**: Selection is tracked at the System level, not individual clusters. Only one cell can be selected across the entire system.
- **Gap System**: `gap_h` and `gap_v` control spacing between tiled windows (default 10.0f)
- **Leaf Cells**: Only leaf cells (no children) can hold windows. Splits create parent-child relationships.
- **Direction Navigation**: `Direction::left/right/top/bottom` for navigation, `SplitDir::horizontal/vertical` for splits

## Dependencies

- **raylib** - Graphics/UI visualization
- **doctest** - Unit testing framework
- **Windows API** - Dwmapi.lib, Psapi.lib for window management

## Syntax Rules

 - Do not use (void), log error for handling return statements from [[nodiscard]] functions.
