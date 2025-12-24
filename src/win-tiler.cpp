#ifdef DOCTEST_CONFIG_DISABLE

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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

// Helper: Print tile layout from a multi-cluster system
void printTileLayout(const multi_cell_logic::System& system,
                     const std::unordered_map<size_t, std::string>& hwndToTitle) {
  size_t totalWindows = multi_cell_logic::countTotalLeaves(system);
  spdlog::info("Total windows: {}", totalWindows);

  for (const auto& pc : system.clusters) {
    spdlog::info("--- Monitor {} ---", pc.id);

    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
      if (cell.isDead || !cell.leafId.has_value()) {
        continue;
      }

      size_t hwndValue = *cell.leafId;
      cell_logic::Rect globalRect = multi_cell_logic::getCellGlobalRect(pc, i);

      std::string title;
      auto titleIt = hwndToTitle.find(hwndValue);
      if (titleIt != hwndToTitle.end()) {
        title = titleIt->second;
      }

      spdlog::info("  Window: \"{}\"", title);
      spdlog::info("    Position: x={}, y={}", static_cast<int>(globalRect.x),
                   static_cast<int>(globalRect.y));
      spdlog::info("    Size: {}x{}", static_cast<int>(globalRect.width),
                   static_cast<int>(globalRect.height));
    }
  }
}

// Helper: Gather current window state for all monitors
std::vector<multi_cell_logic::ClusterCellIds> gatherCurrentWindowState() {
  std::vector<multi_cell_logic::ClusterCellIds> result;
  auto monitors = winapi::get_monitors();

  for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
    auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }
    result.push_back({monitorIndex, cellIds});
  }

  return result;
}

// Helper: Apply tile layout by updating window positions
void applyTileLayout(const multi_cell_logic::System& system) {
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
      if (cell.isDead || !cell.leafId.has_value()) {
        continue;
      }

      size_t hwndValue = *cell.leafId;
      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(hwndValue);
      cell_logic::Rect globalRect = multi_cell_logic::getCellGlobalRect(pc, i);

      winapi::WindowPosition pos;
      pos.x = static_cast<int>(globalRect.x);
      pos.y = static_cast<int>(globalRect.y);
      pos.width = static_cast<int>(globalRect.width);
      pos.height = static_cast<int>(globalRect.height);

      winapi::TileInfo tileInfo{hwnd, pos};
      winapi::update_window_position(tileInfo);
    }
  }
}

bool isHwndInSystem(const multi_cell_logic::System& system, winapi::HWND_T hwnd) {
  size_t hwndValue = reinterpret_cast<size_t>(hwnd);
  for (const auto& pc : system.clusters) {
    if (multi_cell_logic::findCellByLeafId(pc.cluster, hwndValue).has_value()) {
      return true;
    }
  }
  return false;
}

void runLoopTestMode() {
  // 1. Build initial state (like computeTileLayout)
  auto monitors = winapi::get_monitors();

  // Build hwnd -> title lookup
  auto windowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
  std::unordered_map<size_t, std::string> hwndToTitle;
  for (const auto& info : windowInfos) {
    hwndToTitle[reinterpret_cast<size_t>(info.handle)] = info.title;
  }

  // Create cluster init info
  std::vector<multi_cell_logic::ClusterInitInfo> clusterInfos;
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& monitor = monitors[i];
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    auto hwnds = winapi::get_hwnds_for_monitor(i);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }

    clusterInfos.push_back({i, x, y, w, h, cellIds});
  }

  auto system = multi_cell_logic::createSystem(clusterInfos);

  // 2. Print initial layout
  spdlog::info("=== Initial Tile Layout ===");
  printTileLayout(system, hwndToTitle);

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Re-gather window state
    auto currentState = gatherCurrentWindowState();

    // Update title lookup for any new windows
    auto newWindowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
    for (const auto& info : newWindowInfos) {
      size_t key = reinterpret_cast<size_t>(info.handle);
      if (hwndToTitle.find(key) == hwndToTitle.end()) {
        hwndToTitle[key] = info.title;
      }
    }

    // Use updateSystem to sync
    auto result = multi_cell_logic::updateSystem(system, currentState, std::nullopt);

    // If changes detected, log them
    if (!result.deletedLeafIds.empty() || !result.addedLeafIds.empty()) {
      spdlog::info("=== Window Changes Detected ===");

      if (!result.deletedLeafIds.empty()) {
        spdlog::info("Removed windows: {}", result.deletedLeafIds.size());
        for (size_t id : result.deletedLeafIds) {
          auto titleIt = hwndToTitle.find(id);
          if (titleIt != hwndToTitle.end()) {
            spdlog::info("  - \"{}\"", titleIt->second);
          }
        }
      }
      if (!result.addedLeafIds.empty()) {
        spdlog::info("Added windows: {}", result.addedLeafIds.size());
        for (size_t id : result.addedLeafIds) {
          auto titleIt = hwndToTitle.find(id);
          if (titleIt != hwndToTitle.end()) {
            spdlog::info("  + \"{}\"", titleIt->second);
          }
        }
      }

      spdlog::info("=== Updated Tile Layout ===");
      printTileLayout(system, hwndToTitle);
    }
  }
}

void runLoopMode() {
  auto initStart = std::chrono::high_resolution_clock::now();

  // 1. Build initial state (like computeTileLayout)
  auto monitorsStart = std::chrono::high_resolution_clock::now();
  auto monitors = winapi::get_monitors();
  auto monitorsEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace(
      "get_monitors: {}us",
      std::chrono::duration_cast<std::chrono::microseconds>(monitorsEnd - monitorsStart).count());

  // Build hwnd -> title lookup
  auto windowDataStart = std::chrono::high_resolution_clock::now();
  auto windowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
  std::unordered_map<size_t, std::string> hwndToTitle;
  for (const auto& info : windowInfos) {
    hwndToTitle[reinterpret_cast<size_t>(info.handle)] = info.title;
  }
  auto windowDataEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace(
      "gather_raw_window_data + build lookup: {}us",
      std::chrono::duration_cast<std::chrono::microseconds>(windowDataEnd - windowDataStart)
          .count());

  // Create cluster init info
  auto clusterInfoStart = std::chrono::high_resolution_clock::now();
  std::vector<multi_cell_logic::ClusterInitInfo> clusterInfos;
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& monitor = monitors[i];
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    auto hwnds = winapi::get_hwnds_for_monitor(i);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }

    clusterInfos.push_back({i, x, y, w, h, cellIds});
  }
  auto clusterInfoEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace("build cluster infos: {}us", std::chrono::duration_cast<std::chrono::microseconds>(
                                                 clusterInfoEnd - clusterInfoStart)
                                                 .count());

  auto createSystemStart = std::chrono::high_resolution_clock::now();
  auto system = multi_cell_logic::createSystem(clusterInfos);
  auto createSystemEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace("createSystem: {}us", std::chrono::duration_cast<std::chrono::microseconds>(
                                          createSystemEnd - createSystemStart)
                                          .count());

  auto initEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace("total initialization: {}us",
                std::chrono::duration_cast<std::chrono::microseconds>(initEnd - initStart).count());

  // 2. Print initial layout and apply
  spdlog::info("=== Initial Tile Layout ===");
  printTileLayout(system, hwndToTitle);

  auto applyStart = std::chrono::high_resolution_clock::now();
  applyTileLayout(system);
  auto applyEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace(
      "initial applyTileLayout: {}us",
      std::chrono::duration_cast<std::chrono::microseconds>(applyEnd - applyStart).count());

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto loopStart = std::chrono::high_resolution_clock::now();

    // Re-gather window state
    auto gatherStateStart = std::chrono::high_resolution_clock::now();
    auto currentState = gatherCurrentWindowState();
    auto gatherStateEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "gatherCurrentWindowState: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(gatherStateEnd - gatherStateStart)
            .count());

    // Update title lookup for any new windows
    auto titleLookupStart = std::chrono::high_resolution_clock::now();
    auto newWindowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
    for (const auto& info : newWindowInfos) {
      size_t key = reinterpret_cast<size_t>(info.handle);
      if (hwndToTitle.find(key) == hwndToTitle.end()) {
        hwndToTitle[key] = info.title;
      }
    }
    auto titleLookupEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "update title lookup: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(titleLookupEnd - titleLookupStart)
            .count());

    // Use updateSystem to sync
    auto updateSystemStart = std::chrono::high_resolution_clock::now();
    auto result = multi_cell_logic::updateSystem(system, currentState, std::nullopt);
    auto updateSystemEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace("updateSystem: {}us", std::chrono::duration_cast<std::chrono::microseconds>(
                                            updateSystemEnd - updateSystemStart)
                                            .count());

    // === Foreground/Selection Update Logic ===
    auto foregroundHwnd = winapi::get_foreground_window();

    if (foregroundHwnd != nullptr && isHwndInSystem(system, foregroundHwnd)) {
      auto cursorPosOpt = winapi::get_cursor_pos();
      if (!cursorPosOpt.has_value()) {
        continue; // Skip this iteration if cursor pos unavailable
      }
      float cursorX = static_cast<float>(cursorPosOpt->x);
      float cursorY = static_cast<float>(cursorPosOpt->y);

      auto cellAtCursor = multi_cell_logic::findCellAtPoint(system, cursorX, cursorY);

      if (cellAtCursor.has_value()) {
        auto [clusterId, cellIndex] = *cellAtCursor;

        bool needsUpdate = !system.selection.has_value() ||
                           system.selection->clusterId != clusterId ||
                           system.selection->cellIndex != cellIndex;

        if (needsUpdate) {
          system.selection = multi_cell_logic::Selection{clusterId, cellIndex};

          const auto* pc = multi_cell_logic::getCluster(system, clusterId);
          if (pc != nullptr) {
            const auto& cell = pc->cluster.cells[static_cast<size_t>(cellIndex)];
            if (cell.leafId.has_value()) {
              winapi::HWND_T cellHwnd = reinterpret_cast<winapi::HWND_T>(*cell.leafId);
              if (!winapi::set_foreground_window(cellHwnd)) {
                spdlog::error("Failed to set foreground window for HWND {}", cellHwnd);
              }
              spdlog::trace("======================Selection updated: cluster={}, cell={}",
                            clusterId, cellIndex);
            }
          }
        }
      }
    }

    // If changes detected, log and apply
    if (!result.deletedLeafIds.empty() || !result.addedLeafIds.empty()) {
      spdlog::info("=== Window Changes Detected ===");

      if (!result.deletedLeafIds.empty()) {
        spdlog::info("Removed windows: {}", result.deletedLeafIds.size());
        for (size_t id : result.deletedLeafIds) {
          auto titleIt = hwndToTitle.find(id);
          if (titleIt != hwndToTitle.end()) {
            spdlog::info("  - \"{}\"", titleIt->second);
          }
        }
      }
      if (!result.addedLeafIds.empty()) {
        spdlog::info("Added windows: {}", result.addedLeafIds.size());
        for (size_t id : result.addedLeafIds) {
          auto titleIt = hwndToTitle.find(id);
          if (titleIt != hwndToTitle.end()) {
            spdlog::info("  + \"{}\"", titleIt->second);
          }
        }
      }

      spdlog::info("=== Updated Tile Layout ===");
      printTileLayout(system, hwndToTitle);
    }

    auto applyLayoutStart = std::chrono::high_resolution_clock::now();
    applyTileLayout(system);
    auto applyLayoutEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace("applyTileLayout: {}us", std::chrono::duration_cast<std::chrono::microseconds>(
                                               applyLayoutEnd - applyLayoutStart)
                                               .count());

    auto loopEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart).count());
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