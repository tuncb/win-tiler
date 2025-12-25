#pragma once

#include <optional>
#include <string>
#include <utility>
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
  std::string hotkey;  // e.g., "super+shift+h"
};

struct KeyboardOptions {
  std::vector<HotkeyBinding> bindings;
};

// Global options container
struct GlobalOptions {
  IgnoreOptions ignoreOptions;
  KeyboardOptions keyboardOptions;
};

// Get default global options
GlobalOptions get_default_global_options();

// Get default ignore options (convenience function)
IgnoreOptions get_default_ignore_options();

} // namespace wintiler
