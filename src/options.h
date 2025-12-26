#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
};

// Keyboard hotkey actions
enum class HotkeyAction {
  NavigateLeft,
  NavigateDown,
  NavigateUp,
  NavigateRight,
  ToggleSplit,
  Exit,
  ToggleGlobal,
  StoreCell,
  ClearStored,
  Exchange,
  Move
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

// Gap configuration for window spacing
struct GapOptions {
  float horizontal = kDefaultGapHorizontal;
  float vertical = kDefaultGapVertical;
};

// Global options container
struct GlobalOptions {
  IgnoreOptions ignoreOptions;
  KeyboardOptions keyboardOptions;
  GapOptions gapOptions;
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

} // namespace wintiler
