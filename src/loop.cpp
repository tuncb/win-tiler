#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <magic_enum/magic_enum.hpp>
#include <thread>
#include <vector>

#include "multi_cell_renderer.h"
#include "multi_cells.h"
#include "overlay.h"
#include "winapi.h"

namespace wintiler {

namespace {

// Result type for action handlers - signals whether the main loop should continue or exit
enum class ActionResult { Continue, Exit };

// Helper to time a function and log its duration
template <typename F>
auto timed(const char* name, F&& func) {
  auto start = std::chrono::high_resolution_clock::now();
  auto result = func();
  auto end = std::chrono::high_resolution_clock::now();
  spdlog::trace("{}: {}us", name,
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return result;
}

// Helper for void functions
template <typename F>
void timed_void(const char* name, F&& func) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto end = std::chrono::high_resolution_clock::now();
  spdlog::trace("{}: {}us", name,
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

// Convert HotkeyAction to integer ID for Windows hotkey registration
int hotkey_action_to_id(HotkeyAction action) {
  return static_cast<int>(action) + 1; // Start from 1 to avoid 0
}

// Convert integer ID back to HotkeyAction
std::optional<HotkeyAction> id_to_hotkey_action(int id) {
  int index = id - 1;
  if (index >= 0 && index < static_cast<int>(magic_enum::enum_count<HotkeyAction>())) {
    return static_cast<HotkeyAction>(index);
  }
  return std::nullopt;
}

void register_navigation_hotkeys(const KeyboardOptions& keyboard_options) {
  for (const auto& binding : keyboard_options.bindings) {
    int id = hotkey_action_to_id(binding.action);
    auto hotkey = winapi::create_hotkey(binding.hotkey, id);
    if (hotkey) {
      winapi::register_hotkey(*hotkey);
    }
  }
  spdlog::info("Registered {} hotkeys", keyboard_options.bindings.size());
}

void unregister_navigation_hotkeys(const KeyboardOptions& keyboard_options) {
  for (const auto& binding : keyboard_options.bindings) {
    int id = hotkey_action_to_id(binding.action);
    winapi::unregister_hotkey(id);
  }
}

// Convert HotkeyAction to human-readable string
const char* hotkey_action_to_string(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return "Navigate Left";
  case HotkeyAction::NavigateDown:
    return "Navigate Down";
  case HotkeyAction::NavigateUp:
    return "Navigate Up";
  case HotkeyAction::NavigateRight:
    return "Navigate Right";
  case HotkeyAction::ToggleSplit:
    return "Toggle Split";
  case HotkeyAction::Exit:
    return "Exit";
  case HotkeyAction::ToggleGlobal:
    return "Toggle Global";
  case HotkeyAction::StoreCell:
    return "Store Cell";
  case HotkeyAction::ClearStored:
    return "Clear Stored";
  case HotkeyAction::Exchange:
    return "Exchange";
  case HotkeyAction::Move:
    return "Move";
  case HotkeyAction::SplitIncrease:
    return "Split Increase";
  case HotkeyAction::SplitDecrease:
    return "Split Decrease";
  case HotkeyAction::ExchangeSiblings:
    return "Exchange Siblings";
  case HotkeyAction::ToggleZen:
    return "Toggle Zen";
  default:
    return "Unknown";
  }
}

// Convert HotkeyAction to direction (for navigation actions)
std::optional<cells::Direction> hotkey_action_to_direction(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return cells::Direction::Left;
  case HotkeyAction::NavigateDown:
    return cells::Direction::Down;
  case HotkeyAction::NavigateUp:
    return cells::Direction::Up;
  case HotkeyAction::NavigateRight:
    return cells::Direction::Right;
  case HotkeyAction::ToggleSplit:
  case HotkeyAction::Exit:
  case HotkeyAction::ToggleGlobal:
  case HotkeyAction::StoreCell:
  case HotkeyAction::ClearStored:
  case HotkeyAction::Exchange:
  case HotkeyAction::Move:
  case HotkeyAction::SplitIncrease:
  case HotkeyAction::SplitDecrease:
  case HotkeyAction::ExchangeSiblings:
  case HotkeyAction::ToggleZen:
  default:
    return std::nullopt;
  }
}

// Move mouse cursor to center of currently selected cell
void move_cursor_to_selected_cell(const cells::System& system) {
  auto selected_cell = cells::get_selected_cell(system);
  if (!selected_cell.has_value()) {
    return;
  }

  auto [cluster_id, cell_index] = *selected_cell;
  const auto* pc = system.get_cluster(cluster_id);
  if (pc == nullptr) {
    return;
  }

  cells::Rect global_rect = cells::get_cell_global_rect(*pc, cell_index);
  long center_x = static_cast<long>(global_rect.x + global_rect.width / 2.0f);
  long center_y = static_cast<long>(global_rect.y + global_rect.height / 2.0f);

  winapi::set_cursor_pos(center_x, center_y);
}

// Handle keyboard navigation: move selection, set foreground, move mouse to center
void handle_keyboard_navigation(cells::System& system, cells::Direction dir) {
  // Try to move selection in the given direction
  if (!system.move_selection(dir)) {
    spdlog::trace("Cannot move selection in direction");
    return;
  }

  // Get the newly selected cell
  auto selected_cell = cells::get_selected_cell(system);
  if (!selected_cell.has_value()) {
    spdlog::error("No cell selected after move_selection");
    return;
  }

  auto [cluster_id, cell_index] = *selected_cell;
  const auto* pc = system.get_cluster(cluster_id);
  if (pc == nullptr) {
    spdlog::error("Failed to get cluster {}", cluster_id);
    return;
  }

  const auto& cell = pc->cluster.cells[static_cast<size_t>(cell_index)];
  if (!cell.leaf_id.has_value()) {
    spdlog::error("Selected cell has no leaf_id");
    return;
  }

  // Get the window handle
  winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(*cell.leaf_id);

  // Set it as the foreground window
  if (!winapi::set_foreground_window(hwnd)) {
    spdlog::error("Failed to set foreground window");
    return;
  }

  // Move mouse to center of selected cell
  move_cursor_to_selected_cell(system);

  spdlog::trace("Navigated to cell {} in cluster {}", cell_index, cluster_id);
}

// Type alias for stored cell used in swap/move operations
using stored_cell_t = std::optional<std::pair<cells::ClusterId, size_t>>;

ActionResult handle_toggle_split(cells::System& system) {
  if (system.toggle_selected_split_dir()) {
    spdlog::info("Toggled split direction");
  }
  return ActionResult::Continue;
}

ActionResult handle_exit() {
  spdlog::info("Exit hotkey pressed, shutting down...");
  return ActionResult::Exit;
}

ActionResult handle_toggle_global(cells::System& system, std::string& out_message) {
  if (system.toggle_cluster_global_split_dir()) {
    if (system.selection.has_value()) {
      const auto* pc = system.get_cluster(system.selection->cluster_id);
      if (pc != nullptr) {
        const char* dir_str =
            (pc->cluster.global_split_dir == cells::SplitDir::Vertical) ? "vertical" : "horizontal";
        spdlog::info("Toggled cluster global split direction: {}", dir_str);
        out_message = std::string("Toggled: ") + dir_str;
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handle_store_cell(cells::System& system, stored_cell_t& stored_cell) {
  if (system.selection.has_value()) {
    const auto* pc = system.get_cluster(system.selection->cluster_id);
    if (pc != nullptr) {
      const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cell_index)];
      if (cell.leaf_id.has_value()) {
        stored_cell = {system.selection->cluster_id, *cell.leaf_id};
        spdlog::info("Stored cell for operation: cluster={}, leaf_id={}",
                     system.selection->cluster_id, *cell.leaf_id);
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handle_clear_stored(stored_cell_t& stored_cell) {
  stored_cell.reset();
  spdlog::info("Cleared stored cell");
  return ActionResult::Continue;
}

ActionResult handle_exchange(cells::System& system, stored_cell_t& stored_cell) {
  if (stored_cell.has_value() && system.selection.has_value()) {
    const auto* pc = system.get_cluster(system.selection->cluster_id);
    if (pc != nullptr) {
      const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cell_index)];
      if (cell.leaf_id.has_value()) {
        auto result = system.swap_cells(system.selection->cluster_id, *cell.leaf_id,
                                        stored_cell->first, stored_cell->second);
        if (result.success) {
          stored_cell.reset();
          spdlog::info("Exchanged cells successfully");
        }
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handle_move(cells::System& system, stored_cell_t& stored_cell) {
  if (stored_cell.has_value() && system.selection.has_value()) {
    const auto* pc = system.get_cluster(system.selection->cluster_id);
    if (pc != nullptr) {
      const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cell_index)];
      if (cell.leaf_id.has_value()) {
        auto result = system.move_cell(stored_cell->first, stored_cell->second,
                                       system.selection->cluster_id, *cell.leaf_id);
        if (result.success) {
          stored_cell.reset();
          spdlog::info("Moved cell successfully");
        }
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handle_split_increase(cells::System& system) {
  if (system.adjust_selected_split_ratio(0.05f)) {
    spdlog::info("Increased split ratio");
    move_cursor_to_selected_cell(system);
  }
  return ActionResult::Continue;
}

ActionResult handle_split_decrease(cells::System& system) {
  if (system.adjust_selected_split_ratio(-0.05f)) {
    spdlog::info("Decreased split ratio");
    move_cursor_to_selected_cell(system);
  }
  return ActionResult::Continue;
}

ActionResult handle_exchange_siblings(cells::System& system) {
  if (system.exchange_selected_with_sibling()) {
    spdlog::info("Exchanged selected cell with sibling");
    move_cursor_to_selected_cell(system);
  }
  return ActionResult::Continue;
}

ActionResult handle_toggle_zen(cells::System& system) {
  if (system.toggle_selected_zen()) {
    spdlog::info("Toggled zen mode");
  }
  return ActionResult::Continue;
}

ActionResult dispatch_hotkey_action(HotkeyAction action, cells::System& system,
                                    stored_cell_t& stored_cell, std::string& out_message) {
  // Handle other actions
  switch (action) {
  case HotkeyAction::ToggleSplit:
    return handle_toggle_split(system);
  case HotkeyAction::Exit:
    return handle_exit();
  case HotkeyAction::ToggleGlobal:
    return handle_toggle_global(system, out_message);
  case HotkeyAction::StoreCell:
    return handle_store_cell(system, stored_cell);
  case HotkeyAction::ClearStored:
    return handle_clear_stored(stored_cell);
  case HotkeyAction::Exchange:
    return handle_exchange(system, stored_cell);
  case HotkeyAction::Move:
    return handle_move(system, stored_cell);
  case HotkeyAction::SplitIncrease:
    return handle_split_increase(system);
  case HotkeyAction::SplitDecrease:
    return handle_split_decrease(system);
  case HotkeyAction::ExchangeSiblings:
    return handle_exchange_siblings(system);
  case HotkeyAction::ToggleZen:
    return handle_toggle_zen(system);
  case HotkeyAction::NavigateLeft:
  case HotkeyAction::NavigateDown:
  case HotkeyAction::NavigateUp:
  case HotkeyAction::NavigateRight: {
    auto dir = hotkey_action_to_direction(action).value();
    handle_keyboard_navigation(system, dir);
    return ActionResult::Continue;
  }
  default:
    return ActionResult::Continue;
  }
}

void update_foreground_selection_from_mouse_position(cells::System& system) {
  auto foreground_hwnd = winapi::get_foreground_window();

  if (foreground_hwnd == nullptr ||
      !cells::has_leaf_id(system, reinterpret_cast<size_t>(foreground_hwnd))) {
    return;
  }

  auto cursor_pos_opt = winapi::get_cursor_pos();
  if (!cursor_pos_opt.has_value()) {
    spdlog::error("Failed to get cursor position");
    return;
  }

  float cursor_x = static_cast<float>(cursor_pos_opt->x);
  float cursor_y = static_cast<float>(cursor_pos_opt->y);

  auto cell_at_cursor = cells::find_cell_at_point(system, cursor_x, cursor_y);

  if (!cell_at_cursor.has_value()) {
    return;
  }

  auto [cluster_id, cell_index] = *cell_at_cursor;

  // Skip selection update if this cluster has an active zen cell
  const auto* zen_check_pc = system.get_cluster(cluster_id);
  if (zen_check_pc != nullptr && zen_check_pc->cluster.zen_cell_index.has_value()) {
    return;
  }

  bool needs_update = !system.selection.has_value() || system.selection->cluster_id != cluster_id ||
                      system.selection->cell_index != cell_index;

  if (!needs_update) {
    return;
  }

  system.selection = cells::CellIndicatorByIndex{cluster_id, cell_index};

  const auto* pc = system.get_cluster(cluster_id);
  if (pc != nullptr) {
    const auto& cell = pc->cluster.cells[static_cast<size_t>(cell_index)];
    if (cell.leaf_id.has_value()) {
      winapi::HWND_T cell_hwnd = reinterpret_cast<winapi::HWND_T>(*cell.leaf_id);
      if (!winapi::set_foreground_window(cell_hwnd)) {
        spdlog::error("Failed to set foreground window for HWND {}", cell_hwnd);
      }
      spdlog::trace("======================Selection updated: cluster={}, cell={}", cluster_id,
                    cell_index);
    }
  }
}

// Helper: Print tile layout from a multi-cluster system
void print_tile_layout(const cells::System& system) {
  size_t total_windows = cells::count_total_leaves(system);
  spdlog::debug("Total windows: {}", total_windows);

  for (const auto& pc : system.clusters) {
    spdlog::debug("--- Monitor {} ---", pc.id);

    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
      if (cell.is_dead || !cell.leaf_id.has_value()) {
        continue;
      }

      size_t hwnd_value = *cell.leaf_id;
      cells::Rect global_rect = cells::get_cell_global_rect(pc, i);

      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(hwnd_value);
      auto window_info = winapi::get_window_info(hwnd);

      spdlog::debug("  Window: \"{}\" ({})", window_info.title, window_info.processName);
      spdlog::debug("    Position: x={}, y={}", static_cast<int>(global_rect.x),
                    static_cast<int>(global_rect.y));
      spdlog::debug("    Size: {}x{}", static_cast<int>(global_rect.width),
                    static_cast<int>(global_rect.height));
    }
  }
}

// Helper: Gather current window state for all monitors
std::vector<cells::ClusterCellIds>
gather_current_window_state(const IgnoreOptions& ignore_options) {
  std::vector<cells::ClusterCellIds> result;
  auto monitors = winapi::get_monitors();

  for (size_t monitor_index = 0; monitor_index < monitors.size(); ++monitor_index) {
    auto hwnds = winapi::get_hwnds_for_monitor(monitor_index, ignore_options);
    std::vector<size_t> cell_ids;
    for (auto hwnd : hwnds) {
      cell_ids.push_back(reinterpret_cast<size_t>(hwnd));
    }
    result.push_back({monitor_index, cell_ids});
  }

  return result;
}

// Helper: Apply tile layout by updating window positions
void apply_tile_layout(const cells::System& system, float zen_percentage) {
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
      if (cell.is_dead || !cell.leaf_id.has_value()) {
        continue;
      }

      size_t hwnd_value = *cell.leaf_id;
      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(hwnd_value);

      // Check if this cell is the zen cell for its cluster
      bool is_zen = pc.cluster.zen_cell_index.has_value() && *pc.cluster.zen_cell_index == i;

      // Use zen display rect (centered at percentage) or normal rect
      cells::Rect global_rect = cells::get_cell_display_rect(pc, i, is_zen, zen_percentage);

      winapi::WindowPosition pos;
      pos.x = static_cast<int>(global_rect.x);
      pos.y = static_cast<int>(global_rect.y);
      pos.width = static_cast<int>(global_rect.width);
      pos.height = static_cast<int>(global_rect.height);

      winapi::TileInfo tile_info{hwnd, pos};
      winapi::update_window_position(tile_info);
    }
  }
}

cells::System create_initial_system(const GlobalOptions& options) {
  auto monitors = winapi::get_monitors();
  winapi::log_monitors(monitors);

  std::vector<cells::ClusterInitInfo> cluster_infos;
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& monitor = monitors[i];
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    auto hwnds = winapi::get_hwnds_for_monitor(i, options.ignoreOptions);
    std::vector<size_t> cell_ids;
    for (auto hwnd : hwnds) {
      cell_ids.push_back(reinterpret_cast<size_t>(hwnd));
    }
    cluster_infos.push_back({i, x, y, w, h, cell_ids});
  }

  return cells::create_system(cluster_infos, options.gapOptions.horizontal,
                              options.gapOptions.vertical);
}

} // namespace

void run_loop_mode(GlobalOptionsProvider& provider) {
  const auto& options = provider.options;

  auto system =
      timed("create_initial_system", [&options] { return create_initial_system(options); });

  // Print initial layout and apply
  spdlog::info("=== Initial Tile Layout ===");
  print_tile_layout(system);

  timed_void("initial apply_tile_layout",
             [&system, &options] { apply_tile_layout(system, options.zenOptions.percentage); });

  // Register keyboard hotkeys
  register_navigation_hotkeys(options.keyboardOptions);

  // Register window move/resize detection hooks
  winapi::register_move_size_hook();

  // Initialize overlay for rendering
  overlay::init();

  // Print keyboard shortcuts
  spdlog::info("=== Keyboard Shortcuts ===");
  for (const auto& binding : options.keyboardOptions.bindings) {
    spdlog::info("  {}: {}", hotkey_action_to_string(binding.action), binding.hotkey);
  }

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  // Store cell for swap/move operations
  stored_cell_t stored_cell;

  // Toast message state
  std::string toast_message;
  auto toast_expiry = std::chrono::steady_clock::now();
  auto toast_duration = std::chrono::milliseconds(options.visualizationOptions.toastDurationMs);

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(options.loopOptions.intervalMs));

    auto loop_start = std::chrono::high_resolution_clock::now();

    // Skip all processing while user is dragging a window - only render
    if (!winapi::is_any_window_being_moved()) {
      // Check for config file changes and hot-reload
      if (provider.refresh()) {
        // Re-register hotkeys with new bindings (options is ref to provider.options, already
        // updated)
        unregister_navigation_hotkeys(options.keyboardOptions);
        register_navigation_hotkeys(options.keyboardOptions);

        // Update gap settings and recompute cell rects
        system.update_gaps(options.gapOptions.horizontal, options.gapOptions.vertical);

        // Update toast duration
        toast_duration = std::chrono::milliseconds(options.visualizationOptions.toastDurationMs);

        spdlog::info("Config hot-reloaded");
      }

      // Check for keyboard hotkeys
      if (auto hotkey_id = winapi::check_keyboard_action()) {
        auto action_opt = id_to_hotkey_action(*hotkey_id);
        if (!action_opt.has_value()) {
          continue; // Unknown hotkey ID
        }
        std::string action_message;
        if (dispatch_hotkey_action(*action_opt, system, stored_cell, action_message) ==
            ActionResult::Exit) {
          break;
        }
        if (!action_message.empty()) {
          toast_message = action_message;
          toast_expiry = std::chrono::steady_clock::now() + toast_duration;
        }
      }

      // Re-gather window state
      auto current_state = timed("gather_current_window_state", [&options] {
        return gather_current_window_state(options.ignoreOptions);
      });

      // Use update to sync
      auto result = timed("update", [&system, &current_state] {
        return system.update(current_state, std::nullopt);
      });

      update_foreground_selection_from_mouse_position(system);

      // If changes detected, log and apply
      if (!result.deleted_leaf_ids.empty() || !result.added_leaf_ids.empty()) {
        // One-line summary at info level
        spdlog::info("Window changes: +{} added, -{} removed", result.added_leaf_ids.size(),
                     result.deleted_leaf_ids.size());

        // Detailed logging at debug level for added windows
        if (!result.added_leaf_ids.empty()) {
          spdlog::debug("Added windows:");
          for (size_t id : result.added_leaf_ids) {
            winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(id);
            std::string title = winapi::get_window_info(hwnd).title;
            spdlog::debug("  + \"{}\"", title);
          }
        }

        spdlog::debug("=== Updated Tile Layout ===");
        print_tile_layout(system);

        // Move mouse to center of the last added cell
        if (!result.added_leaf_ids.empty()) {
          size_t last_added_id = result.added_leaf_ids.back();
          // Find which cluster contains this leaf
          for (const auto& pc : system.clusters) {
            auto cell_index_opt = cells::find_cell_by_leaf_id(pc.cluster, last_added_id);
            if (cell_index_opt.has_value()) {
              cells::Rect global_rect = cells::get_cell_global_rect(pc, *cell_index_opt);
              long center_x = static_cast<long>(global_rect.x + global_rect.width / 2.0f);
              long center_y = static_cast<long>(global_rect.y + global_rect.height / 2.0f);
              winapi::set_cursor_pos(center_x, center_y);
              spdlog::debug("Moved cursor to center of new cell at ({}, {})", center_x, center_y);
              break;
            }
          }
        }
      }

      timed_void("apply_tile_layout",
                 [&system, &options] { apply_tile_layout(system, options.zenOptions.percentage); });
    }

    // Render cell system overlay
    std::string current_toast =
        (std::chrono::steady_clock::now() < toast_expiry) ? toast_message : "";
    renderer::RenderOptions render_opts{
        options.visualizationOptions.normalColor,   options.visualizationOptions.selectedColor,
        options.visualizationOptions.storedColor,   options.visualizationOptions.borderWidth,
        options.visualizationOptions.toastFontSize, options.zenOptions.percentage,
    };
    renderer::render(system, render_opts, stored_cell, current_toast);

    auto loop_end = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
  }

  // Cleanup hotkeys, hooks, and overlay before exit
  unregister_navigation_hotkeys(options.keyboardOptions);
  winapi::unregister_move_size_hook();
  overlay::shutdown();
  spdlog::info("Hotkeys unregistered, hooks unregistered, overlay shutdown, exiting...");
}

} // namespace wintiler
