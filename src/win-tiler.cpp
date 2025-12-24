#ifdef DOCTEST_CONFIG_DISABLE

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "loop.h"
#include "multi_cells.h"
#include "multi_ui.h"
#include "process.h"
#include "winapi.h"

using namespace wintiler;

const size_t CELL_ID_START = 10;

struct TileResult {
  winapi::HWND_T hwnd;
  winapi::WindowPosition position;
  std::string windowTitle;
  size_t monitorIndex;
};

std::vector<TileResult> computeTileLayout() {
  std::vector<TileResult> results;
  auto monitors = winapi::get_monitors();

  // Build window title lookup
  auto windowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
  std::unordered_map<size_t, std::string> hwndToTitle;
  for (const auto& info : windowInfos) {
    hwndToTitle[reinterpret_cast<size_t>(info.handle)] = info.title;
  }

  // Build cluster infos for all monitors
  std::vector<multi_cell_logic::ClusterInitInfo> clusterInfos;
  for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
    const auto& monitor = monitors[monitorIndex];

    // Get workArea bounds
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    // Get HWNDs for this monitor - these become the leafIds
    auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }

    clusterInfos.push_back({monitorIndex, x, y, w, h, cellIds});
  }

  // Create multi-cluster system
  auto system = multi_cell_logic::createSystem(clusterInfos);

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
      cell_logic::Rect globalRect =
          multi_cell_logic::getCellGlobalRect(pc, static_cast<int>(&cell - &pc.cluster.cells[0]));

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

void runApplyMode() {
  auto tiles = computeTileLayout();
  for (const auto& tile : tiles) {
    winapi::TileInfo tileInfo{tile.hwnd, tile.position};
    winapi::update_window_position(tileInfo);
  }
}

void runApplyTestMode() {
  auto tiles = computeTileLayout();

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

int main(int argc, char* argv[]) {
  // Flush spdlog on info-level messages to ensure immediate output
  spdlog::flush_on(spdlog::level::info);

  // Parse optional logging level argument (must be first)
  int argStart = 1;
  if (argc > 1) {
    std::string firstArg = argv[1];
    if (firstArg.rfind("logging:", 0) == 0) {
      std::string level = firstArg.substr(8);
      if (level == "trace") {
        spdlog::set_level(spdlog::level::trace);
      } else if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
      } else if (level == "info") {
        spdlog::set_level(spdlog::level::info);
      } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
      } else if (level == "err") {
        spdlog::set_level(spdlog::level::err);
      } else if (level == "off") {
        spdlog::set_level(spdlog::level::off);
      }
      argStart = 2;
    }
  }

  for (int i = argStart; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "apply") {
      runApplyMode();
      return 0;
    }

    if (arg == "apply-test") {
      runApplyTestMode();
      return 0;
    }

    if (arg == "loop-test") {
      runLoopTestMode();
      return 0;
    }

    if (arg == "loop") {
      runLoopMode();
      return 0;
    }

    if (arg == "ui-test-monitor") {
      auto monitors = winapi::get_monitors();
      std::vector<multi_cell_logic::ClusterInitInfo> infos;

      for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
        const auto& monitor = monitors[monitorIndex];

        // Get workArea bounds for this monitor
        float x = static_cast<float>(monitor.workArea.left);
        float y = static_cast<float>(monitor.workArea.top);
        float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
        float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

        // Get HWNDs for this monitor and convert to cell IDs
        auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex);
        std::vector<size_t> cellIds;
        for (auto hwnd : hwnds) {
          cellIds.push_back(reinterpret_cast<size_t>(hwnd));
        }

        infos.push_back({monitorIndex, x, y, w, h, cellIds});
      }

      winapi::log_windows_per_monitor();
      runRaylibUIMultiCluster(infos);
      return 0;
    }

    if (arg == "ui-test-multi") {
      std::vector<multi_cell_logic::ClusterInitInfo> infos;

      // Collect remaining arguments after "ui-test-multi"
      int remaining = argc - (i + 1);

      if (remaining == 0) {
        // Default: two monitors side by side
        infos.push_back({0, 0.0f, 0.0f, 1920.0f, 1080.0f, {}});
        infos.push_back({1, 1920.0f, 0.0f, 1920.0f, 1080.0f, {}});
      } else if (remaining % 4 != 0) {
        spdlog::error("ui-test-multi requires 4 numbers per cluster (x y width height)");
        spdlog::error("Usage: ui-test-multi [x1 y1 w1 h1] [x2 y2 w2 h2] ...");
        spdlog::error("Got {} arguments, which is not a multiple of 4", remaining);
        return 1;
      } else {
        // Parse clusters from arguments
        size_t clusterId = 0;
        for (int j = i + 1; j + 3 < argc; j += 4) {
          float x = std::stof(argv[j]);
          float y = std::stof(argv[j + 1]);
          float w = std::stof(argv[j + 2]);
          float h = std::stof(argv[j + 3]);
          infos.push_back({clusterId++, x, y, w, h, {}});
        }
      }

      runRaylibUIMultiCluster(infos);
      return 0;
    }
  }

  winapi::log_windows_per_monitor();
  return 0;
}

#endif