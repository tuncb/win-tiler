#include "options.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <magic_enum/magic_enum.hpp>
#include <system_error>
#include <toml++/toml.hpp>

namespace wintiler {

namespace {

std::chrono::steady_clock::time_point
next_config_refresh_check(std::chrono::steady_clock::time_point now, const GlobalOptions& options) {
  if (options.loopOptions.configRefreshIntervalMs <= 0) {
    return std::chrono::steady_clock::time_point::min();
  }
  return now + std::chrono::milliseconds(options.loopOptions.configRefreshIntervalMs);
}

std::string hotkey_action_to_string(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return "NavigateLeft";
  case HotkeyAction::NavigateDown:
    return "NavigateDown";
  case HotkeyAction::NavigateUp:
    return "NavigateUp";
  case HotkeyAction::NavigateRight:
    return "NavigateRight";
  case HotkeyAction::ToggleSplit:
    return "ToggleSplit";
  case HotkeyAction::Exit:
    return "Exit";
  case HotkeyAction::CycleSplitMode:
    return "CycleSplitMode";
  case HotkeyAction::MoveLeft:
    return "MoveLeft";
  case HotkeyAction::MoveDown:
    return "MoveDown";
  case HotkeyAction::MoveUp:
    return "MoveUp";
  case HotkeyAction::MoveRight:
    return "MoveRight";
  case HotkeyAction::ToggleMovementMode:
    return "ToggleMovementMode";
  case HotkeyAction::SplitIncrease:
    return "SplitIncrease";
  case HotkeyAction::SplitDecrease:
    return "SplitDecrease";
  case HotkeyAction::ExchangeSiblings:
    return "ExchangeSiblings";
  case HotkeyAction::ToggleZen:
    return "ToggleZen";
  case HotkeyAction::ResetSplitRatio:
    return "ResetSplitRatio";
  case HotkeyAction::TogglePause:
    return "TogglePause";
  case HotkeyAction::DumpWindowManagement:
    return "DumpWindowManagement";
  case HotkeyAction::RestartSystem:
    return "RestartSystem";
  case HotkeyAction::ToggleFloating:
    return "ToggleFloating";
  case HotkeyAction::ToggleVerboseLogging:
    return "ToggleVerboseLogging";
  }
  return "Unknown";
}

std::optional<HotkeyAction> string_to_hotkey_action(const std::string& str) {
  if (str == "NavigateLeft")
    return HotkeyAction::NavigateLeft;
  if (str == "NavigateDown")
    return HotkeyAction::NavigateDown;
  if (str == "NavigateUp")
    return HotkeyAction::NavigateUp;
  if (str == "NavigateRight")
    return HotkeyAction::NavigateRight;
  if (str == "ToggleSplit")
    return HotkeyAction::ToggleSplit;
  if (str == "Exit")
    return HotkeyAction::Exit;
  if (str == "CycleSplitMode")
    return HotkeyAction::CycleSplitMode;
  if (str == "MoveLeft")
    return HotkeyAction::MoveLeft;
  if (str == "MoveDown")
    return HotkeyAction::MoveDown;
  if (str == "MoveUp")
    return HotkeyAction::MoveUp;
  if (str == "MoveRight")
    return HotkeyAction::MoveRight;
  if (str == "ToggleMovementMode")
    return HotkeyAction::ToggleMovementMode;
  if (str == "SplitIncrease")
    return HotkeyAction::SplitIncrease;
  if (str == "SplitDecrease")
    return HotkeyAction::SplitDecrease;
  if (str == "ExchangeSiblings")
    return HotkeyAction::ExchangeSiblings;
  if (str == "ToggleZen")
    return HotkeyAction::ToggleZen;
  if (str == "ResetSplitRatio")
    return HotkeyAction::ResetSplitRatio;
  if (str == "TogglePause")
    return HotkeyAction::TogglePause;
  if (str == "DumpWindowManagement")
    return HotkeyAction::DumpWindowManagement;
  if (str == "RestartSystem")
    return HotkeyAction::RestartSystem;
  if (str == "ToggleFloating")
    return HotkeyAction::ToggleFloating;
  if (str == "ToggleVerboseLogging")
    return HotkeyAction::ToggleVerboseLogging;
  return std::nullopt;
}

std::string layout_split_mode_to_string(LayoutSplitMode split_mode) {
  switch (split_mode) {
  case LayoutSplitMode::Dwindle:
    return "dwindle";
  case LayoutSplitMode::Vertical:
    return "vertical";
  case LayoutSplitMode::Horizontal:
    return "horizontal";
  }
  return "dwindle";
}

std::optional<LayoutSplitMode> string_to_layout_split_mode(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (value == "dwindle") {
    return LayoutSplitMode::Dwindle;
  }
  if (value == "vertical") {
    return LayoutSplitMode::Vertical;
  }
  if (value == "horizontal") {
    return LayoutSplitMode::Horizontal;
  }
  return std::nullopt;
}

std::string layout_split_target_to_string(LayoutSplitTarget target) {
  switch (target) {
  case LayoutSplitTarget::Pointer:
    return "pointer";
  case LayoutSplitTarget::Focused:
    return "focused";
  case LayoutSplitTarget::Largest:
    return "largest";
  case LayoutSplitTarget::LargestAllMonitors:
    return "largest_all_monitors";
  }
  return "pointer";
}

std::optional<LayoutSplitTarget> string_to_layout_split_target(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (value == "pointer") {
    return LayoutSplitTarget::Pointer;
  }
  if (value == "focused") {
    return LayoutSplitTarget::Focused;
  }
  if (value == "largest") {
    return LayoutSplitTarget::Largest;
  }
  if (value == "largest_all_monitors") {
    return LayoutSplitTarget::LargestAllMonitors;
  }
  return std::nullopt;
}

std::string mouse_drag_drop_action_to_string(MouseDragDropAction action) {
  switch (action) {
  case MouseDragDropAction::Exchange:
    return "exchange";
  case MouseDragDropAction::Split:
    return "split";
  }
  return "exchange";
}

std::optional<MouseDragDropAction> string_to_mouse_drag_drop_action(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (value == "exchange") {
    return MouseDragDropAction::Exchange;
  }
  if (value == "split") {
    return MouseDragDropAction::Split;
  }
  return std::nullopt;
}

std::string get_default_hotkey(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return "super+shift+h";
  case HotkeyAction::NavigateDown:
    return "super+shift+j";
  case HotkeyAction::NavigateUp:
    return "super+shift+k";
  case HotkeyAction::NavigateRight:
    return "super+shift+l";
  case HotkeyAction::ToggleSplit:
    return "super+shift+y";
  case HotkeyAction::Exit:
    return "super+shift+escape";
  case HotkeyAction::CycleSplitMode:
    return "super+shift+;";
  case HotkeyAction::MoveLeft:
    return "super+alt+shift+h";
  case HotkeyAction::MoveDown:
    return "super+alt+shift+j";
  case HotkeyAction::MoveUp:
    return "super+alt+shift+k";
  case HotkeyAction::MoveRight:
    return "super+alt+shift+l";
  case HotkeyAction::ToggleMovementMode:
    return "super+alt+shift+,";
  case HotkeyAction::SplitIncrease:
    return "super+shift+pageup";
  case HotkeyAction::SplitDecrease:
    return "super+shift+pagedown";
  case HotkeyAction::ExchangeSiblings:
    return "super+shift+e";
  case HotkeyAction::ToggleZen:
    return "super+shift+'";
  case HotkeyAction::ResetSplitRatio:
    return "super+shift+home";
  case HotkeyAction::TogglePause:
    return "super+shift+\\";
  case HotkeyAction::DumpWindowManagement:
    return "super+shift+d";
  case HotkeyAction::RestartSystem:
    return "super+shift+r";
  case HotkeyAction::ToggleFloating:
    return "super+shift+f";
  case HotkeyAction::ToggleVerboseLogging:
    return "super+shift+v";
  }
  return "";
}

// Helper to read a numeric value, accepting both float and integer TOML types
template <typename T>
std::optional<T> get_number(const toml::node_view<toml::node>& node) {
  if (auto fp = node.as_floating_point()) {
    return static_cast<T>(fp->get());
  }
  if (auto integer = node.as_integer()) {
    return static_cast<T>(integer->get());
  }
  return std::nullopt;
}

LayoutOptions parse_layout_options(toml::table& table) {
  LayoutOptions options;

  if (auto split_mode = table["split_mode"].as_string()) {
    auto parsed_split_mode = string_to_layout_split_mode(std::string(split_mode->get()));
    if (parsed_split_mode.has_value()) {
      options.split_mode = *parsed_split_mode;
    } else {
      spdlog::error("Invalid layout.split_mode value ({}): must be \"dwindle\", \"vertical\", "
                    "or \"horizontal\". Using default.",
                    split_mode->get());
    }
  }

  if (table.contains("split_target")) {
    auto value = table["split_target"].value<std::string>();
    auto target = value ? string_to_layout_split_target(*value) : std::nullopt;
    if (target.has_value()) {
      options.split_target = *target;
    } else {
      spdlog::error("Invalid layout.split_target: must be \"pointer\", \"focused\", "
                    "\"largest\", or \"largest_all_monitors\". Using default.");
    }
  }

  if (auto split_width_multiplier = get_number<float>(table["split_width_multiplier"])) {
    if (*split_width_multiplier <= 0.0f) {
      spdlog::error("Invalid layout.split_width_multiplier value ({}): must be positive. "
                    "Using default.",
                    *split_width_multiplier);
    } else {
      options.split_width_multiplier = *split_width_multiplier;
    }
  }

  return options;
}

toml::table layout_options_to_toml(const LayoutOptions& options) {
  toml::table layout;
  layout.insert("split_mode", layout_split_mode_to_string(options.split_mode));
  layout.insert("split_target", layout_split_target_to_string(options.split_target));
  layout.insert("split_width_multiplier", options.split_width_multiplier);

  return layout;
}

std::string get_options_toml_documentation() {
  return R"(# win-tiler configuration
# This file is TOML. Lines that start with # are comments and can be copied,
# uncommented, and edited. Missing options use built-in defaults.
#
# Keyboard hotkey format:
# - Join key names with '+', for example "super+shift+h" or "ctrl+alt+left".
# - "super" is the Windows key.
# - Set a hotkey to an empty string to disable that action.
#
# Keyboard options:
# [keyboard]
# movement_mode = "swap"
# movement_mode: initial directional movement behavior, "swap" or "insert".
# ToggleMovementMode and tray Keyboard movement choices affect only the current desktop session.
# Neither writes movement_mode to config.
# MoveLeft/Down/Up/Right use Win+Alt+Shift+H/J/K/L; toggle uses Win+Alt+Shift+comma.
# Insert follows the direction (left/right: side by side; up/down: stacked).
# With no neighbor in that direction, movement does nothing.
# bindings = [
#   { action = "NavigateLeft", hotkey = "super+shift+h" },
# ]
#
# action: one of the hotkey actions below.
# hotkey: shortcut text in the format described above.
#
# Hotkey actions and what they do:
# NavigateLeft, NavigateDown, NavigateUp, NavigateRight, ToggleSplit, Exit,
# CycleSplitMode, MoveLeft, MoveDown, MoveUp, MoveRight, ToggleMovementMode, SplitIncrease,
# SplitDecrease, ExchangeSiblings, ToggleZen, ResetSplitRatio, TogglePause,
# DumpWindowManagement, RestartSystem, ToggleFloating, ToggleVerboseLogging.
#
# Ignore options:
# [ignore]
# merge_processes_with_defaults = true
# merge_window_titles_with_defaults = true
# merge_process_title_pairs_with_defaults = true
# merge_ignore_children_of_processes_with_defaults = true
# processes = ["MyPopupApp.exe"]
# window_titles = ["Settings"]
# process_title_pairs = [{ process = "app.exe", title = "Tool Window" }]
# ignore_children_of_processes = ["launcher.exe"]
# small_window_barrier = { width = 200, height = 150 }
#
# merge_*_with_defaults: true merges your list with built-in ignored windows;
# false uses only the list in this file.
# processes: executable names to ignore. Matching is case-insensitive.
# window_titles: exact window titles to ignore.
# process_title_pairs: ignore only when process and title both match.
# ignore_children_of_processes: ignore child windows spawned by these processes.
# small_window_barrier: ignore windows at or below this width and height.
#
# Gap options:
# [gap]
# horizontal = 10.0
# vertical = 10.0
#
# horizontal: pixels between tiled windows horizontally. Must be non-negative.
# vertical: pixels between tiled windows vertically. Must be non-negative.
#
# Loop options:
# [loop]
# interval_ms = 100
# config_refresh_interval_ms = 1000
# toggle_zen_on_window_maximize = true
# mouse_drag_drop = "exchange"
#
# interval_ms: main loop polling interval in milliseconds. Must be non-negative.
# config_refresh_interval_ms: how often the config file is checked for changes.
# toggle_zen_on_window_maximize: true toggles zen when a tiled window is maximized.
# mouse_drag_drop: "exchange" swaps windows, "split" inserts as a split.
#
# Tray New windows choices save split_target/split_mode here and apply without restarting.
# CycleSplitMode is a session override; unrelated config edits preserve that override.
# Layout options (apply to every monitor):
# [layout]
# split_mode = "dwindle"
# split_target = "pointer"
# split_width_multiplier = 1.0
#
# split_mode:
# - "dwindle": split direction follows the target cell's aspect ratio.
# - "vertical": prefer left/right splits.
# - "horizontal": prefer top/bottom splits.
# split_target: target for automatic insertion; explicit moves take precedence.
# - "pointer": use the monitor and cell under the pointer (default).
# - "focused": split the last focused tiled window, independent of the pointer.
# - "largest": split the largest cell by area on the active monitor, or fill it if empty.
# - "largest_all_monitors": use the largest cell or empty monitor work area across eligible
# monitors on the current virtual desktop.
# Moving or clicking the pointer activates its monitor, including the desktop and taskbar.
# A later tiled-window focus change or navigation activates that window's monitor. An idle
# pointer and win-tiler's own cursor warps do not override it. New windows taking OS focus
# keep the preceding active monitor until placement. Untiled focus preserves that monitor.
# At startup, use existing tiled focus, then the pointer if no tiled focus is available.
# Focused uses the incoming monitor when no tiled focus is known, then the first leaf.
# Largest uses the incoming monitor when no active monitor is known.
# Equal largest areas use first-child tree order. Each insertion is 50/50.
# Largest is recalculated for every new window, including startup and batches.
# For largest_all_monitors, an empty monitor's entire work area competes with existing cells;
# choosing it places the new window there without splitting. Fullscreen monitors are excluded.
# With no eligible target, keep the incoming monitor. Ties prefer the last focused tiled
# window's monitor, then monitor order and first-child tree order. Recalculate after each
# arrival, following removals.
# Both largest modes compare work-area pixel sizes and split ratios, before gaps, minimum
# window sizes, and zen enlargement. Largest_all_monitors exits zen on the destination monitor.
# largest_all_monitors preserves monitor membership at startup/reinitialization, using local
# largest splits there. Arrivals include windows returning to tiling (e.g. from floating).
# split_width_multiplier: positive multiplier applied to cell width when dwindle chooses
# a split direction.
# Visualization options:
# [visualization]
# toast_duration_ms = 2000
# [visualization.render]
# show_rectangles = true
# show_only_active_window = false
# normal_color = [255, 255, 255, 100]
# selected_color = [0, 120, 255, 200]
# border_width = 3.0
# toast_font_size = 60.0
# zen_percentage = 0.90
# hide_rectangles_when_processes_open = ["ScreenShare.exe"]
#
# toast_duration_ms: how long status toast messages stay visible.
# Tray Window outlines saves show_rectangles and show_only_active_window.
# Enabling outlines restores border_width to 3.0 if it was zero; suppression rules still apply.
# show_rectangles: whether overlay rectangles are visible. Toast messages are unaffected.
# show_only_active_window: draw only the foreground tiled window using selected_color.
# Defaults to false. No rectangle is drawn when the foreground window is not tiled.
# Requires show_rectangles = true; fullscreen and process suppression still apply.
# normal_color: overlay rectangle color for normal cells.
# selected_color: overlay rectangle color for the selected cell.
# border_width: overlay border width in pixels. Must be non-negative; 0 hides rectangles.
# toast_font_size: toast text size. Must be at least 1.0.
# zen_percentage: zen cell size from 0.1 to 1.0 of the monitor cluster.
# hide_rectangles_when_processes_open: executable names that hide overlay rectangles while a
# visible top-level window from that process exists. Matching is case-insensitive.
# Color values are [red, green, blue, alpha], each in the 0-255 range.
# Alpha controls opacity: 0 is transparent, 255 is opaque.
#
)";
}

float clamp_zen_percentage(float zen_percentage) {
  if (zen_percentage < 0.1f) {
    spdlog::error("Invalid zen_percentage value ({}): must be >= 0.1. Using 0.1.", zen_percentage);
    return 0.1f;
  }
  if (zen_percentage > 1.0f) {
    spdlog::error("Invalid zen_percentage value ({}): must be <= 1.0. Using 1.0.", zen_percentage);
    return 1.0f;
  }
  return zen_percentage;
}

} // anonymous namespace

IgnoreOptions get_default_ignore_options() {
  IgnoreOptions options;
  options.ignored_processes = {
      "TextInputHost.exe",       "ApplicationFrameHost.exe",
      "Microsoft.CmdPal.UI.exe", "PowerToys.PowerLauncher.exe",
      "win-tiler.exe",
  };
  // options.ignored_window_titles = {"Windows Widgets", "MSN"};
  options.ignored_window_titles = {};
  options.ignored_process_title_pairs = {
      {"SystemSettings.exe", "Settings"},
      {"explorer.exe", "Program Manager"},
      {"explorer.exe", "System tray overflow window."},
      {"explorer.exe", "PopupHost"},
      {"claude.exe", "Title: Claude"},
      {"WidgetBoard.exe", "Windows Widgets"},
      {"msedgewebview2.exe", "MSN"},
  };
  options.ignore_children_of_processes = {};
  options.small_window_barrier =
      SmallWindowBarrier{kDefaultSmallWindowBarrierWidth, kDefaultSmallWindowBarrierHeight};
  return options;
}

GlobalOptions get_default_global_options() {
  GlobalOptions options;
  options.ignoreOptions = get_default_ignore_options();
  for (auto action : magic_enum::enum_values<HotkeyAction>()) {
    options.keyboardOptions.bindings.push_back({action, get_default_hotkey(action)});
  }
  return options;
}

std::optional<std::string> find_hotkey_binding(const KeyboardOptions& keyboard_options,
                                               HotkeyAction action) {
  for (const auto& binding : keyboard_options.bindings) {
    if (binding.action == action) {
      return binding.hotkey;
    }
  }

  return std::nullopt;
}

std::string format_options_toml(const GlobalOptions& options) {
  toml::table root;

  // Build ignore section
  toml::table ignore;

  // Write merge flags
  ignore.insert("merge_processes_with_defaults", options.ignoreOptions.merge_processes);
  ignore.insert("merge_window_titles_with_defaults", options.ignoreOptions.merge_window_titles);
  ignore.insert("merge_process_title_pairs_with_defaults",
                options.ignoreOptions.merge_process_title_pairs);
  ignore.insert("merge_ignore_children_of_processes_with_defaults",
                options.ignoreOptions.merge_ignore_children_of_processes);

  toml::array processes;
  for (const auto& p : options.ignoreOptions.ignored_processes) {
    processes.push_back(p);
  }
  ignore.insert("processes", processes);

  toml::array window_titles;
  for (const auto& t : options.ignoreOptions.ignored_window_titles) {
    window_titles.push_back(t);
  }
  ignore.insert("window_titles", window_titles);

  toml::array process_title_pairs;
  for (const auto& [process, title] : options.ignoreOptions.ignored_process_title_pairs) {
    toml::table pair;
    pair.insert("process", process);
    pair.insert("title", title);
    process_title_pairs.push_back(pair);
  }
  ignore.insert("process_title_pairs", process_title_pairs);

  toml::array ignore_children_of_processes;
  for (const auto& p : options.ignoreOptions.ignore_children_of_processes) {
    ignore_children_of_processes.push_back(p);
  }
  ignore.insert("ignore_children_of_processes", ignore_children_of_processes);

  if (options.ignoreOptions.small_window_barrier) {
    toml::table barrier;
    barrier.insert("width", options.ignoreOptions.small_window_barrier->width);
    barrier.insert("height", options.ignoreOptions.small_window_barrier->height);
    ignore.insert("small_window_barrier", barrier);
  }

  root.insert("ignore", ignore);

  // Build keyboard section
  toml::table keyboard;
  keyboard.insert("movement_mode",
                  options.keyboardOptions.movement_mode == MovementMode::Swap ? "swap" : "insert");
  toml::array bindings;
  for (const auto& binding : options.keyboardOptions.bindings) {
    toml::table b;
    b.insert("action", hotkey_action_to_string(binding.action));
    b.insert("hotkey", binding.hotkey);
    bindings.push_back(b);
  }
  keyboard.insert("bindings", bindings);
  root.insert("keyboard", keyboard);

  // Build gap section
  toml::table gap;
  gap.insert("horizontal", options.gapOptions.horizontal);
  gap.insert("vertical", options.gapOptions.vertical);
  root.insert("gap", gap);

  // Build loop section
  toml::table loop;
  loop.insert("interval_ms", options.loopOptions.intervalMs);
  loop.insert("config_refresh_interval_ms", options.loopOptions.configRefreshIntervalMs);
  loop.insert("toggle_zen_on_window_maximize", options.loopOptions.toggle_zen_on_window_maximize);
  loop.insert("mouse_drag_drop",
              mouse_drag_drop_action_to_string(options.loopOptions.mouse_drag_drop));
  root.insert("loop", loop);

  // Build layout section
  root.insert("layout", layout_options_to_toml(options.layoutOptions));

  // Build visualization section with nested render
  toml::table visualization;
  toml::table render;
  auto colorToArray = [](const overlay::Color& c) {
    toml::array arr;
    arr.push_back(static_cast<int64_t>(c.r));
    arr.push_back(static_cast<int64_t>(c.g));
    arr.push_back(static_cast<int64_t>(c.b));
    arr.push_back(static_cast<int64_t>(c.a));
    return arr;
  };
  const auto& ro = options.visualizationOptions.renderOptions;
  render.insert("show_rectangles", ro.show_rectangles);
  render.insert("show_only_active_window", ro.show_only_active_window);
  render.insert("normal_color", colorToArray(ro.normal_color));
  render.insert("selected_color", colorToArray(ro.selected_color));
  render.insert("border_width", ro.border_width);
  render.insert("toast_font_size", ro.toast_font_size);
  render.insert("zen_percentage", ro.zen_percentage);
  toml::array hide_rectangles_when_processes_open;
  for (const auto& process : ro.hide_rectangles_when_processes_open) {
    hide_rectangles_when_processes_open.push_back(process);
  }
  render.insert("hide_rectangles_when_processes_open", hide_rectangles_when_processes_open);
  visualization.insert("render", render);
  visualization.insert("toast_duration_ms", options.visualizationOptions.toastDurationMs);
  root.insert("visualization", visualization);

  std::ostringstream stream;
  stream << get_options_toml_documentation() << root;
  return stream.str();
}

tl::expected<void, std::string> write_options_toml(const GlobalOptions& options,
                                                   const std::filesystem::path& filepath) {
  try {
    // Write to file
    std::ofstream file(filepath);
    if (!file) {
      return tl::unexpected("Failed to open file for writing: " + filepath.string());
    }
    file << format_options_toml(options);
    file.close();
    if (!file)
      return tl::unexpected("Failed to write config file: " + filepath.string());
    return {};
  } catch (const std::exception& e) {
    return tl::unexpected(std::string("Error writing TOML: ") + e.what());
  }
}

// Reads options from a TOML file at the given path.
// All fields are optional; missing fields will use default values.
// All fields are validated; invalid values will be replaced with defaults.
// Float values should also accept integer values from TOML files.
tl::expected<GlobalOptions, std::string> read_options_toml(const std::filesystem::path& filepath) {
  try {
    auto tbl = toml::parse_file(filepath.string());
    GlobalOptions options;

    // Parse ignore section
    auto defaultIgnore = get_default_ignore_options();

    // Read merge flags (default to true if not present)
    bool mergeProcesses = true;
    bool mergeWindowTitles = true;
    bool mergeProcessTitlePairs = true;
    bool mergeIgnoreChildrenOfProcesses = true;

    // Temporary storage for user values
    std::vector<std::string> userProcesses;
    std::vector<std::string> userWindowTitles;
    std::vector<std::pair<std::string, std::string>> userProcessTitlePairs;
    std::vector<std::string> userIgnoreChildrenOfProcesses;

    if (auto ignore = tbl["ignore"].as_table()) {
      // Read merge flags
      if (auto flag = (*ignore)["merge_processes_with_defaults"].as_boolean()) {
        mergeProcesses = flag->get();
      }
      if (auto flag = (*ignore)["merge_window_titles_with_defaults"].as_boolean()) {
        mergeWindowTitles = flag->get();
      }
      if (auto flag = (*ignore)["merge_process_title_pairs_with_defaults"].as_boolean()) {
        mergeProcessTitlePairs = flag->get();
      }
      if (auto flag = (*ignore)["merge_ignore_children_of_processes_with_defaults"].as_boolean()) {
        mergeIgnoreChildrenOfProcesses = flag->get();
      }

      if (auto processes = (*ignore)["processes"].as_array()) {
        for (const auto& p : *processes) {
          if (auto str = p.as_string()) {
            userProcesses.push_back(str->get());
          }
        }
      }

      if (auto titles = (*ignore)["window_titles"].as_array()) {
        for (const auto& t : *titles) {
          if (auto str = t.as_string()) {
            userWindowTitles.push_back(str->get());
          }
        }
      }

      if (auto pairs = (*ignore)["process_title_pairs"].as_array()) {
        for (const auto& pair : *pairs) {
          if (auto tbl = pair.as_table()) {
            auto process = (*tbl)["process"].as_string();
            auto title = (*tbl)["title"].as_string();
            if (process && title) {
              userProcessTitlePairs.emplace_back(process->get(), title->get());
            }
          }
        }
      }

      if (auto children = (*ignore)["ignore_children_of_processes"].as_array()) {
        for (const auto& p : *children) {
          if (auto str = p.as_string()) {
            userIgnoreChildrenOfProcesses.push_back(str->get());
          }
        }
      }

      if (auto barrier = (*ignore)["small_window_barrier"].as_table()) {
        auto width = (*barrier)["width"].as_integer();
        auto height = (*barrier)["height"].as_integer();
        if (width && height) {
          int w = static_cast<int>(width->get());
          int h = static_cast<int>(height->get());
          if (w < 0 || h < 0) {
            spdlog::error(
                "Invalid small_window_barrier: dimensions must be non-negative. Using default.");
            options.ignoreOptions.small_window_barrier = SmallWindowBarrier{
                kDefaultSmallWindowBarrierWidth, kDefaultSmallWindowBarrierHeight};
          } else {
            options.ignoreOptions.small_window_barrier = SmallWindowBarrier{w, h};
          }
        }
      }
    }

    // Store merge flags in options
    options.ignoreOptions.merge_processes = mergeProcesses;
    options.ignoreOptions.merge_window_titles = mergeWindowTitles;
    options.ignoreOptions.merge_process_title_pairs = mergeProcessTitlePairs;
    options.ignoreOptions.merge_ignore_children_of_processes = mergeIgnoreChildrenOfProcesses;

    // Apply merge logic for processes
    if (mergeProcesses) {
      options.ignoreOptions.ignored_processes = defaultIgnore.ignored_processes;
      for (const auto& proc : userProcesses) {
        if (std::find(options.ignoreOptions.ignored_processes.begin(),
                      options.ignoreOptions.ignored_processes.end(),
                      proc) == options.ignoreOptions.ignored_processes.end()) {
          options.ignoreOptions.ignored_processes.push_back(proc);
        }
      }
    } else {
      options.ignoreOptions.ignored_processes = std::move(userProcesses);
    }

    // Apply merge logic for window titles
    if (mergeWindowTitles) {
      options.ignoreOptions.ignored_window_titles = defaultIgnore.ignored_window_titles;
      for (const auto& title : userWindowTitles) {
        if (std::find(options.ignoreOptions.ignored_window_titles.begin(),
                      options.ignoreOptions.ignored_window_titles.end(),
                      title) == options.ignoreOptions.ignored_window_titles.end()) {
          options.ignoreOptions.ignored_window_titles.push_back(title);
        }
      }
    } else {
      options.ignoreOptions.ignored_window_titles = std::move(userWindowTitles);
    }

    // Apply merge logic for process/title pairs
    if (mergeProcessTitlePairs) {
      options.ignoreOptions.ignored_process_title_pairs = defaultIgnore.ignored_process_title_pairs;
      for (const auto& pair : userProcessTitlePairs) {
        if (std::find(options.ignoreOptions.ignored_process_title_pairs.begin(),
                      options.ignoreOptions.ignored_process_title_pairs.end(),
                      pair) == options.ignoreOptions.ignored_process_title_pairs.end()) {
          options.ignoreOptions.ignored_process_title_pairs.push_back(pair);
        }
      }
    } else {
      options.ignoreOptions.ignored_process_title_pairs = std::move(userProcessTitlePairs);
    }

    // Apply merge logic for ignore children of processes
    if (mergeIgnoreChildrenOfProcesses) {
      options.ignoreOptions.ignore_children_of_processes =
          defaultIgnore.ignore_children_of_processes;
      for (const auto& proc : userIgnoreChildrenOfProcesses) {
        if (std::find(options.ignoreOptions.ignore_children_of_processes.begin(),
                      options.ignoreOptions.ignore_children_of_processes.end(),
                      proc) == options.ignoreOptions.ignore_children_of_processes.end()) {
          options.ignoreOptions.ignore_children_of_processes.push_back(proc);
        }
      }
    } else {
      options.ignoreOptions.ignore_children_of_processes = std::move(userIgnoreChildrenOfProcesses);
    }

    // Parse keyboard section
    if (auto keyboard = tbl["keyboard"].as_table()) {
      if (auto mode = (*keyboard)["movement_mode"].as_string()) {
        if (mode->get() == "insert") {
          options.keyboardOptions.movement_mode = MovementMode::Insert;
        } else if (mode->get() != "swap") {
          spdlog::error("Invalid keyboard.movement_mode: expected swap or insert. Using swap.");
        }
      } else if ((*keyboard)["movement_mode"]) {
        spdlog::error("Invalid keyboard.movement_mode: expected a string. Using swap.");
      }
      if (auto bindings = (*keyboard)["bindings"].as_array()) {
        for (const auto& b : *bindings) {
          if (auto binding_tbl = b.as_table()) {
            auto action_str = (*binding_tbl)["action"].as_string();
            auto hotkey = (*binding_tbl)["hotkey"].as_string();
            if (action_str && hotkey) {
              auto action = string_to_hotkey_action(action_str->get());
              if (action) {
                options.keyboardOptions.bindings.push_back({*action, hotkey->get()});
              }
            }
          }
        }
      }
    }

    // Merge with default bindings - add defaults for any missing actions
    auto defaultOptions = get_default_global_options();
    for (const auto& defaultBinding : defaultOptions.keyboardOptions.bindings) {
      bool found = false;
      for (const auto& binding : options.keyboardOptions.bindings) {
        if (binding.action == defaultBinding.action) {
          found = true;
          break;
        }
      }
      if (!found) {
        options.keyboardOptions.bindings.push_back(defaultBinding);
      }
    }

    // Parse gap section
    if (auto gap = tbl["gap"].as_table()) {
      if (auto horizontal = get_number<float>((*gap)["horizontal"])) {
        options.gapOptions.horizontal = *horizontal;
      }
      if (auto vertical = get_number<float>((*gap)["vertical"])) {
        options.gapOptions.vertical = *vertical;
      }
    }

    // Validate gap values - negative values not allowed
    if (options.gapOptions.horizontal < 0) {
      spdlog::error("Invalid gap.horizontal value ({}): must be non-negative. Using default.",
                    options.gapOptions.horizontal);
      options.gapOptions.horizontal = kDefaultGapHorizontal;
    }

    if (options.gapOptions.vertical < 0) {
      spdlog::error("Invalid gap.vertical value ({}): must be non-negative. Using default.",
                    options.gapOptions.vertical);
      options.gapOptions.vertical = kDefaultGapVertical;
    }

    // Parse loop section
    if (auto loop = tbl["loop"].as_table()) {
      if (auto intervalMs = (*loop)["interval_ms"].as_integer()) {
        options.loopOptions.intervalMs = static_cast<int>(intervalMs->get());
      }
      if (auto configRefreshIntervalMs = (*loop)["config_refresh_interval_ms"].as_integer()) {
        options.loopOptions.configRefreshIntervalMs =
            static_cast<int>(configRefreshIntervalMs->get());
      }
      if (auto toggleZenOnWindowMaximize = (*loop)["toggle_zen_on_window_maximize"].as_boolean()) {
        options.loopOptions.toggle_zen_on_window_maximize = toggleZenOnWindowMaximize->get();
      }
      if (auto mouseDragDrop = (*loop)["mouse_drag_drop"].as_string()) {
        auto action = string_to_mouse_drag_drop_action(std::string(mouseDragDrop->get()));
        if (action.has_value()) {
          options.loopOptions.mouse_drag_drop = *action;
        } else {
          spdlog::error(
              "Invalid loop.mouse_drag_drop value ({}): must be \"exchange\" or \"split\". "
              "Using default.",
              mouseDragDrop->get());
        }
      }
    }

    // Validate loop interval - negative values not allowed
    if (options.loopOptions.intervalMs < 0) {
      spdlog::error("Invalid loop.interval_ms value ({}): must be non-negative. Using default.",
                    options.loopOptions.intervalMs);
      options.loopOptions.intervalMs = kDefaultLoopIntervalMs;
    }

    if (options.loopOptions.configRefreshIntervalMs < 0) {
      spdlog::error("Invalid loop.config_refresh_interval_ms value ({}): must be non-negative. "
                    "Using default.",
                    options.loopOptions.configRefreshIntervalMs);
      options.loopOptions.configRefreshIntervalMs = kDefaultConfigRefreshIntervalMs;
    }

    // Parse layout section
    if (auto layout = tbl["layout"].as_table()) {
      options.layoutOptions = parse_layout_options(*layout);
    }

    // Parse visualization section with nested render
    if (auto visualization = tbl["visualization"].as_table()) {
      auto parseColor = [](const toml::array* arr) -> std::optional<overlay::Color> {
        if (!arr || arr->size() != 4) {
          return std::nullopt;
        }
        auto r = (*arr)[0].as_integer();
        auto g = (*arr)[1].as_integer();
        auto b = (*arr)[2].as_integer();
        auto a = (*arr)[3].as_integer();
        if (!r || !g || !b || !a) {
          return std::nullopt;
        }
        auto rv = r->get(), gv = g->get(), bv = b->get(), av = a->get();
        if (rv < 0 || rv > 255 || gv < 0 || gv > 255 || bv < 0 || bv > 255 || av < 0 || av > 255) {
          return std::nullopt;
        }
        return overlay::Color{static_cast<uint8_t>(rv), static_cast<uint8_t>(gv),
                              static_cast<uint8_t>(bv), static_cast<uint8_t>(av)};
      };

      // Parse nested render section
      if (auto render = (*visualization)["render"].as_table()) {
        auto& ro = options.visualizationOptions.renderOptions;
        if (auto show_rectangles = (*render)["show_rectangles"].as_boolean()) {
          ro.show_rectangles = show_rectangles->get();
        }
        if (auto active_only = (*render)["show_only_active_window"].as_boolean()) {
          ro.show_only_active_window = active_only->get();
        }
        if (auto color = parseColor((*render)["normal_color"].as_array())) {
          ro.normal_color = *color;
        } else if ((*render)["normal_color"]) {
          spdlog::error("Invalid normal_color: values must be 0-255. Using default.");
        }
        if (auto color = parseColor((*render)["selected_color"].as_array())) {
          ro.selected_color = *color;
        } else if ((*render)["selected_color"]) {
          spdlog::error("Invalid selected_color: values must be 0-255. Using default.");
        }

        if (auto borderWidth = get_number<float>((*render)["border_width"])) {
          ro.border_width = *borderWidth;
        }
        if (auto toastFontSize = get_number<float>((*render)["toast_font_size"])) {
          ro.toast_font_size = *toastFontSize;
        }
        if (auto zenPercentage = get_number<float>((*render)["zen_percentage"])) {
          ro.zen_percentage = *zenPercentage;
        }
        if (auto processes = (*render)["hide_rectangles_when_processes_open"].as_array()) {
          for (const auto& process : *processes) {
            if (auto str = process.as_string()) {
              ro.hide_rectangles_when_processes_open.push_back(str->get());
            }
          }
        }
      }

      // Parse toast_duration_ms from visualization level
      if (auto toastDurationMs = (*visualization)["toast_duration_ms"].as_integer()) {
        options.visualizationOptions.toastDurationMs = static_cast<int>(toastDurationMs->get());
      }
    }

    // Validate toast duration - negative values not allowed
    if (options.visualizationOptions.toastDurationMs < 0) {
      spdlog::error("Invalid toast_duration_ms value ({}): must be non-negative. Using default.",
                    options.visualizationOptions.toastDurationMs);
      options.visualizationOptions.toastDurationMs = kDefaultToastDurationMs;
    }

    auto& ro = options.visualizationOptions.renderOptions;

    // Validate border width - negative values not allowed
    if (ro.border_width < 0) {
      spdlog::error("Invalid border_width value ({}): must be non-negative. Using default.",
                    ro.border_width);
      ro.border_width = kDefaultBorderWidth;
    }

    // Validate toast font size - must be positive
    if (ro.toast_font_size < 1.0f) {
      spdlog::error("Invalid toast_font_size value ({}): must be >= 1.0. Using default.",
                    ro.toast_font_size);
      ro.toast_font_size = kDefaultToastFontSize;
    }

    ro.zen_percentage = clamp_zen_percentage(ro.zen_percentage);

    return options;
  } catch (const toml::parse_error& e) {
    return tl::unexpected(std::string("TOML parse error: ") + e.what());
  } catch (const std::exception& e) {
    return tl::unexpected(std::string("Error reading TOML: ") + e.what());
  }
}

GlobalOptionsProvider::GlobalOptionsProvider(std::optional<std::filesystem::path> path)
    : configPath(std::move(path)),
      options(get_default_global_options()), lastModified{}, nextConfigRefreshCheck{} {
  if (configPath.has_value()) {
    std::error_code error;
    auto currentModified = std::filesystem::last_write_time(*configPath, error);
    if (error) {
      return;
    }

    auto result = read_options_toml(*configPath);
    if (result.has_value()) {
      options = result.value();
      lastModified = currentModified;
      nextConfigRefreshCheck = next_config_refresh_check(std::chrono::steady_clock::now(), options);
    } else {
      spdlog::error("Failed to load config: {}", result.error());
    }
  }
}

tl::expected<void, std::string>
GlobalOptionsProvider::reload_from(const std::filesystem::path& path) {
  auto result = read_options_toml(path);
  if (!result)
    return tl::unexpected(result.error());
  options = std::move(*result);
  configPath = path;
  std::error_code error;
  const auto modified = std::filesystem::last_write_time(path, error);
  if (!error)
    lastModified = modified;
  nextConfigRefreshCheck = std::chrono::steady_clock::time_point::min();
  return {};
}

bool GlobalOptionsProvider::refresh() {
  if (!configPath.has_value()) {
    return false; // No file to monitor
  }

  auto now = std::chrono::steady_clock::now();
  if (now < nextConfigRefreshCheck) {
    return false; // Refresh check is throttled
  }
  nextConfigRefreshCheck = next_config_refresh_check(now, options);

  std::error_code error;
  auto currentModified = std::filesystem::last_write_time(*configPath, error);
  if (error) {
    return false; // File missing or unavailable
  }

  if (currentModified == lastModified) {
    return false; // No change
  }

  auto result = read_options_toml(*configPath);
  if (result.has_value()) {
    options = result.value();
    lastModified = currentModified;
    nextConfigRefreshCheck = next_config_refresh_check(now, options);
    spdlog::info("Config reloaded from: {}", configPath->string());
    return true;
  }
  spdlog::error("Failed to reload config: {}", result.error());
  return false;
}

} // namespace wintiler
