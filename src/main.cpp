#ifdef DOCTEST_CONFIG_DISABLE

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "argument_parser.h"
#include "loop.h"
#include "options.h"
#include "startup.h"
#include "track_windows.h"
#include "version.h"
#include "winapi.h"

namespace {

std::filesystem::path getExecutablePath() {
  std::vector<wchar_t> buffer(MAX_PATH, L'\0');

  while (true) {
    DWORD path_length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (path_length == 0) {
      return {};
    }

    if (path_length < buffer.size()) {
      return std::filesystem::path(std::wstring(buffer.data(), path_length));
    }

    buffer.resize(buffer.size() * 2, L'\0');
  }
}

std::filesystem::path getExecutableDirectory() {
  auto executable_path = getExecutablePath();
  return executable_path.parent_path();
}

std::filesystem::path getDefaultConfigPath() {
  return getExecutableDirectory() / "win-tiler.toml";
}

bool command_uses_options_provider(const wintiler::Command& command) {
  return std::holds_alternative<wintiler::LoopCommand>(command) ||
         std::holds_alternative<wintiler::TrackWindowsCommand>(command);
}

tl::expected<std::optional<std::filesystem::path>, std::string>
resolve_startup_option_path(const std::optional<std::string>& path, const std::string& label) {
  if (!path.has_value()) {
    return std::nullopt;
  }

  std::error_code ec;
  auto absolute_path = std::filesystem::absolute(*path, ec);
  if (ec) {
    return tl::unexpected("Failed to resolve startup " + label + " path: " + ec.message());
  }

  return absolute_path.lexically_normal();
}

} // namespace

// Helper for std::visit with lambdas
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

using namespace wintiler;

void applyLogLevel(LogLevel level) {
  switch (level) {
  case LogLevel::Trace:
    spdlog::set_level(spdlog::level::trace);
    break;
  case LogLevel::Debug:
    spdlog::set_level(spdlog::level::debug);
    break;
  case LogLevel::Info:
    spdlog::set_level(spdlog::level::info);
    break;
  case LogLevel::Warn:
    spdlog::set_level(spdlog::level::warn);
    break;
  case LogLevel::Err:
    spdlog::set_level(spdlog::level::err);
    break;
  case LogLevel::Off:
    spdlog::set_level(spdlog::level::off);
    break;
  }
}

tl::expected<void, std::string> configureLogFile(const CliOptions& options) {
  if (!options.log_file_path.has_value()) {
    return {};
  }

  try {
    auto logger = spdlog::basic_logger_mt(
        "win-tiler-file", std::filesystem::path(*options.log_file_path).string(), true);
    spdlog::set_default_logger(std::move(logger));
  } catch (const spdlog::spdlog_ex& ex) {
    return tl::unexpected("Failed to open log file: " + std::string(ex.what()));
  }

  return {};
}

int main(int argc, char* argv[]) {
  // Set DPI awareness before any Windows API calls that return coordinates
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Parse command-line arguments
  auto result = parse_args(argc, argv);
  if (!result.success) {
    spdlog::error("{}", result.error);
    return 1;
  }

  Command command = result.args.command.value_or(Command{LoopCommand{}});

  auto logging_result = configureLogFile(result.args.options);
  if (!logging_result.has_value()) {
    spdlog::error("{}", logging_result.error());
    return 1;
  }

  // Apply log level if specified
  if (result.args.options.log_level) {
    applyLogLevel(*result.args.options.log_level);
  }

  // Flush spdlog on info-level messages to ensure immediate output
  spdlog::flush_on(spdlog::level::info);

  // Log version at startup for long-running/management commands.
  if (!std::holds_alternative<MonitorInfoCommand>(command)) {
    spdlog::info("win-tiler v{}", get_version_string());
  }

  GlobalOptionsProvider optionsProvider;
  if (command_uses_options_provider(command)) {
    // Determine config path to load
    std::filesystem::path configPath;
    bool configExplicitlySpecified = false;

    if (result.args.options.config_path) {
      configPath = *result.args.options.config_path;
      configExplicitlySpecified = true;
    } else {
      configPath = getDefaultConfigPath();
    }

    // Load config once here so explicit config errors are surfaced before entering the command
    if (configExplicitlySpecified || std::filesystem::exists(configPath)) {
      auto loaded = read_options_toml(configPath);
      if (!loaded.has_value()) {
        if (configExplicitlySpecified) {
          spdlog::error("Failed to load config: {}", loaded.error());
          return 1;
        }
        spdlog::debug("Default config not loaded: {}", loaded.error());
      } else {
        spdlog::info("Loaded config from: {}", configPath.string());
      }
    }

    std::optional<std::filesystem::path> providerPath;
    if (configExplicitlySpecified || std::filesystem::exists(configPath)) {
      providerPath = configPath;
    }
    optionsProvider = GlobalOptionsProvider(providerPath);
  }

  int exitCode = 0;
  std::visit(overloaded{
                 [](const HelpCommand&) { print_usage(); },
                 [](const VersionCommand&) {
                   std::cout << "win-tiler v" << get_version_string() << std::endl;
                 },
                 [&](const LoopCommand&) {
                   run_loop_mode(optionsProvider, LoopRunOptions{result.args.options.perf_stats});
                 },
                 [](const MonitorInfoCommand&) {
                   std::vector<winapi::MonitorInfo> monitors;
                   winapi::fill_monitors(monitors);
                   winapi::log_monitors(monitors);
                 },
                 [&](const TrackWindowsCommand&) { run_track_windows_mode(optionsProvider); },
                 [&](const InitConfigCommand& cmd) {
                   auto targetPath =
                       cmd.filepath ? std::filesystem::path(*cmd.filepath) : getDefaultConfigPath();
                   auto writeResult = write_options_toml(get_default_global_options(), targetPath);
                   if (writeResult.has_value()) {
                     spdlog::info("Config written to: {}", targetPath.string());
                   } else {
                     spdlog::error("Failed to write config: {}", writeResult.error());
                     exitCode = 1;
                   }
                 },
                 [&](const StartupCommand& cmd) {
                   if (cmd.action == StartupAction::Enable) {
                     auto executable_path = getExecutablePath();
                     if (executable_path.empty()) {
                       spdlog::error("Failed to determine executable path");
                       exitCode = 1;
                       return;
                     }

                     auto config_path =
                         resolve_startup_option_path(result.args.options.config_path, "config");
                     if (!config_path.has_value()) {
                       spdlog::error("{}", config_path.error());
                       exitCode = 1;
                       return;
                     }

                     auto log_file_path =
                         resolve_startup_option_path(result.args.options.log_file_path, "log file");
                     if (!log_file_path.has_value()) {
                       spdlog::error("{}", log_file_path.error());
                       exitCode = 1;
                       return;
                     }

                     auto enable_result =
                         enable_startup_registration(executable_path, *config_path, *log_file_path);
                     if (!enable_result.has_value()) {
                       spdlog::error("{}", enable_result.error());
                       exitCode = 1;
                       return;
                     }

                     spdlog::info("Enabled startup registration for the current user");
                     spdlog::info(
                         "Startup command line: {}",
                         build_startup_command_line(executable_path, *config_path, *log_file_path));
                     return;
                   }

                   if (cmd.action == StartupAction::Disable) {
                     auto disable_result = disable_startup_registration();
                     if (!disable_result.has_value()) {
                       spdlog::error("{}", disable_result.error());
                       exitCode = 1;
                       return;
                     }

                     if (*disable_result) {
                       spdlog::info("Removed startup registration for the current user");
                     } else {
                       spdlog::info("Startup registration was already disabled");
                     }
                     return;
                   }

                   auto status_result = get_startup_registration_status();
                   if (!status_result.has_value()) {
                     spdlog::error("{}", status_result.error());
                     exitCode = 1;
                     return;
                   }

                   if (!status_result->enabled) {
                     std::cout << "Startup registration: disabled" << std::endl;
                     return;
                   }

                   std::cout << "Startup registration: enabled" << std::endl;
                   if (status_result->command_line.has_value()) {
                     std::cout << "Command line: " << *status_result->command_line << std::endl;
                   }
                 },
             },
             command);
  return exitCode;
}

#endif
