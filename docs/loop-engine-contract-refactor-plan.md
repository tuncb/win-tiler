# `src/loop.cpp` to `Engine` Contract Refactor Plan

This document turns the current loop analysis into a concrete refactor plan for the following flow:

1. Gather input
2. Send input to logic in `src/engine.h`
3. Get output that describes what needs to happen
4. Apply the output in the loop

## Goal

After the refactor, `run_loop_mode()` should act as a runtime coordinator, not a second tiling controller.

The loop should own:

- waiting for messages and session resume
- config hot reload
- monitor topology refresh
- virtual desktop selection
- calling Windows APIs to apply effects
- rendering the overlay

The engine should own:

- interpreting frame input
- mutating `ctrl::System`
- deciding whether layout changed
- deciding whether selection changed
- deciding whether cursor/focus/toast updates are needed
- deciding whether tiles need to be applied

## Current State Summary

The cleanest existing example of the desired shape is:

- `winapi::gather_loop_input_state(...)`
- `extract_window_state_from_input(...)`
- `engine.update(...)`
- `apply_tile_positions(...)`

That path already looks like:

- gather
- decide in engine
- apply

The main places that still break the boundary are:

- `apply_zen_to_maximized_windows(...)` mutates `ctrl::System` directly in `src/loop.cpp`
- `update_selection_from_hover(...)` mutates `engine.system.selection` directly in `src/loop.cpp`
- hotkey handling calls `engine.process_action(...)`, but the loop still reads `engine.system` to decide which window to focus
- drag-end handling is split between loop helpers and engine/controller calls
- `apply_tile_positions(...)` runs every iteration instead of because the engine explicitly requested it

## Proposed Contract

The refactor should introduce a single per-frame input type and a single per-frame output type.

Prefer a typed struct over a generic command queue. The codebase already uses direct data structs, and a typed struct will be easier to test and easier to read.

### Input

```cpp
struct EngineFrameInput {
  std::vector<ctrl::ClusterCellUpdateInfo> cluster_updates;
  std::optional<HotkeyAction> hotkey_action;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<size_t> drag_ended_leaf_id;
  bool drag_ended = false;
  bool is_ctrl_pressed = false;
  bool auto_zen_on_maximize = false;
  bool update_hover_selection = true;
  std::vector<std::vector<winapi::ManagedWindowInfo>> windows_per_monitor;
  std::optional<int> redirect_cluster_index;
  float gap_h = 0.0f;
  float gap_v = 0.0f;
  float zen_pct = 0.0f;
};
```

Notes:

- This input is already normalized. The loop can still gather it from `winapi`, but the engine should not need to call `winapi` directly.
- `cluster_updates` replaces the current `extract_window_state_from_input(...)` plus later `engine.update(...)` call shape.
- `redirect_cluster_index` should be resolved before the engine call only if the engine cannot derive it itself. Long-term, the engine should probably derive it from cursor hover and current selection.

### Output

```cpp
enum class LoopControl {
  Continue,
  Exit,
  EnterManualPause,
};

struct EngineFrameOutput {
  LoopControl control = LoopControl::Continue;

  bool layout_changed = false;
  bool selection_changed = false;
  bool stored_cell_changed = false;
  bool clear_drag_ended = false;
  bool apply_tiles = false;

  std::optional<size_t> focus_leaf_id;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<std::string> toast_message;

  std::vector<std::vector<ctrl::Rect>> geometries;
};
```

Notes:

- `geometries` should be the final geometries for the frame after all engine-side decisions.
- The loop should not need to call `engine.compute_geometries(...)` separately once `process_frame(...)` exists.
- `focus_leaf_id` must be returned directly so the loop does not inspect `engine.system` to infer which window to focus.
- `apply_tiles` must be explicit so `apply_tile_positions(...)` becomes conditional.

## Desired Loop Shape

The target structure in `run_loop_mode()` should look like this:

```cpp
while (true) {
  wait_for_messages_or_timeout(...);

  if (session_is_paused()) {
    wait_for_session_active();
    continue;
  }

  if (is_manually_paused) {
    if (pending_hotkey == HotkeyAction::TogglePause) {
      is_manually_paused = false;
    } else {
      continue;
    }
  }

  auto gathered = gather_frame_input(...);
  if (!gathered.desktop_id.has_value()) {
    overlay::clear();
    continue;
  }

  auto& engine = select_current_desktop_engine(...);

  EngineFrameOutput output = engine.process_frame(gathered.engine_input);

  if (output.control == LoopControl::Exit) {
    break;
  }

  if (output.control == LoopControl::EnterManualPause) {
    is_manually_paused = true;
    overlay::clear();
    continue;
  }

  apply_frame_output(output, engine.system, ...);

  renderer::render(engine.system, output.geometries, ...);
}
```

## What Should Stay in the Loop

Not everything should move into `Engine`.

The following should remain loop-owned:

- session pause / resume waiting
- config reload and hotkey re-registration
- monitor detection and engine reinitialization
- virtual desktop selection through `MultiEngine`
- `overlay::clear()`, `overlay::shutdown()`, `renderer::render(...)`
- Windows API calls such as `set_cursor_pos`, `set_foreground_window`, `update_window_position`, `clear_drag_ended`

That keeps `Engine` focused on model logic and keeps `winapi` at the boundary.

## What Should Move Behind the Engine Contract

These are the current branches that should stop mutating or inspecting `engine.system` directly from `src/loop.cpp`.

### 1. Hotkey post-processing

Current problem:

- The loop calls `engine.process_action(...)`
- Then the loop reads `engine.system.selection`
- Then the loop derives which window to focus

Target:

- `engine.process_frame(...)` or an expanded action handler returns `focus_leaf_id`
- The loop only applies the focus request

### 2. Hover selection

Current problem:

- `update_selection_from_hover(...)` writes `engine.system.selection` directly

Target:

- Hover selection becomes an engine decision
- Output sets `selection_changed` and possibly `cursor_pos` if needed

### 3. Auto-zen on maximize

Current problem:

- `apply_zen_to_maximized_windows(...)` bypasses `Engine`
- It directly calls `ctrl::set_zen(...)` and `ctrl::clear_zen(...)`

Target:

- Maximized-window inspection is part of frame processing
- Engine decides whether zen changed
- Output marks `layout_changed` and `apply_tiles`

### 4. Drag-end resize and drop-move

Current problem:

- `handle_window_resize(...)` and `handle_mouse_drop_move(...)` split input decoding, model mutation, and side effects across loop and engine

Target:

- Loop gathers drag-end facts
- Engine decides whether the drag means resize, move, swap, or no-op
- Output requests `clear_drag_ended`, `apply_tiles`, `cursor_pos`, and final `geometries`

### 5. Tile apply policy

Current problem:

- `apply_tile_positions(...)` is called every iteration

Target:

- Engine output explicitly says whether tile application is required
- The loop applies tiles only when `output.apply_tiles` is true

## Recommended Migration Plan

This should be done in stages so behavior stays stable.

### Stage 1: Strengthen output types without changing control flow

Add new result structs first:

- replace `bool Engine::update(...)` with a typed `UpdateResult`
- expand `ActionResult`
- add a small result for hover and auto-zen decisions

Suggested fields:

- `layout_changed`
- `selection_changed`
- `focus_leaf_id`
- `cursor_pos`
- `toast_message`
- `apply_tiles`

The loop can continue calling separate engine methods during this stage, but it should stop inferring effects by reading `engine.system`.

### Stage 2: Eliminate direct model mutation from the loop

Move these behind engine methods:

- hover selection update
- auto-zen on maximize
- drag-end resize / move / exchange interpretation

After this stage, `src/loop.cpp` should no longer call `ctrl::*` mutators and should no longer assign directly to `engine.system.selection`.

### Stage 3: Introduce a single frame entry point

Add a new method in `src/engine.h`:

```cpp
[[nodiscard]] EngineFrameOutput process_frame(const EngineFrameInput& input);
```

Internally, `process_frame(...)` should perform:

1. compute or refresh current geometry
2. process hotkey action
3. process drag-end action
4. sync window topology with `cluster_updates`
5. process auto-zen
6. process hover selection
7. compute final geometries for apply/render
8. return explicit apply instructions

### Stage 4: Collapse repeated geometry recomputation

Today the loop recomputes geometry multiple times in the same iteration.

After `process_frame(...)` exists:

- geometry should be computed inside the engine at well-defined points
- the final output should carry the final geometry once
- the loop should use only that final geometry for apply and render

### Stage 5: Make apply explicit and minimal

Introduce a loop helper:

```cpp
void apply_frame_output(const EngineFrameOutput& output, const ctrl::System& system);
```

It should be responsible only for:

- `set_foreground_window(...)`
- `set_cursor_pos(...)`
- `clear_drag_ended()`
- `apply_tile_positions(...)`
- toast update

It should not contain any tiling decision logic.

## Suggested Intermediate Type Changes

If a full `process_frame(...)` jump feels too large, use these incremental changes first.

### `ActionResult`

Current `ActionResult` is too small.

It should at least grow to:

```cpp
struct ActionResult {
  bool success = false;
  bool layout_changed = false;
  bool selection_changed = false;
  bool apply_tiles = false;
  std::optional<size_t> focus_leaf_id;
  std::optional<ctrl::Point> cursor_pos;
  std::optional<std::string> toast_message;
  LoopControl control = LoopControl::Continue;
};
```

### `UpdateResult`

Replace the `bool` return from `Engine::update(...)` with:

```cpp
struct UpdateResult {
  bool topology_changed = false;
  bool selection_changed = false;
  bool layout_changed = false;
  bool apply_tiles = false;
  std::optional<ctrl::Point> cursor_pos;
};
```

That would remove the current ambiguity where `changed` means several different things at once.

## Acceptance Criteria

The refactor is complete when all of the following are true:

- `src/loop.cpp` no longer writes `engine.system.selection` directly
- `src/loop.cpp` no longer calls `ctrl::set_zen(...)`, `ctrl::clear_zen(...)`, or other `ctrl::*` mutators directly
- hotkey handling does not inspect `engine.system` to infer which window to focus
- drag-end handling does not split decision logic across multiple loop helpers
- tile application is conditional on engine output, not unconditional every frame
- one engine call produces the final geometry used by both apply and render

## Test Plan

When this refactor is implemented, tests should be added around the new contract.

Recommended unit test coverage:

- hotkey action returns `focus_leaf_id` and `cursor_pos` without loop-side inference
- update path returns `apply_tiles` only when topology or layout changed
- hover selection updates only engine state and reports `selection_changed`
- auto-zen on maximize reports layout changes through engine output
- drag-end move, exchange, and resize all return explicit apply instructions
- pause and exit actions return `LoopControl` values instead of being special-cased in the loop

## Recommended First Cut

The lowest-risk first implementation is:

1. Introduce `UpdateResult` and expand `ActionResult`
2. Move hover selection and auto-zen behind engine methods
3. Return `focus_leaf_id` from hotkey handling
4. Make tile application conditional
5. Add `process_frame(...)` only after the smaller outputs have proven stable

That sequence keeps the code review surface manageable and removes the worst loop/engine boundary leaks before the larger API consolidation.
