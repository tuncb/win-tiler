#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <magic_enum/magic_enum.hpp>
#include <string_view>
#include <utility>
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

bool operator==(const OverlayRenderRect& lhs, const OverlayRenderRect& rhs) {
  return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.color.r == rhs.color.r && lhs.color.g == rhs.color.g && lhs.color.b == rhs.color.b &&
         lhs.color.a == rhs.color.a && lhs.border_width == rhs.border_width;
}

bool operator==(const OverlayRenderSnapshot& lhs, const OverlayRenderSnapshot& rhs) {
  return lhs.rects == rhs.rects && lhs.message == rhs.message &&
         lhs.toast_font_size == rhs.toast_font_size;
}

OverlayRenderSnapshot make_overlay_render_snapshot(
    const ctrl::System& system, const std::vector<std::vector<ctrl::Rect>>& geometries,
    const renderer::RenderOptions& config, std::optional<StoredCell> stored_cell,
    const std::optional<std::string>& message) {
  OverlayRenderSnapshot snapshot;
  snapshot.message = message;
  snapshot.toast_font_size = message.has_value() ? config.toast_font_size : 0.0f;

  size_t rect_capacity = 0;
  for (const auto& cluster : system.clusters) {
    rect_capacity += cluster.tree.size();
  }
  snapshot.rects.reserve(rect_capacity);

  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& cluster = system.clusters[cluster_idx];
    if (cluster.zen_cell_index.has_value() || cluster.has_fullscreen_cell) {
      continue;
    }

    if (cluster_idx >= geometries.size()) {
      continue;
    }
    const auto& rects = geometries[cluster_idx];

    for (int i = 0; i < cluster.tree.size(); ++i) {
      if (!cluster.tree.is_leaf(i)) {
        continue;
      }

      if (static_cast<size_t>(i) >= rects.size()) {
        continue;
      }

      const auto& cell_data = cluster.tree[i];
      const auto& rect = rects[static_cast<size_t>(i)];
      overlay::Color color = config.normal_color;

      if (system.selection.has_value() &&
          static_cast<size_t>(system.selection->cluster_index) == cluster_idx &&
          system.selection->cell_index == i) {
        color = config.selected_color;
      }

      if (stored_cell.has_value() && stored_cell->cluster_index == cluster_idx &&
          cell_data.leaf_id.has_value() && *cell_data.leaf_id == stored_cell->leaf_id) {
        color = config.stored_color;
      }

      snapshot.rects.push_back(
          {rect.x, rect.y, rect.width, rect.height, color, config.border_width});
    }
  }

  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    const auto& cluster = system.clusters[cluster_idx];
    if (!cluster.zen_cell_index.has_value() || cluster.has_fullscreen_cell) {
      continue;
    }

    int zen_cell_index = *cluster.zen_cell_index;
    if (cluster_idx >= geometries.size()) {
      continue;
    }
    const auto& rects = geometries[cluster_idx];
    if (static_cast<size_t>(zen_cell_index) >= rects.size()) {
      continue;
    }

    const auto& zen_rect = rects[static_cast<size_t>(zen_cell_index)];
    overlay::Color color = config.normal_color;
    if (system.selection.has_value() &&
        static_cast<size_t>(system.selection->cluster_index) == cluster_idx &&
        system.selection->cell_index == zen_cell_index) {
      color = config.selected_color;
    }

    if (stored_cell.has_value() && stored_cell->cluster_index == cluster_idx) {
      const auto& cell_data = cluster.tree[zen_cell_index];
      if (cell_data.leaf_id.has_value() && *cell_data.leaf_id == stored_cell->leaf_id) {
        color = config.stored_color;
      }
    }

    snapshot.rects.push_back(
        {zen_rect.x, zen_rect.y, zen_rect.width, zen_rect.height, color, config.border_width});
  }

  return snapshot;
}

bool should_render_overlay(OverlayRenderCache& cache, OverlayRenderSnapshot snapshot) {
  if (cache.last_presented.has_value() && *cache.last_presented == snapshot) {
    return false;
  }

  cache.last_presented = std::move(snapshot);
  return true;
}

bool should_clear_overlay(OverlayRenderCache& cache) {
  if (!cache.last_presented.has_value()) {
    return false;
  }

  cache.last_presented.reset();
  return true;
}

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
  case HotkeyAction::RestartSystem:
    return NoDesktopHotkeyAction::Ignore;
  case HotkeyAction::DumpWindowManagement:
    return NoDesktopHotkeyAction::DumpWindowManagement;
  case HotkeyAction::ToggleFloating:
    return NoDesktopHotkeyAction::ToggleFloating;
  }

  return NoDesktopHotkeyAction::Ignore;
}

ManualPauseHotkeyAction classify_manual_pause_hotkey(std::optional<HotkeyAction> hotkey_action) {
  if (!hotkey_action.has_value()) {
    return ManualPauseHotkeyAction::None;
  }

  switch (*hotkey_action) {
  case HotkeyAction::TogglePause:
    return ManualPauseHotkeyAction::Resume;
  case HotkeyAction::DumpWindowManagement:
    return ManualPauseHotkeyAction::DumpWindowManagement;
  case HotkeyAction::Exit:
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
  case HotkeyAction::RestartSystem:
  case HotkeyAction::ToggleFloating:
    return ManualPauseHotkeyAction::Ignore;
  }

  return ManualPauseHotkeyAction::Ignore;
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

struct SessionFloatingGroup {
  winapi::HWND_T root = nullptr;
  std::optional<winapi::DWORD_T> pid;
};

struct SessionFloatingState {
  std::vector<SessionFloatingGroup> groups;
};

enum class ToggleFloatingResult {
  NoTarget,
  Floated,
  Unfloated,
};

bool floating_group_root_is_current(const SessionFloatingGroup& group) {
  if (!winapi::is_window_valid(group.root)) {
    return false;
  }

  if (!group.pid.has_value()) {
    return true;
  }

  auto current_pid = winapi::get_window_process_id(group.root);
  return current_pid.has_value() && *current_pid == *group.pid;
}

void prune_session_floating_groups(SessionFloatingState& state) {
  state.groups.erase(std::remove_if(state.groups.begin(), state.groups.end(),
                                    [](const SessionFloatingGroup& group) {
                                      return !floating_group_root_is_current(group);
                                    }),
                     state.groups.end());
}

bool floating_group_matches_window(const SessionFloatingGroup& group, winapi::HWND_T hwnd) {
  return floating_group_root_is_current(group) &&
         winapi::is_window_or_owned_or_parented_by(hwnd, group.root);
}

std::optional<size_t> find_session_floating_group_for_window(const SessionFloatingState& state,
                                                             winapi::HWND_T hwnd) {
  if (hwnd == nullptr) {
    return std::nullopt;
  }

  for (size_t i = 0; i < state.groups.size(); ++i) {
    if (floating_group_matches_window(state.groups[i], hwnd)) {
      return i;
    }
  }
  return std::nullopt;
}

bool is_session_floating_window(const SessionFloatingState& state, winapi::HWND_T hwnd) {
  return find_session_floating_group_for_window(state, hwnd).has_value();
}

ToggleFloatingResult toggle_session_floating(SessionFloatingState& state,
                                             winapi::HWND_T foreground_window,
                                             std::optional<size_t> selected_leaf_id) {
  prune_session_floating_groups(state);

  auto foreground_group = find_session_floating_group_for_window(state, foreground_window);
  if (foreground_group.has_value()) {
    state.groups.erase(state.groups.begin() + static_cast<std::ptrdiff_t>(*foreground_group));
    return ToggleFloatingResult::Unfloated;
  }

  if (!selected_leaf_id.has_value()) {
    return ToggleFloatingResult::NoTarget;
  }

  auto root = reinterpret_cast<winapi::HWND_T>(*selected_leaf_id);
  if (!winapi::is_window_valid(root)) {
    return ToggleFloatingResult::NoTarget;
  }

  auto existing_group = find_session_floating_group_for_window(state, root);
  if (existing_group.has_value()) {
    state.groups.erase(state.groups.begin() + static_cast<std::ptrdiff_t>(*existing_group));
    return ToggleFloatingResult::Unfloated;
  }

  state.groups.push_back(SessionFloatingGroup{root, winapi::get_window_process_id(root)});
  return ToggleFloatingResult::Floated;
}

void filter_session_floating_windows_from_input_state(SessionFloatingState& floating_state,
                                                      winapi::LoopInputState& input_state) {
  prune_session_floating_groups(floating_state);
  if (floating_state.groups.empty()) {
    return;
  }

  for (auto& monitor_windows : input_state.windows_per_monitor) {
    monitor_windows.erase(std::remove_if(monitor_windows.begin(), monitor_windows.end(),
                                         [&](const winapi::ManagedWindowInfo& window) {
                                           return is_session_floating_window(floating_state,
                                                                             window.handle);
                                         }),
                          monitor_windows.end());
  }
}

void filter_session_floating_windows_from_cluster_infos(
    SessionFloatingState& floating_state, std::vector<ctrl::ClusterInitInfo>& cluster_infos) {
  prune_session_floating_groups(floating_state);
  if (floating_state.groups.empty()) {
    return;
  }

  for (auto& info : cluster_infos) {
    size_t original_size = info.initial_cell_ids.size();
    info.initial_cell_ids.erase(
        std::remove_if(info.initial_cell_ids.begin(), info.initial_cell_ids.end(),
                       [&](size_t leaf_id) {
                         return is_session_floating_window(
                             floating_state, reinterpret_cast<winapi::HWND_T>(leaf_id));
                       }),
        info.initial_cell_ids.end());
    if (info.initial_cell_ids.size() != original_size) {
      info.initial_layout_rule.reset();
    }
  }
}

std::optional<std::string> make_toggle_floating_toast(ToggleFloatingResult result) {
  switch (result) {
  case ToggleFloatingResult::Floated:
    return "Floating";
  case ToggleFloatingResult::Unfloated:
    return "Tiling";
  case ToggleFloatingResult::NoTarget:
    return "No window to toggle";
  }
  return std::nullopt;
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
      auto action_name = magic_enum::enum_name(binding.action);
      if (!winapi::register_hotkey(*hotkey, action_name, binding.hotkey)) {
        spdlog::debug("Hotkey registration failed for {}", action_name);
      }
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
  if (winapi::consume_notification_area_exit_requested()) {
    return HotkeyAction::Exit;
  }

  auto hotkey_id = winapi::check_keyboard_action();
  if (!hotkey_id.has_value()) {
    return std::nullopt;
  }
  return id_to_hotkey_action(*hotkey_id);
}

void apply_frame_output(const EngineFrameOutput& output, const ctrl::System& system,
                        const IgnoreOptions& ignore_options,
                        const winapi::LoopInputState& input_state,
                        SessionFloatingState& floating_state, ToastState& toast) {
  if (output.clear_drag_ended) {
    winapi::clear_drag_ended();
  }

  if (output.toggle_floating) {
    auto toggle_result = toggle_session_floating(floating_state, input_state.foreground_window,
                                                 output.floating_leaf_id);
    auto message = make_toggle_floating_toast(toggle_result);
    if (message.has_value()) {
      toast.show(*message);
    }
  }

  if (output.toast_message.has_value()) {
    toast.show(*output.toast_message);
  }

  if (output.dump_window_management) {
    winapi::dump_window_management_state(ignore_options);
  }

  if (output.control != LoopControl::Continue) {
    return;
  }

  if (output.apply_tiles) {
    apply_tile_positions(system, output.geometries);
  } else if (!output.placement_correction_leaf_ids.empty()) {
    apply_tile_positions_for_leaf_ids(system, output.geometries,
                                      output.placement_correction_leaf_ids);
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

void reinitialize_system_from_monitors(std::vector<winapi::MonitorInfo>& monitors,
                                       const std::vector<winapi::MonitorInfo>& updated_monitors,
                                       const GlobalOptions& options,
                                       MultiEngine<LoopDesktopData, std::string>& multi_engine,
                                       SessionFloatingState& floating_state,
                                       std::string_view reason) {
  spdlog::info("{}, reinitializing system...", reason);
  winapi::log_monitors(updated_monitors);
  monitors = updated_monitors;
  auto cluster_infos = create_cluster_infos_from_monitors(monitors, options);
  filter_session_floating_windows_from_cluster_infos(floating_state, cluster_infos);
  reinitialize_all_desktops(multi_engine, cluster_infos);
  spdlog::info("=== Reinitialized Tile Layout ===");
  // Tile layout will be printed and applied by the main loop
}

// Handle monitor configuration changes, returns true if change occurred
bool handle_monitor_change(std::vector<winapi::MonitorInfo>& monitors,
                           std::vector<winapi::MonitorInfo>& current_monitors,
                           const GlobalOptions& options,
                           MultiEngine<LoopDesktopData, std::string>& multi_engine,
                           SessionFloatingState& floating_state) {
  winapi::fill_monitors(current_monitors);
  if (winapi::monitors_equal(monitors, current_monitors)) {
    return false;
  }
  reinitialize_system_from_monitors(monitors, current_monitors, options, multi_engine,
                                    floating_state, "Monitor configuration changed");
  return true;
}

void maybe_print_perf_report(LoopPerfCollector& perf,
                             std::chrono::steady_clock::time_point report_time) {
  if (!perf.should_report(report_time)) {
    return;
  }

  spdlog::info("{}", perf.format_report(report_time));
  perf.reset_window(report_time);
}

void flush_perf_report(LoopPerfCollector& perf) {
  if (!perf.has_samples()) {
    return;
  }

  auto report_time = std::chrono::steady_clock::now();
  spdlog::info("{}", perf.format_report(report_time));
  perf.reset_window(report_time);
}

} // namespace

void run_loop_mode(GlobalOptionsProvider& provider, const LoopRunOptions& run_options) {
  const auto& options = provider.options;

  // Get initial monitor configuration and create engine
  std::vector<winapi::MonitorInfo> monitors;
  winapi::fill_monitors(monitors);
  winapi::log_monitors(monitors);

  // MultiEngine manages separate tiling state per virtual desktop
  // Uses GUID strings as desktop identifiers
  MultiEngine<LoopDesktopData, std::string> multi_engine;

  // Geometries computed per-frame after desktop is determined
  std::vector<std::vector<ctrl::Rect>> geometries;

  // Register keyboard hotkeys
  register_navigation_hotkeys(options.keyboardOptions);

  // Register window move/resize detection hooks
  winapi::register_move_size_hook();

  // Register session/power notifications for pause on lock/sleep/display-off
  winapi::register_session_power_notifications();

  winapi::register_notification_area_icon({run_options.config_path, run_options.log_file_path});

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
  if (auto exit_hotkey = find_hotkey_binding(options.keyboardOptions, HotkeyAction::Exit)) {
    spdlog::info("Monitoring for window changes... (Exit hotkey: {})", *exit_hotkey);
  } else {
    spdlog::info("Monitoring for window changes... (Exit hotkey not configured)");
  }

  // Toast message state
  ToastState toast(std::chrono::milliseconds(options.visualizationOptions.toastDurationMs));
  OverlayRenderCache overlay_render_cache;

  // Manual pause state (toggled by hotkey)
  bool is_manually_paused = false;
  SessionFloatingState floating_state;

  LoopPerfCollector perf(run_options.perf_stats);
  if (perf.enabled) {
    spdlog::info("[perf] reporting every {}s for loop mode stage timings",
                 perf.report_interval.count());
  }

  winapi::LoopInputState input_state;
  std::vector<winapi::HWND_T> input_handles;
  std::vector<winapi::MonitorInfo> current_monitors;
  std::vector<ClusterTilingOptions> cluster_options;
  EngineFrameInput frame_input;

  while (true) {
    // Wait for messages (hotkeys) or timeout - responds immediately to hotkeys
    winapi::wait_for_messages_or_timeout(options.loopOptions.intervalMs);
    winapi::process_pending_non_hotkey_messages();

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
      switch (classify_manual_pause_hotkey(poll_hotkey_action())) {
      case ManualPauseHotkeyAction::Resume:
        is_manually_paused = false;
        mark_all_desktops_for_retile(multi_engine);
        spdlog::info("Manual pause deactivated");
        toast.show("Resumed");
        break;
      case ManualPauseHotkeyAction::DumpWindowManagement:
        spdlog::info("Dumping window management state while manually paused");
        winapi::dump_window_management_state(options.ignoreOptions);
        continue;
      case ManualPauseHotkeyAction::Ignore:
      case ManualPauseHotkeyAction::None:
        continue;
      }
    }

    auto loop_start = std::chrono::steady_clock::now();

    // Gather all Windows API input state in a single call
    auto gather_start = std::chrono::steady_clock::now();
    winapi::gather_loop_input_state_into(options.ignoreOptions, input_state, input_handles);
    filter_session_floating_windows_from_input_state(floating_state, input_state);
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
        if (should_clear_overlay(overlay_render_cache)) {
          overlay::clear();
        }
        continue;
      }
      case NoDesktopHotkeyAction::DumpWindowManagement:
        spdlog::info("Dumping window management state without a desktop ID");
        winapi::dump_window_management_state(provider.options.ignoreOptions);
        break;
      case NoDesktopHotkeyAction::ToggleFloating: {
        auto toggle_result =
            toggle_session_floating(floating_state, input_state.foreground_window, std::nullopt);
        auto message = make_toggle_floating_toast(toggle_result);
        if (message.has_value()) {
          toast.show(*message);
        }
        break;
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
      if (should_clear_overlay(overlay_render_cache)) {
        overlay::clear();
      }
      perf.note_active_frame();
      auto loop_end = std::chrono::steady_clock::now();
      perf.record_stage(LoopPerfStage::ActiveTotal, loop_end - loop_start);
      maybe_print_perf_report(perf, loop_end);
      continue;
    }

    // Virtual desktop management - create new desktop on first encounter, switch as needed
    const std::string& current_desktop_id = *input_state.desktop_id;

    std::vector<ctrl::ClusterInitInfo> cluster_infos;
    if (!multi_engine.has_desktop(current_desktop_id)) {
      cluster_infos = create_cluster_infos_from_monitors(monitors, provider.options);
      filter_session_floating_windows_from_cluster_infos(floating_state, cluster_infos);
    }

    auto activation = activate_loop_desktop(multi_engine, current_desktop_id, cluster_infos);
    if (!activation.has_value()) {
      spdlog::error("Failed to activate virtual desktop engine: {}", current_desktop_id);
      continue;
    }
    if (activation->created) {
      spdlog::debug("Created new virtual desktop engine: {}", current_desktop_id);
    }
    if (activation->switched) {
      spdlog::debug("Switched to virtual desktop: {}", current_desktop_id);
    }

    // Get reference to current desktop's engine
    auto& current_desktop = activation->desktop.get();
    auto& engine = current_desktop.engine;

    resolve_cluster_tiling_options_into(monitors, provider.options, cluster_options);

    // Skip all processing while user is dragging a window - only render
    auto compute_geometry_start = std::chrono::steady_clock::now();
    geometries = engine.compute_geometries(cluster_options);
    perf.record_stage(LoopPerfStage::ComputeGeometry,
                      std::chrono::steady_clock::now() - compute_geometry_start);
    if (input_state.is_any_window_being_moved) {
      auto hotkey_action = poll_hotkey_action();
      if (hotkey_action.has_value()) {
        spdlog::debug("Ignoring hotkey during move/resize frame");
      }

      auto visible_message = toast.get_visible_message();
      auto render_snapshot = make_overlay_render_snapshot(
          engine.system, geometries, options.visualizationOptions.renderOptions, engine.stored_cell,
          visible_message);
      if (should_render_overlay(overlay_render_cache, std::move(render_snapshot))) {
        auto render_start = std::chrono::steady_clock::now();
        renderer::render(engine.system, geometries, options.visualizationOptions.renderOptions,
                         engine.stored_cell, visible_message);
        perf.record_stage(LoopPerfStage::Render, std::chrono::steady_clock::now() - render_start);
      }
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
      mark_all_desktops_for_layout_reapply(multi_engine);
    }
    resolve_cluster_tiling_options_into(monitors, provider.options, cluster_options);

    // Check for monitor configuration changes
    bool monitor_changed = handle_monitor_change(monitors, current_monitors, provider.options,
                                                 multi_engine, floating_state);
    if (monitor_changed) {
      resolve_cluster_tiling_options_into(monitors, provider.options, cluster_options);
      auto updated_geometry_start = std::chrono::steady_clock::now();
      geometries = engine.compute_geometries(cluster_options);
      perf.record_stage(LoopPerfStage::ComputeGeometry,
                        std::chrono::steady_clock::now() - updated_geometry_start);
      spdlog::debug("=== Updated Tile Layout After Monitor Change ===");
      print_tile_layout(engine.system, geometries);
    }

    auto build_frame_input_start = std::chrono::steady_clock::now();
    fill_engine_frame_input(input_state, current_desktop.data, cluster_options,
                            provider.options.loopOptions.toggle_zen_on_window_maximize,
                            poll_hotkey_action(), provider.options.layoutOptions,
                            provider.options.loopOptions.mouse_drag_drop, frame_input);
    perf.record_stage(LoopPerfStage::BuildFrameInput,
                      std::chrono::steady_clock::now() - build_frame_input_start);

    auto engine_start = std::chrono::steady_clock::now();
    EngineFrameOutput frame_output = engine.process_frame(frame_input);
    perf.record_stage(LoopPerfStage::Engine, std::chrono::steady_clock::now() - engine_start);

    winapi::process_pending_non_hotkey_messages();
    if (winapi::is_session_paused()) {
      spdlog::debug("Session paused before applying frame output, waiting for resume...");
      winapi::wait_for_session_active();
      mark_all_desktops_for_retile(multi_engine);
      spdlog::debug("Session resumed, continuing loop");
      continue;
    }

    if (frame_output.restart_system) {
      floating_state.groups.clear();
      winapi::fill_monitors(current_monitors);
      reinitialize_system_from_monitors(monitors, current_monitors, provider.options, multi_engine,
                                        floating_state, "Restart hotkey pressed");
      resolve_cluster_tiling_options_into(monitors, provider.options, cluster_options);
      auto restarted_geometry_start = std::chrono::steady_clock::now();
      geometries = engine.compute_geometries(cluster_options);
      perf.record_stage(LoopPerfStage::ComputeGeometry,
                        std::chrono::steady_clock::now() - restarted_geometry_start);
      frame_output.topology_changed = true;
      frame_output.layout_changed = true;
      frame_output.apply_tiles = true;
      frame_output.placement_correction_leaf_ids.clear();
      frame_output.focus_leaf_id.reset();
      frame_output.cursor_pos.reset();
      frame_output.has_completed_initial_tile_pass = true;
      frame_output.geometries = geometries;
      spdlog::debug("=== Updated Tile Layout After Restart Hotkey ===");
      print_tile_layout(engine.system, geometries);
    }

    auto apply_start = std::chrono::steady_clock::now();
    apply_frame_output(frame_output, engine.system, provider.options.ignoreOptions, input_state,
                       floating_state, toast);
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
      if (should_clear_overlay(overlay_render_cache)) {
        overlay::clear();
      }
      continue;
    }

    current_desktop.data.has_completed_initial_tile_pass =
        frame_output.has_completed_initial_tile_pass;
    current_desktop.data.reapply_layout_templates = false;
    geometries = std::move(frame_output.geometries);

    // Render cell system overlay
    auto visible_message = toast.get_visible_message();
    auto render_snapshot = make_overlay_render_snapshot(
        engine.system, geometries, provider.options.visualizationOptions.renderOptions,
        engine.stored_cell, visible_message);
    if (should_render_overlay(overlay_render_cache, std::move(render_snapshot))) {
      auto render_start = std::chrono::steady_clock::now();
      renderer::render(engine.system, geometries,
                       provider.options.visualizationOptions.renderOptions, engine.stored_cell,
                       visible_message);
      perf.record_stage(LoopPerfStage::Render, std::chrono::steady_clock::now() - render_start);
    }

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
  winapi::unregister_notification_area_icon();
  winapi::unregister_session_power_notifications();
  winapi::unregister_move_size_hook();
  overlay::shutdown();
  spdlog::info("Hotkeys unregistered, hooks unregistered, overlay shutdown, exiting...");
}

} // namespace wintiler
