#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <magic_enum/magic_enum.hpp>
#include <vector>

#include "controller.h"
#include "engine.h"
#include "model.h"
#include "multi_cell_renderer.h"
#include "multi_engine.h"
#include "overlay.h"
#include "winapi.h"

namespace wintiler {

// Empty data struct for now - extension point for future per-desktop state
struct LoopDesktopData {};

namespace {

// Result type for action handlers - signals whether the main loop should continue or exit
enum class LoopActionResult { Continue, Exit };

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

// Handle mouse drag-drop move operation
// Returns true if an operation was performed
bool handle_mouse_drop_move(Engine& engine, const std::vector<std::vector<ctrl::Rect>>& geometries,
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
      engine.perform_drop_move(source_leaf_id, cursor_x, cursor_y, geometries, do_exchange);
  if (result.has_value()) {
    winapi::set_cursor_pos(result->cursor_pos.x, result->cursor_pos.y);
    return true;
  }

  return false;
}

// Handle window resize operation to update split ratios
// Returns true if a resize was performed, false otherwise
bool handle_window_resize(Engine& engine, const std::vector<std::vector<ctrl::Rect>>& geometries,
                          const winapi::LoopInputState& input_state) {
  // Check if drag/resize just ended
  if (!input_state.drag_info.has_value() || !input_state.drag_info->move_ended) {
    return false;
  }

  // Get the HWND and find corresponding cell
  size_t leaf_id = reinterpret_cast<size_t>(input_state.drag_info->hwnd);
  if (!ctrl::has_leaf_id(engine.system, leaf_id)) {
    return false; // Not a managed window
  }

  // Find cluster and cell index
  int cluster_index = -1;
  for (size_t ci = 0; ci < engine.system.clusters.size(); ++ci) {
    auto idx_opt = ctrl::find_cell_by_leaf_id(engine.system.clusters[ci], leaf_id);
    if (idx_opt.has_value()) {
      cluster_index = static_cast<int>(ci);
      break;
    }
  }

  if (cluster_index < 0) {
    return false;
  }

  // Skip if fullscreen or zen mode active
  const auto& cluster = engine.system.clusters[static_cast<size_t>(cluster_index)];
  if (cluster.has_fullscreen_cell || cluster.zen_cell_index.has_value()) {
    return false;
  }

  // Get actual window rect from Windows
  auto actual_rect_opt = winapi::get_window_rect(input_state.drag_info->hwnd);
  if (!actual_rect_opt.has_value()) {
    return false;
  }

  // Convert winapi::WindowPosition to ctrl::Rect
  ctrl::Rect actual_rect{
      static_cast<float>(actual_rect_opt->x), static_cast<float>(actual_rect_opt->y),
      static_cast<float>(actual_rect_opt->width), static_cast<float>(actual_rect_opt->height)};

  // Get expected cell rect from geometries
  auto cell_idx = ctrl::find_cell_by_leaf_id(cluster, leaf_id);
  if (!cell_idx.has_value() ||
      static_cast<size_t>(*cell_idx) >= geometries[static_cast<size_t>(cluster_index)].size()) {
    return false;
  }
  const auto& expected_rect =
      geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(*cell_idx)];

  // Position-only check: skip if size unchanged (with small tolerance for rounding)
  bool size_changed = (std::abs(actual_rect.width - expected_rect.width) > 2.0f ||
                       std::abs(actual_rect.height - expected_rect.height) > 2.0f);
  if (!size_changed) {
    return false; // Only moved, not resized
  }

  // Update split ratio using the geometry (which now includes internal node rects)
  bool result = engine.handle_resize(cluster_index, leaf_id, actual_rect,
                                     geometries[static_cast<size_t>(cluster_index)]);
  if (result) {
    spdlog::info("Window resize: updated split ratio for cluster {}, leaf_id {}", cluster_index,
                 leaf_id);
  }
  return result;
}

// Helper: Print tile layout from a multi-cluster system
void print_tile_layout(const ctrl::System& system,
                       const std::vector<std::vector<ctrl::Rect>>& geometries) {
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& cluster = system.clusters[cluster_idx];
    spdlog::debug("--- Monitor {} ---", cluster_idx);

    if (cluster_idx >= geometries.size()) {
      continue;
    }
    const auto& rects = geometries[cluster_idx];

    for (int i = 0; i < cluster.tree.size(); ++i) {
      if (!cluster.tree.is_leaf(i)) {
        continue;
      }
      const auto& cell_data = cluster.tree[i];
      if (!cell_data.leaf_id.has_value()) {
        continue;
      }

      size_t hwnd_value = *cell_data.leaf_id;
      if (static_cast<size_t>(i) >= rects.size()) {
        continue;
      }
      const auto& global_rect = rects[static_cast<size_t>(i)];

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
std::vector<ctrl::ClusterCellUpdateInfo>
extract_window_state_from_input(const winapi::LoopInputState& input_state) {
  std::vector<ctrl::ClusterCellUpdateInfo> result;

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
    result.push_back({cell_ids, has_fullscreen});
  }

  return result;
}

// Helper: Apply tile positions from geometries
void apply_tile_positions(const ctrl::System& system,
                          const std::vector<std::vector<ctrl::Rect>>& geometries) {
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& cluster = system.clusters[ci];

    // Skip clusters with fullscreen windows
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

      if (static_cast<size_t>(i) >= rects.size()) {
        continue;
      }
      const auto& r = rects[static_cast<size_t>(i)];

      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(*cell_data.leaf_id);
      winapi::WindowPosition pos{static_cast<int>(r.x), static_cast<int>(r.y),
                                 static_cast<int>(r.width), static_cast<int>(r.height)};
      winapi::TileInfo tile_info{hwnd, pos};
      winapi::update_window_position(tile_info);
    }
  }
}

// Helper: Update selection based on mouse hover position
void update_selection_from_hover(Engine& engine,
                                 const std::vector<std::vector<ctrl::Rect>>& geometries,
                                 const winapi::LoopInputState& input_state) {
  if (!input_state.cursor_pos.has_value()) {
    return;
  }

  float cursor_x = static_cast<float>(input_state.cursor_pos->x);
  float cursor_y = static_cast<float>(input_state.cursor_pos->y);

  auto hover_info = engine.get_hover_info(cursor_x, cursor_y, geometries);
  if (hover_info.cell.has_value()) {
    // Update selection if it's different
    if (!engine.system.selection.has_value() ||
        engine.system.selection->cluster_index != hover_info.cell->cluster_index ||
        engine.system.selection->cell_index != hover_info.cell->cell_index) {
      engine.system.selection = *hover_info.cell;
    }
  }
}

std::vector<ctrl::ClusterInitInfo>
create_cluster_infos_from_monitors(const std::vector<winapi::MonitorInfo>& monitors,
                                   const GlobalOptions& options) {
  std::vector<ctrl::ClusterInitInfo> cluster_infos;
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
  return cluster_infos;
}

void initialize_engine_from_monitors(Engine& engine,
                                     const std::vector<winapi::MonitorInfo>& monitors,
                                     const GlobalOptions& options) {
  auto cluster_infos = create_cluster_infos_from_monitors(monitors, options);
  engine.init(cluster_infos);
}

// Handle config file hot-reload
void handle_config_refresh(GlobalOptionsProvider& provider, Engine& engine, ToastState& toast) {
  if (!provider.refresh()) {
    return;
  }
  const auto& options = provider.options;
  unregister_navigation_hotkeys(options.keyboardOptions);
  register_navigation_hotkeys(options.keyboardOptions);
  toast.set_duration(std::chrono::milliseconds(options.visualizationOptions.toastDurationMs));
  spdlog::info("Config hot-reloaded");
}

// Handle monitor configuration changes, returns true if change occurred
bool handle_monitor_change(std::vector<winapi::MonitorInfo>& monitors, const GlobalOptions& options,
                           Engine& engine) {
  auto current_monitors = winapi::get_monitors();
  if (winapi::monitors_equal(monitors, current_monitors)) {
    return false;
  }
  spdlog::info("Monitor configuration changed, reinitializing system...");
  winapi::log_monitors(current_monitors);
  monitors = current_monitors;
  initialize_engine_from_monitors(engine, monitors, options);
  engine.clear_stored_cell();
  spdlog::info("=== Reinitialized Tile Layout ===");
  // Tile layout will be printed and applied by the main loop
  return true;
}

} // namespace

void run_loop_mode(GlobalOptionsProvider& provider) {
  const auto& options = provider.options;

  // Get initial monitor configuration and create engine
  auto monitors = winapi::get_monitors();
  winapi::log_monitors(monitors);

  // MultiEngine manages separate tiling state per virtual desktop
  // Uses GUID strings as desktop identifiers
  MultiEngine<LoopDesktopData, std::string> multi_engine;

  // Gap and zen settings (read per-frame from options in case of hot-reload)
  float gap_h = options.gapOptions.horizontal;
  float gap_v = options.gapOptions.vertical;
  float zen_pct = options.visualizationOptions.renderOptions.zen_percentage;

  // Geometries computed per-frame after desktop is determined
  std::vector<std::vector<ctrl::Rect>> geometries;

  // Register keyboard hotkeys
  register_navigation_hotkeys(options.keyboardOptions);

  // Register window move/resize detection hooks
  winapi::register_move_size_hook();

  // Register session/power notifications for pause on lock/sleep/display-off
  winapi::register_session_power_notifications();

  // Initialize virtual desktop manager for desktop ID detection
  winapi::register_virtual_desktop_notifications();

  // Initialize overlay for rendering
  overlay::init();

  // Print keyboard shortcuts
  spdlog::info("=== Keyboard Shortcuts ===");
  for (const auto& binding : options.keyboardOptions.bindings) {
    spdlog::info("  {}: {}", hotkey_action_to_string(binding.action), binding.hotkey);
  }

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  // Toast message state
  ToastState toast(std::chrono::milliseconds(options.visualizationOptions.toastDurationMs));

  while (true) {
    // Wait for messages (hotkeys) or timeout - responds immediately to hotkeys
    winapi::wait_for_messages_or_timeout(options.loopOptions.intervalMs);

    // Block if session is paused (locked, sleeping, or display off)
    if (winapi::is_session_paused()) {
      spdlog::debug("Session paused, waiting for resume...");
      winapi::wait_for_session_active();
      spdlog::debug("Session resumed, continuing loop");
      continue; // Re-gather state after resume
    }

    auto loop_start = std::chrono::high_resolution_clock::now();

    // Gather all Windows API input state in a single call
    auto input_state = winapi::gather_loop_input_state(options.ignoreOptions);

    // Virtual desktop handling via desktop_id from managed windows
    if (!input_state.desktop_id.has_value()) {
      // No windows - skip iteration
      spdlog::debug("No desktop ID (no windows), skipping iteration");
      overlay::clear();
      continue;
    }

    // Virtual desktop management - create new desktop on first encounter, switch as needed
    const std::string& current_desktop_id = *input_state.desktop_id;

    // Create desktop if this is a new virtual desktop
    if (!multi_engine.has_desktop(current_desktop_id)) {
      auto cluster_infos = create_cluster_infos_from_monitors(monitors, provider.options);
      multi_engine.create_desktop(current_desktop_id, cluster_infos);
      spdlog::info("Created new virtual desktop engine: {}", current_desktop_id);
    }

    // Switch to current desktop if needed
    if (!multi_engine.has_current() || *multi_engine.current_id != current_desktop_id) {
      multi_engine.switch_to(current_desktop_id);
      spdlog::info("Switched to virtual desktop: {}", current_desktop_id);
    }

    // Get reference to current desktop's engine
    auto& engine = multi_engine.current().engine;

    // Update gap and zen settings (in case config was reloaded)
    gap_h = provider.options.gapOptions.horizontal;
    gap_v = provider.options.gapOptions.vertical;
    zen_pct = provider.options.visualizationOptions.renderOptions.zen_percentage;

    // Compute geometries once per frame
    geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);

    // Skip all processing while user is dragging a window - only render
    if (input_state.is_any_window_being_moved) {
      renderer::render(engine.system, geometries, options.visualizationOptions.renderOptions,
                       engine.stored_cell, toast.get_visible_message());
      auto loop_end = std::chrono::high_resolution_clock::now();
      spdlog::trace(
          "loop iteration total: {}us",
          std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
      continue;
    }

    // Check if a drag operation just completed
    if (input_state.drag_info.has_value() && input_state.drag_info->move_ended) {
      // Try resize first (size changed = ratio update)
      bool resized = handle_window_resize(engine, geometries, input_state);

      if (resized) {
        // Resize performed - clear drag flag; layout applied below
        winapi::clear_drag_ended();
      } else {
        // Try move/swap (clear_drag_ended called inside if successful)
        handle_mouse_drop_move(engine, geometries, input_state);
      }

      // Recompute geometries after drag operation
      geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);
    }

    // Check for config file changes and hot-reload
    handle_config_refresh(provider, engine, toast);

    // Check for monitor configuration changes
    if (handle_monitor_change(monitors, provider.options, engine)) {
      geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);
      spdlog::debug("=== Updated Tile Layout After Monitor Change ===");
      print_tile_layout(engine.system, geometries);
    }

    // Check for keyboard hotkeys (kept separate - has side effects on message queue)
    if (auto hotkey_id = winapi::check_keyboard_action()) {
      auto action_opt = id_to_hotkey_action(*hotkey_id);
      if (action_opt.has_value()) {
        HotkeyAction action = *action_opt;

        // Handle exit specially
        if (action == HotkeyAction::Exit) {
          spdlog::info("Exit hotkey pressed, shutting down...");
          break;
        }

        // Handle CycleSplitMode with toast
        if (action == HotkeyAction::CycleSplitMode) {
          auto result = engine.process_action(action, geometries, gap_h, gap_v, zen_pct);
          if (result.success) {
            auto mode_str = magic_enum::enum_name(engine.system.split_mode);
            spdlog::info("Cycled split mode: {}", mode_str);
            toast.show(std::string("Split mode: ").append(mode_str));
          }
        } else {
          // Process other actions through engine
          auto result = engine.process_action(action, geometries, gap_h, gap_v, zen_pct);

          // For navigation actions, set foreground window and move cursor
          if (result.success && result.selection_changed && result.new_cursor_pos.has_value()) {
            // Set foreground window for the selected cell
            if (engine.system.selection.has_value()) {
              int ci = engine.system.selection->cluster_index;
              int cell_idx = engine.system.selection->cell_index;
              if (ci >= 0 && static_cast<size_t>(ci) < engine.system.clusters.size()) {
                const auto& cluster = engine.system.clusters[static_cast<size_t>(ci)];
                if (cell_idx >= 0 && cell_idx < cluster.tree.size() &&
                    cluster.tree.is_leaf(cell_idx)) {
                  const auto& cell_data = cluster.tree[cell_idx];
                  if (cell_data.leaf_id.has_value()) {
                    winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(*cell_data.leaf_id);
                    if (!winapi::set_foreground_window(hwnd)) {
                      spdlog::error("Failed to set foreground window");
                    }
                  }
                }
              }
            }
            // Move cursor to the new position
            winapi::set_cursor_pos(result.new_cursor_pos->x, result.new_cursor_pos->y);
          } else if (result.success && result.new_cursor_pos.has_value()) {
            // For split ratio changes, just move cursor
            winapi::set_cursor_pos(result.new_cursor_pos->x, result.new_cursor_pos->y);
          }
        }

        // Recompute geometries after any action
        geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);
      }
    }

    // Extract window state from consolidated input
    auto current_state = extract_window_state_from_input(input_state);

    // Determine redirect cluster for new windows
    std::optional<int> redirect_cluster;

    // Check if mouse is over an empty cluster (priority)
    if (input_state.cursor_pos.has_value()) {
      float cursor_x = static_cast<float>(input_state.cursor_pos->x);
      float cursor_y = static_cast<float>(input_state.cursor_pos->y);
      auto hover_info = engine.get_hover_info(cursor_x, cursor_y, geometries);

      if (hover_info.cluster_index.has_value()) {
        size_t hover_idx = *hover_info.cluster_index;
        if (hover_idx < engine.system.clusters.size() &&
            ctrl::get_cluster_leaf_ids(engine.system.clusters[hover_idx]).empty()) {
          // Empty cluster under mouse - redirect new windows here
          redirect_cluster = static_cast<int>(hover_idx);
        }
      }
    }

    // If no empty cluster redirect, use selection's cluster
    if (!redirect_cluster.has_value() && engine.system.selection.has_value()) {
      redirect_cluster = engine.system.selection->cluster_index;
    }

    // Update system state with redirect (selection updated inside ctrl::update)
    bool changed = engine.update(current_state, redirect_cluster);

    if (changed) {
      // Recompute geometries after update
      geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);

      // Move cursor to selected cell (which is now the new window if one was added)
      if (auto center = engine.get_selected_center(geometries)) {
        winapi::set_cursor_pos(center->x, center->y);
      }
    }

    // Update selection based on mouse hover position (skip if we just moved cursor)
    if (!changed) {
      update_selection_from_hover(engine, geometries, input_state);
    }

    // Apply tile positions
    apply_tile_positions(engine.system, geometries);

    // Debug: print current system state
    spdlog::debug("=== Current System State ===");
    print_tile_layout(engine.system, geometries);

    // Render cell system overlay
    renderer::render(engine.system, geometries, provider.options.visualizationOptions.renderOptions,
                     engine.stored_cell, toast.get_visible_message());

    auto loop_end = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "=======================loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
  }

  // Cleanup hotkeys, hooks, and overlay before exit
  unregister_navigation_hotkeys(provider.options.keyboardOptions);
  winapi::unregister_virtual_desktop_notifications();
  winapi::unregister_session_power_notifications();
  winapi::unregister_move_size_hook();
  overlay::shutdown();
  spdlog::info("Hotkeys unregistered, hooks unregistered, overlay shutdown, exiting...");
}

} // namespace wintiler
