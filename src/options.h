#pragma once

#include <filesystem>
#include <optional>
#include <string>
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
  std::optional<SmallWindowBarrier> small_window_barrier;

  // When true, user-provided values are merged with defaults; when false, only user values are used
  bool merge_processes = true;
  bool merge_window_titles = true;
  bool merge_process_title_pairs = true;
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
  ToggleZen
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

// Gap configuration for window spacing
struct GapOptions {
  float horizontal = kDefaultGapHorizontal;
  float vertical = kDefaultGapVertical;
};

// Loop configuration
struct LoopOptions {
  int intervalMs = kDefaultLoopIntervalMs;
};

// Visualization configuration for cell rendering
struct VisualizationOptions {
  overlay::Color normalColor{255, 255, 255, 100}; // Semi-transparent white
  overlay::Color selectedColor{0, 120, 255, 200}; // Blue
  overlay::Color storedColor{255, 180, 0, 200};   // Orange
  float borderWidth = 3.0f;
  float toastFontSize = 60.0f;
  int toastDurationMs = 2000;
};

// Zen mode configuration
struct ZenOptions {
  float percentage = kDefaultZenPercentage; // 0.0-1.0 range, centered in cluster
};

// Global options container
struct GlobalOptions {
  IgnoreOptions ignoreOptions;
  KeyboardOptions keyboardOptions;
  GapOptions gapOptions;
  LoopOptions loopOptions;
  VisualizationOptions visualizationOptions;
  ZenOptions zenOptions;
};

// Get default global options
GlobalOptions get_default_global_options();

// Get default ignore options (convenience function)
IgnoreOptions get_default_ignore_options();

// Result type for TOML operations
struct WriteResult {
  bool success;
  std::string error; // Set if success == false
};

struct ReadResult {
  bool success;
  std::string error;     // Set if success == false
  GlobalOptions options; // Valid if success == true
};

// Write GlobalOptions to a TOML file
WriteResult write_options_toml(const GlobalOptions& options, const std::filesystem::path& filepath);

// Read GlobalOptions from a TOML file
ReadResult read_options_toml(const std::filesystem::path& filepath);

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
