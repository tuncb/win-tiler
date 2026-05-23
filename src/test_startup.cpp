#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>
#include <windows.h>

#include <filesystem>
#include <string>

#include "resource.h"
#include "startup.h"
#include "version.h"
#include "winapi.h"

using namespace wintiler;

TEST_SUITE("startup") {
  TEST_CASE("build_startup_command_line omits config when not provided") {
    std::filesystem::path executable = R"(C:\Program Files\win-tiler\win-tiler.exe)";

    auto command_line = build_startup_command_line(executable, std::nullopt, std::nullopt);

    CHECK(command_line == "\"C:\\Program Files\\win-tiler\\win-tiler.exe\" loop");
  }

  TEST_CASE("build_startup_command_line includes quoted config path when provided") {
    std::filesystem::path executable = R"(C:\Program Files\win-tiler\win-tiler.exe)";
    std::filesystem::path config = R"(C:\Users\Test User\AppData\Roaming\win-tiler\config.toml)";

    auto command_line = build_startup_command_line(executable, config, std::nullopt);

    CHECK(command_line ==
          "\"C:\\Program Files\\win-tiler\\win-tiler.exe\" --config "
          "\"C:\\Users\\Test User\\AppData\\Roaming\\win-tiler\\config.toml\" loop");
  }

  TEST_CASE("build_startup_command_line escapes embedded quotes") {
    std::filesystem::path executable = LR"(C:\Apps\win-"tiler"\win-tiler.exe)";

    auto command_line = build_startup_command_line(executable, std::nullopt, std::nullopt);

    CHECK(command_line == "\"C:\\Apps\\win-\\\"tiler\\\"\\win-tiler.exe\" loop");
  }

  TEST_CASE("build_startup_command_line includes quoted log file path when provided") {
    std::filesystem::path executable = R"(C:\Program Files\win-tiler\win-tiler.exe)";
    std::filesystem::path log_file = R"(C:\Users\Test User\AppData\Local\win-tiler\win-tiler.log)";

    auto command_line = build_startup_command_line(executable, std::nullopt, log_file);

    CHECK(command_line ==
          "\"C:\\Program Files\\win-tiler\\win-tiler.exe\" --log-file "
          "\"C:\\Users\\Test User\\AppData\\Local\\win-tiler\\win-tiler.log\" loop");
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

  TEST_CASE("hotkey registration failure includes action name and shortcut") {
    winapi::HotKeyInfo hotkey{19, 12, 82};

    CHECK(winapi::format_register_hotkey_failure(hotkey, "RestartSystem", "super+alt+r", 1409) ==
          "register_hotkey: Failed to register hotkey action=RestartSystem, "
          "shortcut='super+alt+r', id=19, key=82, modifiers=12, error=1409");
  }

  TEST_CASE("display and setting changes invalidate monitor cache") {
    CHECK(winapi::should_invalidate_monitor_cache_for_message(0x007E));
    CHECK(winapi::should_invalidate_monitor_cache_for_message(0x001A));
  }

  TEST_CASE("unrelated messages do not invalidate monitor cache") {
    CHECK_FALSE(winapi::should_invalidate_monitor_cache_for_message(0x0312));
  }

  TEST_CASE("notification area menu enables only available file actions") {
    winapi::NotificationAreaIconOptions options;
    auto availability = winapi::get_notification_area_menu_availability(options);
    CHECK_FALSE(availability.can_open_config);
    CHECK_FALSE(availability.can_show_log);
    CHECK(availability.can_exit);

    options.config_path = R"(C:\Users\Test User\AppData\Roaming\win-tiler\config.toml)";
    availability = winapi::get_notification_area_menu_availability(options);
    CHECK(availability.can_open_config);
    CHECK_FALSE(availability.can_show_log);
    CHECK(availability.can_exit);

    options.log_file_path = R"(C:\Users\Test User\AppData\Local\Temp\win-tiler.log)";
    availability = winapi::get_notification_area_menu_availability(options);
    CHECK(availability.can_open_config);
    CHECK(availability.can_show_log);
    CHECK(availability.can_exit);
  }

  TEST_CASE("notification area about message includes version and repository") {
    std::string version = get_version_string();
    std::wstring expected_version(version.begin(), version.end());

    const std::wstring message = winapi::get_notification_area_about_message();

    CHECK(message.find(L"Win-tiler version " + expected_version) != std::wstring::npos);
    CHECK(message.find(L"https://github.com/tuncb/win-tiler") != std::wstring::npos);
  }

  TEST_CASE("notification area about dialog content marks repository as hyperlink") {
    const std::wstring content = winapi::get_notification_area_about_dialog_content();

    CHECK(content.find(L"<a href=\"https://github.com/tuncb/win-tiler\">") != std::wstring::npos);
    CHECK(content.find(L"</a>") != std::wstring::npos);
  }

  TEST_CASE("application icon resource is available") {
    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));

    CHECK(icon != nullptr);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
