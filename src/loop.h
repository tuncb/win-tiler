#pragma once

#include <optional>
#include <string>
#include <vector>

#include "multi_engine.h"
#include "options.h"

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
                               const std::vector<ctrl::ClusterInitInfo>& cluster_infos) {
  for (auto& [id, desktop] : multi_engine.desktops) {
    desktop.engine.init(cluster_infos);
    desktop.engine.clear_stored_cell();
    desktop.data.has_completed_initial_tile_pass = false;
    desktop.data.reapply_layout_templates = false;
  }
}

struct LoopRunOptions {
  bool perf_stats = false;
};

struct OverlayRenderRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  overlay::Color color{};
  float border_width = 0.0f;
};

struct OverlayRenderSnapshot {
  std::vector<OverlayRenderRect> rects;
  std::optional<std::string> message;
  float toast_font_size = 0.0f;
};

struct OverlayRenderCache {
  std::optional<OverlayRenderSnapshot> last_presented;
};

[[nodiscard]] bool operator==(const OverlayRenderRect& lhs, const OverlayRenderRect& rhs);
[[nodiscard]] bool operator==(const OverlayRenderSnapshot& lhs, const OverlayRenderSnapshot& rhs);

[[nodiscard]] OverlayRenderSnapshot make_overlay_render_snapshot(
    const ctrl::System& system, const std::vector<std::vector<ctrl::Rect>>& geometries,
    const renderer::RenderOptions& config, std::optional<StoredCell> stored_cell,
    const std::optional<std::string>& message);

[[nodiscard]] bool should_render_overlay(OverlayRenderCache& cache, OverlayRenderSnapshot snapshot);
[[nodiscard]] bool should_clear_overlay(OverlayRenderCache& cache);

enum class NoDesktopHotkeyAction {
  None,
  Ignore,
  Exit,
  EnterManualPause,
  DumpWindowManagement,
};

[[nodiscard]] NoDesktopHotkeyAction
classify_no_desktop_hotkey(std::optional<HotkeyAction> hotkey_action);

enum class ManualPauseHotkeyAction {
  None,
  Ignore,
  Resume,
  DumpWindowManagement,
};

[[nodiscard]] ManualPauseHotkeyAction
classify_manual_pause_hotkey(std::optional<HotkeyAction> hotkey_action);

void run_loop_mode(GlobalOptionsProvider& provider, const LoopRunOptions& run_options = {});

} // namespace wintiler
