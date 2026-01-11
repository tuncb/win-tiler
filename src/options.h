#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <utility>
#include <variant>
#include <vector>

#include "overlay.h"

namespace wintiler {

// Window ignore configuration
struct SmallWindowBarrier {
  int width;
  int height;
};

struct IgnoreOptions {
  std::vector<std::string> ignored_processes;
  std::vector<std::string> ignored_window_titles;
  std::vector<std::pair<std::string, std::string>> ignored_process_title_pairs;
  std::vector<std::string> ignore_children_of_processes;
  std::optional<SmallWindowBarrier> small_window_barrier;

  // When true, user-provided values are merged with defaults; when false, only user values are used
  bool merge_processes = true;
  bool merge_window_titles = true;
  bool merge_process_title_pairs = true;
  bool merge_ignore_children_of_processes = true;
};

// Keyboard hotkey actions
enum class HotkeyAction {
  NavigateLeft,
  NavigateDown,
  NavigateUp,
  NavigateRight,
  ToggleSplit,
  Exit,
  CycleSplitMode,
  StoreCell,
  ClearStored,
  Exchange,
  Move,
  SplitIncrease,
  SplitDecrease,
  ExchangeSiblings,
  ToggleZen,
  ResetSplitRatio,
  TogglePause
};

// Maps a hotkey action to its keyboard shortcut string
struct HotkeyBinding {
  HotkeyAction action;
  std::string hotkey; // e.g., "super+shift+h"
};

struct KeyboardOptions {
  std::vector<HotkeyBinding> bindings;
};

// Default gap values
constexpr float kDefaultGapHorizontal = 10.0f;
constexpr float kDefaultGapVertical = 10.0f;

// Default loop interval
constexpr int kDefaultLoopIntervalMs = 100;

// Default zen percentage (0.0-1.0 range, 1.0 = full cluster)
constexpr float kDefaultZenPercentage = 0.85f;

// Default visualization options
constexpr float kDefaultBorderWidth = 3.0f;
constexpr float kDefaultToastFontSize = 60.0f;
constexpr int kDefaultToastDurationMs = 2000;

// Default small window barrier dimensions
constexpr int kDefaultSmallWindowBarrierWidth = 200;
constexpr int kDefaultSmallWindowBarrierHeight = 150;

// Gap configuration for window spacing
struct GapOptions {
  float horizontal = kDefaultGapHorizontal;
  float vertical = kDefaultGapVertical;
};

// Loop configuration
struct LoopOptions {
  int intervalMs = kDefaultLoopIntervalMs;
};

// Render-specific options used by the renderer
namespace renderer {
struct RenderOptions {
  overlay::Color normal_color{255, 255, 255, 100}; // Semi-transparent white
  overlay::Color selected_color{0, 120, 255, 200}; // Blue
  overlay::Color stored_color{255, 180, 0, 200};   // Orange
  float border_width = kDefaultBorderWidth;
  float toast_font_size = kDefaultToastFontSize;
  float zen_percentage = kDefaultZenPercentage; // Zen cell size as percentage of cluster (0.0-1.0)
};
} // namespace renderer

// Visualization configuration for cell rendering
struct VisualizationOptions {
  renderer::RenderOptions renderOptions;
  int toastDurationMs = kDefaultToastDurationMs;
};

// Global options container
struct GlobalOptions {
  IgnoreOptions ignoreOptions;
  KeyboardOptions keyboardOptions;
  GapOptions gapOptions;
  LoopOptions loopOptions;
  VisualizationOptions visualizationOptions;
};

// Get default global options
GlobalOptions get_default_global_options();

// Get default ignore options (convenience function)
IgnoreOptions get_default_ignore_options();

// Write GlobalOptions to a TOML file
tl::expected<void, std::string> write_options_toml(const GlobalOptions& options,
                                                   const std::filesystem::path& filepath);

// Read GlobalOptions from a TOML file
tl::expected<GlobalOptions, std::string> read_options_toml(const std::filesystem::path& filepath);

// Provides GlobalOptions, optionally monitoring a config file for changes
class GlobalOptionsProvider {
public:
  std::optional<std::filesystem::path> configPath;
  GlobalOptions options;
  std::filesystem::file_time_type lastModified;

  explicit GlobalOptionsProvider(std::optional<std::filesystem::path> configPath = std::nullopt);

  // Check for file changes and reload if necessary. Returns true if options changed.
  bool refresh();
};

} // namespace wintiler
