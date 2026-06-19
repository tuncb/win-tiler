#pragma once

#include <spdlog/common.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "loop_coordinator.h"
#include "options.h"

namespace wintiler {

struct LoopRunOptions {
  bool perf_stats = false;
  std::optional<std::filesystem::path> config_path;
  std::optional<std::filesystem::path> log_file_path;
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

struct RuntimeLoggingState {
  spdlog::level::level_enum configured_level = spdlog::level::info;
  bool verbose_logging_enabled = false;
};

[[nodiscard]] bool operator==(const OverlayRenderRect& lhs, const OverlayRenderRect& rhs);
[[nodiscard]] bool operator==(const OverlayRenderSnapshot& lhs, const OverlayRenderSnapshot& rhs);
[[nodiscard]] const char* format_spdlog_level(spdlog::level::level_enum level);
void set_runtime_verbose_logging(RuntimeLoggingState& state, bool enabled);
void toggle_runtime_verbose_logging(RuntimeLoggingState& state);

[[nodiscard]] OverlayRenderSnapshot make_overlay_render_snapshot(
    const ctrl::System& system, const std::vector<std::vector<ctrl::Rect>>& geometries,
    const renderer::RenderOptions& config, std::optional<StoredCell> stored_cell,
    const std::optional<std::string>& message, bool suppress_rectangles);

[[nodiscard]] bool should_render_overlay(OverlayRenderCache& cache, OverlayRenderSnapshot snapshot);
[[nodiscard]] bool should_clear_overlay(OverlayRenderCache& cache);

enum class NoDesktopHotkeyAction {
  None,
  Ignore,
  Exit,
  EnterManualPause,
  DumpWindowManagement,
  ToggleFloating,
  ToggleVerboseLogging,
};

[[nodiscard]] NoDesktopHotkeyAction
classify_no_desktop_hotkey(std::optional<HotkeyAction> hotkey_action);

enum class ManualPauseHotkeyAction {
  None,
  Ignore,
  Resume,
  DumpWindowManagement,
  ToggleVerboseLogging,
};

[[nodiscard]] ManualPauseHotkeyAction
classify_manual_pause_hotkey(std::optional<HotkeyAction> hotkey_action);

void run_loop_mode(GlobalOptionsProvider& provider, const LoopRunOptions& run_options = {});

} // namespace wintiler
