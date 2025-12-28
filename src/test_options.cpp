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

#endif // !DOCTEST_CONFIG_DISABLE
