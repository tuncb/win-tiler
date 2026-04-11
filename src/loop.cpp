#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <magic_enum/magic_enum.hpp>
#include <vector>

#include "engine.h"
#include "model.h"
#include "multi_cell_renderer.h"
#include "multi_engine.h"
#include "overlay.h"
#include "winapi.h"

namespace wintiler {

// Empty data struct for now - extension point for future per-desktop state
struct LoopDesktopData {
  bool has_completed_initial_tile_pass = false;
};

namespace {

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

std::vector<std::vector<ManagedWindowState>>
extract_managed_window_state_from_input(const winapi::LoopInputState& input_state) {
  std::vector<std::vector<ManagedWindowState>> result;
  result.reserve(input_state.windows_per_monitor.size());

  for (const auto& windows : input_state.windows_per_monitor) {
    std::vector<ManagedWindowState> monitor_state;
    monitor_state.reserve(windows.size());
    for (const auto& win : windows) {
      if (win.handle == nullptr) {
        continue;
      }
      monitor_state.push_back(
          {reinterpret_cast<size_t>(win.handle), win.is_fullscreen, win.is_maximized});
    }
    result.push_back(std::move(monitor_state));
  }

  return result;
}

std::optional<HotkeyAction> poll_hotkey_action() {
  auto hotkey_id = winapi::check_keyboard_action();
  if (!hotkey_id.has_value()) {
    return std::nullopt;
  }
  return id_to_hotkey_action(*hotkey_id);
}

EngineFrameInput build_engine_frame_input(const winapi::LoopInputState& input_state,
                                          const LoopDesktopData& desktop_data, float gap_h,
                                          float gap_v, float zen_pct, bool auto_zen_on_maximize,
                                          std::optional<HotkeyAction> hotkey_action) {
  EngineFrameInput frame_input;
  frame_input.cluster_updates = extract_window_state_from_input(input_state);
  frame_input.managed_windows = extract_managed_window_state_from_input(input_state);
  frame_input.hotkey_action = hotkey_action;
  frame_input.auto_zen_on_maximize = auto_zen_on_maximize;
  frame_input.has_completed_initial_tile_pass = desktop_data.has_completed_initial_tile_pass;
  frame_input.gap_h = gap_h;
  frame_input.gap_v = gap_v;
  frame_input.zen_pct = zen_pct;

  if (input_state.cursor_pos.has_value()) {
    frame_input.cursor_pos = ctrl::Point{input_state.cursor_pos->x, input_state.cursor_pos->y};
  }

  if (input_state.drag_info.has_value() && input_state.drag_info->move_ended) {
    CompletedDragRequest drag_request;
    drag_request.leaf_id = reinterpret_cast<size_t>(input_state.drag_info->hwnd);
    drag_request.do_exchange = input_state.is_ctrl_pressed;
    drag_request.cursor_pos = frame_input.cursor_pos;

    auto actual_rect_opt = winapi::get_window_rect(input_state.drag_info->hwnd);
    if (actual_rect_opt.has_value()) {
      drag_request.actual_window_rect = ctrl::Rect{
          static_cast<float>(actual_rect_opt->x), static_cast<float>(actual_rect_opt->y),
          static_cast<float>(actual_rect_opt->width), static_cast<float>(actual_rect_opt->height)};
    }

    frame_input.completed_drag = drag_request;
  }

  return frame_input;
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

void apply_frame_output(const EngineFrameOutput& output, const ctrl::System& system,
                        ToastState& toast) {
  if (output.clear_drag_ended) {
    winapi::clear_drag_ended();
  }

  if (output.toast_message.has_value()) {
    toast.show(*output.toast_message);
  }

  if (output.control != LoopControl::Continue) {
    return;
  }

  if (output.focus_leaf_id.has_value()) {
    winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(*output.focus_leaf_id);
    if (!winapi::set_foreground_window(hwnd)) {
      spdlog::error("Failed to set foreground window");
    }
  }

  if (output.cursor_pos.has_value()) {
    winapi::set_cursor_pos(output.cursor_pos->x, output.cursor_pos->y);
  }

  if (output.apply_tiles) {
    apply_tile_positions(system, output.geometries);
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

  // Manual pause state (toggled by hotkey)
  bool is_manually_paused = false;

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

    // Check for manual pause (hotkey-toggled)
    if (is_manually_paused) {
      // Only check for unpause hotkey when manually paused
      auto action_opt = poll_hotkey_action();
      if (action_opt.has_value() && *action_opt == HotkeyAction::TogglePause) {
        is_manually_paused = false;
        spdlog::info("Manual pause deactivated");
        toast.show("Resumed");
        // Fall through to resume normal processing immediately
      }
      if (is_manually_paused) {
        continue; // Still paused, skip this iteration
      }
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
    auto& current_desktop = multi_engine.current();
    auto& engine = current_desktop.engine;

    // Update gap and zen settings (in case config was reloaded)
    gap_h = provider.options.gapOptions.horizontal;
    gap_v = provider.options.gapOptions.vertical;
    zen_pct = provider.options.visualizationOptions.renderOptions.zen_percentage;

    // Skip all processing while user is dragging a window - only render
    geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);
    if (input_state.is_any_window_being_moved) {
      renderer::render(engine.system, geometries, options.visualizationOptions.renderOptions,
                       engine.stored_cell, toast.get_visible_message());
      auto loop_end = std::chrono::high_resolution_clock::now();
      spdlog::trace(
          "loop iteration total: {}us",
          std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
      continue;
    }

    // Check for config file changes and hot-reload
    handle_config_refresh(provider, engine, toast);
    gap_h = provider.options.gapOptions.horizontal;
    gap_v = provider.options.gapOptions.vertical;
    zen_pct = provider.options.visualizationOptions.renderOptions.zen_percentage;

    // Check for monitor configuration changes
    if (handle_monitor_change(monitors, provider.options, engine)) {
      current_desktop.data.has_completed_initial_tile_pass = false;
      geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);
      spdlog::debug("=== Updated Tile Layout After Monitor Change ===");
      print_tile_layout(engine.system, geometries);
    }

    EngineFrameInput frame_input = build_engine_frame_input(
        input_state, current_desktop.data, gap_h, gap_v, zen_pct,
        provider.options.loopOptions.toggle_zen_on_window_maximize, poll_hotkey_action());

    EngineFrameOutput frame_output = engine.process_frame(frame_input);
    apply_frame_output(frame_output, engine.system, toast);

    if (frame_output.control == LoopControl::Exit) {
      spdlog::info("Exit hotkey pressed, shutting down...");
      break;
    }

    if (frame_output.control == LoopControl::EnterManualPause) {
      is_manually_paused = true;
      spdlog::info("Manual pause activated");
      overlay::clear();
      continue;
    }

    current_desktop.data.has_completed_initial_tile_pass =
        frame_output.has_completed_initial_tile_pass;
    geometries = std::move(frame_output.geometries);

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
