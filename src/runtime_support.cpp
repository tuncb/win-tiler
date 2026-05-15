#include "runtime_support.h"

#include <algorithm>

namespace wintiler {

namespace {

void apply_tile_positions_impl(const ctrl::System& system,
                               const std::vector<std::vector<ctrl::Rect>>& geometries,
                               const std::vector<size_t>* leaf_ids) {
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& cluster = system.clusters[ci];
    if (cluster.has_fullscreen_cell) {
      continue;
    }

    if (ci >= geometries.size()) {
      continue;
    }
    const auto& rects = geometries[ci];

    for (int i = 0; i < cluster.tree.size(); ++i) {
      if (!cluster.tree.is_leaf(i)) {
        continue;
      }
      const auto& cell_data = cluster.tree[i];
      if (!cell_data.leaf_id.has_value()) {
        continue;
      }

      if (leaf_ids != nullptr &&
          std::find(leaf_ids->begin(), leaf_ids->end(), *cell_data.leaf_id) == leaf_ids->end()) {
        continue;
      }

      if (static_cast<size_t>(i) >= rects.size()) {
        continue;
      }
      const auto& rect = rects[static_cast<size_t>(i)];

      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(*cell_data.leaf_id);
      winapi::WindowPosition pos{static_cast<int>(rect.x), static_cast<int>(rect.y),
                                 static_cast<int>(rect.width), static_cast<int>(rect.height)};
      winapi::TileInfo tile_info{hwnd, pos};
      winapi::update_window_position(tile_info);
    }
  }
}

} // namespace

std::vector<ctrl::ClusterCellUpdateInfo>
extract_cluster_updates_from_input(const winapi::LoopInputState& input_state) {
  std::vector<ctrl::ClusterCellUpdateInfo> result;
  result.reserve(input_state.windows_per_monitor.size());

  for (const auto& windows : input_state.windows_per_monitor) {
    std::vector<size_t> cell_ids;
    cell_ids.reserve(windows.size());
    bool has_fullscreen = false;
    for (const auto& win : windows) {
      cell_ids.push_back(reinterpret_cast<size_t>(win.handle));
      if (win.is_fullscreen) {
        has_fullscreen = true;
      }
    }
    result.push_back({cell_ids, has_fullscreen});
  }

  return result;
}

std::vector<std::vector<ManagedWindowState>>
extract_managed_window_states_from_input(const winapi::LoopInputState& input_state) {
  std::vector<std::vector<ManagedWindowState>> result;
  result.reserve(input_state.windows_per_monitor.size());

  for (const auto& windows : input_state.windows_per_monitor) {
    std::vector<ManagedWindowState> monitor_state;
    monitor_state.reserve(windows.size());
    for (const auto& win : windows) {
      if (win.handle == nullptr) {
        continue;
      }
      std::optional<ctrl::Rect> actual_rect;
      if (win.actual_rect.has_value()) {
        actual_rect = ctrl::Rect{static_cast<float>(win.actual_rect->x),
                                 static_cast<float>(win.actual_rect->y),
                                 static_cast<float>(win.actual_rect->width),
                                 static_cast<float>(win.actual_rect->height)};
      }
      monitor_state.push_back({reinterpret_cast<size_t>(win.handle), win.is_fullscreen,
                               win.is_maximized, win.is_minimized, actual_rect});
    }
    result.push_back(std::move(monitor_state));
  }

  return result;
}

std::vector<ctrl::ClusterInitInfo>
create_cluster_infos_from_monitors(const std::vector<winapi::MonitorInfo>& monitors,
                                   const GlobalOptions& options) {
  std::vector<ctrl::ClusterInitInfo> cluster_infos;
  cluster_infos.reserve(monitors.size());

  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& monitor = monitors[i];
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);
    float mx = static_cast<float>(monitor.rect.left);
    float my = static_cast<float>(monitor.rect.top);
    float mw = static_cast<float>(monitor.rect.right - monitor.rect.left);
    float mh = static_cast<float>(monitor.rect.bottom - monitor.rect.top);

    auto hwnds = winapi::get_hwnds_for_monitor(i, options.ignoreOptions);
    std::vector<size_t> cell_ids;
    cell_ids.reserve(hwnds.size());
    for (auto hwnd : hwnds) {
      cell_ids.push_back(reinterpret_cast<size_t>(hwnd));
    }

    auto layout_rule = find_layout_rule_for_window_count(options.layoutOptions, cell_ids.size());
    cluster_infos.push_back({x, y, w, h, mx, my, mw, mh, cell_ids, layout_rule});
  }

  return cluster_infos;
}

void initialize_engine_from_monitors(Engine& engine,
                                     const std::vector<winapi::MonitorInfo>& monitors,
                                     const GlobalOptions& options) {
  auto cluster_infos = create_cluster_infos_from_monitors(monitors, options);
  engine.init(cluster_infos);
}

void apply_tile_positions(const ctrl::System& system,
                          const std::vector<std::vector<ctrl::Rect>>& geometries) {
  apply_tile_positions_impl(system, geometries, nullptr);
}

void apply_tile_positions_for_leaf_ids(const ctrl::System& system,
                                       const std::vector<std::vector<ctrl::Rect>>& geometries,
                                       const std::vector<size_t>& leaf_ids) {
  apply_tile_positions_impl(system, geometries, &leaf_ids);
}

} // namespace wintiler
