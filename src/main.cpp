#ifdef DOCTEST_CONFIG_DISABLE

#include <spdlog/spdlog.h>
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "argument_parser.h"
#include "loop.h"
#include "multi_cells.h"
#include "multi_ui.h"
#include "options.h"
#include "track_windows.h"
#include "version.h"
#include "winapi.h"

namespace {

std::filesystem::path getExecutableDirectory() {
  char path[MAX_PATH];
  GetModuleFileNameA(nullptr, path, MAX_PATH);
  return std::filesystem::path(path).parent_path();
}

std::filesystem::path getDefaultConfigPath() {
  return getExecutableDirectory() / "win-tiler.toml";
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

const size_t CELL_ID_START = 10;

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

void runUiTestMonitor(GlobalOptionsProvider& optionsProvider) {
  const auto& globalOptions = optionsProvider.options;
  auto monitors = winapi::get_monitors();
  std::vector<cells::ClusterInitInfo> infos;

  for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
    const auto& monitor = monitors[monitorIndex];

    // Workspace bounds (for tiling)
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);
    // Full monitor bounds (for pointer detection)
    float mx = static_cast<float>(monitor.rect.left);
    float my = static_cast<float>(monitor.rect.top);
    float mw = static_cast<float>(monitor.rect.right - monitor.rect.left);
    float mh = static_cast<float>(monitor.rect.bottom - monitor.rect.top);

    auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex, globalOptions.ignoreOptions);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }

    infos.push_back({x, y, w, h, mx, my, mw, mh, cellIds});
  }

  winapi::log_windows_per_monitor(globalOptions.ignoreOptions);
  run_raylib_ui_multi_cluster(infos, optionsProvider);
}

void runUiTestMulti(const UiTestMultiCommand& cmd, GlobalOptionsProvider& optionsProvider) {
  std::vector<cells::ClusterInitInfo> infos;

  if (cmd.clusters.empty()) {
    // Default: two monitors side by side (monitor bounds = workspace bounds for UI test)
    infos.push_back({0.0f, 0.0f, 1920.0f, 1080.0f, 0.0f, 0.0f, 1920.0f, 1080.0f, {}});
    infos.push_back({1920.0f, 0.0f, 1920.0f, 1080.0f, 1920.0f, 0.0f, 1920.0f, 1080.0f, {}});
  } else {
    for (const auto& cluster : cmd.clusters) {
      // monitor bounds = workspace bounds for UI test
      infos.push_back({cluster.x,
                       cluster.y,
                       cluster.width,
                       cluster.height,
                       cluster.x,
                       cluster.y,
                       cluster.width,
                       cluster.height,
                       {}});
    }
  }

  run_raylib_ui_multi_cluster(infos, optionsProvider);
}

int main(int argc, char* argv[]) {
  // Set DPI awareness before any Windows API calls that return coordinates
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Flush spdlog on info-level messages to ensure immediate output
  spdlog::flush_on(spdlog::level::info);

  // Parse command-line arguments
  auto result = parse_args(argc, argv);
  if (!result.success) {
    spdlog::error("{}", result.error);
    return 1;
  }

  // Apply log level if specified
  if (result.args.options.log_level) {
    applyLogLevel(*result.args.options.log_level);
  }

  // Log version at startup
  spdlog::info("win-tiler v{}", get_version_string());

  // Get default global options
  auto globalOptions = get_default_global_options();

  // Determine config path to load
  std::filesystem::path configPath;
  bool configExplicitlySpecified = false;

  if (result.args.options.config_path) {
    configPath = *result.args.options.config_path;
    configExplicitlySpecified = true;
  } else {
    configPath = getDefaultConfigPath();
  }

  // Load config
  if (configExplicitlySpecified || std::filesystem::exists(configPath)) {
    auto loaded = read_options_toml(configPath);
    if (loaded.has_value()) {
      globalOptions = loaded.value();
      spdlog::info("Loaded config from: {}", configPath.string());
    } else {
      if (configExplicitlySpecified) {
        // Explicit config path failed - error out
        spdlog::error("Failed to load config: {}", loaded.error());
        return 1;
      }
      // Default config failed to load - just use defaults silently
      spdlog::debug("Default config not loaded: {}", loaded.error());
    }
  }

  // Create GlobalOptionsProvider for commands that support hot-reload
  std::optional<std::filesystem::path> providerPath;
  if (configExplicitlySpecified || std::filesystem::exists(configPath)) {
    providerPath = configPath;
  }
  GlobalOptionsProvider optionsProvider(providerPath);

  // Dispatch command
  if (result.args.command) {
    std::visit(overloaded{
                   [](const HelpCommand&) { print_usage(); },
                   [](const VersionCommand&) {
                     std::cout << "win-tiler v" << get_version_string() << std::endl;
                   },
                   [&](const LoopCommand&) { run_loop_mode(optionsProvider); },
                   [&](const UiTestMonitorCommand&) { runUiTestMonitor(optionsProvider); },
                   [&](const UiTestMultiCommand& cmd) { runUiTestMulti(cmd, optionsProvider); },
                   [&](const TrackWindowsCommand&) { run_track_windows_mode(optionsProvider); },
                   [](const InitConfigCommand& cmd) {
                     auto targetPath = cmd.filepath ? std::filesystem::path(*cmd.filepath)
                                                    : getDefaultConfigPath();
                     auto writeResult =
                         write_options_toml(get_default_global_options(), targetPath);
                     if (writeResult.has_value()) {
                       spdlog::info("Config written to: {}", targetPath.string());
                     } else {
                       spdlog::error("Failed to write config: {}", writeResult.error());
                     }
                   },
               },
               *result.args.command);
    return 0;
  }

  // No command specified - show windows per monitor
  winapi::log_windows_per_monitor(globalOptions.ignoreOptions);
  return 0;
}

#endif
