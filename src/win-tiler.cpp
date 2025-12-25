#ifdef DOCTEST_CONFIG_DISABLE

#include <spdlog/spdlog.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "argument_parser.h"
#include "loop.h"
#include "multi_cells.h"
#include "multi_ui.h"
#include "options.h"
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

}  // namespace

// Helper for std::visit with lambdas
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

using namespace wintiler;

const size_t CELL_ID_START = 10;

struct TileResult {
  winapi::HWND_T hwnd;
  winapi::WindowPosition position;
  std::string windowTitle;
  size_t monitorIndex;
};

std::vector<TileResult> computeTileLayout(const GlobalOptions& globalOptions) {
  const auto& ignoreOptions = globalOptions.ignoreOptions;
  std::vector<TileResult> results;
  auto monitors = winapi::get_monitors();

  // Build window title lookup
  auto windowInfos = winapi::gather_raw_window_data(ignoreOptions);
  std::unordered_map<size_t, std::string> hwndToTitle;
  for (const auto& info : windowInfos) {
    hwndToTitle[reinterpret_cast<size_t>(info.handle)] = info.title;
  }

  // Build cluster infos for all monitors
  std::vector<cells::ClusterInitInfo> clusterInfos;
  for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
    const auto& monitor = monitors[monitorIndex];

    // Get workArea bounds
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    // Get HWNDs for this monitor - these become the leafIds
    auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex, ignoreOptions);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }

    clusterInfos.push_back({monitorIndex, x, y, w, h, cellIds});
  }

  // Create multi-cluster system
  auto system = cells::createSystem(clusterInfos);
  system.gapHorizontal = globalOptions.gapOptions.horizontal;
  system.gapVertical = globalOptions.gapOptions.vertical;

  // Collect tile results from all clusters
  for (const auto& pc : system.clusters) {
    for (const auto& cell : pc.cluster.cells) {
      if (cell.isDead || !cell.leafId.has_value()) {
        continue;
      }

      // leafId is the HWND (passed as initialCellId)
      size_t hwndValue = *cell.leafId;
      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(hwndValue);

      // Get global rect and convert to window position
      cells::Rect globalRect =
          cells::getCellGlobalRect(pc, static_cast<int>(&cell - &pc.cluster.cells[0]));

      winapi::WindowPosition pos;
      pos.x = static_cast<int>(globalRect.x);
      pos.y = static_cast<int>(globalRect.y);
      pos.width = static_cast<int>(globalRect.width);
      pos.height = static_cast<int>(globalRect.height);

      // Get window title
      std::string title;
      auto titleIt = hwndToTitle.find(hwndValue);
      if (titleIt != hwndToTitle.end()) {
        title = titleIt->second;
      }

      results.push_back({hwnd, pos, title, pc.id});
    }
  }

  return results;
}

void runApplyMode(const GlobalOptions& globalOptions) {
  auto tiles = computeTileLayout(globalOptions);
  for (const auto& tile : tiles) {
    winapi::TileInfo tileInfo{tile.hwnd, tile.position};
    winapi::update_window_position(tileInfo);
  }
}

void runApplyTestMode(const GlobalOptions& globalOptions) {
  auto tiles = computeTileLayout(globalOptions);

  if (tiles.empty()) {
    spdlog::info("No windows to tile.");
    return;
  }

  spdlog::info("=== Tile Layout Preview ===");
  spdlog::info("Total windows: {}", tiles.size());

  size_t currentMonitor = SIZE_MAX;
  for (const auto& tile : tiles) {
    if (tile.monitorIndex != currentMonitor) {
      currentMonitor = tile.monitorIndex;
      spdlog::info("--- Monitor {} ---", currentMonitor);
    }

    spdlog::info("  Window: \"{}\"", tile.windowTitle);
    spdlog::info("    Position: x={}, y={}", tile.position.x, tile.position.y);
    spdlog::info("    Size: {}x{}", tile.position.width, tile.position.height);
  }
}

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

void runUiTestMonitor(const GlobalOptions& globalOptions) {
  auto monitors = winapi::get_monitors();
  std::vector<cells::ClusterInitInfo> infos;

  for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
    const auto& monitor = monitors[monitorIndex];

    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex, globalOptions.ignoreOptions);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }

    infos.push_back({monitorIndex, x, y, w, h, cellIds});
  }

  winapi::log_windows_per_monitor(globalOptions.ignoreOptions);
  runRaylibUIMultiCluster(infos, globalOptions.gapOptions);
}

void runTrackWindowsMode(const IgnoreOptions& ignoreOptions) {
  while (true) {
    auto monitors = winapi::get_monitors();
    for (size_t i = 0; i < monitors.size(); ++i) {
      auto hwnds = winapi::get_hwnds_for_monitor(i, ignoreOptions);
      spdlog::info("--- Monitor {} ({} windows) ---", i, hwnds.size());
      for (auto hwnd : hwnds) {
        auto info = winapi::get_window_info(hwnd);
        spdlog::info("  HWND: {}, PID: {}, Process: {}, Title: {}", info.handle,
                     info.pid.value_or(0), info.processName, info.title);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

void runUiTestMulti(const UiTestMultiCommand& cmd) {
  std::vector<cells::ClusterInitInfo> infos;

  if (cmd.clusters.empty()) {
    // Default: two monitors side by side
    infos.push_back({0, 0.0f, 0.0f, 1920.0f, 1080.0f, {}});
    infos.push_back({1, 1920.0f, 0.0f, 1920.0f, 1080.0f, {}});
  } else {
    size_t clusterId = 0;
    for (const auto& cluster : cmd.clusters) {
      infos.push_back({clusterId++, cluster.x, cluster.y, cluster.width, cluster.height, {}});
    }
  }

  runRaylibUIMultiCluster(infos);
}

int main(int argc, char* argv[]) {
  // Flush spdlog on info-level messages to ensure immediate output
  spdlog::flush_on(spdlog::level::info);

  // Parse command-line arguments
  auto result = parseArgs(argc, argv);
  if (!result.success) {
    spdlog::error("{}", result.error);
    return 1;
  }

  // Apply log level if specified
  if (result.args.options.logLevel) {
    applyLogLevel(*result.args.options.logLevel);
  }

  // Get default global options
  auto globalOptions = get_default_global_options();

  // Determine config path to load
  std::filesystem::path configPath;
  bool configExplicitlySpecified = false;

  if (result.args.options.configPath) {
    configPath = *result.args.options.configPath;
    configExplicitlySpecified = true;
  } else {
    configPath = getDefaultConfigPath();
  }

  // Load config
  if (configExplicitlySpecified || std::filesystem::exists(configPath)) {
    auto loaded = read_options_toml(configPath);
    if (loaded.success) {
      globalOptions = loaded.options;
      spdlog::info("Loaded config from: {}", configPath.string());
    } else {
      if (configExplicitlySpecified) {
        // Explicit config path failed - error out
        spdlog::error("Failed to load config: {}", loaded.error);
        return 1;
      }
      // Default config failed to load - just use defaults silently
      spdlog::debug("Default config not loaded: {}", loaded.error);
    }
  }

  // Dispatch command
  if (result.args.command) {
    std::visit(
        overloaded{
            [](const HelpCommand&) { printUsage(); },
            [&](const ApplyCommand&) { runApplyMode(globalOptions); },
            [&](const ApplyTestCommand&) { runApplyTestMode(globalOptions); },
            [&](const LoopCommand&) { runLoopMode(globalOptions); },
            [&](const LoopTestCommand&) { runLoopTestMode(globalOptions); },
            [&](const UiTestMonitorCommand&) { runUiTestMonitor(globalOptions); },
            [](const UiTestMultiCommand& cmd) { runUiTestMulti(cmd); },
            [&](const TrackWindowsCommand&) { runTrackWindowsMode(globalOptions.ignoreOptions); },
            [](const InitConfigCommand& cmd) {
              auto targetPath = cmd.filepath
                  ? std::filesystem::path(*cmd.filepath)
                  : getDefaultConfigPath();
              auto writeResult = write_options_toml(get_default_global_options(), targetPath);
              if (writeResult.success) {
                spdlog::info("Config written to: {}", targetPath.string());
              } else {
                spdlog::error("Failed to write config: {}", writeResult.error);
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