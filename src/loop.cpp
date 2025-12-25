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

// Helper to time a function and log its duration
template <typename F>
auto timed(const char* name, F&& func) {
  auto start = std::chrono::high_resolution_clock::now();
  auto result = func();
  auto end = std::chrono::high_resolution_clock::now();
  spdlog::trace(
      "{}: {}us", name,
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return result;
}

// Helper for void functions
template <typename F>
void timedVoid(const char* name, F&& func) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto end = std::chrono::high_resolution_clock::now();
  spdlog::trace(
      "{}: {}us", name,
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

// Hotkey IDs for vim-style navigation
constexpr int HOTKEY_ID_LEFT = 1;   // Win+Shift+H
constexpr int HOTKEY_ID_DOWN = 2;   // Win+Shift+J
constexpr int HOTKEY_ID_UP = 3;     // Win+Shift+K
constexpr int HOTKEY_ID_RIGHT = 4;  // Win+Shift+L

bool registerNavigationHotkeys() {
  auto hotkeyLeft = winapi::create_hotkey("super+shift+h", HOTKEY_ID_LEFT);
  auto hotkeyDown = winapi::create_hotkey("super+shift+j", HOTKEY_ID_DOWN);
  auto hotkeyUp = winapi::create_hotkey("super+shift+k", HOTKEY_ID_UP);
  auto hotkeyRight = winapi::create_hotkey("super+shift+l", HOTKEY_ID_RIGHT);

  if (!hotkeyLeft || !hotkeyDown || !hotkeyUp || !hotkeyRight) {
    spdlog::error("Failed to create hotkey info");
    return false;
  }

  bool success = true;
  if (!winapi::register_hotkey(*hotkeyLeft)) {
    spdlog::error("Failed to register hotkey Win+Shift+H");
    success = false;
  }
  if (!winapi::register_hotkey(*hotkeyDown)) {
    spdlog::error("Failed to register hotkey Win+Shift+J");
    success = false;
  }
  if (!winapi::register_hotkey(*hotkeyUp)) {
    spdlog::error("Failed to register hotkey Win+Shift+K");
    success = false;
  }
  if (!winapi::register_hotkey(*hotkeyRight)) {
    spdlog::error("Failed to register hotkey Win+Shift+L");
    success = false;
  }

  if (success) {
    spdlog::info("Registered navigation hotkeys: Win+Shift+H/J/K/L");
  }

  return success;
}

void unregisterNavigationHotkeys() {
  winapi::unregister_hotkey(HOTKEY_ID_LEFT);
  winapi::unregister_hotkey(HOTKEY_ID_DOWN);
  winapi::unregister_hotkey(HOTKEY_ID_UP);
  winapi::unregister_hotkey(HOTKEY_ID_RIGHT);
}

// Convert hotkey ID to direction
std::optional<cell_logic::Direction> hotkeyIdToDirection(int hotkeyId) {
  switch (hotkeyId) {
    case HOTKEY_ID_LEFT:
      return cell_logic::Direction::Left;
    case HOTKEY_ID_DOWN:
      return cell_logic::Direction::Down;
    case HOTKEY_ID_UP:
      return cell_logic::Direction::Up;
    case HOTKEY_ID_RIGHT:
      return cell_logic::Direction::Right;
    default:
      return std::nullopt;
  }
}

// Handle keyboard navigation: move selection, set foreground, move mouse to center
void handleKeyboardNavigation(multi_cell_logic::System& system, cell_logic::Direction dir) {
  // Try to move selection in the given direction
  if (!multi_cell_logic::moveSelection(system, dir)) {
    spdlog::trace("Cannot move selection in direction");
    return;
  }

  // Get the newly selected cell
  auto selectedCell = multi_cell_logic::getSelectedCell(system);
  if (!selectedCell.has_value()) {
    spdlog::error("No cell selected after moveSelection");
    return;
  }

  auto [clusterId, cellIndex] = *selectedCell;
  const auto* pc = multi_cell_logic::getCluster(system, clusterId);
  if (pc == nullptr) {
    spdlog::error("Failed to get cluster {}", clusterId);
    return;
  }

  const auto& cell = pc->cluster.cells[static_cast<size_t>(cellIndex)];
  if (!cell.leafId.has_value()) {
    spdlog::error("Selected cell has no leafId");
    return;
  }

  // Get the window handle
  winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(*cell.leafId);

  // Set it as the foreground window
  if (!winapi::set_foreground_window(hwnd)) {
    spdlog::error("Failed to set foreground window");
    return;
  }

  // Get the cell's global rect and move mouse to center
  cell_logic::Rect globalRect = multi_cell_logic::getCellGlobalRect(*pc, cellIndex);
  long centerX = static_cast<long>(globalRect.x + globalRect.width / 2.0f);
  long centerY = static_cast<long>(globalRect.y + globalRect.height / 2.0f);

  if (!winapi::set_cursor_pos(centerX, centerY)) {
    spdlog::error("Failed to set cursor position");
    return;
  }

  spdlog::trace("Navigated to cell {} in cluster {}", cellIndex, clusterId);
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
  auto monitors = timed("get_monitors", [] { return winapi::get_monitors(); });

  // Build hwnd -> title lookup
  auto hwndToTitle = timed("gather_raw_window_data + build lookup", [] {
    auto windowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
    std::unordered_map<size_t, std::string> result;
    for (const auto& info : windowInfos) {
      result[reinterpret_cast<size_t>(info.handle)] = info.title;
    }
    return result;
  });

  // Create cluster init info
  auto clusterInfos = timed("build cluster infos", [&monitors] {
    std::vector<multi_cell_logic::ClusterInitInfo> result;
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

      result.push_back({i, x, y, w, h, cellIds});
    }
    return result;
  });

  auto system = timed("createSystem", [&clusterInfos] {
    return multi_cell_logic::createSystem(clusterInfos);
  });

  auto initEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace("total initialization: {}us",
                std::chrono::duration_cast<std::chrono::microseconds>(initEnd - initStart).count());

  // 2. Print initial layout and apply
  spdlog::info("=== Initial Tile Layout ===");
  printTileLayout(system, hwndToTitle);

  timedVoid("initial applyTileLayout", [&system] { applyTileLayout(system); });

  // Register keyboard hotkeys
  if (!registerNavigationHotkeys()) {
    spdlog::warn("Some hotkeys failed to register - keyboard navigation may not work");
  }

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto loopStart = std::chrono::high_resolution_clock::now();

    // Check for keyboard hotkeys
    if (auto hotkeyId = winapi::check_keyboard_action()) {
      if (auto dir = hotkeyIdToDirection(*hotkeyId)) {
        handleKeyboardNavigation(system, *dir);
      }
    }

    // Re-gather window state
    auto currentState = timed("gatherCurrentWindowState", [] {
      return gatherCurrentWindowState();
    });

    // Update title lookup for any new windows
    timedVoid("update title lookup", [&hwndToTitle] {
      auto newWindowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
      for (const auto& info : newWindowInfos) {
        size_t key = reinterpret_cast<size_t>(info.handle);
        if (hwndToTitle.find(key) == hwndToTitle.end()) {
          hwndToTitle[key] = info.title;
        }
      }
    });

    // Use updateSystem to sync
    auto result = timed("updateSystem", [&system, &currentState] {
      return multi_cell_logic::updateSystem(system, currentState, std::nullopt);
    });

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

    timedVoid("applyTileLayout", [&system] { applyTileLayout(system); });

    auto loopEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart).count());
  }
}

}  // namespace wintiler
