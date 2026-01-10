#ifdef DOCTEST_CONFIG_DISABLE

// Prevent Windows headers from defining functions that conflict with raylib
#if defined(_WIN32)
#define NOGDI  // Excludes GDI functions (Rectangle, etc.)
#define NOUSER // Excludes USER functions (CloseWindow, ShowCursor, etc.)
#endif

#include "multi_ui.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <string>

#include "controller.h"
#include "model.h"
#include "options.h"
#include "raylib.h"
#include "spdlog/spdlog.h"

namespace wintiler {

namespace {

// Convert overlay::Color to Raylib Color
Color to_raylib_color(const overlay::Color& c) {
  return Color{c.r, c.g, c.b, c.a};
}

const size_t CELL_ID_START = 10;

// Semi-transparent cluster background colors
const Color CLUSTER_COLORS[] = {
    {100, 149, 237, 50}, // Cornflower blue
    {144, 238, 144, 50}, // Light green
    {255, 165, 0, 50},   // Orange
    {221, 160, 221, 50}, // Plum
    {255, 182, 193, 50}, // Light pink
    {255, 255, 0, 50},   // Yellow
    {0, 255, 255, 50},   // Cyan
    {255, 99, 71, 50},   // Tomato
};
const size_t NUM_CLUSTER_COLORS = sizeof(CLUSTER_COLORS) / sizeof(CLUSTER_COLORS[0]);

struct ViewTransform {
  float offset_x; // min_x of bounding box
  float offset_y; // min_y of bounding box
  float scale;    // uniform scale factor
  float margin;   // screen margin
  float screen_width;
  float screen_height;
};

struct MultiClusterAppState {
  ctrl::System system;
  std::vector<std::vector<size_t>> leaf_ids_per_cluster;
  std::optional<size_t> hovered_cluster_index; // Track hovered cluster for empty clusters
};

ViewTransform compute_view_transform(const ctrl::System& system, float screen_w, float screen_h,
                                     float margin) {
  if (system.clusters.empty()) {
    return ViewTransform{0.0f, 0.0f, 1.0f, margin, screen_w, screen_h};
  }

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float max_y = std::numeric_limits<float>::lowest();

  for (const auto& cluster : system.clusters) {
    min_x = std::min(min_x, cluster.global_x);
    min_y = std::min(min_y, cluster.global_y);
    max_x = std::max(max_x, cluster.global_x + cluster.window_width);
    max_y = std::max(max_y, cluster.global_y + cluster.window_height);
  }

  float world_w = max_x - min_x;
  float world_h = max_y - min_y;

  if (world_w <= 0.0f)
    world_w = 1.0f;
  if (world_h <= 0.0f)
    world_h = 1.0f;

  float avail_w = screen_w - 2.0f * margin;
  float avail_h = screen_h - 2.0f * margin;

  float scale_x = avail_w / world_w;
  float scale_y = avail_h / world_h;
  float scale = std::min(scale_x, scale_y);

  return ViewTransform{min_x, min_y, scale, margin, screen_w, screen_h};
}

Rectangle to_screen_rect(const ViewTransform& vt, const ctrl::Rect& global_rect) {
  return Rectangle{vt.margin + (global_rect.x - vt.offset_x) * vt.scale,
                   vt.margin + (global_rect.y - vt.offset_y) * vt.scale,
                   global_rect.width * vt.scale, global_rect.height * vt.scale};
}

void to_global_point(const ViewTransform& vt, float screen_x, float screen_y, float& global_x,
                     float& global_y) {
  global_x = (screen_x - vt.margin) / vt.scale + vt.offset_x;
  global_y = (screen_y - vt.margin) / vt.scale + vt.offset_y;
}

void to_screen_point(const ViewTransform& vt, float global_x, float global_y, float& screen_x,
                     float& screen_y) {
  screen_x = vt.margin + (global_x - vt.offset_x) * vt.scale;
  screen_y = vt.margin + (global_y - vt.offset_y) * vt.scale;
}

// Compute all cluster geometries (local coordinates)
std::vector<std::vector<ctrl::Rect>> compute_all_geometries(const ctrl::System& system, float gap_h,
                                                            float gap_v, float zen_pct) {
  std::vector<std::vector<ctrl::Rect>> result;
  result.reserve(system.clusters.size());
  for (const auto& cluster : system.clusters) {
    result.push_back(ctrl::compute_cluster_geometry(cluster, gap_h, gap_v, zen_pct));
  }
  return result;
}

// Convert local geometries to global coordinates for navigation
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
    const auto& cluster = system.clusters[cluster_idx];
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

// Get selected cell rect for cursor positioning
std::optional<ctrl::Rect>
get_selected_rect(const ctrl::System& system,
                  const std::vector<std::vector<ctrl::Rect>>& global_geometries) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int ci = system.selection->cluster_index;
  int cell_idx = system.selection->cell_index;

  if (ci < 0 || static_cast<size_t>(ci) >= global_geometries.size()) {
    return std::nullopt;
  }
  if (cell_idx < 0 ||
      static_cast<size_t>(cell_idx) >= global_geometries[static_cast<size_t>(ci)].size()) {
    return std::nullopt;
  }

  return global_geometries[static_cast<size_t>(ci)][static_cast<size_t>(cell_idx)];
}

// Get sibling cell index for selected cell
std::optional<int> get_selected_sibling_cell_index(const ctrl::System& system) {
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

void center_mouse_on_rect(const ViewTransform& vt, const ctrl::Rect& rect) {
  float center_x = rect.x + rect.width / 2.0f;
  float center_y = rect.y + rect.height / 2.0f;
  float screen_x, screen_y;
  to_screen_point(vt, center_x, center_y, screen_x, screen_y);
  SetMousePosition(static_cast<int>(screen_x), static_cast<int>(screen_y));
}

void center_mouse_on_selection(const MultiClusterAppState& app_state, const ViewTransform& vt,
                               const std::vector<std::vector<ctrl::Rect>>& global_geometries) {
  if (auto rect = get_selected_rect(app_state.system, global_geometries)) {
    center_mouse_on_rect(vt, *rect);
  }
}

std::vector<ctrl::ClusterCellUpdateInfo>
build_current_state(const MultiClusterAppState& app_state) {
  std::vector<ctrl::ClusterCellUpdateInfo> state;
  for (size_t cluster_idx = 0; cluster_idx < app_state.system.clusters.size(); ++cluster_idx) {
    const auto& cluster = app_state.system.clusters[cluster_idx];
    state.push_back({app_state.leaf_ids_per_cluster[cluster_idx], cluster.has_fullscreen_cell});
  }
  return state;
}

void add_new_process_multi(MultiClusterAppState& app_state, size_t& next_process_id) {
  // Determine target cluster:
  // 1. If hovering over an empty cluster, prioritize that cluster
  // 2. Otherwise use selection if available
  // 3. Fall back to hovered cluster
  std::optional<size_t> target_cluster_index;

  if (app_state.hovered_cluster_index.has_value()) {
    size_t hovered_idx = *app_state.hovered_cluster_index;
    const auto& hovered_cluster = app_state.system.clusters[hovered_idx];
    if (hovered_cluster.tree.empty()) {
      // Hovering over empty cluster - prioritize it for new windows
      target_cluster_index = hovered_idx;
    }
  }

  if (!target_cluster_index.has_value() && app_state.system.selection.has_value()) {
    target_cluster_index = static_cast<size_t>(app_state.system.selection->cluster_index);
  }

  if (!target_cluster_index.has_value() && app_state.hovered_cluster_index.has_value()) {
    target_cluster_index = app_state.hovered_cluster_index;
  }

  if (!target_cluster_index.has_value()) {
    return; // No valid target cluster
  }

  // Add the new process ID to our stored list
  size_t new_leaf_id = next_process_id++;
  app_state.leaf_ids_per_cluster[*target_cluster_index].push_back(new_leaf_id);

  // Build state from stored IDs
  auto state = build_current_state(app_state);

  // Update system - new windows go to the target cluster
  if (!ctrl::update(app_state.system, state, static_cast<int>(*target_cluster_index))) {
    spdlog::error("add_new_process_multi: failed to update system");
  }
}

void delete_selected_process_multi(MultiClusterAppState& app_state) {
  if (!app_state.system.selection.has_value()) {
    return;
  }

  auto [cluster_index, cell_index] = *app_state.system.selection;
  const auto& cluster = app_state.system.clusters[static_cast<size_t>(cluster_index)];
  if (cell_index < 0 || static_cast<size_t>(cell_index) >= cluster.tree.size()) {
    return;
  }

  const auto& cell_data = cluster.tree[cell_index];
  if (!cell_data.leaf_id.has_value()) {
    return;
  }

  size_t leaf_id_to_remove = *cell_data.leaf_id;

  // Remove from our stored list
  auto& ids = app_state.leaf_ids_per_cluster[static_cast<size_t>(cluster_index)];
  ids.erase(std::remove(ids.begin(), ids.end(), leaf_id_to_remove), ids.end());

  // Build state from stored IDs (now without the removed ID)
  auto state = build_current_state(app_state);

  // Update system (selection will auto-update)
  if (!ctrl::update(app_state.system, state, std::nullopt)) {
    spdlog::error("delete_selected_process_multi: failed to update system");
  }
}

Color get_cluster_color(size_t cluster_index) {
  return CLUSTER_COLORS[cluster_index % NUM_CLUSTER_COLORS];
}

std::optional<HotkeyAction> get_key_action() {
  if (IsKeyPressed(KEY_H))
    return HotkeyAction::NavigateLeft;
  if (IsKeyPressed(KEY_J))
    return HotkeyAction::NavigateDown;
  if (IsKeyPressed(KEY_K))
    return HotkeyAction::NavigateUp;
  if (IsKeyPressed(KEY_L))
    return HotkeyAction::NavigateRight;
  if (IsKeyPressed(KEY_Y))
    return HotkeyAction::ToggleSplit;
  if (IsKeyPressed(KEY_LEFT_BRACKET))
    return HotkeyAction::StoreCell;
  if (IsKeyPressed(KEY_RIGHT_BRACKET))
    return HotkeyAction::ClearStored;
  if (IsKeyPressed(KEY_COMMA))
    return HotkeyAction::Exchange;
  if (IsKeyPressed(KEY_PERIOD))
    return HotkeyAction::Move;
  if (IsKeyPressed(KEY_PAGE_UP))
    return HotkeyAction::SplitIncrease;
  if (IsKeyPressed(KEY_PAGE_DOWN))
    return HotkeyAction::SplitDecrease;
  if (IsKeyPressed(KEY_E))
    return HotkeyAction::ExchangeSiblings;
  if (IsKeyPressed(KEY_APOSTROPHE))
    return HotkeyAction::ToggleZen;
  if (IsKeyPressed(KEY_SEMICOLON))
    return HotkeyAction::CycleSplitMode;
  if (IsKeyPressed(KEY_HOME))
    return HotkeyAction::ResetSplitRatio;
  return std::nullopt;
}

} // namespace

void run_raylib_ui_multi_cluster(const std::vector<ctrl::ClusterInitInfo>& infos,
                                 GlobalOptionsProvider& options_provider) {
  const auto& options = options_provider.options;

  MultiClusterAppState app_state;
  app_state.system = ctrl::create_system(infos);

  // Initialize leaf_ids_per_cluster from infos
  app_state.leaf_ids_per_cluster.resize(infos.size());
  for (size_t i = 0; i < infos.size(); ++i) {
    app_state.leaf_ids_per_cluster[i] = infos[i].initial_cell_ids;
  }

  // Set next_process_id to avoid collisions with any pre-existing leaf IDs
  size_t next_process_id = CELL_ID_START;
  for (const auto& cluster : app_state.system.clusters) {
    for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
      const auto& cell_data = cluster.tree[i];
      if (ctrl::is_leaf(cluster, i) && cell_data.leaf_id.has_value()) {
        next_process_id = std::max(next_process_id, *cell_data.leaf_id + 1);
      }
    }
  }

  const int screen_width = 1600;
  const int screen_height = 900;
  const float margin = 20.0f;

  InitWindow(screen_width, screen_height, "win-tiler multi-cluster");

  ViewTransform vt =
      compute_view_transform(app_state.system, (float)screen_width, (float)screen_height, margin);

  SetTargetFPS(60);

  // Store cell for swap/move operations (cluster_index, leaf_id)
  std::optional<StoredCell> stored_cell;

  float gap_h = options.gapOptions.horizontal;
  float gap_v = options.gapOptions.vertical;

  while (!WindowShouldClose()) {
    // Check for config changes and hot-reload
    if (options_provider.refresh()) {
      gap_h = options.gapOptions.horizontal;
      gap_v = options.gapOptions.vertical;
      // Geometry is computed on-demand each frame, no explicit recompute needed
    }

    // Process tree-modifying input BEFORE computing geometries
    // This ensures geometries are always in sync with the current tree state
    if (IsKeyPressed(KEY_SPACE)) {
      add_new_process_multi(app_state, next_process_id);
    }

    if (IsKeyPressed(KEY_D)) {
      delete_selected_process_multi(app_state);
    }

    if (IsKeyPressed(KEY_I)) {
      ctrl::debug_print_system(app_state.system);
    }

    if (IsKeyPressed(KEY_C)) {
      if (!ctrl::validate_system(app_state.system)) {
        spdlog::error("System validation failed");
      }
    }

    // Compute geometries once per frame (after tree-modifying input)
    auto local_geometries = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
    auto global_geometries = to_global_geometries(app_state.system, local_geometries);

    // Mouse hover selection
    Vector2 mouse_pos = GetMousePosition();
    float global_x, global_y;
    to_global_point(vt, mouse_pos.x, mouse_pos.y, global_x, global_y);

    // Always track which cluster the mouse is over (even if empty)
    app_state.hovered_cluster_index =
        find_cluster_at_global_point(app_state.system, global_x, global_y);

    auto cell_at_mouse =
        find_cell_at_global_point(app_state.system, global_geometries, global_x, global_y);
    if (cell_at_mouse.has_value()) {
      auto [cluster_index, cell_index] = *cell_at_mouse;

      // Update selection if different
      const auto& current_sel = app_state.system.selection;
      if (!current_sel.has_value() ||
          current_sel->cluster_index != static_cast<int>(cluster_index) ||
          current_sel->cell_index != cell_index) {
        // Set new selection
        app_state.system.selection =
            ctrl::CellIndicatorByIndex{static_cast<int>(cluster_index), cell_index};
      }
    }
    // Note: Empty clusters no longer maintain "selected" state - selection requires a cell

    // Keyboard input (HotkeyAction enum actions)
    auto action = get_key_action();
    if (action.has_value()) {
      switch (*action) {
      case HotkeyAction::NavigateLeft:
        spdlog::info("NavigateLeft: moving selection to the left");
        if (ctrl::move_selection(app_state.system, ctrl::Direction::Left, global_geometries)) {
          // Recompute geometries and center on selection
          auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
          auto new_global = to_global_geometries(app_state.system, new_local);
          if (auto rect = get_selected_rect(app_state.system, new_global)) {
            center_mouse_on_rect(vt, *rect);
          }
        }
        break;
      case HotkeyAction::NavigateDown:
        spdlog::info("NavigateDown: moving selection downward");
        if (ctrl::move_selection(app_state.system, ctrl::Direction::Down, global_geometries)) {
          auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
          auto new_global = to_global_geometries(app_state.system, new_local);
          if (auto rect = get_selected_rect(app_state.system, new_global)) {
            center_mouse_on_rect(vt, *rect);
          }
        }
        break;
      case HotkeyAction::NavigateUp:
        spdlog::info("NavigateUp: moving selection upward");
        if (ctrl::move_selection(app_state.system, ctrl::Direction::Up, global_geometries)) {
          auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
          auto new_global = to_global_geometries(app_state.system, new_local);
          if (auto rect = get_selected_rect(app_state.system, new_global)) {
            center_mouse_on_rect(vt, *rect);
          }
        }
        break;
      case HotkeyAction::NavigateRight:
        spdlog::info("NavigateRight: moving selection to the right");
        if (ctrl::move_selection(app_state.system, ctrl::Direction::Right, global_geometries)) {
          auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
          auto new_global = to_global_geometries(app_state.system, new_local);
          if (auto rect = get_selected_rect(app_state.system, new_global)) {
            center_mouse_on_rect(vt, *rect);
          }
        }
        break;
      case HotkeyAction::ToggleSplit:
        spdlog::info("ToggleSplit: toggling split direction of selected cell");
        if (!ctrl::toggle_selected_split_dir(app_state.system)) {
          spdlog::trace("Failed to toggle split direction");
        }
        break;
      case HotkeyAction::StoreCell:
        spdlog::info("StoreCell: storing current cell for swap/move operation");
        if (app_state.system.selection.has_value()) {
          const auto& cluster =
              app_state.system
                  .clusters[static_cast<size_t>(app_state.system.selection->cluster_index)];
          const auto& cell_data = cluster.tree[app_state.system.selection->cell_index];
          if (cell_data.leaf_id.has_value()) {
            stored_cell = StoredCell{static_cast<size_t>(app_state.system.selection->cluster_index),
                                     *cell_data.leaf_id};
          }
        }
        break;
      case HotkeyAction::ClearStored:
        spdlog::info("ClearStored: clearing stored cell reference");
        stored_cell.reset();
        break;
      case HotkeyAction::Exchange:
        spdlog::info("Exchange: swapping stored cell with selected cell");
        if (stored_cell.has_value() && app_state.system.selection.has_value()) {
          // Find stored cell index from leaf_id
          auto stored_cell_idx = ctrl::find_cell_by_leaf_id(
              app_state.system.clusters[stored_cell->cluster_index], stored_cell->leaf_id);
          if (stored_cell_idx.has_value()) {
            if (ctrl::swap_cells(app_state.system, app_state.system.selection->cluster_index,
                                 app_state.system.selection->cell_index,
                                 static_cast<int>(stored_cell->cluster_index), *stored_cell_idx)) {
              stored_cell.reset();
            }
          }
        }
        break;
      case HotkeyAction::Move:
        spdlog::info("Move: moving stored cell to selected cell's position");
        if (stored_cell.has_value() && app_state.system.selection.has_value()) {
          // Find stored cell index from leaf_id
          auto stored_cell_idx = ctrl::find_cell_by_leaf_id(
              app_state.system.clusters[stored_cell->cluster_index], stored_cell->leaf_id);
          if (stored_cell_idx.has_value()) {
            if (ctrl::move_cell(app_state.system, static_cast<int>(stored_cell->cluster_index),
                                *stored_cell_idx, app_state.system.selection->cluster_index,
                                app_state.system.selection->cell_index)) {
              stored_cell.reset();
            }
          }
        }
        break;
      case HotkeyAction::SplitIncrease:
        spdlog::info("SplitIncrease: increasing split ratio by 5%%");
        if (ctrl::adjust_selected_split_ratio(app_state.system, 0.05f)) {
          auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
          auto new_global = to_global_geometries(app_state.system, new_local);
          if (auto rect = get_selected_rect(app_state.system, new_global)) {
            center_mouse_on_rect(vt, *rect);
          }
        }
        break;
      case HotkeyAction::SplitDecrease:
        spdlog::info("SplitDecrease: decreasing split ratio by 5%%");
        if (ctrl::adjust_selected_split_ratio(app_state.system, -0.05f)) {
          auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
          auto new_global = to_global_geometries(app_state.system, new_local);
          if (auto rect = get_selected_rect(app_state.system, new_global)) {
            center_mouse_on_rect(vt, *rect);
          }
        }
        break;
      case HotkeyAction::ExchangeSiblings:
        spdlog::info("ExchangeSiblings: exchanging selected cell with its sibling");
        if (app_state.system.selection.has_value()) {
          if (auto sibling_idx = get_selected_sibling_cell_index(app_state.system)) {
            if (ctrl::swap_cells(app_state.system, app_state.system.selection->cluster_index,
                                 app_state.system.selection->cell_index,
                                 app_state.system.selection->cluster_index, *sibling_idx)) {
              // Recompute and center mouse
              auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
              auto new_global = to_global_geometries(app_state.system, new_local);
              if (auto rect = get_selected_rect(app_state.system, new_global)) {
                center_mouse_on_rect(vt, *rect);
              }
            }
          }
        }
        break;
      case HotkeyAction::ToggleZen:
        spdlog::info("ToggleZen: toggling zen mode for selected cell");
        if (!ctrl::toggle_selected_zen(app_state.system)) {
          spdlog::error("ToggleZen: failed to toggle zen mode");
        }
        break;
      case HotkeyAction::CycleSplitMode:
        if (!ctrl::cycle_split_mode(app_state.system)) {
          spdlog::error("CycleSplitMode: failed to cycle split mode");
        }
        spdlog::info("CycleSplitMode: switched to {}",
                     magic_enum::enum_name(app_state.system.split_mode));
        break;
      case HotkeyAction::ResetSplitRatio:
        spdlog::info("ResetSplitRatio: resetting split ratio of parent to 50%%");
        if (ctrl::set_selected_split_ratio(app_state.system, 0.5f)) {
          auto new_local = compute_all_geometries(app_state.system, gap_h, gap_v, 0.85f);
          auto new_global = to_global_geometries(app_state.system, new_local);
          if (auto rect = get_selected_rect(app_state.system, new_global)) {
            center_mouse_on_rect(vt, *rect);
          }
        }
        break;
      case HotkeyAction::Exit:
        spdlog::info("Exit: exit action (not implemented in multi_ui)");
        // Not implemented in multi_ui
        break;
      }
    }

    // Drawing
    BeginDrawing();
    ClearBackground(RAYWHITE);

    // Draw cluster backgrounds first
    for (size_t cluster_idx = 0; cluster_idx < app_state.system.clusters.size(); ++cluster_idx) {
      const auto& cluster = app_state.system.clusters[cluster_idx];
      ctrl::Rect cluster_global_rect{cluster.global_x, cluster.global_y, cluster.window_width,
                                     cluster.window_height};
      Rectangle screen_rect = to_screen_rect(vt, cluster_global_rect);
      DrawRectangleRec(screen_rect, get_cluster_color(cluster_idx));
      DrawRectangleLinesEx(screen_rect, 2.0f, DARKGRAY);
    }

    // Draw cells
    const auto& selected_cell = app_state.system.selection;

    for (size_t cluster_idx = 0; cluster_idx < app_state.system.clusters.size(); ++cluster_idx) {
      const auto& cluster = app_state.system.clusters[cluster_idx];
      const auto& cluster_geom = global_geometries[cluster_idx];

      for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
        if (!ctrl::is_leaf(cluster, i)) {
          continue;
        }

        // Use precomputed geometry
        const auto& global_rect = cluster_geom[static_cast<size_t>(i)];
        if (global_rect.width <= 0.0f || global_rect.height <= 0.0f) {
          continue; // Skip non-leaf cells
        }

        const auto& cell_data = cluster.tree[i];
        Rectangle screen_rect = to_screen_rect(vt, global_rect);

        bool is_selected = selected_cell.has_value() &&
                           selected_cell->cluster_index == static_cast<int>(cluster_idx) &&
                           selected_cell->cell_index == i;

        // Check if this cell is the stored cell
        bool is_stored_cell = false;
        if (stored_cell.has_value() && stored_cell->cluster_index == cluster_idx) {
          auto stored_idx = ctrl::find_cell_by_leaf_id(cluster, stored_cell->leaf_id);
          if (stored_idx.has_value() && *stored_idx == i) {
            is_stored_cell = true;
          }
        }

        // Determine border color and width from VisualizationOptions
        const auto& ro = options.visualizationOptions.renderOptions;
        Color border_color;
        float border_width;
        if (is_selected && is_stored_cell) {
          border_color = PURPLE;
          border_width = ro.border_width + 1.0f;
        } else if (is_stored_cell) {
          border_color = to_raylib_color(ro.stored_color);
          border_width = ro.border_width;
        } else if (is_selected) {
          border_color = to_raylib_color(ro.selected_color);
          border_width = ro.border_width;
        } else {
          border_color = to_raylib_color(ro.normal_color);
          border_width = ro.border_width;
        }

        DrawRectangleLinesEx(screen_rect, border_width, border_color);

        // Draw process ID (leaf_id is the process ID)
        if (cell_data.leaf_id.has_value()) {
          std::string label_text = "P:" + std::to_string(*cell_data.leaf_id);
          float font_size = std::min(screen_rect.width, screen_rect.height) * 0.2f;
          if (font_size < 10.0f)
            font_size = 10.0f;

          int text_width = MeasureText(label_text.c_str(), (int)font_size);
          int text_x = (int)(screen_rect.x + (screen_rect.width - text_width) / 2);
          int text_y = (int)(screen_rect.y + (screen_rect.height - font_size) / 2);

          DrawText(label_text.c_str(), text_x, text_y, (int)font_size, DARKGRAY);
        }
      }
    }

    // Draw zen cell overlays for each cluster
    for (size_t cluster_idx = 0; cluster_idx < app_state.system.clusters.size(); ++cluster_idx) {
      const auto& cluster = app_state.system.clusters[cluster_idx];
      if (!cluster.zen_cell_index.has_value()) {
        continue;
      }

      int zen_cell_index = *cluster.zen_cell_index;

      // Get zen display rect from precomputed geometry
      // Note: compute_cluster_geometry already handles zen mode
      const auto& zen_local_rect =
          local_geometries[cluster_idx][static_cast<size_t>(zen_cell_index)];
      ctrl::Rect zen_display_rect{cluster.global_x + zen_local_rect.x,
                                  cluster.global_y + zen_local_rect.y, zen_local_rect.width,
                                  zen_local_rect.height};
      Rectangle zen_screen_rect = to_screen_rect(vt, zen_display_rect);

      // Draw semi-transparent fill
      Color zen_fill = {100, 149, 237, 80}; // Cornflower blue, semi-transparent
      DrawRectangleRec(zen_screen_rect, zen_fill);

      // Determine border color based on selection state (same as normal cells)
      bool is_zen_selected = selected_cell.has_value() &&
                             selected_cell->cluster_index == static_cast<int>(cluster_idx) &&
                             selected_cell->cell_index == zen_cell_index;
      const auto& ro = options.visualizationOptions.renderOptions;
      Color border_color =
          is_zen_selected ? to_raylib_color(ro.selected_color) : to_raylib_color(ro.normal_color);

      DrawRectangleLinesEx(zen_screen_rect, ro.border_width, border_color);

      // Draw label "Z:<id>"
      const auto& zen_cell_data = cluster.tree[zen_cell_index];
      if (zen_cell_data.leaf_id.has_value()) {
        std::string label = "Z:" + std::to_string(*zen_cell_data.leaf_id);
        float font_size = std::min(zen_screen_rect.width, zen_screen_rect.height) * 0.2f;
        if (font_size < 10.0f)
          font_size = 10.0f;
        int text_width = MeasureText(label.c_str(), (int)font_size);
        int text_x = (int)(zen_screen_rect.x + (zen_screen_rect.width - text_width) / 2);
        int text_y = (int)(zen_screen_rect.y + (zen_screen_rect.height - font_size) / 2);
        DrawText(label.c_str(), text_x, text_y, (int)font_size, DARKGRAY);
      }
    }

    EndDrawing();
  }

  CloseWindow();
}

} // namespace wintiler

#endif
