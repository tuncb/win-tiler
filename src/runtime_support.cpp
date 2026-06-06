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
      winapi::TileInfo tile_info{hwnd, pos,
                                 leaf_ids == nullptr
                                     ? winapi::TilePlacementKind::Layout
                                     : winapi::TilePlacementKind::PlacementCorrection};
      winapi::update_window_position(tile_info);
    }
  }
}

bool monitor_profile_matches(const MonitorProfileOptions& profile,
                             const winapi::MonitorInfo& monitor, size_t monitor_index) {
  if (profile.match.device_name.has_value() && *profile.match.device_name != monitor.deviceName) {
    return false;
  }
  if (profile.match.index.has_value() && *profile.match.index != monitor_index) {
    return false;
  }
  if (profile.match.primary.has_value() && *profile.match.primary != monitor.isPrimary) {
    return false;
  }
  return true;
}

} // namespace

void extract_cluster_updates_from_input_into(const winapi::LoopInputState& input_state,
                                             std::vector<ctrl::ClusterCellUpdateInfo>& result) {
  result.resize(input_state.windows_per_monitor.size());

  for (size_t i = 0; i < input_state.windows_per_monitor.size(); ++i) {
    const auto& windows = input_state.windows_per_monitor[i];
    auto& update = result[i];
    update.leaf_ids.clear();
    auto& cell_ids = update.leaf_ids;
    cell_ids.reserve(windows.size());
    update.has_fullscreen_cell = false;
    for (const auto& win : windows) {
      cell_ids.push_back(reinterpret_cast<size_t>(win.handle));
      if (win.is_fullscreen) {
        update.has_fullscreen_cell = true;
      }
    }
  }
}

void extract_managed_window_states_from_input_into(
    const winapi::LoopInputState& input_state,
    std::vector<std::vector<ManagedWindowState>>& result) {
  result.resize(input_state.windows_per_monitor.size());

  for (size_t i = 0; i < input_state.windows_per_monitor.size(); ++i) {
    const auto& windows = input_state.windows_per_monitor[i];
    auto& monitor_state = result[i];
    monitor_state.clear();
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
      int min_track_width = 0;
      int min_track_height = 0;
      if (win.minmax_info.has_value()) {
        min_track_width = win.minmax_info->min_track_width;
        min_track_height = win.minmax_info->min_track_height;
      }
      monitor_state.push_back({reinterpret_cast<size_t>(win.handle), win.is_fullscreen,
                               win.is_maximized, win.is_minimized, actual_rect, min_track_width,
                               min_track_height});
    }
  }
}

ctrl::SplitMode to_engine_split_mode(LayoutSplitMode split_mode) {
  switch (split_mode) {
  case LayoutSplitMode::Dwindle:
    return ctrl::SplitMode::Dwindle;
  case LayoutSplitMode::Vertical:
    return ctrl::SplitMode::Vertical;
  case LayoutSplitMode::Horizontal:
    return ctrl::SplitMode::Horizontal;
  }
  return ctrl::SplitMode::Dwindle;
}

ClusterTilingOptions resolve_monitor_tiling_options(const GlobalOptions& options,
                                                    const winapi::MonitorInfo& monitor,
                                                    size_t monitor_index) {
  ClusterTilingOptions resolved;
  resolved.gapOptions = options.gapOptions;
  resolved.layoutOptions = options.layoutOptions;
  resolved.zen_percentage = options.visualizationOptions.renderOptions.zen_percentage;

  for (const auto& profile : options.monitorProfiles) {
    if (!monitor_profile_matches(profile, monitor, monitor_index)) {
      continue;
    }

    if (profile.gapOptions.has_value()) {
      if (profile.gapOptions->horizontal.has_value()) {
        resolved.gapOptions.horizontal = *profile.gapOptions->horizontal;
      }
      if (profile.gapOptions->vertical.has_value()) {
        resolved.gapOptions.vertical = *profile.gapOptions->vertical;
      }
    }
    if (profile.layoutOptions.has_value()) {
      resolved.layoutOptions = *profile.layoutOptions;
    }
    if (profile.zen_percentage.has_value()) {
      resolved.zen_percentage = *profile.zen_percentage;
    }
  }

  return resolved;
}

void resolve_cluster_tiling_options_into(const std::vector<winapi::MonitorInfo>& monitors,
                                         const GlobalOptions& options,
                                         std::vector<ClusterTilingOptions>& cluster_options) {
  cluster_options.clear();
  cluster_options.reserve(monitors.size());
  for (size_t i = 0; i < monitors.size(); ++i) {
    cluster_options.push_back(resolve_monitor_tiling_options(options, monitors[i], i));
  }
}

std::vector<ctrl::ClusterInitInfo>
create_cluster_infos_from_monitors(const std::vector<winapi::MonitorInfo>& monitors,
                                   const GlobalOptions& options) {
  std::vector<ctrl::ClusterInitInfo> cluster_infos;
  cluster_infos.reserve(monitors.size());
  std::vector<ClusterTilingOptions> cluster_options;
  resolve_cluster_tiling_options_into(monitors, options, cluster_options);

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

    auto layout_rule =
        find_layout_rule_for_window_count(cluster_options[i].layoutOptions, cell_ids.size());
    cluster_infos.push_back({x, y, w, h, mx, my, mw, mh, cell_ids, layout_rule,
                             cluster_options[i].layoutOptions.split_width_multiplier});
  }

  return cluster_infos;
}

void initialize_engine_from_monitors(Engine& engine,
                                     const std::vector<winapi::MonitorInfo>& monitors,
                                     const GlobalOptions& options) {
  auto cluster_infos = create_cluster_infos_from_monitors(monitors, options);
  engine.init(cluster_infos, to_engine_split_mode(options.layoutOptions.split_mode));
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
