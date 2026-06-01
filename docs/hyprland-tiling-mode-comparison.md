# Hyprland Tiling Modes Compared to win-tiler

Research date: 2026-05-31.

This document compares the current Hyprland layout engines with the layout behavior available in
win-tiler. The main terminology trap is that Hyprland's `general.layout` selects a full layout
engine, while win-tiler's `layout.split_mode` selects how new or moved windows choose the next split
inside one binary space partitioning engine.

## Sources

Hyprland sources:

- [Variables](https://wiki.hypr.land/Configuring/Basics/Variables/) - `general.layout` supports
  `"dwindle"`, `"master"`, `"scrolling"`, and `"monocle"`, with `"dwindle"` as the default.
- [Layouts](https://wiki.hypr.land/Configuring/Layouts/) - layout section index.
- [Dwindle Layout](https://wiki.hypr.land/Configuring/Layouts/Dwindle-Layout/) - BSP-like tree,
  dynamic split direction, config, and layout messages.
- [Master Layout](https://wiki.hypr.land/Configuring/Layouts/Master-Layout/) - master/slave layout,
  orientation, `mfact`, stack operations, and workspace rules.
- [Scrolling Layout](https://wiki.hypr.land/Configuring/Layouts/Scrolling-Layout/) - infinite tape,
  column width, fit/focus behavior, and column commands.
- [Monocle Layout](https://wiki.hypr.land/Configuring/Layouts/Monocle-Layout/) - every tiled window
  takes the whole available area, with layout-level cycling.
- [Custom Layouts](https://wiki.hypr.land/Configuring/Layouts/Custom-Layouts/) - Lua custom layout
  registration.

win-tiler sources:

- `README.md` - project overview, config examples, split mode descriptions, and layout rule syntax.
- `src/engine.h` - `ctrl::SplitMode` enum and engine state.
- `src/engine.cpp` - split-direction selection, tree geometry, zen geometry, layout template
  application, and hotkey behavior.
- `src/options.h` / `src/options.cpp` - parsed `LayoutSplitMode`, layout rules, and config values.
- `src/loop.cpp` - session floating state and loop-side floating filter.

## Executive Summary

Hyprland currently exposes four built-in layout engines:

- `dwindle`: BSP-like tree layout.
- `master`: DWM-style master area plus slave stack.
- `scrolling`: columns on an infinitely growing tape.
- `monocle`: all windows occupy the full available area, with cycling.

win-tiler exposes three split modes inside its BSP engine:

- `dwindle`: default; chooses vertical for wide target cells and horizontal for tall target cells.
- `vertical`: always splits left/right.
- `horizontal`: always splits top/bottom.

The only close name match is Hyprland `dwindle` to win-tiler `dwindle`, and even that is not exact.
win-tiler does not currently have native equivalents for Hyprland `master`, `scrolling`, or
`monocle`. Some behavior can be approximated with declarative layout rules, zen mode, fullscreen
state, or floating windows, but those are not separate layout engines.

## Hyprland Layouts

| Hyprland layout | Core model | Notable controls | Closest win-tiler behavior |
| --- | --- | --- | --- |
| `dwindle` | BSP-like binary tree. Each workspace window is a member of the tree. By default, the split direction is derived dynamically from the parent node's width/height ratio. | `preserve_split`, `force_split`, `smart_split`, `split_width_multiplier`, `default_split_ratio`, `split_bias`, `splitratio`, `togglesplit`, `swapsplit`, `rotatesplit`, `preselect`, pseudo mode. | Closest to win-tiler `dwindle`, but Hyprland's default split direction remains dynamic unless `preserve_split` is enabled. win-tiler stores the selected split direction in the tree once the split is created. |
| `master` | One or more master windows plus a slave stack. The master area can be left, right, top, bottom, or center. | `mfact`, `new_status`, `new_on_top`, `new_on_active`, `addmaster`, `removemaster`, `swapwithmaster`, `rollnext`, `rollprev`, orientation messages, per-workspace orientation rule. | No native equivalent. Fixed win-tiler layout rules can imitate specific 2-window or 3-window master-like shapes, but there is no persistent master/slave role or stack operation model. |
| `scrolling` | Windows are arranged on an infinitely growing tape, generally as columns, and the viewport moves/fits around focus. | `column_width`, `direction`, `follow_focus`, `focus_fit_method`, `move`, `colresize`, `fit`, `promote`, `swapcol`, `expel`, `consume`. | No equivalent. win-tiler computes bounded rectangles inside monitor work areas and does not model a scrollable layout viewport. |
| `monocle` | Tiled windows take the full available area; layout messages cycle which window is active. | `cyclenext`, `cycleprev`. | Partial approximation through zen or OS fullscreen, but not the same. win-tiler zen enlarges one selected leaf to a centered percentage while the underlying BSP tree remains. |
| Custom Lua layout | User registers a layout with `hl.layout.register(name, { recalculate, layout_msg? })`, then uses it as `lua:name`. | Programmatic placement with helpers such as `column`, `row`, `grid_cell`, and `split`. | No programmable layout engine. win-tiler has static declarative templates selected by window count. |

## win-tiler Layout Behavior

win-tiler's layout core is a binary tree of cells. A split node stores:

- `SplitDir::Vertical`, which means a left/right split.
- `SplitDir::Horizontal`, which means a top/bottom split.
- `split_ratio`, clamped to `0.1` through `0.9` for interactive ratio changes.

`layout.split_mode` affects the split direction chosen when a new window is inserted or a moved
window is split into a target leaf:

| win-tiler split mode | Behavior | Hyprland comparison |
| --- | --- | --- |
| `dwindle` | Uses the target cell's current aspect ratio: wide cells split left/right; tall cells split top/bottom. | Closest to Hyprland `dwindle`, especially to a persistent-split interpretation. Hyprland default dwindle can keep recalculating split orientation from container dimensions. |
| `vertical` | Always creates left/right splits. | No separate Hyprland layout. Hyprland Dwindle can be steered with split controls, and a custom layout could implement this directly. |
| `horizontal` | Always creates top/bottom splits. | No separate Hyprland layout. Same caveat as `vertical`. |

The runtime `CycleSplitMode` action cycles:

```text
Dwindle -> Vertical -> Horizontal -> Dwindle
```

Changing split mode does not rebuild existing tree geometry by itself; it changes future split
decisions. Existing split nodes keep their stored direction and ratio unless another action or
layout template changes them.

## win-tiler Features That Are Not Split Modes

win-tiler has several layout-adjacent features that overlap with Hyprland behavior but should not be
counted as direct layout modes:

- Declarative layout rules: `[layout.rules]` are selected by managed window count and define a static
  split tree with `split = "vertical"` / `"horizontal"` and `ratio`. These can imitate fixed master
  shapes for specific window counts, but they do not create a master stack or layout-specific
  commands.
- Zen mode: selected leaf is drawn as a centered percentage of the monitor/cluster. This is closest
  to a temporary focus presentation mode, not Hyprland `monocle`.
- Fullscreen awareness: the engine tracks fullscreen state so it can avoid conflicting tiling
  effects. This is OS/window state, not a tiling mode.
- Floating toggle: the loop keeps session floating state and filters floating windows out of the
  tiling input. This is similar in spirit to floating support in tiling WMs, but it is outside the
  layout engine comparison.
- Mouse drag/drop: `loop.mouse_drag_drop` can exchange windows or split a target cell. This mutates
  the existing BSP tree; it is not a standalone layout.

## One-to-One Comparison

| Question | Answer |
| --- | --- |
| Does win-tiler have Hyprland `dwindle`? | Partially. win-tiler `dwindle` chooses split direction from target-cell aspect ratio when inserting/moving windows. Hyprland `dwindle` is also BSP-like, but its default split orientation is dynamically derived from parent dimensions unless `preserve_split` is enabled. |
| Does win-tiler have Hyprland `master`? | No. Templates can approximate fixed master-like geometries for exact window counts, but there is no master/slave stack, `mfact`, orientation cycling, or add/remove-master operation. |
| Does win-tiler have Hyprland `scrolling`? | No. win-tiler layouts are bounded to monitor work areas and do not have a scrollable tape or viewport. |
| Does win-tiler have Hyprland `monocle`? | No native monocle engine. Zen/fullscreen can approximate "focus one window" behavior, but not a full stack where all tiled windows occupy the same layout rectangle and cycle through focus. |
| Does Hyprland have win-tiler `vertical` / `horizontal`? | Not as built-in layout names. A user can steer Dwindle split direction or write a custom Lua layout, but Hyprland's built-in set is layout-engine oriented, not insertion-policy oriented. |

## Implementation Implications for win-tiler

If win-tiler wants closer Hyprland parity, the likely work is not just adding more values to
`SplitMode`. `SplitMode` is currently a policy for choosing the next split direction inside the BSP
engine. Hyprland `master`, `scrolling`, and `monocle` are different geometry engines.

Practical paths:

1. Add a real layout-engine abstraction above the existing BSP tree if `master`, `scrolling`, or
   `monocle` should behave like Hyprland modes.
2. Keep the current BSP engine and add higher-level templates if the goal is only fixed master-like
   arrangements for common window counts.
3. Treat monocle as a small first-class presentation mode if the desired behavior is "all tiled
   windows get the same cluster rectangle and navigation cycles focus."
4. Defer scrolling unless there is a strong product need. It requires a viewport/tape model that is
   structurally different from win-tiler's current bounded per-monitor geometry.

The most compatible Hyprland idea to borrow is better Dwindle configurability: options equivalent to
split bias, explicit default split ratio, and one-shot preselect direction would fit the current BSP
model better than Master or Scrolling.
