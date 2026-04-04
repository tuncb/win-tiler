#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <filesystem>

#include "startup.h"

using namespace wintiler;

TEST_SUITE("startup") {
  TEST_CASE("build_startup_command_line omits config when not provided") {
    std::filesystem::path executable = R"(C:\Program Files\win-tiler\win-tiler.exe)";

    auto command_line = build_startup_command_line(executable, std::nullopt);

    CHECK(command_line == "\"C:\\Program Files\\win-tiler\\win-tiler.exe\" loop");
  }

  TEST_CASE("build_startup_command_line includes quoted config path when provided") {
    std::filesystem::path executable = R"(C:\Program Files\win-tiler\win-tiler.exe)";
    std::filesystem::path config = R"(C:\Users\Test User\AppData\Roaming\win-tiler\config.toml)";

    auto command_line = build_startup_command_line(executable, config);

    CHECK(command_line ==
          "\"C:\\Program Files\\win-tiler\\win-tiler.exe\" --config "
          "\"C:\\Users\\Test User\\AppData\\Roaming\\win-tiler\\config.toml\" loop");
  }

  TEST_CASE("build_startup_command_line escapes embedded quotes") {
    std::filesystem::path executable = LR"(C:\Apps\win-"tiler"\win-tiler.exe)";

    auto command_line = build_startup_command_line(executable, std::nullopt);

    CHECK(command_line == "\"C:\\Apps\\win-\\\"tiler\\\"\\win-tiler.exe\" loop");
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
