#include "app_console.h"

#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace wintiler {

namespace {

std::string wide_to_utf8(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }

  int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                 nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  std::string result(static_cast<size_t>(size), '\0');
  int converted = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      result.data(), size, nullptr, nullptr);
  if (converted <= 0) {
    return {};
  }

  return result;
}

bool reopen_standard_streams_to_console() {
  bool stdout_ready = false;
  bool stderr_ready = false;
  bool stdin_ready = false;
  FILE* stream = nullptr;

  if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0) {
    std::cout.setstate(std::ios::badbit);
  } else {
    stdout_ready = true;
  }
  if (freopen_s(&stream, "CONOUT$", "w", stderr) != 0) {
    std::cerr.setstate(std::ios::badbit);
    std::clog.setstate(std::ios::badbit);
  } else {
    stderr_ready = true;
  }
  if (freopen_s(&stream, "CONIN$", "r", stdin) != 0) {
    std::cin.setstate(std::ios::badbit);
  } else {
    stdin_ready = true;
  }

  std::ios::sync_with_stdio(true);
  if (stdout_ready) {
    std::cout.clear();
  }
  if (stderr_ready) {
    std::cerr.clear();
    std::clog.clear();
  }
  if (stdin_ready) {
    std::cin.clear();
  }

  return stdout_ready || stderr_ready;
}

} // namespace

bool command_should_attach_console(const Command& command, const CliOptions& options) {
  return std::visit(
      [&](const auto& concrete_command) -> bool {
        using CommandType = std::decay_t<decltype(concrete_command)>;
        if constexpr (std::is_same_v<CommandType, LoopCommand>) {
          return options.perf_stats;
        } else if constexpr (std::is_same_v<CommandType, InstallCommand> ||
                             std::is_same_v<CommandType, UninstallCommand> ||
                             std::is_same_v<CommandType, FinishUninstallCommand> ||
                             std::is_same_v<CommandType, FinishUpdateCommand>) {
          return false;
        } else {
          return true;
        }
      },
      command);
}

bool command_should_default_to_temp_log_file(const Command& command, const CliOptions& options,
                                             bool console_attached) {
  if (console_attached || options.log_file_path.has_value()) {
    return false;
  }

  return std::holds_alternative<LoopCommand>(command);
}

std::filesystem::path get_default_temp_log_file_path(const std::filesystem::path& temp_directory) {
  return temp_directory / "win-tiler.log";
}

bool attach_parent_console() {
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
    DWORD error = GetLastError();
    if (error != ERROR_ACCESS_DENIED) {
      return false;
    }
  }

  return reopen_standard_streams_to_console();
}

void configure_default_console_logger() {
  auto logger = spdlog::get("win-tiler-console");
  if (logger == nullptr) {
    logger = spdlog::stdout_color_mt("win-tiler-console");
  }
  spdlog::set_default_logger(logger);
}

void configure_default_null_logger() {
  auto logger = spdlog::get("win-tiler-null");
  if (logger == nullptr) {
    logger = spdlog::null_logger_mt("win-tiler-null");
  }
  spdlog::set_default_logger(logger);
}

std::vector<std::string> wide_args_to_utf8(int argc, wchar_t* argv[]) {
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.push_back(wide_to_utf8(argv[i]));
  }
  return args;
}

} // namespace wintiler
