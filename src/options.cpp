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
  case HotkeyAction::StoreCell:
    return "StoreCell";
  case HotkeyAction::ClearStored:
    return "ClearStored";
  case HotkeyAction::Exchange:
    return "Exchange";
  case HotkeyAction::Move:
    return "Move";
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
  if (str == "StoreCell")
    return HotkeyAction::StoreCell;
  if (str == "ClearStored")
    return HotkeyAction::ClearStored;
  if (str == "Exchange")
    return HotkeyAction::Exchange;
  if (str == "Move")
    return HotkeyAction::Move;
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
  return std::nullopt;
}

std::string layout_split_dir_to_string(LayoutSplitDir split_dir) {
  switch (split_dir) {
  case LayoutSplitDir::Vertical:
    return "vertical";
  case LayoutSplitDir::Horizontal:
    return "horizontal";
  }
  return "vertical";
}

std::optional<LayoutSplitDir> string_to_layout_split_dir(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (value == "vertical") {
    return LayoutSplitDir::Vertical;
  }
  if (value == "horizontal") {
    return LayoutSplitDir::Horizontal;
  }
  return std::nullopt;
}

std::string layout_split_mode_to_string(LayoutSplitMode split_mode) {
  switch (split_mode) {
  case LayoutSplitMode::Zigzag:
    return "zigzag";
  case LayoutSplitMode::Dwindle:
    return "dwindle";
  case LayoutSplitMode::Vertical:
    return "vertical";
  case LayoutSplitMode::Horizontal:
    return "horizontal";
  }
  return "zigzag";
}

std::optional<LayoutSplitMode> string_to_layout_split_mode(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  if (value == "zigzag") {
    return LayoutSplitMode::Zigzag;
  }
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
  case HotkeyAction::StoreCell:
    return "super+shift+[";
  case HotkeyAction::ClearStored:
    return "super+shift+]";
  case HotkeyAction::Exchange:
    return "super+shift+,";
  case HotkeyAction::Move:
    return "super+shift+.";
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

float clamp_layout_ratio(float ratio) {
  if (ratio < 0.1f) {
    spdlog::error("Invalid layout split ratio ({}): must be >= 0.1. Using 0.1.", ratio);
    return 0.1f;
  }
  if (ratio > 0.9f) {
    spdlog::error("Invalid layout split ratio ({}): must be <= 0.9. Using 0.9.", ratio);
    return 0.9f;
  }
  return ratio;
}

std::optional<std::shared_ptr<LayoutTreeNode>>
parse_layout_child(const toml::node_view<toml::node>& node, std::string_view child_name);

std::optional<LayoutTreeNode> parse_layout_tree_node(toml::table& table) {
  auto split = table["split"].as_string();
  if (!split) {
    spdlog::error("Invalid layout rule: split must be \"vertical\" or \"horizontal\".");
    return std::nullopt;
  }

  auto split_dir = string_to_layout_split_dir(std::string(split->get()));
  if (!split_dir.has_value()) {
    spdlog::error("Invalid layout rule: unknown split value \"{}\".", split->get());
    return std::nullopt;
  }

  LayoutTreeNode node;
  node.split_dir = *split_dir;
  if (auto ratio = get_number<float>(table["ratio"])) {
    node.split_ratio = clamp_layout_ratio(*ratio);
  }

  auto first = parse_layout_child(table["first"], "first");
  if (!first.has_value()) {
    return std::nullopt;
  }
  node.first = *first;

  auto second = parse_layout_child(table["second"], "second");
  if (!second.has_value()) {
    return std::nullopt;
  }
  node.second = *second;

  return node;
}

std::optional<std::shared_ptr<LayoutTreeNode>>
parse_layout_child(const toml::node_view<toml::node>& node, std::string_view child_name) {
  if (!node) {
    return std::optional<std::shared_ptr<LayoutTreeNode>>(std::shared_ptr<LayoutTreeNode>{});
  }

  if (auto value = node.as_string()) {
    std::string text = value->get();
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (text == "window") {
      return std::optional<std::shared_ptr<LayoutTreeNode>>(std::shared_ptr<LayoutTreeNode>{});
    }

    spdlog::error("Invalid layout rule: {} must be \"window\" or a split table.", child_name);
    return std::nullopt;
  }

  if (auto table = node.as_table()) {
    auto parsed_node = parse_layout_tree_node(*table);
    if (!parsed_node.has_value()) {
      return std::nullopt;
    }
    return std::make_shared<LayoutTreeNode>(std::move(*parsed_node));
  }

  spdlog::error("Invalid layout rule: {} must be \"window\" or a split table.", child_name);
  return std::nullopt;
}

std::optional<LayoutRule> parse_layout_rule(toml::table& table) {
  auto window_count = table["window_count"].as_integer();
  if (!window_count || window_count->get() <= 0) {
    spdlog::error("Invalid layout rule: window_count must be positive.");
    return std::nullopt;
  }

  toml::table* tree_table = table["tree"].as_table();
  if (tree_table == nullptr) {
    tree_table = &table;
  }

  auto tree = parse_layout_tree_node(*tree_table);
  if (!tree.has_value()) {
    return std::nullopt;
  }

  LayoutRule rule;
  rule.window_count = static_cast<size_t>(window_count->get());
  rule.tree = std::move(*tree);

  size_t leaf_count = count_layout_windows(rule.tree);
  if (leaf_count != rule.window_count) {
    spdlog::error("Invalid layout rule for window_count {}: tree contains {} window leaves.",
                  rule.window_count, leaf_count);
    return std::nullopt;
  }

  return rule;
}

LayoutOptions parse_layout_options(toml::table& table) {
  LayoutOptions options;

  if (auto enabled = table["enabled"].as_boolean()) {
    options.enabled = enabled->get();
  }

  if (auto split_mode = table["split_mode"].as_string()) {
    auto parsed_split_mode = string_to_layout_split_mode(std::string(split_mode->get()));
    if (parsed_split_mode.has_value()) {
      options.split_mode = *parsed_split_mode;
    } else {
      spdlog::error("Invalid layout.split_mode value ({}): must be \"zigzag\", \"dwindle\", "
                    "\"vertical\", or \"horizontal\". Using default.",
                    split_mode->get());
    }
  }

  if (auto rules = table["rules"].as_array()) {
    for (auto& rule_value : *rules) {
      if (auto rule_table = rule_value.as_table()) {
        auto rule = parse_layout_rule(*rule_table);
        if (rule.has_value()) {
          options.rules.push_back(std::move(*rule));
        }
      } else {
        spdlog::error("Invalid layout rule: each rule must be a table.");
      }
    }
  }

  return options;
}

toml::table layout_tree_node_to_toml(const LayoutTreeNode& node) {
  toml::table result;
  result.insert("split", layout_split_dir_to_string(node.split_dir));
  result.insert("ratio", node.split_ratio);

  if (node.first) {
    result.insert("first", layout_tree_node_to_toml(*node.first));
  }
  if (node.second) {
    result.insert("second", layout_tree_node_to_toml(*node.second));
  }

  return result;
}

toml::table layout_options_to_toml(const LayoutOptions& options) {
  toml::table layout;
  layout.insert("enabled", options.enabled);
  layout.insert("split_mode", layout_split_mode_to_string(options.split_mode));

  toml::array layout_rules;
  for (const auto& rule : options.rules) {
    toml::table rule_table;
    rule_table.insert("window_count", static_cast<int64_t>(rule.window_count));
    rule_table.insert("tree", layout_tree_node_to_toml(rule.tree));
    layout_rules.push_back(rule_table);
  }
  layout.insert("rules", layout_rules);

  return layout;
}

std::optional<GapOverrideOptions> parse_gap_override_options(toml::table& table) {
  GapOverrideOptions options;

  if (auto horizontal = get_number<float>(table["horizontal"])) {
    if (*horizontal < 0.0f) {
      spdlog::error("Invalid monitor profile gap.horizontal value ({}): must be non-negative. "
                    "Using fallback.",
                    *horizontal);
    } else {
      options.horizontal = *horizontal;
    }
  }

  if (auto vertical = get_number<float>(table["vertical"])) {
    if (*vertical < 0.0f) {
      spdlog::error("Invalid monitor profile gap.vertical value ({}): must be non-negative. "
                    "Using fallback.",
                    *vertical);
    } else {
      options.vertical = *vertical;
    }
  }

  if (!options.horizontal.has_value() && !options.vertical.has_value()) {
    return std::nullopt;
  }

  return options;
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

std::optional<MonitorProfileOptions> parse_monitor_profile(toml::table& table) {
  MonitorProfileOptions profile;

  if (auto name = table["name"].as_string()) {
    profile.name = name->get();
  }

  if (auto match = table["match"].as_table()) {
    if (auto device_name = (*match)["device_name"].as_string()) {
      profile.match.device_name = device_name->get();
    }
    if (auto index = (*match)["index"].as_integer()) {
      if (index->get() >= 0) {
        profile.match.index = static_cast<size_t>(index->get());
      } else {
        spdlog::error("Invalid monitor profile match.index ({}): must be non-negative.",
                      index->get());
      }
    }
    if (auto primary = (*match)["primary"].as_boolean()) {
      profile.match.primary = primary->get();
    }
  }

  if (!profile.match.device_name.has_value() && !profile.match.index.has_value() &&
      !profile.match.primary.has_value()) {
    spdlog::error("Invalid monitor profile: match must include device_name, index, or primary.");
    return std::nullopt;
  }

  if (auto gap = table["gap"].as_table()) {
    profile.gapOptions = parse_gap_override_options(*gap);
  }

  if (auto layout = table["layout"].as_table()) {
    profile.layoutOptions = parse_layout_options(*layout);
  }

  if (auto visualization = table["visualization"].as_table()) {
    if (auto render = (*visualization)["render"].as_table()) {
      if (auto zen_percentage = get_number<float>((*render)["zen_percentage"])) {
        profile.zen_percentage = clamp_zen_percentage(*zen_percentage);
      }
    }
  }

  return profile;
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

size_t count_layout_windows(const LayoutTreeNode& node) {
  size_t count = 0;
  count += node.first ? count_layout_windows(*node.first) : 1;
  count += node.second ? count_layout_windows(*node.second) : 1;
  return count;
}

std::optional<LayoutRule> find_layout_rule_for_window_count(const LayoutOptions& layout_options,
                                                            size_t window_count) {
  if (!layout_options.enabled) {
    return std::nullopt;
  }

  for (const auto& rule : layout_options.rules) {
    if (rule.window_count == window_count) {
      return rule;
    }
  }

  return std::nullopt;
}

tl::expected<void, std::string> write_options_toml(const GlobalOptions& options,
                                                   const std::filesystem::path& filepath) {
  try {
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
    render.insert("normal_color", colorToArray(ro.normal_color));
    render.insert("selected_color", colorToArray(ro.selected_color));
    render.insert("stored_color", colorToArray(ro.stored_color));
    render.insert("border_width", ro.border_width);
    render.insert("toast_font_size", ro.toast_font_size);
    render.insert("zen_percentage", ro.zen_percentage);
    visualization.insert("render", render);
    visualization.insert("toast_duration_ms", options.visualizationOptions.toastDurationMs);
    root.insert("visualization", visualization);

    toml::array monitor_profiles;
    for (const auto& profile : options.monitorProfiles) {
      toml::table profile_table;
      if (!profile.name.empty()) {
        profile_table.insert("name", profile.name);
      }

      toml::table match;
      if (profile.match.device_name.has_value()) {
        match.insert("device_name", *profile.match.device_name);
      }
      if (profile.match.index.has_value()) {
        match.insert("index", static_cast<int64_t>(*profile.match.index));
      }
      if (profile.match.primary.has_value()) {
        match.insert("primary", *profile.match.primary);
      }
      profile_table.insert("match", match);

      if (profile.gapOptions.has_value()) {
        toml::table profile_gap;
        if (profile.gapOptions->horizontal.has_value()) {
          profile_gap.insert("horizontal", *profile.gapOptions->horizontal);
        }
        if (profile.gapOptions->vertical.has_value()) {
          profile_gap.insert("vertical", *profile.gapOptions->vertical);
        }
        profile_table.insert("gap", profile_gap);
      }

      if (profile.layoutOptions.has_value()) {
        profile_table.insert("layout", layout_options_to_toml(*profile.layoutOptions));
      }

      if (profile.zen_percentage.has_value()) {
        toml::table profile_visualization;
        toml::table profile_render;
        profile_render.insert("zen_percentage", *profile.zen_percentage);
        profile_visualization.insert("render", profile_render);
        profile_table.insert("visualization", profile_visualization);
      }

      monitor_profiles.push_back(profile_table);
    }
    if (!monitor_profiles.empty()) {
      root.insert("monitor_profiles", monitor_profiles);
    }

    // Write to file
    std::ofstream file(filepath);
    if (!file) {
      return tl::unexpected("Failed to open file for writing: " + filepath.string());
    }
    file << root;
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
        if (auto color = parseColor((*render)["stored_color"].as_array())) {
          ro.stored_color = *color;
        } else if ((*render)["stored_color"]) {
          spdlog::error("Invalid stored_color: values must be 0-255. Using default.");
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

    if (auto monitor_profiles = tbl["monitor_profiles"].as_array()) {
      for (auto& profile_value : *monitor_profiles) {
        if (auto profile_table = profile_value.as_table()) {
          auto profile = parse_monitor_profile(*profile_table);
          if (profile.has_value()) {
            options.monitorProfiles.push_back(std::move(*profile));
          }
        } else {
          spdlog::error("Invalid monitor profile: each profile must be a table.");
        }
      }
    }

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
