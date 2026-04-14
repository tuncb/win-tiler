#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <vector>

#include "engine.h"
#include "loop_perf.h"
#include "model.h"
#include "multi_cell_renderer.h"
#include "multi_engine.h"
#include "overlay.h"
#include "runtime_support.h"
#include "winapi.h"

namespace wintiler {

NoDesktopHotkeyAction classify_no_desktop_hotkey(std::optional<HotkeyAction> hotkey_action) {
  if (!hotkey_action.has_value()) {
    return NoDesktopHotkeyAction::None;
  }

  switch (*hotkey_action) {
  case HotkeyAction::Exit:
    return NoDesktopHotkeyAction::Exit;
  case HotkeyAction::TogglePause:
    return NoDesktopHotkeyAction::EnterManualPause;
  case HotkeyAction::NavigateLeft:
  case HotkeyAction::NavigateDown:
  case HotkeyAction::NavigateUp:
  case HotkeyAction::NavigateRight:
  case HotkeyAction::ToggleSplit:
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
    return NoDesktopHotkeyAction::Ignore;
  }

  return NoDesktopHotkeyAction::Ignore;
}

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
  frame_input.cluster_updates = extract_cluster_updates_from_input(input_state);
  frame_input.managed_windows = extract_managed_window_states_from_input(input_state);
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

  if (output.apply_tiles) {
    apply_tile_positions(system, output.geometries);
  }

  if (output.focus_leaf_id.has_value()) {
    winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(*output.focus_leaf_id);
    if (!winapi::set_foreground_window(hwnd)) {
      spdlog::error("Failed to set foreground window");
    }
  }

  if (output.cursor_pos.has_value()) {
    if (!winapi::set_cursor_pos(output.cursor_pos->x, output.cursor_pos->y)) {
      spdlog::error("Failed to set cursor position");
    }
  }
}
// Handle config file hot-reload
bool handle_config_refresh(GlobalOptionsProvider& provider, ToastState& toast) {
  if (!provider.refresh()) {
    return false;
  }
  const auto& options = provider.options;
  unregister_navigation_hotkeys(options.keyboardOptions);
  register_navigation_hotkeys(options.keyboardOptions);
  toast.set_duration(std::chrono::milliseconds(options.visualizationOptions.toastDurationMs));
  spdlog::info("Config hot-reloaded");
  return true;
}

// Handle monitor configuration changes, returns true if change occurred
bool handle_monitor_change(std::vector<winapi::MonitorInfo>& monitors, const GlobalOptions& options,
                           MultiEngine<LoopDesktopData, std::string>& multi_engine) {
  auto current_monitors = winapi::get_monitors();
  if (winapi::monitors_equal(monitors, current_monitors)) {
    return false;
  }
  spdlog::info("Monitor configuration changed, reinitializing system...");
  winapi::log_monitors(current_monitors);
  monitors = current_monitors;
  auto cluster_infos = create_cluster_infos_from_monitors(monitors, options);
  reinitialize_all_desktops(multi_engine, cluster_infos);
  spdlog::info("=== Reinitialized Tile Layout ===");
  // Tile layout will be printed and applied by the main loop
  return true;
}

void maybe_print_perf_report(LoopPerfCollector& perf,
                             std::chrono::steady_clock::time_point report_time) {
  if (!perf.should_report(report_time)) {
    return;
  }

  std::cout << perf.format_report(report_time);
  std::cout.flush();
  perf.reset_window(report_time);
}

void flush_perf_report(LoopPerfCollector& perf) {
  if (!perf.has_samples()) {
    return;
  }

  auto report_time = std::chrono::steady_clock::now();
  std::cout << perf.format_report(report_time);
  std::cout.flush();
  perf.reset_window(report_time);
}

} // namespace

void run_loop_mode(GlobalOptionsProvider& provider, const LoopRunOptions& run_options) {
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

  LoopPerfCollector perf(run_options.perf_stats);
  if (perf.enabled) {
    std::cout << "[perf] reporting every " << perf.report_interval.count()
              << "s for loop mode stage timings\n";
    std::cout.flush();
  }

  while (true) {
    // Wait for messages (hotkeys) or timeout - responds immediately to hotkeys
    winapi::wait_for_messages_or_timeout(options.loopOptions.intervalMs);

    // Block if session is paused (locked, sleeping, or display off)
    if (winapi::is_session_paused()) {
      spdlog::debug("Session paused, waiting for resume...");
      winapi::wait_for_session_active();
      mark_all_desktops_for_retile(multi_engine);
      spdlog::debug("Session resumed, continuing loop");
      continue; // Re-gather state after resume
    }

    // Check for manual pause (hotkey-toggled)
    if (is_manually_paused) {
      // Only check for unpause hotkey when manually paused
      auto action_opt = poll_hotkey_action();
      if (action_opt.has_value() && *action_opt == HotkeyAction::TogglePause) {
        is_manually_paused = false;
        mark_all_desktops_for_retile(multi_engine);
        spdlog::info("Manual pause deactivated");
        toast.show("Resumed");
        // Fall through to resume normal processing immediately
      }
      if (is_manually_paused) {
        continue; // Still paused, skip this iteration
      }
    }

    auto loop_start = std::chrono::steady_clock::now();

    // Gather all Windows API input state in a single call
    auto gather_start = std::chrono::steady_clock::now();
    auto input_state = winapi::gather_loop_input_state(options.ignoreOptions);
    perf.record_stage(LoopPerfStage::GatherInput, std::chrono::steady_clock::now() - gather_start);

    // Virtual desktop handling via desktop_id from managed windows
    if (!input_state.desktop_id.has_value()) {
      bool should_exit_without_desktop = false;
      auto hotkey_action = poll_hotkey_action();
      switch (classify_no_desktop_hotkey(hotkey_action)) {
      case NoDesktopHotkeyAction::Exit: {
        perf.note_active_frame();
        auto loop_end = std::chrono::steady_clock::now();
        perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
        maybe_print_perf_report(perf, loop_end);
        spdlog::info("Exit hotkey pressed without a desktop ID, shutting down...");
        should_exit_without_desktop = true;
        break;
      }
      case NoDesktopHotkeyAction::EnterManualPause: {
        perf.note_active_frame();
        auto loop_end = std::chrono::steady_clock::now();
        perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
        maybe_print_perf_report(perf, loop_end);
        is_manually_paused = true;
        spdlog::info("Manual pause activated without a desktop ID");
        overlay::clear();
        continue;
      }
      case NoDesktopHotkeyAction::Ignore:
        spdlog::debug("Ignoring hotkey without a desktop ID");
        break;
      case NoDesktopHotkeyAction::None:
        break;
      }

      if (should_exit_without_desktop) {
        break;
      }

      // No windows - skip iteration
      spdlog::debug("No desktop ID (no windows), skipping iteration");
      overlay::clear();
      perf.note_active_frame();
      auto loop_end = std::chrono::steady_clock::now();
      perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
      maybe_print_perf_report(perf, loop_end);
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
    auto compute_geometry_start = std::chrono::steady_clock::now();
    geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);
    perf.record_stage(LoopPerfStage::ComputeGeometry,
                      std::chrono::steady_clock::now() - compute_geometry_start);
    if (input_state.is_any_window_being_moved) {
      auto hotkey_action = poll_hotkey_action();
      if (hotkey_action.has_value()) {
        spdlog::debug("Ignoring hotkey during move/resize frame");
      }

      auto render_start = std::chrono::steady_clock::now();
      renderer::render(engine.system, geometries, options.visualizationOptions.renderOptions,
                       engine.stored_cell, toast.get_visible_message());
      perf.record_stage(LoopPerfStage::Render, std::chrono::steady_clock::now() - render_start);
      perf.note_active_frame();
      perf.note_drag_only_frame();
      auto loop_end = std::chrono::steady_clock::now();
      perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
      maybe_print_perf_report(perf, loop_end);
      spdlog::trace(
          "loop iteration total: {}us",
          std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
      continue;
    }

    // Check for config file changes and hot-reload
    if (handle_config_refresh(provider, toast)) {
      mark_all_desktops_for_retile(multi_engine);
    }
    gap_h = provider.options.gapOptions.horizontal;
    gap_v = provider.options.gapOptions.vertical;
    zen_pct = provider.options.visualizationOptions.renderOptions.zen_percentage;

    // Check for monitor configuration changes
    bool monitor_changed = handle_monitor_change(monitors, provider.options, multi_engine);
    if (monitor_changed) {
      auto updated_geometry_start = std::chrono::steady_clock::now();
      geometries = engine.compute_geometries(gap_h, gap_v, zen_pct);
      perf.record_stage(LoopPerfStage::ComputeGeometry,
                        std::chrono::steady_clock::now() - updated_geometry_start);
      spdlog::debug("=== Updated Tile Layout After Monitor Change ===");
      print_tile_layout(engine.system, geometries);
    }

    auto build_frame_input_start = std::chrono::steady_clock::now();
    EngineFrameInput frame_input = build_engine_frame_input(
        input_state, current_desktop.data, gap_h, gap_v, zen_pct,
        provider.options.loopOptions.toggle_zen_on_window_maximize, poll_hotkey_action());
    perf.record_stage(LoopPerfStage::BuildFrameInput,
                      std::chrono::steady_clock::now() - build_frame_input_start);

    auto engine_start = std::chrono::steady_clock::now();
    EngineFrameOutput frame_output = engine.process_frame(frame_input);
    perf.record_stage(LoopPerfStage::Engine, std::chrono::steady_clock::now() - engine_start);
    auto apply_start = std::chrono::steady_clock::now();
    apply_frame_output(frame_output, engine.system, toast);
    perf.record_stage(LoopPerfStage::Apply, std::chrono::steady_clock::now() - apply_start);
    perf.note_active_frame();
    if (frame_output.apply_tiles) {
      perf.note_apply_tiles_frame();
    }
    if (monitor_changed || frame_output.topology_changed) {
      perf.note_topology_changed_frame();
    }

    if (frame_output.control == LoopControl::Exit) {
      auto loop_end = std::chrono::steady_clock::now();
      perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
      maybe_print_perf_report(perf, loop_end);
      spdlog::info("Exit hotkey pressed, shutting down...");
      break;
    }

    if (frame_output.control == LoopControl::EnterManualPause) {
      auto loop_end = std::chrono::steady_clock::now();
      perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
      maybe_print_perf_report(perf, loop_end);
      is_manually_paused = true;
      spdlog::info("Manual pause activated");
      overlay::clear();
      continue;
    }

    current_desktop.data.has_completed_initial_tile_pass =
        frame_output.has_completed_initial_tile_pass;
    geometries = std::move(frame_output.geometries);

    // Render cell system overlay
    auto render_start = std::chrono::steady_clock::now();
    renderer::render(engine.system, geometries, provider.options.visualizationOptions.renderOptions,
                     engine.stored_cell, toast.get_visible_message());
    perf.record_stage(LoopPerfStage::Render, std::chrono::steady_clock::now() - render_start);

    auto loop_end = std::chrono::steady_clock::now();
    perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
    maybe_print_perf_report(perf, loop_end);
    spdlog::trace(
        "=======================loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loop_end - loop_start).count());
  }

  flush_perf_report(perf);

  // Cleanup hotkeys, hooks, and overlay before exit
  unregister_navigation_hotkeys(provider.options.keyboardOptions);
  winapi::unregister_virtual_desktop_notifications();
  winapi::unregister_session_power_notifications();
  winapi::unregister_move_size_hook();
  overlay::shutdown();
  spdlog::info("Hotkeys unregistered, hooks unregistered, overlay shutdown, exiting...");
}

} // namespace wintiler
