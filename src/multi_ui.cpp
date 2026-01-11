#ifdef DOCTEST_CONFIG_DISABLE

// Prevent Windows headers from defining functions that conflict with raylib
#if defined(_WIN32)
#define NOGDI  // Excludes GDI functions (Rectangle, etc.)
#define NOUSER // Excludes USER functions (CloseWindow, ShowCursor, etc.)
#endif

#include "multi_ui.h"

#include <cmath>
#include <limits>
#include <string>

#include "controller.h"
#include "engine.h"
#include "options.h"
#include "raylib.h"
#include "spdlog/spdlog.h"

namespace wintiler {

namespace {

// Convert overlay::Color to Raylib Color
Color to_raylib_color(const overlay::Color& c) {
  return Color{c.r, c.g, c.b, c.a};
}

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

void center_mouse_on_rect(const ViewTransform& vt, const ctrl::Rect& rect) {
  float center_x = rect.x + rect.width / 2.0f;
  float center_y = rect.y + rect.height / 2.0f;
  float screen_x, screen_y;
  to_screen_point(vt, center_x, center_y, screen_x, screen_y);
  SetMousePosition(static_cast<int>(screen_x), static_cast<int>(screen_y));
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

// Build cluster cell update info for add/delete operations
std::vector<ctrl::ClusterCellUpdateInfo> build_current_state(const Engine& engine) {
  std::vector<ctrl::ClusterCellUpdateInfo> state;
  for (size_t cluster_idx = 0; cluster_idx < engine.system.clusters.size(); ++cluster_idx) {
    const auto& cluster = engine.system.clusters[cluster_idx];
    state.push_back({engine.leaf_ids_per_cluster[cluster_idx], cluster.has_fullscreen_cell});
  }
  return state;
}

void add_new_process(Engine& engine) {
  // Determine target cluster:
  // 1. If hovering over an empty cluster, prioritize that cluster
  // 2. Otherwise use selection if available
  // 3. Fall back to hovered cluster
  std::optional<size_t> target_cluster_index;

  if (engine.hovered_cluster_index.has_value()) {
    size_t hovered_idx = *engine.hovered_cluster_index;
    const auto& hovered_cluster = engine.system.clusters[hovered_idx];
    if (hovered_cluster.tree.empty()) {
      // Hovering over empty cluster - prioritize it for new windows
      target_cluster_index = hovered_idx;
    }
  }

  if (!target_cluster_index.has_value() && engine.system.selection.has_value()) {
    target_cluster_index = static_cast<size_t>(engine.system.selection->cluster_index);
  }

  if (!target_cluster_index.has_value() && engine.hovered_cluster_index.has_value()) {
    target_cluster_index = engine.hovered_cluster_index;
  }

  if (!target_cluster_index.has_value()) {
    return; // No valid target cluster
  }

  // Add the new process ID to our stored list
  size_t new_leaf_id = engine.next_process_id++;
  engine.leaf_ids_per_cluster[*target_cluster_index].push_back(new_leaf_id);

  // Build state and update
  auto state = build_current_state(engine);
  if (!engine.update(state, static_cast<int>(*target_cluster_index))) {
    spdlog::error("add_new_process: failed to update system");
  }
}

void delete_selected_process(Engine& engine) {
  if (!engine.system.selection.has_value()) {
    return;
  }

  auto [cluster_index, cell_index] = *engine.system.selection;
  const auto& cluster = engine.system.clusters[static_cast<size_t>(cluster_index)];
  if (cell_index < 0 || static_cast<size_t>(cell_index) >= cluster.tree.size()) {
    return;
  }

  const auto& cell_data = cluster.tree[cell_index];
  if (!cell_data.leaf_id.has_value()) {
    return;
  }

  size_t leaf_id_to_remove = *cell_data.leaf_id;

  // Remove from stored list
  auto& ids = engine.leaf_ids_per_cluster[static_cast<size_t>(cluster_index)];
  ids.erase(std::remove(ids.begin(), ids.end(), leaf_id_to_remove), ids.end());

  // Build state and update
  auto state = build_current_state(engine);
  if (!engine.update(state, std::nullopt)) {
    spdlog::error("delete_selected_process: failed to update system");
  }
}

} // namespace

void run_raylib_ui_multi_cluster(const std::vector<ctrl::ClusterInitInfo>& infos,
                                 GlobalOptionsProvider& options_provider) {
  const auto& options = options_provider.options;

  Engine engine;
  engine.init(infos);

  const int screen_width = 1600;
  const int screen_height = 900;
  const float margin = 20.0f;

  InitWindow(screen_width, screen_height, "win-tiler multi-cluster");

  ViewTransform vt =
      compute_view_transform(engine.system, (float)screen_width, (float)screen_height, margin);

  SetTargetFPS(60);

  float gap_h = options.gapOptions.horizontal;
  float gap_v = options.gapOptions.vertical;
  const float zen_pct = 0.85f;

  while (!WindowShouldClose()) {
    // Check for config changes and hot-reload
    if (options_provider.refresh()) {
      gap_h = options.gapOptions.horizontal;
      gap_v = options.gapOptions.vertical;
    }

    // Process tree-modifying input BEFORE computing geometries
    if (IsKeyPressed(KEY_SPACE)) {
      add_new_process(engine);
    }

    if (IsKeyPressed(KEY_D)) {
      delete_selected_process(engine);
    }

    if (IsKeyPressed(KEY_I)) {
      ctrl::debug_print_system(engine.system);
    }

    if (IsKeyPressed(KEY_C)) {
      if (!ctrl::validate_system(engine.system)) {
        spdlog::error("System validation failed");
      }
    }

    // Compute geometries once per frame (after tree-modifying input)
    auto global_geom = engine.compute_geometries(gap_h, gap_v, zen_pct);

    // Mouse hover selection
    Vector2 mouse_pos = GetMousePosition();
    float global_x, global_y;
    to_global_point(vt, mouse_pos.x, mouse_pos.y, global_x, global_y);
    engine.update_hover(global_x, global_y, global_geom);

    // Keyboard input (HotkeyAction enum actions)
    auto action = get_key_action();
    if (action.has_value()) {
      auto result = engine.process_action(*action, global_geom, gap_h, gap_v, zen_pct);
      if (result.selection_changed && engine.system.selection.has_value()) {
        int ci = engine.system.selection->cluster_index;
        int cell_idx = engine.system.selection->cell_index;
        const auto& rect = global_geom[static_cast<size_t>(ci)][static_cast<size_t>(cell_idx)];
        center_mouse_on_rect(vt, rect);
      }
    }

    // Drawing
    BeginDrawing();
    ClearBackground(RAYWHITE);

    // Draw cluster backgrounds first
    for (size_t cluster_idx = 0; cluster_idx < engine.system.clusters.size(); ++cluster_idx) {
      const auto& cluster = engine.system.clusters[cluster_idx];
      ctrl::Rect cluster_global_rect{cluster.global_x, cluster.global_y, cluster.window_width,
                                     cluster.window_height};
      Rectangle screen_rect = to_screen_rect(vt, cluster_global_rect);
      DrawRectangleRec(screen_rect, get_cluster_color(cluster_idx));
      DrawRectangleLinesEx(screen_rect, 2.0f, DARKGRAY);
    }

    // Draw cells
    const auto& selected_cell = engine.system.selection;

    for (size_t cluster_idx = 0; cluster_idx < engine.system.clusters.size(); ++cluster_idx) {
      const auto& cluster = engine.system.clusters[cluster_idx];
      const auto& cluster_geom = global_geom[cluster_idx];

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
        if (engine.stored_cell.has_value() && engine.stored_cell->cluster_index == cluster_idx) {
          auto stored_idx = ctrl::find_cell_by_leaf_id(cluster, engine.stored_cell->leaf_id);
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
    for (size_t cluster_idx = 0; cluster_idx < engine.system.clusters.size(); ++cluster_idx) {
      const auto& cluster = engine.system.clusters[cluster_idx];
      if (!cluster.zen_cell_index.has_value()) {
        continue;
      }

      int zen_cell_index = *cluster.zen_cell_index;

      // Get zen display rect from precomputed geometry
      const auto& zen_display_rect = global_geom[cluster_idx][static_cast<size_t>(zen_cell_index)];
      Rectangle zen_screen_rect = to_screen_rect(vt, zen_display_rect);

      // Draw semi-transparent fill
      Color zen_fill = {100, 149, 237, 80}; // Cornflower blue, semi-transparent
      DrawRectangleRec(zen_screen_rect, zen_fill);

      // Determine border color based on selection state
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
