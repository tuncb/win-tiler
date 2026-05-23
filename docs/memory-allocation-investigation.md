# Memory Allocation Investigation

**Date:** May 16, 2026
**Scope:** Investigation only; no code changes.
**Goal:** Identify allocation sources in the app and evaluate whether allocation reduction,
arenas, pools, or persistent scratch buffers are a good fit.

## Executive Summary

The application has very little explicit heap management in production code. There is almost no
direct `new`, `delete`, `malloc`, or `free` usage in the core app. The relevant allocation cost is
mostly short-lived allocation from STL containers, strings, JSON/TOML objects, and COM/D2D resource
creation.

The highest-impact allocation work is in the frame loop:

- `winapi::gather_loop_input_state(...)` builds fresh monitor and window vectors each iteration.
- `build_engine_frame_input(...)` converts that snapshot into additional fresh vectors.
- `Engine::compute_geometries(...)` allocates nested geometry vectors, sometimes more than once per
  frame.
- `Engine::update(...)` builds several temporary vectors to compare desired and current window
  state.
- Rendering creates Direct2D brushes per draw call and toast text resources while a toast is visible.

An arena can help, but the first priority should be reducing duplicate work and reusing frame
buffers. Arena allocation would reduce allocator overhead for unavoidable temporary data, but it
will not fix the current duplication by itself.

## Allocation Profile By Area

### Main Loop Input Snapshot

Relevant code:

- `src/winapi.cpp`: `gather_loop_input_state(...)`
- `src/winapi.cpp`: `gather_raw_window_data(...)`
- `src/loop.cpp`: `build_engine_frame_input(...)`
- `src/runtime_support.cpp`: `extract_cluster_updates_from_input(...)`
- `src/runtime_support.cpp`: `extract_managed_window_states_from_input(...)`

Current behavior:

1. Each loop iteration creates a new `LoopInputState`.
2. `LoopInputState::monitors` is filled from `get_monitors()`.
3. `LoopInputState::windows_per_monitor` is rebuilt as `std::vector<std::vector<ManagedWindowInfo>>`.
4. `build_engine_frame_input(...)` then creates new vectors for:
   - `EngineFrameInput::cluster_updates`
   - `EngineFrameInput::managed_windows`
   - `EngineFrameInput::cluster_options`

Expected allocation pattern:

- One vector allocation for monitors.
- One outer vector allocation for `windows_per_monitor`.
- One inner vector allocation per monitor with managed windows.
- One vector allocation for all enumerated handles.
- Additional outer and inner vector allocations when converting to engine input.

Risk:

This path runs every loop iteration. Even when the window topology is stable, the containers are
rebuilt and then discarded.

Recommended direction:

- Introduce reusable loop-frame buffers that live outside the `while` loop.
- Prefer `clear()` plus retained capacity over constructing fresh vectors.
- Consider having `gather_loop_input_state(...)` fill an output object instead of returning a new
  object by value.
- Consider passing spans/views from `LoopInputState` into the engine instead of materializing
  duplicate `cluster_updates` and `managed_windows`.

### Geometry Computation

Relevant code:

- `src/loop.cpp`: `engine.compute_geometries(cluster_options)`
- `src/engine.cpp`: `Engine::compute_geometries(...)`
- `src/engine.cpp`: `ctrl::compute_cluster_geometry(...)`
- `src/engine.cpp`: `Engine::process_frame(...)`

Current behavior:

`Engine::compute_geometries(...)` returns `std::vector<std::vector<ctrl::Rect>>`. For each cluster,
`compute_cluster_geometry(...)` allocates a `std::vector<Rect>` sized to the cluster tree.

The loop computes geometry before `process_frame(...)`, and `process_frame(...)` can compute its own
local geometry again through `ensure_geometries()`. At the end of `process_frame(...)`, geometry is
copied or moved into `EngineFrameOutput::geometries`, then moved again by the loop.

Risk:

Geometry is cheap per cell, but the allocation pattern is frame-frequency and duplicated. This is a
better target for caching or output-buffer reuse than for an arena alone.

Recommended direction:

- Make geometry a per-desktop cached value with a dirty flag.
- Compute geometry only once per frame.
- Add an overload that fills a caller-owned output buffer:
  `compute_geometries(cluster_options, output_geometries)`.
- Keep the outer and inner vector capacity across frames.
- Avoid returning geometry from `EngineFrameOutput` when the loop already owns the current geometry
  buffer. The output can instead indicate whether geometry changed or which buffer should be used.

### Engine Update Scratch Data

Relevant code:

- `src/engine.cpp`: `ctrl::update_impl(...)`
- `src/engine.cpp`: `ctrl::get_cluster_leaf_ids(...)`
- `src/engine.cpp`: `update_zen_for_maximized_windows(...)`
- `src/engine.cpp`: `find_placement_correction_leaf_ids(...)`
- `src/binary_tree.h`: `BinaryTree::remove(...)`

Current behavior:

`update_impl(...)` allocates several temporary containers:

- `redirected_updates`, a full copy of input updates.
- `new_windows` when redirecting new windows to a target cluster.
- `current_leaf_ids`.
- `sorted_current`.
- `sorted_desired`.
- `to_delete`.
- `to_add`.

`update_zen_for_maximized_windows(...)` allocates `current_maximized_leaf_ids` each call.

`find_placement_correction_leaf_ids(...)` allocates a vector of leaf IDs when placement correction is
needed.

`BinaryTree::remove(...)` allocates:

- `std::set<int> to_remove`
- `std::vector<int> remap`
- `std::vector<Node> new_nodes`

Risk:

Most engine scratch allocation is proportional to monitor count and window count. It is manageable
for normal desktop sizes, but it is still unnecessary frame churn when topology is stable.

Recommended direction:

- Add a no-change fast path before sorting and set-difference work.
- Avoid copying `cluster_updates` unless redirecting is actually needed.
- Replace small `std::set<int>` usage in `BinaryTree::remove(...)` with a small fixed-size path or
  a linear predicate. Most removals remove exactly two indices.
- Reuse scratch vectors through a `FrameScratch` object passed into update processing.
- Keep `previous_fullscreen_state` and `current_maximized_leaf_ids` as reusable buffers on `Engine`
  or in frame scratch.

### WinAPI Metadata Strings

Relevant code:

- `src/winapi.cpp`: `WindowEnumProc(...)`
- `src/winapi.cpp`: `get_process_name_from_pid(...)`
- `src/winapi.cpp`: `get_window_info(...)`
- `src/winapi.cpp`: `gather_window_management_states(...)`

Current behavior:

Enumeration uses stack buffers for title/class names, but then copies class and process names into
`std::string`. Process name queries open a process handle and return a string. Debug paths also
materialize title, class, and process strings.

Risk:

The CPU cost is probably higher than the heap cost here, but string allocations can still happen
every enumeration, especially for process names and titles exceeding small-string optimization.

Recommended direction:

- Cache process names by PID.
- Cache static-ish window metadata by `HWND`, invalidated on destroy/title/name-related events or
  refreshed periodically.
- Keep full metadata out of the hot frame snapshot unless needed by debug output.

### Renderer And Overlay Resources

Relevant code:

- `src/multi_cell_renderer.cpp`: `renderer::render(...)`
- `src/overlay.cpp`: `draw_rect(...)`
- `src/overlay.cpp`: `draw_toast(...)`

Current behavior:

`draw_rect(...)` creates and releases an `ID2D1SolidColorBrush` for each rectangle. Toast rendering
creates:

- `IDWriteTextFormat`
- `std::wstring` from UTF-8 text
- `IDWriteTextLayout`
- background brush
- text brush

`renderer::render(...)` also calls `winapi::get_monitors()` while drawing a toast.

Risk:

These are not ordinary C++ heap allocations only; they are COM/D2D resource churn. The brush
creation path runs every rendered cell every frame.

Recommended direction:

- Cache brushes for normal, selected, stored, toast background, and toast text colors.
- Cache `IDWriteTextFormat` by font size.
- Cache toast text layout while the toast message is unchanged.
- Reuse known monitor data from the loop instead of calling `get_monitors()` inside rendering.

### Config And Startup Paths

Relevant code:

- `src/options.cpp`: TOML parsing and `LayoutTreeNode` construction.
- `src/startup.cpp`: startup registration helpers.

Current behavior:

These paths use strings, vectors, TOML values, `std::ostringstream`, `std::istringstream`, and
`std::shared_ptr<LayoutTreeNode>`.

Risk:

These allocations are acceptable because they occur on startup, config reload, explicit CLI requests,
or startup-management commands. They are not the primary frame-loop allocation source.

Recommended direction:

- Do not prioritize arenas here.
- Layout parsing could eventually use value-owned tree storage instead of `shared_ptr`, but this is
  not urgent unless config reload becomes frequent or layout rule counts become large.

## Arena And Pool Suitability

### Good Fit: Frame Scratch Arena

A frame scratch arena is a good fit for data that dies at the end of one loop iteration:

- temporary window handle lists
- converted cluster updates
- managed window state snapshots
- geometry scratch
- update comparison buffers
- placement correction leaf IDs

Suggested shape:

```cpp
struct FrameScratch {
  std::pmr::monotonic_buffer_resource arena;
  std::pmr::vector<winapi::HWND_T> handles;
  std::pmr::vector<winapi::MonitorInfo> monitors;
  std::pmr::vector<std::pmr::vector<winapi::ManagedWindowInfo>> windows_per_monitor;
  std::pmr::vector<ctrl::ClusterCellUpdateInfo> cluster_updates;
  std::pmr::vector<std::pmr::vector<ManagedWindowState>> managed_windows;
  std::pmr::vector<std::pmr::vector<ctrl::Rect>> geometries;
};
```

Practical note: migrating nested `std::vector<std::vector<T>>` to `std::pmr` is invasive. A staged
approach can get most of the benefit first by using normal persistent vectors with retained capacity.

### Good Fit: Persistent Per-Desktop Geometry Cache

Geometry is naturally tied to `Engine` or per-desktop state. A persistent cache avoids repeated
allocation and repeated computation.

Suggested shape:

- `Engine` or `LoopDesktopData` owns cached geometry.
- Dirty flags are set on topology, split-ratio, zen, fullscreen, monitor, and per-cluster option
  changes.
- The loop renders from the cached geometry.

This is likely a better first step than a generic arena because it removes work rather than merely
making allocation cheaper.

### Conditional Fit: BinaryTree Pooling

`BinaryTree` already has an allocator template parameter, which is useful. However, `Cluster` fixes
the type as `BinaryTree<CellData>`, so allocator plumbing is not currently exposed through the
engine model.

Pooling tree nodes could help when windows are frequently added, removed, moved, or layout templates
are reapplied. It is probably not the first target because normal desktop trees are small.

Recommended prerequisite:

- First optimize `BinaryTree::remove(...)` for the common two-index removal case.
- Then consider exposing allocator-aware cluster/tree construction if profiling still shows tree
  allocation as relevant.

### Poor Fit: Config TOML

TOML allocations are reload-scoped. They are not important enough to justify arena complexity.

## Recommended Implementation Order

### Phase 1: Remove Duplicate Frame Allocations

1. Keep loop-owned buffers outside the `while` loop.
2. Rework input gathering and conversion helpers to fill caller-owned buffers.
3. Stop computing geometry both before and inside `Engine::process_frame(...)`.
4. Reuse `cluster_options` instead of rebuilding it multiple times in the same frame unless config
   or monitor state actually changed.

Expected result:

- Lower allocator pressure.
- Lower CPU from less repeated copying and geometry computation.
- Minimal architectural risk.

### Phase 2: Add Geometry And Metadata Caches

1. Add per-desktop geometry cache with dirty flags.
2. Cache monitor data and only refresh on display changes or periodic validation.
3. Cache PID to process name.
4. Cache `HWND` metadata where safe.

Expected result:

- Less allocation and much less WinAPI/string churn.
- Pairs well with the existing CPU optimization opportunities.

### Phase 3: Introduce Frame Scratch

1. Add a `FrameScratch` object to the loop.
2. Move hot-path temporary vectors into it.
3. Reset scratch once per frame.
4. Consider `std::pmr` only after the buffer ownership boundaries are clear.

Expected result:

- Fewer heap calls for unavoidable temporary data.
- More predictable allocation behavior under window churn.

### Phase 4: Renderer Resource Reuse

1. Cache D2D brushes by color.
2. Cache toast text format and layout while toast content is unchanged.
3. Avoid monitor enumeration from the renderer.

Expected result:

- Reduced COM/D2D resource churn.
- More stable render-frame cost.

## Measurement Plan

Before implementing allocation changes, collect a baseline:

- Use Visual Studio Diagnostic Tools memory allocation profiling.
- Run the app idle with a stable desktop for at least 30 seconds.
- Repeat with active hotkey navigation.
- Repeat with dragging and dropping windows.
- Repeat with toast rendering visible.

Useful counters to add later:

- allocations per frame
- bytes allocated per frame
- geometry recomputes per frame
- window snapshot capacity reuse counts
- frame scratch high-water mark

Success metrics:

- near-zero per-frame heap allocations during stable idle
- one geometry computation or fewer per active frame
- no unbounded growth in cached metadata
- no stale window metadata behavior regressions

## Conclusion

The app is a good candidate for allocation reduction, but the first step should not be a broad arena
rewrite. The strongest path is:

1. remove duplicated frame data and geometry work
2. retain and reuse buffer capacity
3. cache stable metadata and render resources
4. introduce a frame arena for the remaining short-lived scratch data

This sequence reduces complexity risk while still moving toward an arena-friendly architecture.
