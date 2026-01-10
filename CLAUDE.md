# CLAUDE.md

win-tiler is a tiling windows manager for Windows OS.

## Build Commands

```batch
build-run.bat build --Debug           # Build debug version
build-run.bat build --Release         # Build release version
build-run.bat build-run --Test-Debug  # Build and run unit tests
```

For validation run unit tests and then build in debug mode.
The development is going on windows OS, so use \\ for bash commands path separation.
cd to the current folder and run build-run.bat directly.
Ie. cmd //c "build-run.bat build --Debug"

## Running the Application

```batch
build-run.bat build-run --Debug loop                         # Run in loop mode (hotkey-driven tiling)
build-run.bat build-run --Debug ui-test-monitor 0            # Launch UI visualizer for monitor 0
build-run.bat build-run --Debug ui-test-multi 0 0 1920 1080  # UI with custom cluster dimensions
build-run.bat build-run --Debug track-windows                # Track and log windows per monitor
build-run.bat build-run --Debug init-config config.toml      # Create default config file
```

## Architecture

Win-tiler is a Windows window tiling manager using a binary space partition (BSP) tree algorithm.

### Three-Layer Design

1. **Cell Logic** (`cells` namespace in `src/multi_cells.h/cpp`)
   - BSP tree implementation where each `Cell` is either a leaf or has two children
   - Core operations: split, delete, toggle split direction, navigate
   - `CellCluster` manages a tree of cells for one logical area
   - `System` manages a list of `CellCluster`s.
2. `src/winapi.h/cpp` - Windows API wrapper for monitor/window enumeration
3. `src/multi_cell_renderer.h/cpp` - Renders cells::System using the overlay unit
4. `src/overlay.h/cpp` - Direct2D overlay rendering for visual feedback
5. `src/multi_ui.h/cpp` - Raylib-based interactive visualization, used for test purposes.
6. `src/loop.h/cpp` - Main loop mode with hotkey handling and window tiling.
   - Uses multi_cell_renderer.h for rendering,
   - Uses `src/winapi.h/cpp` for user interaction.
7. - `src/main.cpp` - Main entry point with command dispatch
8. - `src/argument_parser.h/cpp` - CLI argument parsing
9. - `src/options.h/cpp` - Configuration management (TOML-based)

### Key Concepts

- **Selection Model**: Selection is tracked at the System level, not individual clusters. Only one cell can be selected across the entire system.
- **Gap System**: `gap_horizontal` and `gap_vertical` control spacing between tiled windows (default 10.0f)
- **Leaf Cells**: Only leaf cells (no children) can hold windows. Splits create parent-child relationships.
- **Direction Navigation**: `Direction::Left/Right/Up/Down` for navigation, `SplitDir::Horizontal/Vertical` for splits
- **Split Mode**: Controls how new splits are oriented:
  - `SplitMode::Zigzag` - Alternate direction based on parent (default)
  - `SplitMode::Vertical` - Always split vertically
  - `SplitMode::Horizontal` - Always split horizontally
- **Zen Mode**: Allows a single cell to expand to fill the entire cluster area. Toggle with `toggle_selected_zen()`

## Dependencies

- **raylib** - Graphics/UI visualization
- **doctest** - Unit testing framework
- **spdlog** - Structured logging
- **tomlplusplus** - TOML configuration parsing
- **magic-enum** - Enum reflection
- **tl-expected** - Expected/Result type
- **Windows API** - Dwmapi.lib, Psapi.lib for window management

## Syntax Rules

 - Do not use (void), log error for handling return statements from [[nodiscard]] functions.
