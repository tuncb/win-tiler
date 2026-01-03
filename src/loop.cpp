#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <magic_enum/magic_enum.hpp>
#include <unordered_set>
#include <vector>

#include "model.h"
#include "multi_cell_renderer.h"
#include "multi_cells.h"
#include "overlay.h"
#include "utility.h"
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

// Move mouse cursor to center of currently selected cell
void move_cursor_to_selected_cell(const cells::System& system) {
  auto selected_cell = cells::get_selected_cell(system);
  if (!selected_cell.has_value()) {
    return;
  }

  auto [cluster_index, cell_index] = *selected_cell;
  const auto& pc = system.clusters[cluster_index];

  cells::Rect global_rect = cells::get_cell_global_rect(pc, cell_index);
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

  auto [cluster_index, cell_index] = *selected_cell;
  const auto& pc = system.clusters[cluster_index];

  const auto& cell = pc.cluster.cells[static_cast<size_t>(cell_index)];
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

  spdlog::trace("Navigated to cell {} in cluster {}", cell_index, cluster_index);
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

ActionResult handle_reset_split_ratio(cells::System& system) {
  if (system.set_selected_split_ratio(0.5f)) {
    spdlog::info("Reset split ratio to 50%%");
    move_cursor_to_selected_cell(system);
  }
  return ActionResult::Continue;
}

// Handle mouse drag-drop move operation
// Returns true if an operation was performed
bool handle_mouse_drop_move(cells::System& system,
                            const std::unordered_set<size_t>& fullscreen_clusters,
                            float zen_percentage, const winapi::LoopInputState& input_state) {
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

  float cursor_x = static_cast<float>(input_state.cursor_pos->x);
  float cursor_y = static_cast<float>(input_state.cursor_pos->y);

  // Find cell at cursor position (target)
  auto target_cell = cells::find_cell_at_point(system, cursor_x, cursor_y, zen_percentage);
  if (!target_cell.has_value()) {
    spdlog::trace("Mouse drop: no cell at cursor position ({}, {})", cursor_x, cursor_y);
    return false;
  }

  auto [target_cluster_index, target_cell_index] = *target_cell;

  // Skip if target cluster has fullscreen app
  if (fullscreen_clusters.contains(target_cluster_index)) {
    spdlog::trace("Mouse drop: target cluster has fullscreen app");
    return false;
  }

  // Get target cell's leaf_id
  const auto& target_pc = system.clusters[target_cluster_index];
  const auto& target_cell_data = target_pc.cluster.cells[static_cast<size_t>(target_cell_index)];
  if (!target_cell_data.leaf_id.has_value()) {
    return false;
  }
  size_t target_leaf_id = *target_cell_data.leaf_id;

  // Find source cell by dragged HWND
  size_t source_leaf_id = reinterpret_cast<size_t>(input_state.drag_info->hwnd);

  // Check if source window is managed by the system
  if (!cells::has_leaf_id(system, source_leaf_id)) {
    spdlog::trace("Mouse drop: dragged window not managed by system");
    return false;
  }

  // Find which cluster contains the source
  size_t source_cluster_index = 0;
  bool found_source = false;
  for (size_t i = 0; i < system.clusters.size(); ++i) {
    auto cell_idx = cells::find_cell_by_leaf_id(system.clusters[i].cluster, source_leaf_id);
    if (cell_idx.has_value()) {
      source_cluster_index = i;
      found_source = true;
      break;
    }
  }

  if (!found_source) {
    return false;
  }

  // Check if dropping on same cell (source == target)
  if (source_cluster_index == target_cluster_index && source_leaf_id == target_leaf_id) {
    spdlog::trace("Mouse drop: dropped on same cell, no-op");
    return false;
  }

  // Check if Ctrl is held for exchange operation
  bool do_exchange = input_state.is_ctrl_pressed;

  if (do_exchange) {
    // Exchange: swap source and target positions
    auto result = system.swap_cells(source_cluster_index, source_leaf_id, target_cluster_index,
                                    target_leaf_id);

    if (result.has_value()) {
      spdlog::info("Mouse drop: exchanged windows between cluster {} and cluster {}",
                   source_cluster_index, target_cluster_index);
      move_cursor_to_selected_cell(system);
      return true;
    } else {
      spdlog::warn("Mouse drop: exchange failed - {}", result.error());
      return false;
    }
  } else {
    // Move: source becomes sibling of target
    auto result = system.move_cell(source_cluster_index, source_leaf_id, target_cluster_index,
                                   target_leaf_id);

    if (result.has_value()) {
      spdlog::info("Mouse drop: moved window from cluster {} to cluster {}", source_cluster_index,
                   target_cluster_index);
      move_cursor_to_selected_cell(system);
      return true;
    } else {
      spdlog::warn("Mouse drop: move failed - {}", result.error());
      return false;
    }
  }
}

// Handle window resize operation to update split ratios
// Returns true if a resize was performed, false otherwise
bool handle_window_resize(cells::System& system,
                          const std::unordered_set<size_t>& fullscreen_clusters,
                          const winapi::LoopInputState& input_state) {
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
  if (fullscreen_clusters.contains(cluster_index)) {
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

void update_foreground_selection_from_mouse_position(
    cells::System& system, const std::unordered_set<size_t>& fullscreen_clusters,
    float zen_percentage, const winapi::LoopInputState& input_state) {
  if (input_state.foreground_window == nullptr ||
      !cells::has_leaf_id(system, reinterpret_cast<size_t>(input_state.foreground_window))) {
    return;
  }

  if (!input_state.cursor_pos.has_value()) {
    spdlog::error("Failed to get cursor position");
    return;
  }

  float cursor_x = static_cast<float>(input_state.cursor_pos->x);
  float cursor_y = static_cast<float>(input_state.cursor_pos->y);

  auto cell_at_cursor = cells::find_cell_at_point(system, cursor_x, cursor_y, zen_percentage);

  if (!cell_at_cursor.has_value()) {
    return;
  }

  auto [cluster_index, cell_index] = *cell_at_cursor;

  // Skip selection update if this cluster has a fullscreen app
  if (fullscreen_clusters.contains(cluster_index)) {
    return;
  }

  // Skip selection update if this cluster has a zen cell and we're NOT hovering over it
  const auto& zen_check_pc = system.clusters[cluster_index];
  if (zen_check_pc.cluster.zen_cell_index.has_value() &&
      *zen_check_pc.cluster.zen_cell_index != cell_index) {
    return;
  }

  bool needs_update = !system.selection.has_value() ||
                      system.selection->cluster_index != cluster_index ||
                      system.selection->cell_index != cell_index;

  if (!needs_update) {
    return;
  }

  system.selection = cells::CellIndicatorByIndex{cluster_index, cell_index};

  const auto& pc = system.clusters[cluster_index];
  const auto& cell = pc.cluster.cells[static_cast<size_t>(cell_index)];
  if (cell.leaf_id.has_value()) {
    winapi::HWND_T cell_hwnd = reinterpret_cast<winapi::HWND_T>(*cell.leaf_id);
    if (!winapi::set_foreground_window(cell_hwnd)) {
      spdlog::error("Failed to set foreground window for HWND {}", cell_hwnd);
    }
    spdlog::trace("======================Selection updated: cluster={}, cell={}", cluster_index,
                  cell_index);
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

// Helper: Extract ClusterCellIds from consolidated input state
std::vector<cells::ClusterCellIds>
extract_window_state_from_input(const winapi::LoopInputState& input_state) {
  std::vector<cells::ClusterCellIds> result;

  for (size_t monitor_index = 0; monitor_index < input_state.windows_per_monitor.size();
       ++monitor_index) {
    const auto& windows = input_state.windows_per_monitor[monitor_index];
    std::vector<size_t> cell_ids;
    cell_ids.reserve(windows.size());
    for (const auto& win : windows) {
      cell_ids.push_back(reinterpret_cast<size_t>(win.handle));
    }
    result.push_back({monitor_index, cell_ids});
  }

  return result;
}

// Helper: Update fullscreen state for all clusters using pre-gathered data
void update_fullscreen_state(std::unordered_set<size_t>& fullscreen_clusters,
                             const winapi::LoopInputState& input_state) {
  std::unordered_set<size_t> new_fullscreen;

  for (size_t cluster_idx = 0; cluster_idx < input_state.windows_per_monitor.size();
       ++cluster_idx) {
    const auto& windows = input_state.windows_per_monitor[cluster_idx];
    // Check if any window in this cluster is fullscreen
    for (const auto& win : windows) {
      if (win.is_fullscreen) {
        new_fullscreen.insert(cluster_idx);
        break;
      }
    }

    // Log state changes
    bool was_fullscreen = fullscreen_clusters.contains(cluster_idx);
    bool is_fullscreen = new_fullscreen.contains(cluster_idx);
    if (is_fullscreen && !was_fullscreen) {
      spdlog::debug("Fullscreen app detected on monitor {}", cluster_idx);
    } else if (!is_fullscreen && was_fullscreen) {
      spdlog::debug("Fullscreen app exited on monitor {}", cluster_idx);
    }
  }

  fullscreen_clusters = std::move(new_fullscreen);
}

// Helper: Apply tile layout by updating window positions
void apply_tile_layout(const cells::System& system, float zen_percentage,
                       const std::unordered_set<size_t>& fullscreen_clusters) {
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& pc = system.clusters[cluster_idx];
    // Skip tiling if this cluster has a fullscreen app
    if (fullscreen_clusters.contains(cluster_idx)) {
      continue;
    }

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
                           cells::System& system, std::unordered_set<size_t>& fullscreen_clusters,
                           std::optional<StoredCell>& stored_cell) {
  auto current_monitors = winapi::get_monitors();
  if (winapi::monitors_equal(monitors, current_monitors)) {
    return false;
  }
  spdlog::info("Monitor configuration changed, reinitializing system...");
  winapi::log_monitors(current_monitors);
  monitors = current_monitors;
  system = create_initial_system_from_monitors(monitors, options);
  fullscreen_clusters.clear();
  stored_cell.reset();
  spdlog::info("=== Reinitialized Tile Layout ===");
  print_tile_layout(system);
  apply_tile_layout(system, options.visualizationOptions.renderOptions.zen_percentage,
                    fullscreen_clusters);
  return true;
}

// Handle logging and cursor positioning for window changes
void handle_window_changes(const cells::System& system, const cells::UpdateResult& result) {
  if (result.deleted_leaf_ids.empty() && result.added_leaf_ids.empty()) {
    return;
  }

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

  if (!result.added_leaf_ids.empty()) {
    size_t last_added_id = result.added_leaf_ids.back();
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

} // namespace

void run_loop_mode(GlobalOptionsProvider& provider) {
  const auto& options = provider.options;

  // Get initial monitor configuration and create system
  auto monitors = winapi::get_monitors();
  winapi::log_monitors(monitors);
  auto system = timed("create_initial_system", [&monitors, &options] {
    return create_initial_system_from_monitors(monitors, options);
  });

  // Track which clusters have fullscreen apps
  std::unordered_set<size_t> fullscreen_clusters;

  // Print initial layout and apply
  spdlog::info("=== Initial Tile Layout ===");
  print_tile_layout(system);

  timed_void("initial apply_tile_layout", [&system, &options, &fullscreen_clusters] {
    apply_tile_layout(system, options.visualizationOptions.renderOptions.zen_percentage,
                      fullscreen_clusters);
  });

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
    auto input_state = timed("gather_loop_input_state", [&options] {
      return winapi::gather_loop_input_state(options.ignoreOptions);
    });

    // Skip all processing while user is dragging a window - only render
    if (input_state.is_any_window_being_moved) {
      renderer::render(system, options.visualizationOptions.renderOptions, stored_cell,
                       toast.get_visible_message(), fullscreen_clusters);
      auto loop_end = std::chrono::high_resolution_clock::now();
      spdlog::trace(
          "loop iteration total: {}us",
          std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
      continue;
    }

    // Check if a drag operation just completed
    if (input_state.drag_info.has_value() && input_state.drag_info->move_ended) {
      // Try resize first (size changed = ratio update)
      bool resized = handle_window_resize(system, fullscreen_clusters, input_state);

      if (resized) {
        // Resize performed - apply layout and clear drag flag
        apply_tile_layout(system, options.visualizationOptions.renderOptions.zen_percentage,
                          fullscreen_clusters);
        winapi::clear_drag_ended();
      } else if (handle_mouse_drop_move(system, fullscreen_clusters,
                                        options.visualizationOptions.renderOptions.zen_percentage,
                                        input_state)) {
        // Move/swap performed (clear_drag_ended already called inside)
        apply_tile_layout(system, options.visualizationOptions.renderOptions.zen_percentage,
                          fullscreen_clusters);
      }
    }

    // Check for config file changes and hot-reload
    handle_config_refresh(provider, system, toast);

    // Check for monitor configuration changes
    if (timed("handle_monitor_change", [&] {
          return handle_monitor_change(monitors, options, system, fullscreen_clusters, stored_cell);
        })) {
      continue;
    }

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
    auto current_state = timed("extract_window_state", [&input_state] {
      return extract_window_state_from_input(input_state);
    });

    // Use update to sync - cursor position from consolidated state
    float cursor_x =
        input_state.cursor_pos.has_value() ? static_cast<float>(input_state.cursor_pos->x) : 0.0f;
    float cursor_y =
        input_state.cursor_pos.has_value() ? static_cast<float>(input_state.cursor_pos->y) : 0.0f;
    auto result = system.update(current_state, std::nullopt, {cursor_x, cursor_y});

    // Update fullscreen state before selection (affects mouse selection and rendering)
    update_fullscreen_state(fullscreen_clusters, input_state);

    update_foreground_selection_from_mouse_position(
        system, fullscreen_clusters, options.visualizationOptions.renderOptions.zen_percentage,
        input_state);

    // Log window changes and move cursor to new windows
    handle_window_changes(system, result);

    timed_void("apply_tile_layout", [&system, &options, &fullscreen_clusters] {
      apply_tile_layout(system, options.visualizationOptions.renderOptions.zen_percentage,
                        fullscreen_clusters);
    });

    // Render cell system overlay
    timed_void("render", [&] {
      renderer::render(system, options.visualizationOptions.renderOptions, stored_cell,
                       toast.get_visible_message(), fullscreen_clusters);
    });

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
