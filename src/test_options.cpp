#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>

#include "options.h"

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
void write_valid_config(const std::filesystem::path& path, float gap_h = 20.0f,
                        float gap_v = 25.0f) {
  std::ofstream file(path);
  file << std::fixed << std::setprecision(1);
  file << "[gap]\n";
  file << "horizontal = " << gap_h << "\n";
  file << "vertical = " << gap_v << "\n";
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

    write_valid_config(temp_path, 30.0f, 35.0f);

    GlobalOptionsProvider provider(temp_path);

    CHECK(provider.configPath.has_value());
    CHECK(provider.options.gapOptions.horizontal == 30.0f);
    CHECK(provider.options.gapOptions.vertical == 35.0f);
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

    // Should have all default bindings (13 total)
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
    REQUIRE(result.success);
    CHECK(result.options.ignoreOptions.merge_processes == true);
    CHECK(result.options.ignoreOptions.merge_window_titles == true);
    CHECK(result.options.ignoreOptions.merge_process_title_pairs == true);
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
    REQUIRE(result.success);

    auto& processes = result.options.ignoreOptions.ignored_processes;
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
    REQUIRE(result.success);

    auto& processes = result.options.ignoreOptions.ignored_processes;

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
    REQUIRE(result.success);

    auto& titles = result.options.ignoreOptions.ignored_window_titles;
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
    REQUIRE(result.success);

    auto& titles = result.options.ignoreOptions.ignored_window_titles;
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
    REQUIRE(result.success);

    auto& pairs = result.options.ignoreOptions.ignored_process_title_pairs;
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
    REQUIRE(result.success);

    auto& pairs = result.options.ignoreOptions.ignored_process_title_pairs;
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
    REQUIRE(result.success);

    auto& processes = result.options.ignoreOptions.ignored_processes;

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
    REQUIRE(writeResult.success);

    auto readResult = read_options_toml(temp_path);
    REQUIRE(readResult.success);

    // When merge is false, we get only user values back
    CHECK(readResult.options.ignoreOptions.merge_processes == false);
    CHECK(readResult.options.ignoreOptions.merge_window_titles == true);
    CHECK(readResult.options.ignoreOptions.merge_process_title_pairs == false);
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
    REQUIRE(result.success);

    auto defaults = get_default_ignore_options();

    // processes: merge=false, should have only user value
    CHECK(result.options.ignoreOptions.ignored_processes.size() == 1);
    CHECK(result.options.ignoreOptions.ignored_processes[0] == "custom.exe");

    // window_titles: merge=true, should have defaults + user (defaults is empty)
    CHECK(result.options.ignoreOptions.ignored_window_titles.size() ==
          defaults.ignored_window_titles.size() + 1);

    // process_title_pairs: merge=false, should have only user value
    CHECK(result.options.ignoreOptions.ignored_process_title_pairs.size() == 1);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
