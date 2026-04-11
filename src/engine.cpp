#include "engine.h"

#include <algorithm>
#include <cmath>
#include <magic_enum/magic_enum.hpp>

#include "spdlog/spdlog.h"

namespace wintiler {

namespace {

// Find the cluster and cell index at a global point using precomputed geometries
std::optional<std::pair<size_t, int>>
find_cell_at_global_point(const ctrl::System& system,
                          const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                          float global_x, float global_y) {
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    if (cluster_idx >= global_geometries.size()) {
      continue;
    }
    const auto& cluster = system.clusters[cluster_idx];
    const auto& rects = global_geometries[cluster_idx];

    if (cluster.zen_cell_index.has_value()) {
      int zen_idx = *cluster.zen_cell_index;
      if (!cluster.tree.is_valid_index(zen_idx) || !cluster.tree.is_leaf(zen_idx) ||
          static_cast<size_t>(zen_idx) >= rects.size()) {
        continue;
      }

      const auto& zen_rect = rects[static_cast<size_t>(zen_idx)];
      if (global_x >= zen_rect.x && global_x < zen_rect.x + zen_rect.width &&
          global_y >= zen_rect.y && global_y < zen_rect.y + zen_rect.height) {
        return std::make_pair(cluster_idx, zen_idx);
      }
      continue;
    }

    for (int i = 0; i < static_cast<int>(rects.size()); ++i) {
      // Skip non-leaf cells
      if (!cluster.tree.is_leaf(i)) {
        continue;
      }

      const auto& r = rects[static_cast<size_t>(i)];
      if (global_x >= r.x && global_x < r.x + r.width && global_y >= r.y &&
          global_y < r.y + r.height) {
        return std::make_pair(cluster_idx, i);
      }
    }
  }
  return std::nullopt;
}

// Find which cluster contains a global point (for empty cluster hover detection)
std::optional<size_t> find_cluster_at_global_point(const ctrl::System& system, float global_x,
                                                   float global_y) {
  for (size_t i = 0; i < system.clusters.size(); ++i) {
    const auto& cluster = system.clusters[i];
    if (global_x >= cluster.global_x && global_x < cluster.global_x + cluster.window_width &&
        global_y >= cluster.global_y && global_y < cluster.global_y + cluster.window_height) {
      return i;
    }
  }
  return std::nullopt;
}

bool selections_equal(const std::optional<ctrl::CellIndicatorByIndex>& lhs,
                      const std::optional<ctrl::CellIndicatorByIndex>& rhs) {
  if (!lhs.has_value() && !rhs.has_value()) {
    return true;
  }
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  return lhs->cluster_index == rhs->cluster_index && lhs->cell_index == rhs->cell_index;
}

std::optional<size_t> get_selected_leaf_id(const ctrl::System& system) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return std::nullopt;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return std::nullopt;
  }

  return cluster.tree[cell_index].leaf_id;
}

std::optional<ctrl::CellIndicatorByIndex> find_cell_by_leaf_id(const ctrl::System& system,
                                                               size_t leaf_id) {
  for (size_t cluster_index = 0; cluster_index < system.clusters.size(); ++cluster_index) {
    auto cell_index = ctrl::find_cell_by_leaf_id(system.clusters[cluster_index], leaf_id);
    if (cell_index.has_value()) {
      return ctrl::CellIndicatorByIndex{static_cast<int>(cluster_index), *cell_index};
    }
  }
  return std::nullopt;
}

struct AutoZenResult {
  bool layout_changed = false;
  bool apply_tiles = false;
  bool initial_tile_pass_completed = false;
};

struct DragResult {
  bool handled = false;
  bool selection_changed = false;
  bool layout_changed = false;
  bool apply_tiles = false;
  bool clear_drag_ended = false;
  std::optional<size_t> cursor_leaf_id;
};

void store_selected_cell(const ctrl::System& system, std::optional<StoredCell>& stored_cell) {
  if (!system.selection.has_value()) {
    return;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return;
  }

  const auto& cell_data = cluster.tree[cell_index];
  if (cell_data.leaf_id.has_value()) {
    stored_cell = StoredCell{static_cast<size_t>(cluster_index), *cell_data.leaf_id};
  }
}

std::optional<int> get_selected_sibling_index(const ctrl::System& system) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return std::nullopt;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  return cluster.tree.get_sibling(cell_index);
}

std::optional<ctrl::Point>
get_selected_center(const ctrl::System& system,
                    const std::vector<std::vector<ctrl::Rect>>& geometries) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= geometries.size()) {
    return std::nullopt;
  }
  if (cell_index < 0 ||
      static_cast<size_t>(cell_index) >= geometries[static_cast<size_t>(cluster_index)].size()) {
    return std::nullopt;
  }

  const auto& rect =
      geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(cell_index)];
  return ctrl::get_rect_center(rect);
}

std::optional<ctrl::Point> get_leaf_center(const ctrl::System& system, size_t leaf_id,
                                           const std::vector<std::vector<ctrl::Rect>>& geometries) {
  auto cell = find_cell_by_leaf_id(system, leaf_id);
  if (!cell.has_value()) {
    return std::nullopt;
  }

  int cluster_index = cell->cluster_index;
  int cell_index = cell->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= geometries.size()) {
    return std::nullopt;
  }
  if (cell_index < 0 ||
      static_cast<size_t>(cell_index) >= geometries[static_cast<size_t>(cluster_index)].size()) {
    return std::nullopt;
  }

  return ctrl::get_rect_center(
      geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(cell_index)]);
}

void ensure_maximized_tracking_size(std::vector<std::optional<size_t>>& previous_maximized_leaf_ids,
                                    size_t cluster_count) {
  previous_maximized_leaf_ids.resize(cluster_count);
}

AutoZenResult
update_zen_for_maximized_windows(ctrl::System& system,
                                 std::vector<std::optional<size_t>>& previous_maximized_leaf_ids,
                                 const std::vector<std::vector<ManagedWindowState>>& windows,
                                 bool has_completed_initial_tile_pass) {
  AutoZenResult result;
  result.initial_tile_pass_completed = true;
  ensure_maximized_tracking_size(previous_maximized_leaf_ids, system.clusters.size());

  std::vector<std::optional<size_t>> current_maximized_leaf_ids(system.clusters.size());
  size_t cluster_count = std::min(system.clusters.size(), windows.size());
  for (size_t cluster_index = 0; cluster_index < cluster_count; ++cluster_index) {
    const auto& cluster = system.clusters[cluster_index];
    if (cluster.has_fullscreen_cell) {
      continue;
    }

    for (const auto& window : windows[cluster_index]) {
      if (window.leaf_id == 0) {
        continue;
      }
      if (!window.is_maximized || window.is_fullscreen) {
        continue;
      }

      current_maximized_leaf_ids[cluster_index] = window.leaf_id;
      break;
    }
  }

  if (!has_completed_initial_tile_pass) {
    previous_maximized_leaf_ids = std::move(current_maximized_leaf_ids);
    return result;
  }

  for (size_t cluster_index = 0; cluster_index < system.clusters.size(); ++cluster_index) {
    auto& cluster = system.clusters[cluster_index];
    std::optional<size_t> current_leaf_id = current_maximized_leaf_ids[cluster_index];
    if (!current_leaf_id.has_value()) {
      previous_maximized_leaf_ids[cluster_index].reset();
      continue;
    }

    if (previous_maximized_leaf_ids[cluster_index].has_value() &&
        *previous_maximized_leaf_ids[cluster_index] == *current_leaf_id) {
      continue;
    }

    auto target_cell_index = ctrl::find_cell_by_leaf_id(cluster, *current_leaf_id);
    if (!target_cell_index.has_value()) {
      previous_maximized_leaf_ids[cluster_index] = current_leaf_id;
      continue;
    }

    bool zen_changed = false;
    if (cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == *target_cell_index) {
      ctrl::clear_zen(system, static_cast<int>(cluster_index));
      zen_changed = true;
    } else {
      bool set_zen_result =
          ctrl::set_zen(system, static_cast<int>(cluster_index), *target_cell_index);
      if (!set_zen_result) {
        spdlog::error("Failed to set zen for maximized window {}", *current_leaf_id);
        previous_maximized_leaf_ids[cluster_index] = current_leaf_id;
        continue;
      }
      zen_changed = true;
    }

    if (zen_changed) {
      result.layout_changed = true;
    }

    previous_maximized_leaf_ids[cluster_index] = current_leaf_id;
  }

  result.apply_tiles = result.layout_changed;
  return result;
}

DragResult process_completed_drag(ctrl::System& system, const CompletedDragRequest& request,
                                  const std::vector<std::vector<ctrl::Rect>>& geometries) {
  DragResult result;
  result.clear_drag_ended = true;

  auto previous_selection = system.selection;
  if (!ctrl::has_leaf_id(system, request.leaf_id)) {
    return result;
  }

  auto cell = find_cell_by_leaf_id(system, request.leaf_id);
  if (!cell.has_value()) {
    return result;
  }

  int cluster_index = cell->cluster_index;
  int cell_index = cell->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return result;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];

  bool can_try_resize =
      request.actual_window_rect.has_value() && !cluster.has_fullscreen_cell &&
      !cluster.zen_cell_index.has_value() &&
      static_cast<size_t>(cluster_index) < geometries.size() &&
      static_cast<size_t>(cell_index) < geometries[static_cast<size_t>(cluster_index)].size();

  if (can_try_resize) {
    const auto& expected_rect =
        geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(cell_index)];
    const auto& actual_rect = *request.actual_window_rect;
    bool size_changed = (std::abs(actual_rect.width - expected_rect.width) > 2.0f ||
                         std::abs(actual_rect.height - expected_rect.height) > 2.0f);
    if (size_changed) {
      result.handled =
          ctrl::update_split_ratio_from_resize(system, cluster_index, request.leaf_id, actual_rect,
                                               geometries[static_cast<size_t>(cluster_index)]);
      if (result.handled) {
        result.layout_changed = true;
        result.apply_tiles = true;
        spdlog::info("Window resize: updated split ratio for cluster {}, leaf_id {}", cluster_index,
                     request.leaf_id);
        result.selection_changed = !selections_equal(previous_selection, system.selection);
        return result;
      }
    }
  }

  if (!request.cursor_pos.has_value()) {
    return result;
  }

  auto drop_result = ctrl::perform_drop_move(
      system, request.leaf_id, static_cast<float>(request.cursor_pos->x),
      static_cast<float>(request.cursor_pos->y), geometries, request.do_exchange);
  if (!drop_result.has_value()) {
    result.selection_changed = !selections_equal(previous_selection, system.selection);
    return result;
  }

  result.handled = true;
  result.selection_changed = !selections_equal(previous_selection, system.selection);
  result.layout_changed = true;
  result.apply_tiles = true;
  result.cursor_leaf_id = request.leaf_id;
  return result;
}

} // namespace

void Engine::init(const std::vector<ctrl::ClusterInitInfo>& infos) {
  system = ctrl::create_system(infos);
  previous_maximized_leaf_ids.assign(system.clusters.size(), std::nullopt);
}

std::vector<std::vector<ctrl::Rect>> Engine::compute_geometries(float gap_h, float gap_v,
                                                                float zen_pct) const {
  std::vector<std::vector<ctrl::Rect>> geometries;
  geometries.reserve(system.clusters.size());
  for (const auto& cluster : system.clusters) {
    geometries.push_back(ctrl::compute_cluster_geometry(cluster, gap_h, gap_v, zen_pct));
  }
  return geometries;
}

HoverInfo
Engine::get_hover_info(float global_x, float global_y,
                       const std::vector<std::vector<ctrl::Rect>>& global_geometries) const {
  HoverInfo info;
  info.cluster_index = find_cluster_at_global_point(system, global_x, global_y);

  auto cell_at_mouse = find_cell_at_global_point(system, global_geometries, global_x, global_y);
  if (cell_at_mouse.has_value()) {
    auto [cluster_index, cell_index] = *cell_at_mouse;
    info.cell = ctrl::CellIndicatorByIndex{static_cast<int>(cluster_index), cell_index};
  }
  return info;
}

UpdateResult Engine::update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                            std::optional<int> redirect_cluster_index) {
  UpdateResult result;
  auto previous_selection = system.selection;
  std::vector<bool> previous_fullscreen_state;
  previous_fullscreen_state.reserve(system.clusters.size());
  for (const auto& cluster : system.clusters) {
    previous_fullscreen_state.push_back(cluster.has_fullscreen_cell);
  }

  result.topology_changed = ctrl::update(system, cluster_updates, redirect_cluster_index);
  result.selection_changed = !selections_equal(previous_selection, system.selection);

  bool fullscreen_state_changed = false;
  for (size_t i = 0; i < system.clusters.size() && i < previous_fullscreen_state.size(); ++i) {
    if (previous_fullscreen_state[i] != system.clusters[i].has_fullscreen_cell) {
      fullscreen_state_changed = true;
      break;
    }
  }

  result.layout_changed = result.topology_changed || fullscreen_state_changed;
  result.apply_tiles = result.layout_changed;
  return result;
}

HoverSelectionResult
Engine::update_selection_from_hover(float global_x, float global_y,
                                    const std::vector<std::vector<ctrl::Rect>>& global_geometries) {
  HoverSelectionResult result;
  auto previous_selection = system.selection;

  auto hover_info = get_hover_info(global_x, global_y, global_geometries);
  if (hover_info.cell.has_value()) {
    system.selection = *hover_info.cell;
  }

  result.selection_changed = !selections_equal(previous_selection, system.selection);
  return result;
}

EngineFrameOutput Engine::process_frame(const EngineFrameInput& input) {
  EngineFrameOutput output;
  output.has_completed_initial_tile_pass = input.has_completed_initial_tile_pass;

  if (!input.has_completed_initial_tile_pass) {
    output.apply_tiles = true;
    output.has_completed_initial_tile_pass = true;
  }

  std::vector<std::vector<ctrl::Rect>> geometries;
  bool has_geometries = false;
  bool geometries_dirty = false;
  std::optional<size_t> drag_cursor_leaf_id;

  auto ensure_geometries = [&]() -> const std::vector<std::vector<ctrl::Rect>>& {
    if (!has_geometries || geometries_dirty) {
      geometries = compute_geometries(input.gap_h, input.gap_v, input.zen_pct);
      has_geometries = true;
      geometries_dirty = false;
    }
    return geometries;
  };

  auto mark_geometries_dirty = [&]() { geometries_dirty = true; };

  if (input.completed_drag.has_value()) {
    DragResult drag_result =
        process_completed_drag(system, *input.completed_drag, ensure_geometries());
    output.clear_drag_ended = drag_result.clear_drag_ended;
    output.selection_changed = output.selection_changed || drag_result.selection_changed;
    output.layout_changed = output.layout_changed || drag_result.layout_changed;
    output.apply_tiles = output.apply_tiles || drag_result.apply_tiles;
    if (drag_result.cursor_leaf_id.has_value()) {
      drag_cursor_leaf_id = drag_result.cursor_leaf_id;
    }
    if (drag_result.layout_changed) {
      mark_geometries_dirty();
    }
  }

  if (input.hotkey_action.has_value()) {
    ActionResult action_result = process_action(*input.hotkey_action, ensure_geometries(),
                                                input.gap_h, input.gap_v, input.zen_pct);
    output.control = action_result.control;
    output.selection_changed = output.selection_changed || action_result.selection_changed;
    output.layout_changed = output.layout_changed || action_result.layout_changed;
    output.apply_tiles = output.apply_tiles || action_result.apply_tiles;
    if (action_result.focus_leaf_id.has_value()) {
      output.focus_leaf_id = action_result.focus_leaf_id;
    }
    if (action_result.cursor_pos.has_value()) {
      output.cursor_pos = action_result.cursor_pos;
    }
    if (action_result.toast_message.has_value()) {
      output.toast_message = action_result.toast_message;
    }
    if (action_result.layout_changed) {
      mark_geometries_dirty();
    }
    if (output.control != LoopControl::Continue) {
      output.geometries = ensure_geometries();
      return output;
    }
  }

  std::optional<int> redirect_cluster_index;
  if (input.cursor_pos.has_value()) {
    auto hover_cluster_index = find_cluster_at_global_point(
        system, static_cast<float>(input.cursor_pos->x), static_cast<float>(input.cursor_pos->y));
    if (hover_cluster_index.has_value()) {
      size_t hover_idx = *hover_cluster_index;
      if (hover_idx < system.clusters.size() &&
          ctrl::get_cluster_leaf_ids(system.clusters[hover_idx]).empty()) {
        redirect_cluster_index = static_cast<int>(hover_idx);
      }
    }
  }
  if (!redirect_cluster_index.has_value() && system.selection.has_value()) {
    redirect_cluster_index = system.selection->cluster_index;
  }

  UpdateResult update_result = update(input.cluster_updates, redirect_cluster_index);
  output.topology_changed = output.topology_changed || update_result.topology_changed;
  output.selection_changed = output.selection_changed || update_result.selection_changed;
  output.layout_changed = output.layout_changed || update_result.layout_changed;
  output.apply_tiles = output.apply_tiles || update_result.apply_tiles;
  if (update_result.layout_changed) {
    mark_geometries_dirty();
  }
  if (update_result.topology_changed) {
    if (update_result.cursor_pos.has_value()) {
      output.cursor_pos = update_result.cursor_pos;
    } else if (auto center = get_selected_center(system, ensure_geometries())) {
      output.cursor_pos = center;
    }
  }

  if (input.auto_zen_on_maximize) {
    AutoZenResult zen_result =
        update_zen_for_maximized_windows(system, previous_maximized_leaf_ids, input.managed_windows,
                                         input.has_completed_initial_tile_pass);
    output.has_completed_initial_tile_pass = zen_result.initial_tile_pass_completed;
    output.layout_changed = output.layout_changed || zen_result.layout_changed;
    output.apply_tiles = output.apply_tiles || zen_result.apply_tiles;
    if (zen_result.layout_changed) {
      mark_geometries_dirty();
    }
  }

  if (input.update_hover_selection && input.cursor_pos.has_value() && !output.selection_changed &&
      !output.cursor_pos.has_value()) {
    HoverSelectionResult hover_result =
        update_selection_from_hover(static_cast<float>(input.cursor_pos->x),
                                    static_cast<float>(input.cursor_pos->y), ensure_geometries());
    output.selection_changed = output.selection_changed || hover_result.selection_changed;
  }

  if (!output.cursor_pos.has_value() && drag_cursor_leaf_id.has_value()) {
    output.cursor_pos = get_leaf_center(system, *drag_cursor_leaf_id, ensure_geometries());
  }

  output.geometries = ensure_geometries();
  return output;
}

void Engine::clear_stored_cell() {
  stored_cell.reset();
}

ActionResult Engine::process_action(HotkeyAction action,
                                    const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                                    float gap_h, float gap_v, float zen_pct) {
  ActionResult result;

  switch (action) {
  case HotkeyAction::NavigateLeft:
    spdlog::info("NavigateLeft: moving selection to the left");
    if (ctrl::move_selection(system, ctrl::Direction::Left, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::NavigateDown:
    spdlog::info("NavigateDown: moving selection downward");
    if (ctrl::move_selection(system, ctrl::Direction::Down, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::NavigateUp:
    spdlog::info("NavigateUp: moving selection upward");
    if (ctrl::move_selection(system, ctrl::Direction::Up, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::NavigateRight:
    spdlog::info("NavigateRight: moving selection to the right");
    if (ctrl::move_selection(system, ctrl::Direction::Right, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::ToggleSplit:
    spdlog::info("ToggleSplit: toggling split direction of selected cell");
    result.success = ctrl::toggle_selected_split_dir(system);
    result.layout_changed = result.success;
    result.apply_tiles = result.success;
    if (!result.success) {
      spdlog::trace("Failed to toggle split direction");
    }
    break;

  case HotkeyAction::StoreCell:
    spdlog::info("StoreCell: storing current cell for swap/move operation");
    store_selected_cell(system, stored_cell);
    result.success = stored_cell.has_value();
    break;

  case HotkeyAction::ClearStored:
    spdlog::info("ClearStored: clearing stored cell reference");
    clear_stored_cell();
    result.success = true;
    break;

  case HotkeyAction::Exchange:
    spdlog::info("Exchange: swapping stored cell with selected cell");
    if (stored_cell.has_value() && system.selection.has_value()) {
      // Find stored cell index from leaf_id
      auto stored_cell_idx = ctrl::find_cell_by_leaf_id(system.clusters[stored_cell->cluster_index],
                                                        stored_cell->leaf_id);
      if (stored_cell_idx.has_value()) {
        if (ctrl::swap_cells(system, system.selection->cluster_index, system.selection->cell_index,
                             static_cast<int>(stored_cell->cluster_index), *stored_cell_idx)) {
          clear_stored_cell();
          result.success = true;
          result.layout_changed = true;
          result.apply_tiles = true;
        }
      }
    }
    break;

  case HotkeyAction::Move:
    spdlog::info("Move: moving stored cell to selected cell's position");
    if (stored_cell.has_value() && system.selection.has_value()) {
      // Find stored cell index from leaf_id
      auto stored_cell_idx = ctrl::find_cell_by_leaf_id(system.clusters[stored_cell->cluster_index],
                                                        stored_cell->leaf_id);
      if (stored_cell_idx.has_value()) {
        if (ctrl::move_cell(system, static_cast<int>(stored_cell->cluster_index), *stored_cell_idx,
                            system.selection->cluster_index, system.selection->cell_index)) {
          clear_stored_cell();
          result.success = true;
          result.layout_changed = true;
          result.apply_tiles = true;
        }
      }
    }
    break;

  case HotkeyAction::SplitIncrease:
    spdlog::info("SplitIncrease: increasing split ratio by 5%%");
    if (ctrl::adjust_selected_split_ratio(system, 0.05f)) {
      result.success = true;
      result.layout_changed = true;
      result.selection_changed = true;
      result.apply_tiles = true;
      // Recompute geometry for the affected cluster to get updated center
      if (system.selection.has_value()) {
        int ci = system.selection->cluster_index;
        if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
          auto updated_geom = ctrl::compute_cluster_geometry(
              system.clusters[static_cast<size_t>(ci)], gap_h, gap_v, zen_pct);
          int cell_idx = system.selection->cell_index;
          if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
            result.cursor_pos = ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
          }
        }
      }
    }
    break;

  case HotkeyAction::SplitDecrease:
    spdlog::info("SplitDecrease: decreasing split ratio by 5%%");
    if (ctrl::adjust_selected_split_ratio(system, -0.05f)) {
      result.success = true;
      result.layout_changed = true;
      result.selection_changed = true;
      result.apply_tiles = true;
      // Recompute geometry for the affected cluster to get updated center
      if (system.selection.has_value()) {
        int ci = system.selection->cluster_index;
        if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
          auto updated_geom = ctrl::compute_cluster_geometry(
              system.clusters[static_cast<size_t>(ci)], gap_h, gap_v, zen_pct);
          int cell_idx = system.selection->cell_index;
          if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
            result.cursor_pos = ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
          }
        }
      }
    }
    break;

  case HotkeyAction::ExchangeSiblings:
    spdlog::info("ExchangeSiblings: exchanging selected cell with its sibling");
    if (system.selection.has_value()) {
      if (auto sibling_idx = get_selected_sibling_index(system)) {
        if (ctrl::swap_cells(system, system.selection->cluster_index, system.selection->cell_index,
                             system.selection->cluster_index, *sibling_idx)) {
          result.success = true;
          result.layout_changed = true;
          result.selection_changed = true;
          result.apply_tiles = true;
          // Recompute geometry to get updated center
          int ci = system.selection->cluster_index;
          if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
            auto updated_geom = ctrl::compute_cluster_geometry(
                system.clusters[static_cast<size_t>(ci)], gap_h, gap_v, zen_pct);
            int cell_idx = system.selection->cell_index;
            if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
              result.cursor_pos =
                  ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
            }
          }
        }
      }
    }
    break;

  case HotkeyAction::ToggleZen:
    spdlog::info("ToggleZen: toggling zen mode for selected cell");
    result.success = ctrl::toggle_selected_zen(system);
    result.layout_changed = result.success;
    result.apply_tiles = result.success;
    if (!result.success) {
      spdlog::error("ToggleZen: failed to toggle zen mode");
    }
    break;

  case HotkeyAction::CycleSplitMode:
    result.success = ctrl::cycle_split_mode(system);
    if (!result.success) {
      spdlog::error("CycleSplitMode: failed to cycle split mode");
    } else {
      const auto mode_name = magic_enum::enum_name(system.split_mode);
      result.toast_message = std::string("Split mode: ").append(mode_name.data(), mode_name.size());
      spdlog::info("CycleSplitMode: switched to {}", mode_name);
    }
    break;

  case HotkeyAction::ResetSplitRatio:
    spdlog::info("ResetSplitRatio: resetting split ratio of parent to 50%%");
    if (ctrl::set_selected_split_ratio(system, 0.5f)) {
      result.success = true;
      result.layout_changed = true;
      result.selection_changed = true;
      result.apply_tiles = true;
      // Recompute geometry to get updated center
      if (system.selection.has_value()) {
        int ci = system.selection->cluster_index;
        if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
          auto updated_geom = ctrl::compute_cluster_geometry(
              system.clusters[static_cast<size_t>(ci)], gap_h, gap_v, zen_pct);
          int cell_idx = system.selection->cell_index;
          if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
            result.cursor_pos = ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
          }
        }
      }
    }
    break;

  case HotkeyAction::Exit:
    spdlog::info("Exit: exit requested");
    result.success = true;
    result.control = LoopControl::Exit;
    break;

  case HotkeyAction::TogglePause:
    result.success = true;
    result.control = LoopControl::EnterManualPause;
    break;
  }

  if (result.success && result.selection_changed) {
    result.focus_leaf_id = get_selected_leaf_id(system);
  }

  return result;
}

} // namespace wintiler
