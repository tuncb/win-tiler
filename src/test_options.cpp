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
std::filesystem::path createTempFilePath() {
  auto tempDir = std::filesystem::temp_directory_path();
  auto filename = "win-tiler-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  ".toml";
  return tempDir / filename;
}

// Helper to write a simple valid TOML config
void writeValidConfig(const std::filesystem::path& path, float gapH = 20.0f, float gapV = 25.0f) {
  std::ofstream file(path);
  file << std::fixed << std::setprecision(1);
  file << "[gap]\n";
  file << "horizontal = " << gapH << "\n";
  file << "vertical = " << gapV << "\n";
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
    auto tempPath = createTempFilePath();
    // Don't create the file - it shouldn't exist

    GlobalOptionsProvider provider(tempPath);

    CHECK(provider.configPath.has_value());
    CHECK(provider.options.gapOptions.horizontal == kDefaultGapHorizontal);
    CHECK(provider.options.gapOptions.vertical == kDefaultGapVertical);
  }

  TEST_CASE("constructor with valid file loads options") {
    auto tempPath = createTempFilePath();
    TempFileGuard guard(tempPath);

    writeValidConfig(tempPath, 30.0f, 35.0f);

    GlobalOptionsProvider provider(tempPath);

    CHECK(provider.configPath.has_value());
    CHECK(provider.options.gapOptions.horizontal == 30.0f);
    CHECK(provider.options.gapOptions.vertical == 35.0f);
  }

  TEST_CASE("refresh returns false when no config path") {
    GlobalOptionsProvider provider;

    CHECK(provider.refresh() == false);
  }

  TEST_CASE("refresh returns false when file unchanged") {
    auto tempPath = createTempFilePath();
    TempFileGuard guard(tempPath);

    writeValidConfig(tempPath, 20.0f, 25.0f);

    GlobalOptionsProvider provider(tempPath);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    // refresh without changing file
    CHECK(provider.refresh() == false);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
  }

  TEST_CASE("refresh returns true and updates options when file changed") {
    auto tempPath = createTempFilePath();
    TempFileGuard guard(tempPath);

    writeValidConfig(tempPath, 20.0f, 25.0f);

    GlobalOptionsProvider provider(tempPath);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
    CHECK(provider.options.gapOptions.vertical == 25.0f);

    // Wait a bit to ensure file modification time changes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Modify the file
    writeValidConfig(tempPath, 40.0f, 45.0f);

    CHECK(provider.refresh() == true);
    CHECK(provider.options.gapOptions.horizontal == 40.0f);
    CHECK(provider.options.gapOptions.vertical == 45.0f);
  }

  TEST_CASE("refresh returns false and keeps options when file becomes invalid") {
    auto tempPath = createTempFilePath();
    TempFileGuard guard(tempPath);

    writeValidConfig(tempPath, 20.0f, 25.0f);

    GlobalOptionsProvider provider(tempPath);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    // Wait a bit to ensure file modification time changes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Write invalid TOML
    {
      std::ofstream file(tempPath);
      file << "this is not valid toml {{{\n";
    }

    CHECK(provider.refresh() == false);
    // Options should remain unchanged
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
    CHECK(provider.options.gapOptions.vertical == 25.0f);
  }

  TEST_CASE("refresh returns false when file is deleted") {
    auto tempPath = createTempFilePath();

    writeValidConfig(tempPath, 20.0f, 25.0f);

    GlobalOptionsProvider provider(tempPath);
    CHECK(provider.options.gapOptions.horizontal == 20.0f);

    // Delete the file
    std::filesystem::remove(tempPath);

    CHECK(provider.refresh() == false);
    // Options should remain unchanged
    CHECK(provider.options.gapOptions.horizontal == 20.0f);
  }

  TEST_CASE("refresh detects file creation after provider construction") {
    auto tempPath = createTempFilePath();
    TempFileGuard guard(tempPath);

    // Create provider before file exists
    GlobalOptionsProvider provider(tempPath);
    CHECK(provider.options.gapOptions.horizontal == kDefaultGapHorizontal);

    // Now create the file
    writeValidConfig(tempPath, 50.0f, 55.0f);

    CHECK(provider.refresh() == true);
    CHECK(provider.options.gapOptions.horizontal == 50.0f);
    CHECK(provider.options.gapOptions.vertical == 55.0f);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
