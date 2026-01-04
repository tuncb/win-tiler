#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <magic_enum/magic_enum.hpp>
#include <vector>

#include "model.h"
#include "multi_cell_renderer.h"
#include "multi_cells.h"
#include "overlay.h"
#include "winapi.h"

namespace wintiler {

namespace {

// Result type for action handlers - signals whether the main loop should continue or exit
enum class ActionResult { Continue, Exit };

// Toast message display state
struct ToastState {
  std::string message;
  std::chrono::steady_clock::time_point expiry;
  std::chrono::milliseconds duration;

  explicit ToastState(std::chrono::milliseconds dur)
      : duration(dur), expiry(std::chrono::steady_clock::now()) {
  }

  void show(std::string_view msg) {
    message = msg;
    expiry = std::chrono::steady_clock::now() + duration;
  }

  void set_duration(std::chrono::milliseconds dur) {
    duration = dur;
  }

  std::optional<std::string> get_visible_message() const {
    if (std::chrono::steady_clock::now() < expiry) {
      return message;
    }
    return std::nullopt;
  }
};

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
  return magic_enum::enum_name(action).data();
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
  case HotkeyAction::CycleSplitMode:
  case HotkeyAction::StoreCell:
  case HotkeyAction::ClearStored:
  case HotkeyAction::Exchange:
  case HotkeyAction::Move:
  case HotkeyAction::SplitIncrease:
  case HotkeyAction::SplitDecrease:
  case HotkeyAction::ExchangeSiblings:
  case HotkeyAction::ToggleZen:
  case HotkeyAction::ResetSplitRatio:
    return std::nullopt;
  }
  return std::nullopt;
}

// Handle keyboard navigation: move selection, set foreground, move mouse to center
void handle_keyboard_navigation(cells::System& system, cells::Direction dir) {
  auto result = system.move_selection(dir);
  if (!result) {
    spdlog::trace("Cannot move selection in direction");
    return;
  }

  winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(result->leaf_id);
  if (!winapi::set_foreground_window(hwnd)) {
    spdlog::error("Failed to set foreground window");
    return;
  }

  winapi::set_cursor_pos(result->center.x, result->center.y);
}

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

ActionResult handle_cycle_split_mode(cells::System& system, std::string& out_message) {
  if (!system.cycle_split_mode()) {
    spdlog::error("Failed to cycle split mode");
  }
  auto mode_str = magic_enum::enum_name(system.split_mode);
  spdlog::info("Cycled split mode: {}", mode_str);
  out_message = std::string("Split mode: ").append(mode_str);
  return ActionResult::Continue;
}

ActionResult handle_store_cell(cells::System& system, std::optional<StoredCell>& stored_cell) {
  if (system.selection.has_value()) {
    const auto& pc = system.clusters[system.selection->cluster_index];
    const auto& cell = pc.cluster.cells[static_cast<size_t>(system.selection->cell_index)];
    if (cell.leaf_id.has_value()) {
      stored_cell = StoredCell{system.selection->cluster_index, *cell.leaf_id};
      spdlog::info("Stored cell for operation: cluster={}, leaf_id={}",
                   system.selection->cluster_index, *cell.leaf_id);
    }
  }
  return ActionResult::Continue;
}

ActionResult handle_clear_stored(std::optional<StoredCell>& stored_cell) {
  stored_cell.reset();
  spdlog::info("Cleared stored cell");
  return ActionResult::Continue;
}

ActionResult handle_exchange(cells::System& system, std::optional<StoredCell>& stored_cell) {
  if (stored_cell.has_value() && system.selection.has_value()) {
    const auto& pc = system.clusters[system.selection->cluster_index];
    const auto& cell = pc.cluster.cells[static_cast<size_t>(system.selection->cell_index)];
    if (cell.leaf_id.has_value()) {
      auto result = system.swap_cells(system.selection->cluster_index, *cell.leaf_id,
                                      stored_cell->cluster_index, stored_cell->leaf_id);
      if (result.has_value()) {
        stored_cell.reset();
        spdlog::info("Exchanged cells successfully");
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handle_move(cells::System& system, std::optional<StoredCell>& stored_cell) {
  if (stored_cell.has_value() && system.selection.has_value()) {
    const auto& pc = system.clusters[system.selection->cluster_index];
    const auto& cell = pc.cluster.cells[static_cast<size_t>(system.selection->cell_index)];
    if (cell.leaf_id.has_value()) {
      auto result = system.move_cell(stored_cell->cluster_index, stored_cell->leaf_id,
                                     system.selection->cluster_index, *cell.leaf_id);
      if (result.has_value()) {
        stored_cell.reset();
        spdlog::info("Moved cell successfully");
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handle_split_increase(cells::System& system) {
  if (auto center = system.adjust_selected_split_ratio(0.05f)) {
    spdlog::info("Increased split ratio");
    winapi::set_cursor_pos(center->x, center->y);
  }
  return ActionResult::Continue;
}

ActionResult handle_split_decrease(cells::System& system) {
  if (auto center = system.adjust_selected_split_ratio(-0.05f)) {
    spdlog::info("Decreased split ratio");
    winapi::set_cursor_pos(center->x, center->y);
  }
  return ActionResult::Continue;
}

ActionResult handle_exchange_siblings(cells::System& system) {
  if (auto center = system.exchange_selected_with_sibling()) {
    spdlog::info("Exchanged selected cell with sibling");
    winapi::set_cursor_pos(center->x, center->y);
  }
  return ActionResult::Continue;
}

ActionResult handle_toggle_zen(cells::System& system) {
  if (system.toggle_selected_zen()) {
    spdlog::info("Toggled zen mode");
  }
  return ActionResult::Continue;
}

ActionResult handle_reset_split_ratio(cells::System& system) {
  if (auto center = system.set_selected_split_ratio(0.5f)) {
    spdlog::info("Reset split ratio to 50%%");
    winapi::set_cursor_pos(center->x, center->y);
  }
  return ActionResult::Continue;
}

// Handle mouse drag-drop move operation
// Returns true if an operation was performed
bool handle_mouse_drop_move(cells::System& system, float zen_percentage,
                            const winapi::LoopInputState& input_state) {
  if (!input_state.drag_info.has_value() || !input_state.drag_info->move_ended) {
    return false;
  }

  // Clear the flag immediately to avoid re-processing
  winapi::clear_drag_ended();

  // Get cursor position from consolidated state
  if (!input_state.cursor_pos.has_value()) {
    spdlog::trace("Mouse drop: could not get cursor position");
    return false;
  }

  size_t source_leaf_id = reinterpret_cast<size_t>(input_state.drag_info->hwnd);
  float cursor_x = static_cast<float>(input_state.cursor_pos->x);
  float cursor_y = static_cast<float>(input_state.cursor_pos->y);
  bool do_exchange = input_state.is_ctrl_pressed;

  auto result =
      system.perform_drop_move(source_leaf_id, cursor_x, cursor_y, zen_percentage, do_exchange);
  if (result.has_value()) {
    winapi::set_cursor_pos(result->cursor_pos.x, result->cursor_pos.y);
    return true;
  }

  spdlog::trace("Mouse drop: {}", result.error());
  return false;
}

// Handle window resize operation to update split ratios
// Returns true if a resize was performed, false otherwise
bool handle_window_resize(cells::System& system, const winapi::LoopInputState& input_state) {
  // Check if drag/resize just ended
  if (!input_state.drag_info.has_value() || !input_state.drag_info->move_ended) {
    return false;
  }

  // Get the HWND and find corresponding cell
  size_t leaf_id = reinterpret_cast<size_t>(input_state.drag_info->hwnd);
  if (!cells::has_leaf_id(system, leaf_id)) {
    return false; // Not a managed window
  }

  // Find cluster and cell index
  size_t cluster_index = 0;
  int cell_index = -1;
  bool found = false;
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    auto idx_opt = cells::find_cell_by_leaf_id(system.clusters[ci].cluster, leaf_id);
    if (idx_opt.has_value()) {
      cluster_index = ci;
      cell_index = *idx_opt;
      found = true;
      break;
    }
  }

  if (!found || cell_index < 0) {
    return false;
  }

  // Skip if fullscreen or zen mode active
  if (system.clusters[cluster_index].cluster.has_fullscreen_cell) {
    return false;
  }
  if (system.clusters[cluster_index].cluster.zen_cell_index.has_value()) {
    return false;
  }

  // Get actual window rect from Windows
  auto actual_rect_opt = winapi::get_window_rect(input_state.drag_info->hwnd);
  if (!actual_rect_opt.has_value()) {
    return false;
  }

  // Convert winapi::WindowPosition to cells::Rect
  cells::Rect actual_rect{
      static_cast<float>(actual_rect_opt->x), static_cast<float>(actual_rect_opt->y),
      static_cast<float>(actual_rect_opt->width), static_cast<float>(actual_rect_opt->height)};

  // Get expected cell rect
  auto expected_rect = cells::get_cell_global_rect(system.clusters[cluster_index], cell_index);

  // Position-only check: skip if size unchanged (with small tolerance for rounding)
  bool size_changed = (std::abs(actual_rect.width - expected_rect.width) > 2.0f ||
                       std::abs(actual_rect.height - expected_rect.height) > 2.0f);
  if (!size_changed) {
    return false; // Only moved, not resized
  }

  // Update split ratio
  bool result = system.update_split_ratio_from_resize(cluster_index, leaf_id, actual_rect);
  if (result) {
    spdlog::info("Window resize: updated split ratio for cluster {}, leaf_id {}", cluster_index,
                 leaf_id);
  }
  return result;
}

ActionResult dispatch_hotkey_action(HotkeyAction action, cells::System& system,
                                    std::optional<StoredCell>& stored_cell,
                                    std::string& out_message) {
  // Handle other actions
  switch (action) {
  case HotkeyAction::ToggleSplit:
    return handle_toggle_split(system);
  case HotkeyAction::Exit:
    return handle_exit();
  case HotkeyAction::CycleSplitMode:
    return handle_cycle_split_mode(system, out_message);
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
  case HotkeyAction::ResetSplitRatio:
    return handle_reset_split_ratio(system);
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

// Helper: Print tile layout from a multi-cluster system
void print_tile_layout(const cells::System& system) {
  size_t total_windows = cells::count_total_leaves(system);
  spdlog::debug("Total windows: {}", total_windows);

  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& pc = system.clusters[cluster_idx];
    spdlog::debug("--- Monitor {} ---", cluster_idx);

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

// Helper: Extract ClusterCellUpdateInfo from consolidated input state
std::vector<cells::ClusterCellUpdateInfo>
extract_window_state_from_input(const winapi::LoopInputState& input_state) {
  std::vector<cells::ClusterCellUpdateInfo> result;

  for (size_t monitor_index = 0; monitor_index < input_state.windows_per_monitor.size();
       ++monitor_index) {
    const auto& windows = input_state.windows_per_monitor[monitor_index];
    std::vector<size_t> cell_ids;
    cell_ids.reserve(windows.size());
    bool has_fullscreen = false;
    for (const auto& win : windows) {
      cell_ids.push_back(reinterpret_cast<size_t>(win.handle));
      if (win.is_fullscreen) {
        has_fullscreen = true;
      }
    }
    result.push_back({monitor_index, cell_ids, has_fullscreen});
  }

  return result;
}

// Helper: Run system update and apply tile positions
void run_update_and_apply_tiles(cells::System& system, const GlobalOptions& options,
                                const winapi::LoopInputState& input_state) {
  auto current_state = extract_window_state_from_input(input_state);

  float cursor_x =
      input_state.cursor_pos.has_value() ? static_cast<float>(input_state.cursor_pos->x) : 0.0f;
  float cursor_y =
      input_state.cursor_pos.has_value() ? static_cast<float>(input_state.cursor_pos->y) : 0.0f;
  float zen_percentage = options.visualizationOptions.renderOptions.zen_percentage;
  size_t fg_leaf_id = reinterpret_cast<size_t>(input_state.foreground_window);

  auto result =
      system.update(current_state, std::nullopt, {cursor_x, cursor_y}, zen_percentage, fg_leaf_id);

  // Apply foreground window change
  if (result.selection_update.window_to_foreground.has_value()) {
    winapi::HWND_T hwnd =
        reinterpret_cast<winapi::HWND_T>(*result.selection_update.window_to_foreground);
    if (!winapi::set_foreground_window(hwnd)) {
      spdlog::error("Failed to set foreground window for HWND {}", hwnd);
    }
  }

  // Apply tile updates
  for (const auto& upd : result.tile_updates) {
    winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(upd.leaf_id);
    winapi::WindowPosition pos{upd.x, upd.y, upd.width, upd.height};
    winapi::TileInfo tile_info{hwnd, pos};
    winapi::update_window_position(tile_info);
  }
}

cells::System create_initial_system_from_monitors(const std::vector<winapi::MonitorInfo>& monitors,
                                                  const GlobalOptions& options) {
  std::vector<cells::ClusterInitInfo> cluster_infos;
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& monitor = monitors[i];
    // Workspace bounds (for tiling)
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);
    // Full monitor bounds (for pointer detection)
    float mx = static_cast<float>(monitor.rect.left);
    float my = static_cast<float>(monitor.rect.top);
    float mw = static_cast<float>(monitor.rect.right - monitor.rect.left);
    float mh = static_cast<float>(monitor.rect.bottom - monitor.rect.top);

    auto hwnds = winapi::get_hwnds_for_monitor(i, options.ignoreOptions);
    std::vector<size_t> cell_ids;
    for (auto hwnd : hwnds) {
      cell_ids.push_back(reinterpret_cast<size_t>(hwnd));
    }
    cluster_infos.push_back({x, y, w, h, mx, my, mw, mh, cell_ids});
  }

  return cells::create_system(cluster_infos, options.gapOptions.horizontal,
                              options.gapOptions.vertical);
}

cells::System create_initial_system(const GlobalOptions& options) {
  auto monitors = winapi::get_monitors();
  winapi::log_monitors(monitors);
  return create_initial_system_from_monitors(monitors, options);
}

// Handle config file hot-reload
void handle_config_refresh(GlobalOptionsProvider& provider, cells::System& system,
                           ToastState& toast) {
  if (!provider.refresh()) {
    return;
  }
  const auto& options = provider.options;
  unregister_navigation_hotkeys(options.keyboardOptions);
  register_navigation_hotkeys(options.keyboardOptions);
  system.update_gaps(options.gapOptions.horizontal, options.gapOptions.vertical);
  toast.set_duration(std::chrono::milliseconds(options.visualizationOptions.toastDurationMs));
  spdlog::info("Config hot-reloaded");
}

// Handle monitor configuration changes, returns true if change occurred
bool handle_monitor_change(std::vector<winapi::MonitorInfo>& monitors, const GlobalOptions& options,
                           cells::System& system, std::optional<StoredCell>& stored_cell) {
  auto current_monitors = winapi::get_monitors();
  if (winapi::monitors_equal(monitors, current_monitors)) {
    return false;
  }
  spdlog::info("Monitor configuration changed, reinitializing system...");
  winapi::log_monitors(current_monitors);
  monitors = current_monitors;
  system = create_initial_system_from_monitors(monitors, options);
  stored_cell.reset();
  spdlog::info("=== Reinitialized Tile Layout ===");
  print_tile_layout(system);
  // Tile layout will be applied by the main loop's system.update() call
  return true;
}

} // namespace

void run_loop_mode(GlobalOptionsProvider& provider) {
  const auto& options = provider.options;

  // Get initial monitor configuration and create system
  auto monitors = winapi::get_monitors();
  winapi::log_monitors(monitors);
  auto system = create_initial_system_from_monitors(monitors, options);

  // Print initial layout and apply via system.update()
  spdlog::info("=== Initial Tile Layout ===");
  print_tile_layout(system);

  {
    auto input_state = winapi::gather_loop_input_state(options.ignoreOptions);
    run_update_and_apply_tiles(system, options, input_state);
  }

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
  std::optional<StoredCell> stored_cell;

  // Toast message state
  ToastState toast(std::chrono::milliseconds(options.visualizationOptions.toastDurationMs));

  while (true) {
    // Wait for messages (hotkeys) or timeout - responds immediately to hotkeys
    winapi::wait_for_messages_or_timeout(options.loopOptions.intervalMs);

    auto loop_start = std::chrono::high_resolution_clock::now();

    // Gather all Windows API input state in a single call
    auto input_state = winapi::gather_loop_input_state(options.ignoreOptions);

    // Skip all processing while user is dragging a window - only render
    if (input_state.is_any_window_being_moved) {
      renderer::render(system, options.visualizationOptions.renderOptions, stored_cell,
                       toast.get_visible_message());
      auto loop_end = std::chrono::high_resolution_clock::now();
      spdlog::trace(
          "loop iteration total: {}us",
          std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
      continue;
    }

    // Check if a drag operation just completed
    if (input_state.drag_info.has_value() && input_state.drag_info->move_ended) {
      // Try resize first (size changed = ratio update)
      bool resized = handle_window_resize(system, input_state);

      if (resized) {
        // Resize performed - clear drag flag; layout applied by system.update() below
        winapi::clear_drag_ended();
      } else {
        // Try move/swap (clear_drag_ended called inside if successful)
        handle_mouse_drop_move(system, options.visualizationOptions.renderOptions.zen_percentage,
                               input_state);
      }
    }

    // Check for config file changes and hot-reload
    handle_config_refresh(provider, system, toast);

    // Check for monitor configuration changes (tile layout applied by system.update() below)
    handle_monitor_change(monitors, options, system, stored_cell);

    // Check for keyboard hotkeys (kept separate - has side effects on message queue)
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
        toast.show(action_message);
      }
    }

    // Extract window state from consolidated input
    auto current_state = extract_window_state_from_input(input_state);

    // Use update to sync - cursor position from consolidated state
    float cursor_x =
        input_state.cursor_pos.has_value() ? static_cast<float>(input_state.cursor_pos->x) : 0.0f;
    float cursor_y =
        input_state.cursor_pos.has_value() ? static_cast<float>(input_state.cursor_pos->y) : 0.0f;
    float zen_percentage = options.visualizationOptions.renderOptions.zen_percentage;
    size_t fg_leaf_id = reinterpret_cast<size_t>(input_state.foreground_window);
    auto result = system.update(current_state, std::nullopt, {cursor_x, cursor_y}, zen_percentage,
                                fg_leaf_id);

    // Apply foreground window change (selection already updated inside update())
    if (result.selection_update.window_to_foreground.has_value()) {
      winapi::HWND_T hwnd =
          reinterpret_cast<winapi::HWND_T>(*result.selection_update.window_to_foreground);
      if (!winapi::set_foreground_window(hwnd)) {
        spdlog::error("Failed to set foreground window for HWND {}", hwnd);
      }
    }

    // Log window changes
    if (!result.deleted_leaf_ids.empty() || !result.added_leaf_ids.empty()) {
      spdlog::info("Window changes: +{} added, -{} removed", result.added_leaf_ids.size(),
                   result.deleted_leaf_ids.size());
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
    }

    // Move cursor to center of new window (if any)
    if (result.new_window_cursor_pos.has_value()) {
      winapi::set_cursor_pos(result.new_window_cursor_pos->x, result.new_window_cursor_pos->y);
      spdlog::debug("Moved cursor to center of new cell at ({}, {})",
                    result.new_window_cursor_pos->x, result.new_window_cursor_pos->y);
    }

    // Apply tile updates
    for (const auto& upd : result.tile_updates) {
      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(upd.leaf_id);
      winapi::WindowPosition pos{upd.x, upd.y, upd.width, upd.height};
      winapi::TileInfo tile_info{hwnd, pos};
      winapi::update_window_position(tile_info);
    }

    // Render cell system overlay
    renderer::render(system, options.visualizationOptions.renderOptions, stored_cell,
                     toast.get_visible_message());

    auto loop_end = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "=======================loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
  }

  // Cleanup hotkeys, hooks, and overlay before exit
  unregister_navigation_hotkeys(options.keyboardOptions);
  winapi::unregister_move_size_hook();
  overlay::shutdown();
  spdlog::info("Hotkeys unregistered, hooks unregistered, overlay shutdown, exiting...");
}

} // namespace wintiler
