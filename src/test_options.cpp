#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ignore_config.h"
#include "options.h"
#include "runtime_support.h"
#include "save_layout.h"

using namespace wintiler;

// Helper to create a temp file path
std::filesystem::path create_temp_file_path() {
  auto temp_dir = std::filesystem::temp_directory_path();
  auto filename = "win-tiler-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  ".toml";
  return temp_dir / filename;
}

// Helper to write a simple valid TOML config
void write_valid_config(const std::filesystem::path& path, float gap_h = 20.0f, float gap_v = 25.0f,
                        int config_refresh_interval_ms = 0) {
  std::ofstream file(path);
  file << std::fixed << std::setprecision(1);
  file << "[gap]\n";
  file << "horizontal = " << gap_h << "\n";
  file << "vertical = " << gap_v << "\n";
  file << "\n[loop]\n";
  file << "config_refresh_interval_ms = " << config_refresh_interval_ms << "\n";
}

LayoutRule make_test_layout_rule(size_t window_count, LayoutSplitDir split_dir, float ratio) {
  LayoutRule rule;
  rule.window_count = window_count;
  rule.tree.split_dir = split_dir;
  rule.tree.split_ratio = ratio;
  if (window_count == 3) {
    auto second = std::make_shared<LayoutTreeNode>();
    second->split_dir = LayoutSplitDir::Horizontal;
    second->split_ratio = 0.50f;
    rule.tree.second = second;
  }
  return rule;
}

winapi::MonitorInfo make_test_monitor(std::string device_name, long left, bool primary = false) {
  winapi::MonitorInfo monitor;
  monitor.deviceName = std::move(device_name);
  monitor.rect = {left, 0, left + 1920, 1080};
  monitor.workArea = monitor.rect;
  monitor.isPrimary = primary;
  return monitor;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// RAII helper to clean up temp files
struct TempFileGuard {
  std::filesystem::path path;
  explicit TempFileGuard(const std::filesystem::path& p) : path(p) {
  }
  ~TempFileGuard() {
    if (std::filesystem::exists(path)) {
      std::filesystem::remove(path);
    }
  }
};

// ============================================================================
// GlobalOptionsProvider Tests
// ============================================================================

TEST_SUITE("GlobalOptionsProvider") {
  TEST_CASE("constructor without config path returns defaults") {
    GlobalOptionsProvider provider;

    CHECK(!provider.configPath.has_value());
    CHECK(provider.options.gapOptions.horizontal == kDefaultGapHorizontal);
    CHECK(provider.options.gapOptions.vertical == kDefaultGapVertical);
  }

  TEST_CASE("constructor with nullopt returns defaults") {
    GlobalOptionsProvider provider(std::nullopt);

    CHECK(!provider.configPath.has_value());
    CHECK(provider.options.gapOptions.horizontal == kDefaultGapHorizontal);
    CHECK(provider.options.gapOptions.vertical == kDefaultGapVertical);
  }

  TEST_CASE("constructor with non-existent file returns defaults") {
    auto temp_path = create_temp_file_path();
    // Don't create the file - it shouldn't exist

    GlobalOptionsProvider provider(temp_path);

    CHECK(provider.configPath.has_value());
    CHECK(provider.options.gapOptions.horizontal == kDefaultGapHorizontal);
    CHECK(provider.options.gapOptions.vertical == kDefaultGapVertical);
  }

  TEST_CASE("constructor with valid file loads options") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    write_valid_config(temp_path, 30.0f, 35.0f, 750);

    GlobalOptionsProvider provider(temp_path);

    CHECK(provider.configPath.has_value());
    CHECK(provider.options.gapOptions.horizontal == 30.0f);
    CHECK(provider.options.gapOptions.vertical == 35.0f);
    CHECK(provider.options.loopOptions.configRefreshIntervalMs == 750);
  }

  TEST_CASE("refresh returns false when no config path") {
    GlobalOptionsProvider provider;

    CHECK(provider.refresh() == false);
  }

  TEST_CASE("refresh returns false when file unchanged") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    write_valid_config(temp_path, 20.0f, 25.0f);

    GlobalOptionsProvider provider(temp_path);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    // refresh without changing file
    CHECK(provider.refresh() == false);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
  }

  TEST_CASE("refresh returns true and updates options when file changed") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    write_valid_config(temp_path, 20.0f, 25.0f);

    GlobalOptionsProvider provider(temp_path);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
    CHECK(provider.options.gapOptions.vertical == 25.0f);

    // Wait a bit to ensure file modification time changes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Modify the file
    write_valid_config(temp_path, 40.0f, 45.0f);

    CHECK(provider.refresh() == true);
    CHECK(provider.options.gapOptions.horizontal == 40.0f);
    CHECK(provider.options.gapOptions.vertical == 45.0f);
  }

  TEST_CASE("refresh is throttled by configured interval") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    write_valid_config(temp_path, 20.0f, 25.0f, 1000);

    GlobalOptionsProvider provider(temp_path);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    write_valid_config(temp_path, 40.0f, 45.0f, 1000);
    provider.lastModified = std::filesystem::file_time_type{};

    CHECK(provider.refresh() == false);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    provider.nextConfigRefreshCheck =
        std::chrono::steady_clock::now() - std::chrono::milliseconds(1);

    CHECK(provider.refresh() == true);
    CHECK(provider.options.gapOptions.horizontal == 40.0f);
    CHECK(provider.options.gapOptions.vertical == 45.0f);
  }

  TEST_CASE("refresh returns false and keeps options when file becomes invalid") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    write_valid_config(temp_path, 20.0f, 25.0f);

    GlobalOptionsProvider provider(temp_path);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    // Wait a bit to ensure file modification time changes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Write invalid TOML
    {
      std::ofstream file(temp_path);
      file << "this is not valid toml {{{\n";
    }

    CHECK(provider.refresh() == false);
    // Options should remain unchanged
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
    CHECK(provider.options.gapOptions.vertical == 25.0f);
  }

  TEST_CASE("refresh returns false when file is deleted") {
    auto temp_path = create_temp_file_path();

    write_valid_config(temp_path, 20.0f, 25.0f);

    GlobalOptionsProvider provider(temp_path);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    // Delete the file
    std::filesystem::remove(temp_path);

    CHECK(provider.refresh() == false);
    // Options should remain unchanged
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
  }

  TEST_CASE("refresh detects file creation after provider construction") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    // Create provider before file exists
    GlobalOptionsProvider provider(temp_path);
    CHECK(provider.options.gapOptions.horizontal == kDefaultGapHorizontal);

    // Now create the file
    write_valid_config(temp_path, 50.0f, 55.0f);

    CHECK(provider.refresh() == true);
    CHECK(provider.options.gapOptions.horizontal == 50.0f);
    CHECK(provider.options.gapOptions.vertical == 55.0f);
  }

  TEST_CASE("partial keyboard config falls back to defaults for missing bindings") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    // Write a config with only one keyboard binding
    {
      std::ofstream file(temp_path);
      file << "[keyboard]\n";
      file << "bindings = [\n";
      file << "  { action = \"NavigateLeft\", hotkey = \"alt+h\" }\n";
      file << "]\n";
    }

    GlobalOptionsProvider provider(temp_path);
    auto& bindings = provider.options.keyboardOptions.bindings;

    // Should have all default bindings
    auto default_options = get_default_global_options();
    CHECK(bindings.size() == default_options.keyboardOptions.bindings.size());

    // The overridden binding should use the custom hotkey
    auto find_binding = [&](HotkeyAction action) -> std::string {
      for (const auto& b : bindings) {
        if (b.action == action)
          return b.hotkey;
      }
      return "";
    };

    CHECK(find_binding(HotkeyAction::NavigateLeft) == "alt+h");

    // Other bindings should use defaults
    CHECK(find_binding(HotkeyAction::NavigateRight) == "super+shift+l");
    CHECK(find_binding(HotkeyAction::Exit) == "super+shift+escape");
    CHECK(find_binding(HotkeyAction::ToggleSplit) == "super+shift+y");
    CHECK(find_binding(HotkeyAction::DumpWindowManagement) == "super+shift+d");
    CHECK(find_binding(HotkeyAction::RestartSystem) == "super+shift+r");
    CHECK(find_binding(HotkeyAction::ToggleFloating) == "super+shift+f");
    CHECK(find_binding(HotkeyAction::ToggleVerboseLogging) == "super+shift+v");
  }

  TEST_CASE("empty keyboard section uses all default bindings") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    // Write a config with no keyboard section
    {
      std::ofstream file(temp_path);
      file << "[gap]\n";
      file << "horizontal = 15.0\n";
    }

    GlobalOptionsProvider provider(temp_path);
    auto& bindings = provider.options.keyboardOptions.bindings;

    // Should have all default bindings
    auto default_options = get_default_global_options();
    CHECK(bindings.size() == default_options.keyboardOptions.bindings.size());

    // Verify a few default bindings are present
    auto find_binding = [&](HotkeyAction action) -> std::string {
      for (const auto& b : bindings) {
        if (b.action == action)
          return b.hotkey;
      }
      return "";
    };

    CHECK(find_binding(HotkeyAction::NavigateLeft) == "super+shift+h");
    CHECK(find_binding(HotkeyAction::NavigateDown) == "super+shift+j");
    CHECK(find_binding(HotkeyAction::Exit) == "super+shift+escape");
    CHECK(find_binding(HotkeyAction::DumpWindowManagement) == "super+shift+d");
    CHECK(find_binding(HotkeyAction::RestartSystem) == "super+shift+r");
    CHECK(find_binding(HotkeyAction::ToggleFloating) == "super+shift+f");
    CHECK(find_binding(HotkeyAction::ToggleVerboseLogging) == "super+shift+v");
  }

  TEST_CASE("restart system keyboard binding can be configured") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[keyboard]\n";
      file << "bindings = [\n";
      file << "  { action = \"RestartSystem\", hotkey = \"super+alt+r\" }\n";
      file << "]\n";
    }

    GlobalOptionsProvider provider(temp_path);
    auto hotkey =
        find_hotkey_binding(provider.options.keyboardOptions, HotkeyAction::RestartSystem);

    REQUIRE(hotkey.has_value());
    CHECK(*hotkey == "super+alt+r");
  }

  TEST_CASE("verbose logging keyboard binding can be configured") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[keyboard]\n";
      file << "bindings = [\n";
      file << "  { action = \"ToggleVerboseLogging\", hotkey = \"super+alt+v\" }\n";
      file << "]\n";
    }

    GlobalOptionsProvider provider(temp_path);
    auto hotkey =
        find_hotkey_binding(provider.options.keyboardOptions, HotkeyAction::ToggleVerboseLogging);

    REQUIRE(hotkey.has_value());
    CHECK(*hotkey == "super+alt+v");
  }
}

TEST_SUITE("KeyboardOptions helpers") {
  TEST_CASE("find_hotkey_binding returns configured hotkey text for an action") {
    KeyboardOptions keyboard_options;
    keyboard_options.bindings = {
        {HotkeyAction::NavigateLeft, "super+h"},
        {HotkeyAction::Exit, "alt+f4"},
    };

    auto hotkey = find_hotkey_binding(keyboard_options, HotkeyAction::Exit);

    REQUIRE(hotkey.has_value());
    CHECK(*hotkey == "alt+f4");
  }

  TEST_CASE("find_hotkey_binding returns nullopt when an action is not configured") {
    KeyboardOptions keyboard_options;
    keyboard_options.bindings = {
        {HotkeyAction::NavigateLeft, "super+h"},
    };

    auto hotkey = find_hotkey_binding(keyboard_options, HotkeyAction::Exit);

    CHECK(!hotkey.has_value());
  }
}

TEST_SUITE("Generated TOML") {
  TEST_CASE("written config includes comments and examples") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    auto write_result = write_options_toml(get_default_global_options(), temp_path);
    REQUIRE(write_result.has_value());

    auto text = read_text_file(temp_path);
    auto generated_toml_start = text.find("\n[");
    REQUIRE(generated_toml_start != std::string::npos);
    auto documentation = text.substr(0, generated_toml_start);

    CHECK(text.find("# win-tiler configuration") != std::string::npos);
    CHECK(text.find("# Hotkey actions and what they do:") != std::string::npos);
    CHECK(text.find("# Layout rule example:") != std::string::npos);
    CHECK(text.find("# Monitor profile example:") != std::string::npos);
    CHECK(text.find("# Ignore options:") != std::string::npos);

    const std::vector<std::string> documented_keys = {
        "merge_processes_with_defaults",
        "merge_window_titles_with_defaults",
        "merge_process_title_pairs_with_defaults",
        "merge_ignore_children_of_processes_with_defaults",
        "processes",
        "window_titles",
        "process_title_pairs",
        "ignore_children_of_processes",
        "small_window_barrier",
        "width",
        "height",
        "bindings",
        "action",
        "hotkey",
        "horizontal",
        "vertical",
        "interval_ms",
        "config_refresh_interval_ms",
        "toggle_zen_on_window_maximize",
        "mouse_drag_drop",
        "enabled",
        "split_mode",
        "split_width_multiplier",
        "rules",
        "window_count",
        "split",
        "ratio",
        "first",
        "second",
        "toast_duration_ms",
        "normal_color",
        "selected_color",
        "stored_color",
        "border_width",
        "toast_font_size",
        "zen_percentage",
        "hide_rectangles_when_processes_open",
        "name",
        "primary",
        "index",
        "device_name",
    };
    for (const auto& key : documented_keys) {
      CHECK(documentation.find(key) != std::string::npos);
    }

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());
    CHECK(read_result.value().gapOptions.horizontal == kDefaultGapHorizontal);
  }

  TEST_CASE("render process suppression list can be configured") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[visualization.render]\n";
      file << "hide_rectangles_when_processes_open = [\"ShareApp.exe\", \"overlay.exe\"]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    const auto& processes =
        result.value().visualizationOptions.renderOptions.hide_rectangles_when_processes_open;
    CHECK(processes == std::vector<std::string>{"ShareApp.exe", "overlay.exe"});
  }

  TEST_CASE("render process suppression list is written to TOML") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    GlobalOptions options = get_default_global_options();
    options.visualizationOptions.renderOptions.hide_rectangles_when_processes_open = {
        "ShareApp.exe", "overlay.exe"};

    auto write_result = write_options_toml(options, temp_path);
    REQUIRE(write_result.has_value());

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());

    const auto& processes =
        read_result.value().visualizationOptions.renderOptions.hide_rectangles_when_processes_open;
    CHECK(processes == std::vector<std::string>{"ShareApp.exe", "overlay.exe"});
  }
}

// ============================================================================
// IgnoreOptions Merge Tests
// ============================================================================

TEST_SUITE("IgnoreOptions Merge") {
  TEST_CASE("merge flags default to true when not specified in config") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "processes = [\"CustomApp.exe\"]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());
    CHECK(result.value().ignoreOptions.merge_processes == true);
    CHECK(result.value().ignoreOptions.merge_window_titles == true);
    CHECK(result.value().ignoreOptions.merge_process_title_pairs == true);
  }

  TEST_CASE("merge_processes = true merges user values with defaults") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_processes_with_defaults = true\n";
      file << "processes = [\"CustomApp.exe\", \"AnotherApp.exe\"]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto& processes = result.value().ignoreOptions.ignored_processes;
    auto defaults = get_default_ignore_options();

    // Should contain all defaults plus user additions
    CHECK(processes.size() == defaults.ignored_processes.size() + 2);

    // Check defaults are present
    for (const auto& def : defaults.ignored_processes) {
      CHECK(std::find(processes.begin(), processes.end(), def) != processes.end());
    }

    // Check user additions are present
    CHECK(std::find(processes.begin(), processes.end(), "CustomApp.exe") != processes.end());
    CHECK(std::find(processes.begin(), processes.end(), "AnotherApp.exe") != processes.end());
  }

  TEST_CASE("merge_processes = false uses only user values") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_processes_with_defaults = false\n";
      file << "processes = [\"OnlyThis.exe\"]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto& processes = result.value().ignoreOptions.ignored_processes;

    // Should only contain user value
    CHECK(processes.size() == 1);
    CHECK(processes[0] == "OnlyThis.exe");
  }

  TEST_CASE("merge_window_titles = true merges user values with defaults") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_window_titles_with_defaults = true\n";
      file << "window_titles = [\"My Popup\", \"Another Window\"]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto& titles = result.value().ignoreOptions.ignored_window_titles;
    auto defaults = get_default_ignore_options();

    // Default window_titles is empty, so should just have user values
    CHECK(titles.size() == defaults.ignored_window_titles.size() + 2);
    CHECK(std::find(titles.begin(), titles.end(), "My Popup") != titles.end());
    CHECK(std::find(titles.begin(), titles.end(), "Another Window") != titles.end());
  }

  TEST_CASE("merge_window_titles = false uses only user values") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_window_titles_with_defaults = false\n";
      file << "window_titles = [\"Only This Title\"]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto& titles = result.value().ignoreOptions.ignored_window_titles;
    CHECK(titles.size() == 1);
    CHECK(titles[0] == "Only This Title");
  }

  TEST_CASE("merge_process_title_pairs = true merges user values with defaults") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_process_title_pairs_with_defaults = true\n";
      file << "process_title_pairs = [\n";
      file << "  { process = \"myapp.exe\", title = \"My Window\" }\n";
      file << "]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto& pairs = result.value().ignoreOptions.ignored_process_title_pairs;
    auto defaults = get_default_ignore_options();

    // Should contain all defaults plus user addition
    CHECK(pairs.size() == defaults.ignored_process_title_pairs.size() + 1);

    // Check user addition is present
    auto userPair = std::make_pair(std::string("myapp.exe"), std::string("My Window"));
    CHECK(std::find(pairs.begin(), pairs.end(), userPair) != pairs.end());
  }

  TEST_CASE("merge_process_title_pairs = false uses only user values") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_process_title_pairs_with_defaults = false\n";
      file << "process_title_pairs = [\n";
      file << "  { process = \"only.exe\", title = \"Only Window\" }\n";
      file << "]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto& pairs = result.value().ignoreOptions.ignored_process_title_pairs;
    CHECK(pairs.size() == 1);
    CHECK(pairs[0].first == "only.exe");
    CHECK(pairs[0].second == "Only Window");
  }

  TEST_CASE("duplicate values are not added when merging") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    auto defaults = get_default_ignore_options();
    REQUIRE(!defaults.ignored_processes.empty());

    // Use a default process name as user value
    std::string duplicateProcess = defaults.ignored_processes[0];

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_processes_with_defaults = true\n";
      file << "processes = [\"" << duplicateProcess << "\", \"NewApp.exe\"]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto& processes = result.value().ignoreOptions.ignored_processes;

    // Should have defaults + 1 new (duplicate should not be added twice)
    CHECK(processes.size() == defaults.ignored_processes.size() + 1);

    // Count occurrences of duplicate
    auto count = std::count(processes.begin(), processes.end(), duplicateProcess);
    CHECK(count == 1);
  }

  TEST_CASE("merge flags are written to TOML") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    GlobalOptions options;
    options.ignoreOptions.merge_processes = false;
    options.ignoreOptions.merge_window_titles = true;
    options.ignoreOptions.merge_process_title_pairs = false;
    options.ignoreOptions.ignored_processes = {"test.exe"};

    auto writeResult = write_options_toml(options, temp_path);
    REQUIRE(writeResult.has_value());

    auto readResult = read_options_toml(temp_path);
    REQUIRE(readResult.has_value());

    // When merge is false, we get only user values back
    CHECK(readResult.value().ignoreOptions.merge_processes == false);
    CHECK(readResult.value().ignoreOptions.merge_window_titles == true);
    CHECK(readResult.value().ignoreOptions.merge_process_title_pairs == false);
  }

  TEST_CASE("independent merge flags work correctly") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_processes_with_defaults = false\n";
      file << "merge_window_titles_with_defaults = true\n";
      file << "merge_process_title_pairs_with_defaults = false\n";
      file << "processes = [\"custom.exe\"]\n";
      file << "window_titles = [\"Custom Title\"]\n";
      file << "process_title_pairs = [\n";
      file << "  { process = \"app.exe\", title = \"Window\" }\n";
      file << "]\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    auto defaults = get_default_ignore_options();

    // processes: merge=false, should have only user value
    CHECK(result.value().ignoreOptions.ignored_processes.size() == 1);
    CHECK(result.value().ignoreOptions.ignored_processes[0] == "custom.exe");

    // window_titles: merge=true, should have defaults + user (defaults is empty)
    CHECK(result.value().ignoreOptions.ignored_window_titles.size() ==
          defaults.ignored_window_titles.size() + 1);

    // process_title_pairs: merge=false, should have only user value
    CHECK(result.value().ignoreOptions.ignored_process_title_pairs.size() == 1);
  }
}

// ============================================================================
// Layout Options Tests
// ============================================================================

TEST_SUITE("Layout Options") {
  TEST_CASE("parses split mode") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[layout]\n";
      file << "split_mode = \"dwindle\"\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().layoutOptions.split_mode == LayoutSplitMode::Dwindle);
    CHECK(to_engine_split_mode(result.value().layoutOptions.split_mode) ==
          ctrl::SplitMode::Dwindle);
  }

  TEST_CASE("invalid split mode uses default") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[layout]\n";
      file << "split_mode = \"spiral\"\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().layoutOptions.split_mode == LayoutSplitMode::Dwindle);
  }

  TEST_CASE("removed zigzag split mode uses default") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[layout]\n";
      file << "split_mode = \"zigzag\"\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().layoutOptions.split_mode == LayoutSplitMode::Dwindle);
  }

  TEST_CASE("parses split width multiplier") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[layout]\n";
      file << "split_width_multiplier = 0.75\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().layoutOptions.split_width_multiplier == doctest::Approx(0.75f));
  }

  TEST_CASE("invalid split width multiplier uses default") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[layout]\n";
      file << "split_width_multiplier = 0\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().layoutOptions.split_width_multiplier ==
          doctest::Approx(kDefaultSplitWidthMultiplier));
  }

  TEST_CASE("parses two-window layout with implicit window leaves") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[layout]\n";
      file << "enabled = true\n";
      file << "\n";
      file << "[[layout.rules]]\n";
      file << "window_count = 2\n";
      file << "split = \"vertical\"\n";
      file << "ratio = 0.30\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    const auto& layout = result.value().layoutOptions;
    CHECK(layout.enabled == true);
    REQUIRE(layout.rules.size() == 1);
    CHECK(layout.rules[0].window_count == 2);
    CHECK(layout.rules[0].tree.split_dir == LayoutSplitDir::Vertical);
    CHECK(layout.rules[0].tree.split_ratio == doctest::Approx(0.30f));
    CHECK(layout.rules[0].tree.first == nullptr);
    CHECK(layout.rules[0].tree.second == nullptr);
    CHECK(count_layout_windows(layout.rules[0].tree) == 2);
  }

  TEST_CASE("parses nested layout with omitted leaf children") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[[layout.rules]]\n";
      file << "window_count = 3\n";
      file << "\n";
      file << "[layout.rules.tree]\n";
      file << "split = \"vertical\"\n";
      file << "ratio = 0.30\n";
      file << "\n";
      file << "[layout.rules.tree.second]\n";
      file << "split = \"horizontal\"\n";
      file << "ratio = 0.50\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    const auto& layout = result.value().layoutOptions;
    REQUIRE(layout.rules.size() == 1);
    const auto& rule = layout.rules[0];
    CHECK(rule.window_count == 3);
    CHECK(rule.tree.first == nullptr);
    REQUIRE(rule.tree.second != nullptr);
    CHECK(rule.tree.second->split_dir == LayoutSplitDir::Horizontal);
    CHECK(count_layout_windows(rule.tree) == 3);
  }

  TEST_CASE("ignores layout rule when window_count does not match leaf count") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[[layout.rules]]\n";
      file << "window_count = 3\n";
      file << "split = \"vertical\"\n";
      file << "ratio = 0.30\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().layoutOptions.rules.empty());
  }

  TEST_CASE("disabled layout does not return matching rules") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[layout]\n";
      file << "enabled = false\n";
      file << "\n";
      file << "[[layout.rules]]\n";
      file << "window_count = 2\n";
      file << "split = \"vertical\"\n";
      file << "ratio = 0.30\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());
    REQUIRE(result.value().layoutOptions.rules.size() == 1);

    auto rule = find_layout_rule_for_window_count(result.value().layoutOptions, 2);
    CHECK(!rule.has_value());
  }

  TEST_CASE("writes and reads split mode") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    GlobalOptions options = get_default_global_options();
    options.layoutOptions.split_mode = LayoutSplitMode::Dwindle;
    options.layoutOptions.split_width_multiplier = 0.80f;

    auto write_result = write_options_toml(options, temp_path);
    REQUIRE(write_result.has_value());

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());

    CHECK(read_result.value().layoutOptions.split_mode == LayoutSplitMode::Dwindle);
    CHECK(read_result.value().layoutOptions.split_width_multiplier == doctest::Approx(0.80f));
  }
}

TEST_SUITE("Monitor Profile Options") {
  TEST_CASE("parses monitor profile tiling overrides") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[[monitor_profiles]]\n";
      file << "name = \"External\"\n";
      file << "\n";
      file << "[monitor_profiles.match]\n";
      file << "device_name = \"\\\\\\\\.\\\\DISPLAY2\"\n";
      file << "index = 1\n";
      file << "\n";
      file << "[monitor_profiles.gap]\n";
      file << "horizontal = 4\n";
      file << "\n";
      file << "[monitor_profiles.visualization.render]\n";
      file << "zen_percentage = 0.75\n";
      file << "\n";
      file << "[[monitor_profiles.layout.rules]]\n";
      file << "window_count = 2\n";
      file << "split = \"horizontal\"\n";
      file << "ratio = 0.40\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    REQUIRE(result.value().monitorProfiles.size() == 1);
    const auto& profile = result.value().monitorProfiles[0];
    CHECK(profile.name == "External");
    REQUIRE(profile.match.device_name.has_value());
    CHECK(*profile.match.device_name == "\\\\.\\DISPLAY2");
    REQUIRE(profile.match.index.has_value());
    CHECK(*profile.match.index == 1);
    REQUIRE(profile.gapOptions.has_value());
    REQUIRE(profile.gapOptions->horizontal.has_value());
    CHECK(*profile.gapOptions->horizontal == 4.0f);
    CHECK_FALSE(profile.gapOptions->vertical.has_value());
    REQUIRE(profile.zen_percentage.has_value());
    CHECK(*profile.zen_percentage == doctest::Approx(0.75f));
    REQUIRE(profile.layoutOptions.has_value());
    CHECK(profile.layoutOptions->split_width_multiplier ==
          doctest::Approx(kDefaultSplitWidthMultiplier));
    REQUIRE(profile.layoutOptions->rules.size() == 1);
    CHECK(profile.layoutOptions->rules[0].tree.split_dir == LayoutSplitDir::Horizontal);
  }

  TEST_CASE("ignores monitor profile without match criteria") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[[monitor_profiles]]\n";
      file << "name = \"No match\"\n";
      file << "[monitor_profiles.gap]\n";
      file << "horizontal = 4\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());
    CHECK(result.value().monitorProfiles.empty());
  }

  TEST_CASE("resolves monitor profile overrides over global fallback") {
    GlobalOptions options = get_default_global_options();
    options.gapOptions.horizontal = 10.0f;
    options.gapOptions.vertical = 12.0f;
    options.visualizationOptions.renderOptions.zen_percentage = 0.90f;

    MonitorProfileOptions profile;
    profile.match.device_name = "\\\\.\\DISPLAY2";
    GapOverrideOptions gap;
    gap.horizontal = 4.0f;
    profile.gapOptions = gap;
    profile.zen_percentage = 0.75f;
    LayoutRule rule;
    rule.window_count = 2;
    rule.tree.split_dir = LayoutSplitDir::Vertical;
    rule.tree.split_ratio = 0.30f;
    LayoutOptions layout;
    layout.split_width_multiplier = 0.60f;
    layout.rules.push_back(rule);
    profile.layoutOptions = layout;
    options.monitorProfiles.push_back(profile);

    winapi::MonitorInfo monitor;
    monitor.deviceName = "\\\\.\\DISPLAY2";
    monitor.isPrimary = false;

    auto resolved = resolve_monitor_tiling_options(options, monitor, 1);

    CHECK(resolved.gapOptions.horizontal == 4.0f);
    CHECK(resolved.gapOptions.vertical == 12.0f);
    CHECK(resolved.zen_percentage == doctest::Approx(0.75f));
    CHECK(resolved.layoutOptions.split_width_multiplier == doctest::Approx(0.60f));
    REQUIRE(resolved.layoutOptions.rules.size() == 1);
    CHECK(resolved.layoutOptions.rules[0].tree.split_ratio == doctest::Approx(0.30f));
  }

  TEST_CASE("save layout replaces only matching monitor profile rule") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "# keep this comment\n";
      file << "[gap]\n";
      file << "horizontal = 21\n";
      file << "\n";
      file << "[layout]\n";
      file << "enabled = true\n";
      file << "\n";
      file << "[[layout.rules]]\n";
      file << "window_count = 2\n";
      file << "split = \"horizontal\"\n";
      file << "ratio = 0.80\n";
      file << "\n";
      file << "[[monitor_profiles]]\n";
      file << "name = \"Left\"\n";
      file << "match = { device_name = \"\\\\\\\\.\\\\DISPLAY1\" }\n";
      file << "\n";
      file << "[[monitor_profiles.layout.rules]]\n";
      file << "window_count = 2\n";
      file << "split = \"vertical\"\n";
      file << "ratio = 0.70\n";
      file << "\n";
      file << "[[monitor_profiles.layout.rules]]\n";
      file << "window_count = 3\n";
      file << "[monitor_profiles.layout.rules.tree]\n";
      file << "split = \"vertical\"\n";
      file << "ratio = 0.40\n";
      file << "first = \"window\"\n";
      file << "[monitor_profiles.layout.rules.tree.second]\n";
      file << "split = \"horizontal\"\n";
      file << "ratio = 0.50\n";
      file << "first = \"window\"\n";
      file << "second = \"window\"\n";
      file << "\n";
      file << "[[monitor_profiles]]\n";
      file << "name = \"Right\"\n";
      file << "match = { device_name = \"\\\\\\\\.\\\\DISPLAY2\" }\n";
      file << "\n";
      file << "[[monitor_profiles.layout.rules]]\n";
      file << "window_count = 2\n";
      file << "split = \"horizontal\"\n";
      file << "ratio = 0.20\n";
    }

    std::vector<winapi::MonitorInfo> monitors = {
        make_test_monitor("\\\\.\\DISPLAY1", 0, true),
        make_test_monitor("\\\\.\\DISPLAY2", 1920)};
    MonitorLayoutRuleUpdate update{0, monitors[0],
                                   make_test_layout_rule(2, LayoutSplitDir::Horizontal, 0.25f)};

    auto save_result = save_monitor_layout_rules_to_config(temp_path, monitors, {update});
    REQUIRE(save_result.has_value());
    CHECK(save_result->saved_monitor_count == 1);

    std::string text = read_text_file(temp_path);
    CHECK(text.find("# keep this comment") != std::string::npos);
    CHECK(text.find("horizontal = 21") != std::string::npos);

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());
    CHECK(read_result->layoutOptions.rules[0].tree.split_ratio == doctest::Approx(0.80f));
    REQUIRE(read_result->monitorProfiles.size() == 2);

    const auto& left_profile = read_result->monitorProfiles[0];
    REQUIRE(left_profile.layoutOptions.has_value());
    REQUIRE(left_profile.layoutOptions->rules.size() == 2);
    auto left_two =
        find_layout_rule_for_window_count(*left_profile.layoutOptions, static_cast<size_t>(2));
    REQUIRE(left_two.has_value());
    CHECK(left_two->tree.split_dir == LayoutSplitDir::Horizontal);
    CHECK(left_two->tree.split_ratio == doctest::Approx(0.25f));
    auto left_three =
        find_layout_rule_for_window_count(*left_profile.layoutOptions, static_cast<size_t>(3));
    REQUIRE(left_three.has_value());
    CHECK(left_three->tree.split_ratio == doctest::Approx(0.40f));

    const auto& right_profile = read_result->monitorProfiles[1];
    REQUIRE(right_profile.layoutOptions.has_value());
    auto right_two =
        find_layout_rule_for_window_count(*right_profile.layoutOptions, static_cast<size_t>(2));
    REQUIRE(right_two.has_value());
    CHECK(right_two->tree.split_dir == LayoutSplitDir::Horizontal);
    CHECK(right_two->tree.split_ratio == doctest::Approx(0.20f));
  }

  TEST_CASE("save layout appends monitor profile when no profile matches") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[gap]\n";
      file << "horizontal = 12\n";
    }

    std::vector<winapi::MonitorInfo> monitors = {
        make_test_monitor("\\\\.\\DISPLAY3", 0, true)};
    MonitorLayoutRuleUpdate update{0, monitors[0],
                                   make_test_layout_rule(3, LayoutSplitDir::Vertical, 0.33f)};

    auto save_result = save_monitor_layout_rules_to_config(temp_path, monitors, {update});
    REQUIRE(save_result.has_value());

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());
    REQUIRE(read_result->monitorProfiles.size() == 1);
    const auto& profile = read_result->monitorProfiles[0];
    REQUIRE(profile.match.device_name.has_value());
    CHECK(*profile.match.device_name == "\\\\.\\DISPLAY3");
    REQUIRE(profile.layoutOptions.has_value());
    auto rule = find_layout_rule_for_window_count(*profile.layoutOptions, static_cast<size_t>(3));
    REQUIRE(rule.has_value());
    CHECK(rule->tree.split_ratio == doctest::Approx(0.33f));
    REQUIRE(rule->tree.second);
    CHECK(rule->tree.second->split_dir == LayoutSplitDir::Horizontal);
  }
}

TEST_SUITE("Ignore Config Updates") {
  TEST_CASE("add process title pair creates missing ignore section and preserves other sections") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[gap]\n";
      file << "horizontal = 12\n";
    }

    auto result = add_ignore_process_title_pair_to_config(temp_path, "app.exe", "Tool Window");
    REQUIRE(result.has_value());
    CHECK(result->changed);
    CHECK(result->process_title_pair_count == 1);

    std::string text = read_text_file(temp_path);
    CHECK(text.find("[gap]") != std::string::npos);
    CHECK(text.find("horizontal = 12") != std::string::npos);
    CHECK(text.find("[ignore]") != std::string::npos);

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());
    const auto& pairs = read_result->ignoreOptions.ignored_process_title_pairs;
    CHECK(std::find(pairs.begin(), pairs.end(), std::make_pair(std::string("app.exe"),
                                                               std::string("Tool Window"))) !=
          pairs.end());
  }

  TEST_CASE("add process title pair updates existing ignore section without duplicating") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_process_title_pairs_with_defaults = false\n";
      file << "process_title_pairs = [{ process = \"app.exe\", title = \"Tool Window\" }]\n";
      file << "\n";
      file << "[gap]\n";
      file << "horizontal = 12\n";
    }

    auto duplicate =
        add_ignore_process_title_pair_to_config(temp_path, "APP.exe", "tool window");
    REQUIRE(duplicate.has_value());
    CHECK_FALSE(duplicate->changed);
    CHECK(duplicate->process_title_pair_count == 1);

    auto added = add_ignore_process_title_pair_to_config(temp_path, "other.exe", "Other Window");
    REQUIRE(added.has_value());
    CHECK(added->changed);
    CHECK(added->process_title_pair_count == 2);

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());
    const auto& pairs = read_result->ignoreOptions.ignored_process_title_pairs;
    CHECK(pairs.size() == 2);
    CHECK(std::find(pairs.begin(), pairs.end(), std::make_pair(std::string("other.exe"),
                                                               std::string("Other Window"))) !=
          pairs.end());
  }

  TEST_CASE("remove process title pair removes only matching user rule") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "merge_process_title_pairs_with_defaults = false\n";
      file << "process_title_pairs = [\n";
      file << "  { process = \"app.exe\", title = \"Tool Window\" },\n";
      file << "  { process = \"other.exe\", title = \"Other Window\" },\n";
      file << "]\n";
    }

    auto result = remove_ignore_process_title_pair_from_config(temp_path, "APP.exe", "tool window");
    REQUIRE(result.has_value());
    CHECK(result->changed);
    CHECK(result->process_title_pair_count == 1);

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());
    const auto& pairs = read_result->ignoreOptions.ignored_process_title_pairs;
    REQUIRE(pairs.size() == 1);
    CHECK(pairs[0] == std::make_pair(std::string("other.exe"), std::string("Other Window")));
  }

  TEST_CASE("remove missing process title pair leaves config unchanged") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "process_title_pairs = [{ process = \"app.exe\", title = \"Tool Window\" }]\n";
    }
    std::string before = read_text_file(temp_path);

    auto result =
        remove_ignore_process_title_pair_from_config(temp_path, "missing.exe", "Missing");
    REQUIRE(result.has_value());
    CHECK_FALSE(result->changed);
    CHECK(result->process_title_pair_count == 1);
    CHECK(read_text_file(temp_path) == before);
  }

  TEST_CASE("invalid TOML is rejected without modifying file") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore\n";
      file << "process_title_pairs = []\n";
    }
    std::string before = read_text_file(temp_path);

    auto result = add_ignore_process_title_pair_to_config(temp_path, "app.exe", "Tool Window");
    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("TOML parse error") != std::string::npos);
    CHECK(read_text_file(temp_path) == before);
  }
}

TEST_SUITE("Ignore Dialog Window Filtering") {
  TEST_CASE("shows windows ignored only by a user-facing ignore rule") {
    winapi::WindowManagementSnapshot snapshot;
    snapshot.status = winapi::WindowManagementStatus::Ignored;
    snapshot.is_ignored_by_user_configuration = true;
    snapshot.is_rejected_by_runtime_or_system_filter = false;

    CHECK(winapi::should_show_window_management_snapshot_in_ignore_dialog(snapshot));
  }

  TEST_CASE("hides windows that also match runtime or system filters") {
    winapi::WindowManagementSnapshot snapshot;
    snapshot.status = winapi::WindowManagementStatus::Ignored;
    snapshot.is_ignored_by_user_configuration = true;
    snapshot.is_rejected_by_runtime_or_system_filter = true;

    CHECK_FALSE(winapi::should_show_window_management_snapshot_in_ignore_dialog(snapshot));
  }

  TEST_CASE("shows managed windows so users can add ignore rules") {
    winapi::WindowManagementSnapshot managed;
    managed.status = winapi::WindowManagementStatus::Managed;

    CHECK(winapi::should_show_window_management_snapshot_in_ignore_dialog(managed));
  }

  TEST_CASE("hides runtime-only rejected windows") {
    winapi::WindowManagementSnapshot rejected;
    rejected.status = winapi::WindowManagementStatus::Rejected;
    rejected.is_rejected_by_runtime_or_system_filter = true;

    CHECK_FALSE(winapi::should_show_window_management_snapshot_in_ignore_dialog(rejected));
  }
}

// ============================================================================
// TOML Parse Error Tests
// ============================================================================

TEST_SUITE("TOML Parse Errors") {
  TEST_CASE("unclosed table bracket returns error") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[gap\n"; // Missing closing bracket
      file << "horizontal = 20.0\n";
    }

    auto result = read_options_toml(temp_path);
    CHECK(!result.has_value());
    CHECK(result.error().find("parse error") != std::string::npos);
  }

  TEST_CASE("unclosed string returns error") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[ignore]\n";
      file << "processes = [\"unclosed\n"; // Missing closing quote and bracket
    }

    auto result = read_options_toml(temp_path);
    CHECK(!result.has_value());
    CHECK(result.error().find("parse error") != std::string::npos);
  }

  TEST_CASE("invalid value returns error") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[gap]\n";
      file << "horizontal = @invalid\n"; // Invalid TOML value
    }

    auto result = read_options_toml(temp_path);
    CHECK(!result.has_value());
    CHECK(result.error().find("parse error") != std::string::npos);
  }

  TEST_CASE("empty file parses successfully with defaults") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      // Empty file
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    // Should have all defaults
    CHECK(result.value().gapOptions.horizontal == kDefaultGapHorizontal);
    CHECK(result.value().gapOptions.vertical == kDefaultGapVertical);
  }
}

// ============================================================================
// Type Coercion Tests
// ============================================================================

TEST_SUITE("Type Coercion") {
  TEST_CASE("integer gap values are accepted and converted to float") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[gap]\n";
      file << "horizontal = 20\n"; // Integer, not float
      file << "vertical = 25\n";   // Integer, not float
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    // Integers should be converted to float
    CHECK(result.value().gapOptions.horizontal == 20.0f);
    CHECK(result.value().gapOptions.vertical == 25.0f);
  }

  TEST_CASE("float gap values are correctly read") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[gap]\n";
      file << "horizontal = 20.5\n"; // Float
      file << "vertical = 25.5\n";   // Float
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().gapOptions.horizontal == 20.5f);
    CHECK(result.value().gapOptions.vertical == 25.5f);
  }

  TEST_CASE("mixed integer and float gap values both work") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[gap]\n";
      file << "horizontal = 20\n"; // Integer
      file << "vertical = 25.5\n"; // Float
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().gapOptions.horizontal == 20.0f);
    CHECK(result.value().gapOptions.vertical == 25.5f);
  }

  TEST_CASE("integer border_width is accepted") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[visualization.render]\n";
      file << "border_width = 5\n"; // Integer
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().visualizationOptions.renderOptions.border_width == 5.0f);
  }

  TEST_CASE("integer toast_font_size is accepted") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[visualization.render]\n";
      file << "toast_font_size = 24\n"; // Integer
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().visualizationOptions.renderOptions.toast_font_size == 24.0f);
  }

  TEST_CASE("integer zen percentage is accepted") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[visualization.render]\n";
      file << "zen_percentage = 1\n"; // Integer (will be clamped to 1.0)
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().visualizationOptions.renderOptions.zen_percentage == 1.0f);
  }

  TEST_CASE("integer loop interval_ms works (already uses as_integer)") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[loop]\n";
      file << "interval_ms = 100\n"; // Integer (expected type)
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().loopOptions.intervalMs == 100);
  }

  TEST_CASE("loop config refresh interval defaults to 1000 ms") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[loop]\n";
      file << "interval_ms = 100\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().loopOptions.configRefreshIntervalMs == kDefaultConfigRefreshIntervalMs);
  }

  TEST_CASE("loop config refresh interval can be configured") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[loop]\n";
      file << "config_refresh_interval_ms = 250\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().loopOptions.configRefreshIntervalMs == 250);
  }

  TEST_CASE("mouse drag drop defaults to exchange") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[loop]\n";
      file << "interval_ms = 100\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().loopOptions.mouse_drag_drop == MouseDragDropAction::Exchange);
  }

  TEST_CASE("mouse drag drop can be configured to split") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[loop]\n";
      file << "mouse_drag_drop = \"split\"\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().loopOptions.mouse_drag_drop == MouseDragDropAction::Split);
  }

  TEST_CASE("invalid mouse drag drop falls back to default") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[loop]\n";
      file << "mouse_drag_drop = \"move\"\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().loopOptions.mouse_drag_drop == MouseDragDropAction::Exchange);
  }

  TEST_CASE("mouse drag drop option is written to TOML") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    GlobalOptions options = get_default_global_options();
    options.loopOptions.mouse_drag_drop = MouseDragDropAction::Split;

    auto writeResult = write_options_toml(options, temp_path);
    REQUIRE(writeResult.has_value());

    auto readResult = read_options_toml(temp_path);
    REQUIRE(readResult.has_value());

    CHECK(readResult.value().loopOptions.mouse_drag_drop == MouseDragDropAction::Split);
  }

  TEST_CASE("negative loop config refresh interval falls back to default") {
    auto temp_path = create_temp_file_path();
    TempFileGuard guard(temp_path);

    {
      std::ofstream file(temp_path);
      file << "[loop]\n";
      file << "config_refresh_interval_ms = -1\n";
    }

    auto result = read_options_toml(temp_path);
    REQUIRE(result.has_value());

    CHECK(result.value().loopOptions.configRefreshIntervalMs == kDefaultConfigRefreshIntervalMs);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
