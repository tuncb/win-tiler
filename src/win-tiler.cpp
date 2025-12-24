#ifdef DOCTEST_CONFIG_DISABLE

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cells.h"
#include "multi_cells.h"
#include "multi_ui.h"
#include "process.h"
#include "raylib.h"
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
      cell_logic::Rect globalRect = multi_cell_logic::getCellGlobalRect(pc,
          static_cast<int>(&cell - &pc.cluster.cells[0]));

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
    std::cout << "No windows to tile." << std::endl;
    return;
  }

  std::cout << "=== Tile Layout Preview ===" << std::endl;
  std::cout << "Total windows: " << tiles.size() << std::endl;
  std::cout << std::endl;

  size_t currentMonitor = SIZE_MAX;
  for (const auto& tile : tiles) {
    if (tile.monitorIndex != currentMonitor) {
      currentMonitor = tile.monitorIndex;
      std::cout << "--- Monitor " << currentMonitor << " ---" << std::endl;
    }

    std::cout << "  Window: \"" << tile.windowTitle << "\"" << std::endl;
    std::cout << "    Position: x=" << tile.position.x << ", y=" << tile.position.y << std::endl;
    std::cout << "    Size: " << tile.position.width << "x" << tile.position.height << std::endl;
    std::cout << std::endl;
  }
}

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "apply") {
      runApplyMode();
      return 0;
    }

    if (arg == "apply-test") {
      runApplyTestMode();
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
        std::cerr << "Error: ui-test-multi requires 4 numbers per cluster (x y width height)"
                  << std::endl;
        std::cerr << "Usage: ui-test-multi [x1 y1 w1 h1] [x2 y2 w2 h2] ..." << std::endl;
        std::cerr << "Got " << remaining << " arguments, which is not a multiple of 4" << std::endl;
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