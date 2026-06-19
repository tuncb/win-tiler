#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

#include "app_console.h"
#include "argument_parser.h"
#include "version.h"

using namespace wintiler;

namespace {

ParseResult parse(std::initializer_list<const char*> rawArgs) {
  std::vector<std::string> storage;
  storage.reserve(rawArgs.size());
  for (const char* arg : rawArgs) {
    storage.emplace_back(arg);
  }

  std::vector<char*> argv;
  argv.reserve(storage.size());
  for (auto& arg : storage) {
    argv.push_back(arg.data());
  }

  return parse_args(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST_SUITE("argument_parser") {
  TEST_CASE("defaults to loop when no command is provided") {
    auto result = parse({"win-tiler"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("defaults to loop when only options are provided") {
    auto result = parse({"win-tiler", "--logmode", "debug"});

    CHECK(result.success);
    CHECK(result.args.options.log_level == LogLevel::Debug);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("parses log file option with implicit loop command") {
    auto result = parse({"win-tiler", "--log-file", R"(C:\logs\win-tiler.log)"});

    CHECK(result.success);
    REQUIRE(result.args.options.log_file_path.has_value());
    CHECK(*result.args.options.log_file_path == R"(C:\logs\win-tiler.log)");
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("parses perf stats option with implicit loop command") {
    auto result = parse({"win-tiler", "--perf-stats"});

    CHECK(result.success);
    CHECK(result.args.options.perf_stats);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("parses perf stats option with explicit loop command") {
    auto result = parse({"win-tiler", "--perf-stats", "loop"});

    CHECK(result.success);
    CHECK(result.args.options.perf_stats);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<LoopCommand>(*result.args.command));
  }

  TEST_CASE("parses monitor info option") {
    auto result = parse({"win-tiler", "--monitor-info"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<MonitorInfoCommand>(*result.args.command));
  }

  TEST_CASE("parses monitor info command") {
    auto result = parse({"win-tiler", "monitor-info"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<MonitorInfoCommand>(*result.args.command));
  }

  TEST_CASE("help still overrides the default loop command") {
    auto result = parse({"win-tiler", "--help"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<HelpCommand>(*result.args.command));
  }

  TEST_CASE("parses startup enable command") {
    auto result = parse({"win-tiler", "startup", "enable"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<StartupCommand>(*result.args.command));
    CHECK(std::get<StartupCommand>(*result.args.command).action == StartupAction::Enable);
  }

  TEST_CASE("parses startup disable command") {
    auto result = parse({"win-tiler", "startup", "disable"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<StartupCommand>(*result.args.command));
    CHECK(std::get<StartupCommand>(*result.args.command).action == StartupAction::Disable);
  }

  TEST_CASE("parses startup status command") {
    auto result = parse({"win-tiler", "startup", "status"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<StartupCommand>(*result.args.command));
    CHECK(std::get<StartupCommand>(*result.args.command).action == StartupAction::Status);
  }

  TEST_CASE("parses install option") {
    auto result = parse({"win-tiler", "--install"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    CHECK(std::holds_alternative<InstallCommand>(*result.args.command));
  }

  TEST_CASE("parses quiet uninstall option") {
    auto result = parse({"win-tiler", "--uninstall", "--quiet"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<UninstallCommand>(*result.args.command));
    CHECK(std::get<UninstallCommand>(*result.args.command).quiet);
  }

  TEST_CASE("parses direct uninstall option") {
    auto result = parse({"win-tiler", "--uninstall"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<UninstallCommand>(*result.args.command));
    CHECK_FALSE(std::get<UninstallCommand>(*result.args.command).quiet);
  }

  TEST_CASE("parses finish uninstall option") {
    auto result = parse({"win-tiler", "--finish-uninstall", "--pid", "1234", "--dir",
                         R"(C:\Install Dir)", "--running-pid", "5678"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<FinishUninstallCommand>(*result.args.command));
    CHECK(std::get<FinishUninstallCommand>(*result.args.command).pid == 1234);
    REQUIRE(std::get<FinishUninstallCommand>(*result.args.command).running_pid.has_value());
    CHECK(*std::get<FinishUninstallCommand>(*result.args.command).running_pid == 5678);
    CHECK(std::get<FinishUninstallCommand>(*result.args.command).install_dir ==
          R"(C:\Install Dir)");
  }

  TEST_CASE("parses finish update option") {
    auto result =
        parse({"win-tiler", "--finish-update", "--pid", "1234", "--dir", R"(C:\Install Dir)",
               "--downloaded-exe", R"(C:\Temp\win-tiler-update.exe)", "--expected-sha256",
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "--target-version",
               "1.2.3", "--running-pid", "5678", "--restart"});

    CHECK(result.success);
    REQUIRE(result.args.command.has_value());
    REQUIRE(std::holds_alternative<FinishUpdateCommand>(*result.args.command));
    const auto& command = std::get<FinishUpdateCommand>(*result.args.command);
    CHECK(command.pid == 1234);
    REQUIRE(command.running_pid.has_value());
    CHECK(*command.running_pid == 5678);
    CHECK(command.install_dir == R"(C:\Install Dir)");
    CHECK(command.downloaded_executable == R"(C:\Temp\win-tiler-update.exe)");
    CHECK(command.expected_sha256 ==
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(command.target_version == "1.2.3");
    CHECK(command.restart);
  }

  TEST_CASE("agent command is no longer supported") {
    auto result = parse({"win-tiler", "agent"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "Unknown command: agent");
  }

  TEST_CASE("startup command requires an action") {
    auto result = parse({"win-tiler", "startup"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "startup requires an action: enable, disable, or status");
  }

  TEST_CASE("startup command rejects extra arguments") {
    auto result = parse({"win-tiler", "startup", "enable", "extra"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "startup does not accept extra arguments");
  }

  TEST_CASE("finish uninstall requires pid and directory") {
    auto result = parse({"win-tiler", "--finish-uninstall", "--pid", "1234"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "--finish-uninstall requires --dir");
  }

  TEST_CASE("finish update requires downloaded executable and hash") {
    auto result =
        parse({"win-tiler", "--finish-update", "--pid", "1234", "--dir", R"(C:\Install Dir)"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "--finish-update requires --downloaded-exe");
  }

  TEST_CASE("finish update requires target version") {
    auto result =
        parse({"win-tiler", "--finish-update", "--pid", "1234", "--dir", R"(C:\Install Dir)",
               "--downloaded-exe", R"(C:\Temp\win-tiler-update.exe)", "--expected-sha256",
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "--finish-update requires --target-version");
  }

  TEST_CASE("ui-test-monitor command is no longer supported") {
    auto result = parse({"win-tiler", "ui-test-monitor"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "Unknown command: ui-test-monitor");
  }

  TEST_CASE("ui-test-multi command is no longer supported") {
    auto result = parse({"win-tiler", "ui-test-multi", "0", "0", "1920", "1080"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "Unknown command: ui-test-multi");
  }

  TEST_CASE("log file option requires a filepath") {
    auto result = parse({"win-tiler", "--log-file"});

    CHECK_FALSE(result.success);
    CHECK(result.error == "--log-file requires a filepath");
  }

  TEST_CASE("terminal-oriented commands attach to a parent console") {
    CliOptions options;

    CHECK(command_should_attach_console(Command{HelpCommand{}}, options));
    CHECK(command_should_attach_console(Command{VersionCommand{}}, options));
    CHECK(command_should_attach_console(Command{MonitorInfoCommand{}}, options));
    CHECK(command_should_attach_console(Command{TrackWindowsCommand{}}, options));
    CHECK(command_should_attach_console(Command{InitConfigCommand{}}, options));
    CHECK(command_should_attach_console(Command{StartupCommand{StartupAction::Status}}, options));
    CHECK_FALSE(command_should_attach_console(Command{InstallCommand{}}, options));
    CHECK_FALSE(command_should_attach_console(Command{UninstallCommand{}}, options));
    CHECK_FALSE(command_should_attach_console(Command{FinishUninstallCommand{}}, options));
    CHECK_FALSE(command_should_attach_console(Command{FinishUpdateCommand{}}, options));
  }

  TEST_CASE("loop mode only attaches to a parent console for perf stats") {
    CliOptions options;

    CHECK_FALSE(command_should_attach_console(Command{LoopCommand{}}, options));

    options.perf_stats = true;
    CHECK(command_should_attach_console(Command{LoopCommand{}}, options));
  }

  TEST_CASE("detached loop defaults to a temp log file") {
    CliOptions options;

    CHECK(command_should_default_to_temp_log_file(Command{LoopCommand{}}, options, false));
    CHECK_FALSE(command_should_default_to_temp_log_file(Command{LoopCommand{}}, options, true));

    options.log_file_path = R"(C:\logs\custom.log)";
    CHECK_FALSE(command_should_default_to_temp_log_file(Command{LoopCommand{}}, options, false));
  }

  TEST_CASE("terminal-oriented commands do not default to a temp log file") {
    CliOptions options;

    CHECK_FALSE(command_should_default_to_temp_log_file(Command{HelpCommand{}}, options, false));
    CHECK_FALSE(command_should_default_to_temp_log_file(Command{VersionCommand{}}, options, false));
    CHECK_FALSE(
        command_should_default_to_temp_log_file(Command{MonitorInfoCommand{}}, options, false));
    CHECK_FALSE(
        command_should_default_to_temp_log_file(Command{TrackWindowsCommand{}}, options, false));
    CHECK_FALSE(
        command_should_default_to_temp_log_file(Command{InitConfigCommand{}}, options, false));
    CHECK_FALSE(command_should_default_to_temp_log_file(
        Command{StartupCommand{StartupAction::Status}}, options, false));
    CHECK_FALSE(command_should_default_to_temp_log_file(Command{InstallCommand{}}, options, false));
    CHECK_FALSE(
        command_should_default_to_temp_log_file(Command{UninstallCommand{}}, options, false));
    CHECK_FALSE(
        command_should_default_to_temp_log_file(Command{FinishUpdateCommand{}}, options, false));
  }

  TEST_CASE("default temp log file uses a stable filename") {
    auto path = get_default_temp_log_file_path(R"(C:\Users\Test\AppData\Local\Temp)");

    CHECK(path.filename() == std::filesystem::path("win-tiler.log"));
    CHECK(path.parent_path() == std::filesystem::path(R"(C:\Users\Test\AppData\Local\Temp)"));
  }
}

TEST_SUITE("version") {
  TEST_CASE("version string matches the current release version") {
    CHECK(get_version_string() == "0.10.3");
  }

  TEST_CASE("version string does not include a prerelease suffix") {
    CHECK(get_version_string().find('-') == std::string::npos);
  }
}

#endif // !DOCTEST_CONFIG_DISABLE
