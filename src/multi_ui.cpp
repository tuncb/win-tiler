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
#include <optional>
#include <string>

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
  cells::System system;
};

ViewTransform compute_view_transform(const cells::System& system, float screen_w, float screen_h,
                                     float margin) {
  if (system.clusters.empty()) {
    return ViewTransform{0.0f, 0.0f, 1.0f, margin, screen_w, screen_h};
  }

  float min_x = std::numeric_limits<float>::max();
  float min_y = std::numeric_limits<float>::max();
  float max_x = std::numeric_limits<float>::lowest();
  float max_y = std::numeric_limits<float>::lowest();

  for (const auto& pc : system.clusters) {
    min_x = std::min(min_x, pc.global_x);
    min_y = std::min(min_y, pc.global_y);
    max_x = std::max(max_x, pc.global_x + pc.cluster.window_width);
    max_y = std::max(max_y, pc.global_y + pc.cluster.window_height);
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

Rectangle to_screen_rect(const ViewTransform& vt, const cells::Rect& global_rect) {
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

// Find the cluster and cell index at a global point
std::optional<std::pair<cells::ClusterId, int>>
find_cell_at_global_point(const cells::System& system, float global_x, float global_y) {
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (!cells::is_leaf(pc.cluster, i)) {
        continue;
      }

      cells::Rect global_rect = cells::get_cell_global_rect(pc, i);

      if (global_x >= global_rect.x && global_x < global_rect.x + global_rect.width &&
          global_y >= global_rect.y && global_y < global_rect.y + global_rect.height) {
        return std::make_pair(pc.id, i);
      }
    }
  }
  return std::nullopt;
}

void center_mouse_on_selection(const MultiClusterAppState& app_state, const ViewTransform& vt) {
  auto selected_rect = cells::get_selected_cell_global_rect(app_state.system);
  if (selected_rect.has_value()) {
    float center_x = selected_rect->x + selected_rect->width / 2.0f;
    float center_y = selected_rect->y + selected_rect->height / 2.0f;

    float screen_x, screen_y;
    to_screen_point(vt, center_x, center_y, screen_x, screen_y);

    SetMousePosition(static_cast<int>(screen_x), static_cast<int>(screen_y));
  }
}

std::vector<cells::ClusterCellIds> build_current_state(const cells::System& system) {
  std::vector<cells::ClusterCellIds> state;
  for (const auto& pc : system.clusters) {
    state.push_back({pc.id, cells::get_cluster_leaf_ids(pc.cluster)});
  }
  return state;
}

void add_new_process_multi(MultiClusterAppState& app_state, size_t& next_process_id) {
  if (!app_state.system.selection.has_value()) {
    return;
  }

  auto selected_cluster_id = app_state.system.selection->cluster_id;
  auto state = build_current_state(app_state.system);

  // Add the new process ID (which becomes the leaf ID) to the selected cluster
  size_t new_leaf_id = next_process_id++;
  for (auto& cluster_cell_ids : state) {
    if (cluster_cell_ids.cluster_id == selected_cluster_id) {
      cluster_cell_ids.leaf_ids.push_back(new_leaf_id);
      break;
    }
  }

  // Update system and select the newly added cell
  app_state.system.update(state, std::make_pair(selected_cluster_id, new_leaf_id));
}

void delete_selected_process_multi(MultiClusterAppState& app_state) {
  auto selected = cells::get_selected_cell(app_state.system);
  if (!selected.has_value()) {
    return;
  }

  auto [cluster_id, cell_index] = *selected;
  const auto* pc = app_state.system.get_cluster(cluster_id);
  if (!pc || cell_index < 0 || static_cast<size_t>(cell_index) >= pc->cluster.cells.size()) {
    return;
  }

  const auto& cell = pc->cluster.cells[static_cast<size_t>(cell_index)];
  if (!cell.leaf_id.has_value()) {
    return;
  }

  size_t leaf_id_to_remove = *cell.leaf_id;
  auto state = build_current_state(app_state.system);

  // Remove the leaf ID from the appropriate cluster's list
  for (auto& cluster_cell_ids : state) {
    if (cluster_cell_ids.cluster_id == cluster_id) {
      auto& ids = cluster_cell_ids.leaf_ids;
      ids.erase(std::remove(ids.begin(), ids.end(), leaf_id_to_remove), ids.end());
      break;
    }
  }

  // Update system (selection will auto-update)
  app_state.system.update(state, std::nullopt);
}

Color get_cluster_color(cells::ClusterId id) {
  return CLUSTER_COLORS[id % NUM_CLUSTER_COLORS];
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
  return std::nullopt;
}

} // namespace

void run_raylib_ui_multi_cluster(const std::vector<cells::ClusterInitInfo>& infos,
                                 GlobalOptionsProvider& options_provider) {
  const auto& options = options_provider.options;

  MultiClusterAppState app_state;
  app_state.system =
      cells::create_system(infos, options.gapOptions.horizontal, options.gapOptions.vertical);

  // Set next_process_id to avoid collisions with any pre-existing leaf IDs
  size_t next_process_id = CELL_ID_START;
  for (const auto& pc : app_state.system.clusters) {
    for (const auto& cell : pc.cluster.cells) {
      if (!cell.is_dead && cell.leaf_id.has_value()) {
        next_process_id = std::max(next_process_id, *cell.leaf_id + 1);
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

  // Store cell for swap/move operations (cluster_id, leaf_id)
  std::optional<std::pair<cells::ClusterId, size_t>> stored_cell;

  while (!WindowShouldClose()) {
    // Check for config changes and hot-reload
    if (options_provider.refresh()) {
      app_state.system.update_gaps(options.gapOptions.horizontal, options.gapOptions.vertical);
    }

    // Mouse hover selection
    Vector2 mouse_pos = GetMousePosition();
    float global_x, global_y;
    to_global_point(vt, mouse_pos.x, mouse_pos.y, global_x, global_y);

    auto cell_at_mouse = find_cell_at_global_point(app_state.system, global_x, global_y);
    if (cell_at_mouse.has_value()) {
      auto [cluster_id, cell_index] = *cell_at_mouse;

      // Update selection if different
      auto current_sel = cells::get_selected_cell(app_state.system);
      if (!current_sel.has_value() || current_sel->first != cluster_id ||
          current_sel->second != cell_index) {
        // Set new selection
        app_state.system.selection = cells::Selection{cluster_id, cell_index};
      }
    }
    // Note: Empty clusters no longer maintain "selected" state - selection requires a cell

    // Keyboard input (actions not in HotkeyAction enum)
    if (IsKeyPressed(KEY_SPACE)) {
      add_new_process_multi(app_state, next_process_id);
    }

    if (IsKeyPressed(KEY_D)) {
      delete_selected_process_multi(app_state);
    }

    if (IsKeyPressed(KEY_I)) {
      cells::debug_print_system(app_state.system);
    }

    if (IsKeyPressed(KEY_C)) {
      cells::validate_system(app_state.system);
    }

    // Keyboard input (HotkeyAction enum actions)
    auto action = get_key_action();
    if (action.has_value()) {
      switch (*action) {
      case HotkeyAction::NavigateLeft:
        if (app_state.system.move_selection(cells::Direction::Left)) {
          center_mouse_on_selection(app_state, vt);
        }
        break;
      case HotkeyAction::NavigateDown:
        if (app_state.system.move_selection(cells::Direction::Down)) {
          center_mouse_on_selection(app_state, vt);
        }
        break;
      case HotkeyAction::NavigateUp:
        if (app_state.system.move_selection(cells::Direction::Up)) {
          center_mouse_on_selection(app_state, vt);
        }
        break;
      case HotkeyAction::NavigateRight:
        if (app_state.system.move_selection(cells::Direction::Right)) {
          center_mouse_on_selection(app_state, vt);
        }
        break;
      case HotkeyAction::ToggleSplit:
        if (!app_state.system.toggle_selected_split_dir()) {
          spdlog::trace("Failed to toggle split direction");
        }
        break;
      case HotkeyAction::StoreCell:
        if (app_state.system.selection.has_value()) {
          auto* pc = app_state.system.get_cluster(app_state.system.selection->cluster_id);
          if (pc) {
            auto& cell =
                pc->cluster.cells[static_cast<size_t>(app_state.system.selection->cell_index)];
            if (cell.leaf_id.has_value()) {
              stored_cell = {app_state.system.selection->cluster_id, *cell.leaf_id};
            }
          }
        }
        break;
      case HotkeyAction::ClearStored:
        stored_cell.reset();
        break;
      case HotkeyAction::Exchange:
        if (stored_cell.has_value() && app_state.system.selection.has_value()) {
          auto* pc = app_state.system.get_cluster(app_state.system.selection->cluster_id);
          if (pc) {
            auto& cell =
                pc->cluster.cells[static_cast<size_t>(app_state.system.selection->cell_index)];
            if (cell.leaf_id.has_value()) {
              auto result =
                  app_state.system.swap_cells(app_state.system.selection->cluster_id, *cell.leaf_id,
                                              stored_cell->first, stored_cell->second);
              if (result.success) {
                stored_cell.reset();
              }
            }
          }
        }
        break;
      case HotkeyAction::Move:
        if (stored_cell.has_value() && app_state.system.selection.has_value()) {
          auto* pc = app_state.system.get_cluster(app_state.system.selection->cluster_id);
          if (pc) {
            auto& cell =
                pc->cluster.cells[static_cast<size_t>(app_state.system.selection->cell_index)];
            if (cell.leaf_id.has_value()) {
              auto result =
                  app_state.system.move_cell(stored_cell->first, stored_cell->second,
                                             app_state.system.selection->cluster_id, *cell.leaf_id);
              if (result.success) {
                stored_cell.reset();
              }
            }
          }
        }
        break;
      case HotkeyAction::SplitIncrease:
        if (app_state.system.adjust_selected_split_ratio(0.05f)) {
          center_mouse_on_selection(app_state, vt);
        }
        break;
      case HotkeyAction::SplitDecrease:
        if (app_state.system.adjust_selected_split_ratio(-0.05f)) {
          center_mouse_on_selection(app_state, vt);
        }
        break;
      case HotkeyAction::Exit:
      case HotkeyAction::ToggleGlobal:
        // Not implemented in multi_ui
        break;
      }
    }

    // Drawing
    BeginDrawing();
    ClearBackground(RAYWHITE);

    // Draw cluster backgrounds first
    for (const auto& pc : app_state.system.clusters) {
      cells::Rect cluster_global_rect{pc.global_x, pc.global_y, pc.cluster.window_width,
                                      pc.cluster.window_height};
      Rectangle screen_rect = to_screen_rect(vt, cluster_global_rect);
      DrawRectangleRec(screen_rect, get_cluster_color(pc.id));
      DrawRectangleLinesEx(screen_rect, 2.0f, DARKGRAY);
    }

    // Draw cells
    auto selected_cell = cells::get_selected_cell(app_state.system);

    for (const auto& pc : app_state.system.clusters) {
      for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
        if (!cells::is_leaf(pc.cluster, i)) {
          continue;
        }

        const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
        cells::Rect global_rect = cells::get_cell_global_rect(pc, i);
        Rectangle screen_rect = to_screen_rect(vt, global_rect);

        bool is_selected = selected_cell.has_value() && selected_cell->first == pc.id &&
                           selected_cell->second == i;

        // Check if this cell is the stored cell
        bool is_stored_cell = false;
        if (stored_cell.has_value() && stored_cell->first == pc.id) {
          auto stored_idx = cells::find_cell_by_leaf_id(pc.cluster, stored_cell->second);
          if (stored_idx.has_value() && *stored_idx == i) {
            is_stored_cell = true;
          }
        }

        // Determine border color and width from VisualizationOptions
        const auto& viz_opts = options.visualizationOptions;
        Color border_color;
        float border_width;
        if (is_selected && is_stored_cell) {
          border_color = PURPLE;
          border_width = viz_opts.borderWidth + 1.0f;
        } else if (is_stored_cell) {
          border_color = to_raylib_color(viz_opts.storedColor);
          border_width = viz_opts.borderWidth;
        } else if (is_selected) {
          border_color = to_raylib_color(viz_opts.selectedColor);
          border_width = viz_opts.borderWidth;
        } else {
          border_color = to_raylib_color(viz_opts.normalColor);
          border_width = viz_opts.borderWidth;
        }

        DrawRectangleLinesEx(screen_rect, border_width, border_color);

        // Draw process ID (leaf_id is the process ID)
        if (cell.leaf_id.has_value()) {
          std::string label_text = "P:" + std::to_string(*cell.leaf_id);
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

    EndDrawing();
  }

  CloseWindow();
}

} // namespace wintiler

#endif
