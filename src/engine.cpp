#include "engine.h"

#include <algorithm>
#include <magic_enum/magic_enum.hpp>

#include "spdlog/spdlog.h"

namespace wintiler {

namespace {

// Convert local geometries to global coordinates
std::vector<std::vector<ctrl::Rect>>
to_global_geometries(const ctrl::System& system,
                     const std::vector<std::vector<ctrl::Rect>>& local_geometries) {
  std::vector<std::vector<ctrl::Rect>> result;
  result.reserve(system.clusters.size());

  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& cluster = system.clusters[ci];
    const auto& local_rects = local_geometries[ci];
    std::vector<ctrl::Rect> global_rects;
    global_rects.reserve(local_rects.size());

    for (const auto& r : local_rects) {
      global_rects.push_back({cluster.global_x + r.x, cluster.global_y + r.y, r.width, r.height});
    }
    result.push_back(std::move(global_rects));
  }
  return result;
}

// Find the cluster and cell index at a global point using precomputed geometries
std::optional<std::pair<size_t, int>>
find_cell_at_global_point(const ctrl::System& system,
                          const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                          float global_x, float global_y) {
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    if (cluster_idx >= global_geometries.size()) {
      continue;
    }
    const auto& rects = global_geometries[cluster_idx];

    for (int i = 0; i < static_cast<int>(rects.size()); ++i) {
      const auto& r = rects[static_cast<size_t>(i)];
      // Skip non-leaf cells (they have zero size in geometry)
      if (r.width <= 0.0f || r.height <= 0.0f) {
        continue;
      }

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

} // namespace

void Engine::init(const std::vector<ctrl::ClusterInitInfo>& infos) {
  system = ctrl::create_system(infos);
}

std::vector<std::vector<ctrl::Rect>> Engine::compute_geometries(float gap_h, float gap_v,
                                                                float zen_pct) const {
  std::vector<std::vector<ctrl::Rect>> local_geometries;
  local_geometries.reserve(system.clusters.size());
  for (const auto& cluster : system.clusters) {
    local_geometries.push_back(ctrl::compute_cluster_geometry(cluster, gap_h, gap_v, zen_pct));
  }
  return to_global_geometries(system, local_geometries);
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

bool Engine::update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                    std::optional<int> redirect_cluster_index) {
  return ctrl::update(system, cluster_updates, redirect_cluster_index);
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
    }
    break;

  case HotkeyAction::NavigateDown:
    spdlog::info("NavigateDown: moving selection downward");
    if (ctrl::move_selection(system, ctrl::Direction::Down, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
    }
    break;

  case HotkeyAction::NavigateUp:
    spdlog::info("NavigateUp: moving selection upward");
    if (ctrl::move_selection(system, ctrl::Direction::Up, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
    }
    break;

  case HotkeyAction::NavigateRight:
    spdlog::info("NavigateRight: moving selection to the right");
    if (ctrl::move_selection(system, ctrl::Direction::Right, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
    }
    break;

  case HotkeyAction::ToggleSplit:
    spdlog::info("ToggleSplit: toggling split direction of selected cell");
    result.success = ctrl::toggle_selected_split_dir(system);
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
        }
      }
    }
    break;

  case HotkeyAction::SplitIncrease:
    spdlog::info("SplitIncrease: increasing split ratio by 5%%");
    if (ctrl::adjust_selected_split_ratio(system, 0.05f)) {
      result.success = true;
      result.selection_changed = true;
    }
    break;

  case HotkeyAction::SplitDecrease:
    spdlog::info("SplitDecrease: decreasing split ratio by 5%%");
    if (ctrl::adjust_selected_split_ratio(system, -0.05f)) {
      result.success = true;
      result.selection_changed = true;
    }
    break;

  case HotkeyAction::ExchangeSiblings:
    spdlog::info("ExchangeSiblings: exchanging selected cell with its sibling");
    if (system.selection.has_value()) {
      if (auto sibling_idx = get_selected_sibling_index()) {
        if (ctrl::swap_cells(system, system.selection->cluster_index, system.selection->cell_index,
                             system.selection->cluster_index, *sibling_idx)) {
          result.success = true;
          result.selection_changed = true;
        }
      }
    }
    break;

  case HotkeyAction::ToggleZen:
    spdlog::info("ToggleZen: toggling zen mode for selected cell");
    result.success = ctrl::toggle_selected_zen(system);
    if (!result.success) {
      spdlog::error("ToggleZen: failed to toggle zen mode");
    }
    break;

  case HotkeyAction::CycleSplitMode:
    result.success = ctrl::cycle_split_mode(system);
    if (!result.success) {
      spdlog::error("CycleSplitMode: failed to cycle split mode");
    } else {
      spdlog::info("CycleSplitMode: switched to {}", magic_enum::enum_name(system.split_mode));
    }
    break;

  case HotkeyAction::ResetSplitRatio:
    spdlog::info("ResetSplitRatio: resetting split ratio of parent to 50%%");
    if (ctrl::set_selected_split_ratio(system, 0.5f)) {
      result.success = true;
      result.selection_changed = true;
    }
    break;

  case HotkeyAction::Exit:
    spdlog::info("Exit: exit action (not implemented in engine)");
    // Not implemented in engine
    break;
  }

  return result;
}

} // namespace wintiler
