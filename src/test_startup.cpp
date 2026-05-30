#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "installer.h"
#include "options.h"
#include "resource.h"
#include "startup.h"
#include "version.h"
#include "winapi.h"

using namespace wintiler;

namespace {

std::filesystem::path make_unique_temp_directory(const std::string& name) {
  auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              (name + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(ticks));
  std::filesystem::create_directories(path);
  return path;
}

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream file(path);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

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

TEST_SUITE("installer") {
  TEST_CASE("build_uninstall_command_line includes quiet flag only when requested") {
    std::filesystem::path executable =
        R"(C:\Users\Test User\AppData\Local\win-tiler\win-tiler.exe)";

    CHECK(build_uninstall_command_line_wide(executable, false) ==
          L"\"C:\\Users\\Test User\\AppData\\Local\\win-tiler\\win-tiler.exe\" --uninstall");
    CHECK(build_uninstall_command_line_wide(executable, true) ==
          L"\"C:\\Users\\Test User\\AppData\\Local\\win-tiler\\win-tiler.exe\" --uninstall "
          L"--quiet");
  }

  TEST_CASE("build_finish_uninstall_command_line quotes helper and install directory") {
    std::filesystem::path helper = R"(C:\Users\Test User\AppData\Local\Temp\helper.exe)";
    std::filesystem::path install_dir = R"(C:\Users\Test User\AppData\Local\win-tiler)";

    CHECK(build_finish_uninstall_command_line_wide(helper, 4321, install_dir) ==
          L"\"C:\\Users\\Test User\\AppData\\Local\\Temp\\helper.exe\" --finish-uninstall "
          L"--pid 4321 --dir \"C:\\Users\\Test User\\AppData\\Local\\win-tiler\"");
  }

  TEST_CASE("build_finish_uninstall_command_line includes running instance pid when provided") {
    std::filesystem::path helper = R"(C:\Users\Test User\AppData\Local\Temp\helper.exe)";
    std::filesystem::path install_dir = R"(C:\Users\Test User\AppData\Local\win-tiler)";

    CHECK(build_finish_uninstall_command_line_wide(helper, 4321, install_dir, 5678) ==
          L"\"C:\\Users\\Test User\\AppData\\Local\\Temp\\helper.exe\" --finish-uninstall "
          L"--pid 4321 --dir \"C:\\Users\\Test User\\AppData\\Local\\win-tiler\" "
          L"--running-pid 5678");
  }

  TEST_CASE("build_finish_update_command_line quotes paths and restart flag") {
    std::filesystem::path helper = R"(C:\Users\Test User\AppData\Local\Temp\helper.exe)";
    std::filesystem::path install_dir = R"(C:\Users\Test User\AppData\Local\win-tiler)";
    std::filesystem::path downloaded_executable =
        R"(C:\Users\Test User\AppData\Local\Temp\win-tiler-update.exe)";

    CHECK(build_finish_update_command_line_wide(
              helper, 4321, install_dir, downloaded_executable,
              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 5678, true) ==
          L"\"C:\\Users\\Test User\\AppData\\Local\\Temp\\helper.exe\" --finish-update "
          L"--pid 4321 --dir \"C:\\Users\\Test User\\AppData\\Local\\win-tiler\" "
          L"--downloaded-exe "
          L"\"C:\\Users\\Test User\\AppData\\Local\\Temp\\win-tiler-update.exe\" "
          L"--expected-sha256 "
          L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa "
          L"--running-pid 5678 --restart");
  }

  TEST_CASE("format_install_date_for_registry uses yyyymmdd") {
    CHECK(format_install_date_for_registry(2026, 5, 9) == L"20260509");
  }

  TEST_CASE("version tags parse and compare") {
    auto parsed = parse_version_tag("v1.2.3");

    REQUIRE(parsed.has_value());
    CHECK(parsed->major == 1);
    CHECK(parsed->minor == 2);
    CHECK(parsed->patch == 3);
    CHECK(is_version_newer(*parsed, VersionNumber{1, 2, 2}));
    CHECK_FALSE(is_version_newer(*parsed, VersionNumber{1, 2, 3}));
    CHECK_FALSE(parse_version_tag("v1.2.3-beta").has_value());
  }

  TEST_CASE("extract_sha256_from_text reads first hash") {
    auto hash = extract_sha256_from_text(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa  win-tiler.exe\n");

    REQUIRE(hash.has_value());
    CHECK(*hash == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK_FALSE(extract_sha256_from_text("not a hash").has_value());
  }

  TEST_CASE("parse_latest_release_response reads tag and raw executable assets") {
    const std::string response = R"json({
      "tag_name": "v1.2.3",
      "html_url": "https://github.com/tuncb/win-tiler/releases/tag/v1.2.3",
      "draft": false,
      "prerelease": false,
      "assets": [
        {
          "name": "win-tiler-v1.2.3.exe",
          "browser_download_url": "https://github.com/tuncb/win-tiler/releases/download/v1.2.3/win-tiler-v1.2.3.exe",
          "digest": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "uploader": { "login": "github-actions[bot]" }
        },
        {
          "name": "win-tiler-v1.2.3.exe.sha256",
          "browser_download_url": "https://github.com/tuncb/win-tiler/releases/download/v1.2.3/win-tiler-v1.2.3.exe.sha256"
        }
      ]
    })json";

    auto release = parse_latest_release_response(response);

    REQUIRE(release.has_value());
    CHECK(release->tag_name == "v1.2.3");
    CHECK(release->version.major == 1);
    CHECK(release->assets.size() == 2);
    CHECK(release->assets[0].name == "win-tiler-v1.2.3.exe");
    CHECK(release->assets[1].name == "win-tiler-v1.2.3.exe.sha256");
  }

  TEST_CASE("startup command target detection requires installed executable command") {
    std::filesystem::path executable =
        R"(C:\Users\Test User\AppData\Local\win-tiler\win-tiler.exe)";
    auto command_line = build_startup_command_line(executable, std::nullopt, std::nullopt);

    CHECK(startup_command_targets_executable(command_line, executable));
    CHECK_FALSE(
        startup_command_targets_executable("\"C:\\Downloads\\win-tiler.exe\" loop", executable));
    CHECK_FALSE(startup_command_targets_executable(
        "\"C:\\Users\\Test User\\AppData\\Local\\win-tiler\\win-tiler.exe\" --log-file "
        "\"C:\\temp\\log.txt\" loop",
        executable));
  }

  TEST_CASE("installation is present only when installed executable exists") {
    auto install_dir = make_unique_temp_directory("win-tiler-install-state-test");

    CHECK_FALSE(is_installation_present(install_dir));

    {
      std::ofstream executable(get_installed_executable_path(install_dir));
      executable << "exe";
    }

    CHECK(is_installation_present(install_dir));

    std::filesystem::remove_all(install_dir);
  }

  TEST_CASE("ensure_installed_config_file creates default config when missing") {
    auto install_dir = make_unique_temp_directory("win-tiler-config-create-test");
    auto config_path = get_installed_config_path(install_dir);

    auto result = ensure_installed_config_file(install_dir);

    CHECK(result.has_value());
    CHECK(std::filesystem::exists(config_path));
    CHECK(read_options_toml(config_path).has_value());

    std::filesystem::remove_all(install_dir);
  }

  TEST_CASE("ensure_installed_config_file leaves existing config unchanged") {
    auto install_dir = make_unique_temp_directory("win-tiler-config-preserve-test");
    auto config_path = get_installed_config_path(install_dir);
    {
      std::ofstream config(config_path);
      config << "custom = true\n";
    }

    auto result = ensure_installed_config_file(install_dir);

    CHECK(result.has_value());
    CHECK(read_text_file(config_path) == "custom = true\n");

    std::filesystem::remove_all(install_dir);
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

  TEST_CASE("notification area hotkey requests are consumed once") {
    CHECK_FALSE(winapi::consume_notification_area_hotkey_action().has_value());

    winapi::request_notification_area_hotkey_action(HotkeyAction::TogglePause);
    auto pause_action = winapi::consume_notification_area_hotkey_action();

    REQUIRE(pause_action.has_value());
    CHECK(*pause_action == HotkeyAction::TogglePause);
    CHECK_FALSE(winapi::consume_notification_area_hotkey_action().has_value());
  }

  TEST_CASE("notification area reset request uses restart system action") {
    winapi::request_notification_area_hotkey_action(HotkeyAction::RestartSystem);
    auto reset_action = winapi::consume_notification_area_hotkey_action();

    REQUIRE(reset_action.has_value());
    CHECK(*reset_action == HotkeyAction::RestartSystem);
  }

  TEST_CASE("notification area pause menu text reflects manual pause state") {
    CHECK(std::wstring(winapi::get_notification_area_toggle_pause_menu_text(false)) == L"Pause");
    CHECK(std::wstring(winapi::get_notification_area_toggle_pause_menu_text(true)) == L"Unpause");
  }

  TEST_CASE("application icon resource is available") {
    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));

    CHECK(icon != nullptr);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
