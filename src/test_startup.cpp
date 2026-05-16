#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <filesystem>

#include "startup.h"
#include "winapi.h"

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

TEST_SUITE("winapi") {
  TEST_CASE("owned standard dialog windows are ignored") {
    CHECK(winapi::should_ignore_owned_dialog_window(true, "#32770"));
  }

  TEST_CASE("unowned standard dialog windows are not ignored by the owned-dialog rule") {
    CHECK_FALSE(winapi::should_ignore_owned_dialog_window(false, "#32770"));
  }

  TEST_CASE("owned non-dialog windows are not ignored by the owned-dialog rule") {
    CHECK_FALSE(winapi::should_ignore_owned_dialog_window(true, "Chrome_WidgetWin_1"));
  }

  TEST_CASE("hotkey messages stay queued for hotkey polling") {
    CHECK(winapi::should_defer_message_to_hotkey_poll(0x0312));
  }

  TEST_CASE("power messages are not deferred to hotkey polling") {
    CHECK_FALSE(winapi::should_defer_message_to_hotkey_poll(0x0218));
  }

  TEST_CASE("display and setting changes invalidate monitor cache") {
    CHECK(winapi::should_invalidate_monitor_cache_for_message(0x007E));
    CHECK(winapi::should_invalidate_monitor_cache_for_message(0x001A));
  }

  TEST_CASE("unrelated messages do not invalidate monitor cache") {
    CHECK_FALSE(winapi::should_invalidate_monitor_cache_for_message(0x0312));
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
