#include "loop.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <thread>
#include <unordered_map>
#include <vector>

#include "multi_cells.h"
#include "winapi.h"

namespace wintiler {

namespace {

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

}  // namespace

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
        continue;  // Skip this iteration if cursor pos unavailable
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

}  // namespace wintiler
