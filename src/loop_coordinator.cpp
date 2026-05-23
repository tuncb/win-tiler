#include "loop_coordinator.h"

#include "runtime_support.h"

namespace wintiler {

namespace {

std::optional<ctrl::Rect> find_actual_window_rect(const winapi::LoopInputState& input_state,
                                                  winapi::HWND_T hwnd) {
  if (hwnd == nullptr) {
    return std::nullopt;
  }

  for (const auto& monitor_windows : input_state.windows_per_monitor) {
    for (const auto& window : monitor_windows) {
      if (window.handle != hwnd || !window.actual_rect.has_value()) {
        continue;
      }
      return ctrl::Rect{static_cast<float>(window.actual_rect->x),
                        static_cast<float>(window.actual_rect->y),
                        static_cast<float>(window.actual_rect->width),
                        static_cast<float>(window.actual_rect->height)};
    }
  }

  return std::nullopt;
}

} // namespace

std::optional<LoopDesktopActivation>
activate_loop_desktop(MultiEngine<LoopDesktopData, std::string>& multi_engine,
                      const std::string& desktop_id,
                      const std::vector<ctrl::ClusterInitInfo>& cluster_infos) {
  bool created = false;
  if (!multi_engine.has_desktop(desktop_id)) {
    auto created_desktop = multi_engine.create_desktop(desktop_id, cluster_infos);
    if (!created_desktop.has_value()) {
      return std::nullopt;
    }
    created = true;
  }

  bool switched = false;
  if (!multi_engine.has_current() || *multi_engine.current_id != desktop_id) {
    if (!multi_engine.switch_to(desktop_id)) {
      return std::nullopt;
    }
    switched = true;
  }

  return LoopDesktopActivation{std::ref(multi_engine.current()), created, switched};
}

bool should_exchange_mouse_drag_drop(MouseDragDropAction base_action, bool is_ctrl_pressed,
                                     bool is_right_mouse_pressed) {
  bool is_modifier_pressed = is_ctrl_pressed || is_right_mouse_pressed;

  switch (base_action) {
  case MouseDragDropAction::Exchange:
    return !is_modifier_pressed;
  case MouseDragDropAction::Split:
    return is_modifier_pressed;
  }
  return !is_modifier_pressed;
}

void fill_engine_frame_input(const winapi::LoopInputState& input_state,
                             const LoopDesktopData& desktop_data,
                             const std::vector<ClusterTilingOptions>& cluster_options,
                             bool auto_zen_on_maximize, std::optional<HotkeyAction> hotkey_action,
                             const LayoutOptions& layout_options,
                             MouseDragDropAction mouse_drag_drop_action,
                             EngineFrameInput& frame_input) {
  extract_cluster_updates_from_input_into(input_state, frame_input.cluster_updates);
  extract_managed_window_states_from_input_into(input_state, frame_input.managed_windows);
  frame_input.hotkey_action = hotkey_action;
  frame_input.cursor_pos.reset();
  frame_input.completed_drag.reset();
  frame_input.auto_zen_on_maximize = auto_zen_on_maximize;
  frame_input.update_hover_selection = true;
  frame_input.has_completed_initial_tile_pass = desktop_data.has_completed_initial_tile_pass;
  frame_input.reapply_layout_templates = desktop_data.reapply_layout_templates;
  frame_input.layout_options = &layout_options;
  frame_input.cluster_options = cluster_options;
  frame_input.gap_h = 0.0f;
  frame_input.gap_v = 0.0f;
  frame_input.zen_pct = 0.0f;

  if (input_state.cursor_pos.has_value()) {
    frame_input.cursor_pos = ctrl::Point{input_state.cursor_pos->x, input_state.cursor_pos->y};
  }

  if (input_state.drag_info.has_value() && input_state.drag_info->move_ended) {
    CompletedDragRequest drag_request;
    drag_request.leaf_id = reinterpret_cast<size_t>(input_state.drag_info->hwnd);
    drag_request.do_exchange = should_exchange_mouse_drag_drop(
        mouse_drag_drop_action, input_state.is_ctrl_pressed, input_state.is_right_mouse_pressed);
    drag_request.cursor_pos = frame_input.cursor_pos;
    drag_request.actual_window_rect =
        find_actual_window_rect(input_state, input_state.drag_info->hwnd);

    frame_input.completed_drag = drag_request;
  }
}

} // namespace wintiler
