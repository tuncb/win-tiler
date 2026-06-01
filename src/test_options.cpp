#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>

#include "options.h"
#include "runtime_support.h"

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

    auto write_result = write_options_toml(options, temp_path);
    REQUIRE(write_result.has_value());

    auto read_result = read_options_toml(temp_path);
    REQUIRE(read_result.has_value());

    CHECK(read_result.value().layoutOptions.split_mode == LayoutSplitMode::Dwindle);
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
    REQUIRE(resolved.layoutOptions.rules.size() == 1);
    CHECK(resolved.layoutOptions.rules[0].tree.split_ratio == doctest::Approx(0.30f));
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
