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

} // namespace

void Engine::init(const std::vector<ctrl::ClusterInitInfo>& infos) {
  system = ctrl::create_system(infos);
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

AutoZenResult Engine::update_zen_for_maximized_windows(
    const std::vector<std::vector<ManagedWindowState>>& windows,
    bool has_completed_initial_tile_pass) {
  AutoZenResult result;
  result.initial_tile_pass_completed = true;

  if (!has_completed_initial_tile_pass) {
    return result;
  }

  for (const auto& monitor_windows : windows) {
    for (const auto& window : monitor_windows) {
      if (window.leaf_id == 0) {
        continue;
      }
      if (!window.is_maximized || window.is_fullscreen) {
        continue;
      }

      auto cell = find_cell_by_leaf_id(system, window.leaf_id);
      if (!cell.has_value()) {
        continue;
      }

      auto& cluster = system.clusters[static_cast<size_t>(cell->cluster_index)];
      if (cluster.has_fullscreen_cell) {
        continue;
      }
      if (cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == cell->cell_index) {
        ctrl::clear_zen(system, cell->cluster_index);
        result.layout_changed = true;
        continue;
      }

      bool set_zen_result = ctrl::set_zen(system, cell->cluster_index, cell->cell_index);
      if (!set_zen_result) {
        spdlog::error("Failed to set zen for maximized window {}", window.leaf_id);
        continue;
      }

      result.layout_changed = true;
    }
  }

  result.apply_tiles = result.layout_changed;
  return result;
}

void Engine::store_selected_cell() {
  if (system.selection.has_value()) {
    const auto& cluster = system.clusters[static_cast<size_t>(system.selection->cluster_index)];
    const auto& cell_data = cluster.tree[system.selection->cell_index];
    if (cell_data.leaf_id.has_value()) {
      stored_cell =
          StoredCell{static_cast<size_t>(system.selection->cluster_index), *cell_data.leaf_id};
    }
  }
}

void Engine::clear_stored_cell() {
  stored_cell.reset();
}

std::optional<int> Engine::get_selected_sibling_index() const {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }
  int ci = system.selection->cluster_index;
  int cell_idx = system.selection->cell_index;
  if (ci < 0 || static_cast<size_t>(ci) >= system.clusters.size()) {
    return std::nullopt;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(ci)];
  return cluster.tree.get_sibling(cell_idx);
}

std::optional<size_t> Engine::get_selected_sibling_leaf_id() const {
  auto sibling_idx = get_selected_sibling_index();
  if (!sibling_idx.has_value() || !system.selection.has_value()) {
    return std::nullopt;
  }

  int ci = system.selection->cluster_index;
  if (ci < 0 || static_cast<size_t>(ci) >= system.clusters.size()) {
    return std::nullopt;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(ci)];
  if (!cluster.tree.is_leaf(*sibling_idx)) {
    return std::nullopt;
  }

  return cluster.tree[*sibling_idx].leaf_id;
}

std::optional<ctrl::DropMoveResult>
Engine::perform_drop_move(size_t source_leaf_id, float cursor_x, float cursor_y,
                          const std::vector<std::vector<ctrl::Rect>>& geometries,
                          bool do_exchange) {
  return ctrl::perform_drop_move(system, source_leaf_id, cursor_x, cursor_y, geometries,
                                 do_exchange);
}

bool Engine::handle_resize(int cluster_index, size_t leaf_id, const ctrl::Rect& actual_rect,
                           const std::vector<ctrl::Rect>& cluster_geometry) {
  return ctrl::update_split_ratio_from_resize(system, cluster_index, leaf_id, actual_rect,
                                              cluster_geometry);
}

DragResult Engine::process_completed_drag(const CompletedDragRequest& request,
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
  result.cursor_pos = drop_result->cursor_pos;
  return result;
}

std::optional<ctrl::Point>
Engine::get_selected_center(const std::vector<std::vector<ctrl::Rect>>& geometries) const {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int ci = system.selection->cluster_index;
  int cell_idx = system.selection->cell_index;

  if (ci < 0 || static_cast<size_t>(ci) >= geometries.size()) {
    return std::nullopt;
  }
  if (cell_idx < 0 || static_cast<size_t>(cell_idx) >= geometries[static_cast<size_t>(ci)].size()) {
    return std::nullopt;
  }

  const auto& rect = geometries[static_cast<size_t>(ci)][static_cast<size_t>(cell_idx)];
  return ctrl::get_rect_center(rect);
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
      result.cursor_pos = get_selected_center(global_geometries);
    }
    break;

  case HotkeyAction::NavigateDown:
    spdlog::info("NavigateDown: moving selection downward");
    if (ctrl::move_selection(system, ctrl::Direction::Down, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(global_geometries);
    }
    break;

  case HotkeyAction::NavigateUp:
    spdlog::info("NavigateUp: moving selection upward");
    if (ctrl::move_selection(system, ctrl::Direction::Up, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(global_geometries);
    }
    break;

  case HotkeyAction::NavigateRight:
    spdlog::info("NavigateRight: moving selection to the right");
    if (ctrl::move_selection(system, ctrl::Direction::Right, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(global_geometries);
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
    store_selected_cell();
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
      if (auto sibling_idx = get_selected_sibling_index()) {
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
