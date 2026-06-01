#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine.h"
#include "multi_engine.h"
#include "options.h"
#include "winapi.h"

namespace wintiler {

struct LoopDesktopData {
  bool has_completed_initial_tile_pass = false;
  bool reapply_layout_templates = false;
};

template <typename DesktopId>
void mark_all_desktops_for_retile(MultiEngine<LoopDesktopData, DesktopId>& multi_engine) {
  for (auto& [id, desktop] : multi_engine.desktops) {
    desktop.data.has_completed_initial_tile_pass = false;
  }
}

template <typename DesktopId>
void mark_all_desktops_for_layout_reapply(MultiEngine<LoopDesktopData, DesktopId>& multi_engine) {
  for (auto& [id, desktop] : multi_engine.desktops) {
    desktop.data.has_completed_initial_tile_pass = false;
    desktop.data.reapply_layout_templates = true;
  }
}

template <typename DesktopId>
void reinitialize_all_desktops(MultiEngine<LoopDesktopData, DesktopId>& multi_engine,
                               const std::vector<ctrl::ClusterInitInfo>& cluster_infos,
                               ctrl::SplitMode split_mode = ctrl::SplitMode::Dwindle) {
  for (auto& [id, desktop] : multi_engine.desktops) {
    desktop.engine.init(cluster_infos, split_mode);
    desktop.engine.clear_stored_cell();
    desktop.data.has_completed_initial_tile_pass = false;
    desktop.data.reapply_layout_templates = false;
  }
}

struct LoopDesktopActivation {
  std::reference_wrapper<MultiEngine<LoopDesktopData, std::string>::Desktop> desktop;
  bool created = false;
  bool switched = false;
};

[[nodiscard]] std::optional<LoopDesktopActivation>
activate_loop_desktop(MultiEngine<LoopDesktopData, std::string>& multi_engine,
                      const std::string& desktop_id,
                      const std::vector<ctrl::ClusterInitInfo>& cluster_infos,
                      ctrl::SplitMode split_mode = ctrl::SplitMode::Dwindle);

[[nodiscard]] bool should_exchange_mouse_drag_drop(MouseDragDropAction base_action,
                                                   bool is_ctrl_pressed,
                                                   bool is_right_mouse_pressed);

void fill_engine_frame_input(const winapi::LoopInputState& input_state,
                             const LoopDesktopData& desktop_data,
                             const std::vector<ClusterTilingOptions>& cluster_options,
                             bool auto_zen_on_maximize, std::optional<HotkeyAction> hotkey_action,
                             const LayoutOptions& layout_options,
                             MouseDragDropAction mouse_drag_drop_action,
                             EngineFrameInput& frame_input);

} // namespace wintiler
