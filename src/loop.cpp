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
  spdlog::trace("{}: {}us", name,
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
  return result;
}

// Helper for void functions
template <typename F>
void timedVoid(const char* name, F&& func) {
  auto start = std::chrono::high_resolution_clock::now();
  func();
  auto end = std::chrono::high_resolution_clock::now();
  spdlog::trace("{}: {}us", name,
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

// Hotkey IDs for vim-style navigation
constexpr int HOTKEY_ID_LEFT = 1;          // Win+Shift+H
constexpr int HOTKEY_ID_DOWN = 2;          // Win+Shift+J
constexpr int HOTKEY_ID_UP = 3;            // Win+Shift+K
constexpr int HOTKEY_ID_RIGHT = 4;         // Win+Shift+L
constexpr int HOTKEY_ID_TOGGLE_SPLIT = 5;  // Win+Shift+Y
constexpr int HOTKEY_ID_EXIT = 6;          // Win+Shift+ESC
constexpr int HOTKEY_ID_TOGGLE_GLOBAL = 7; // Win+Shift+;
constexpr int HOTKEY_ID_STORE_CELL = 8;    // Win+Shift+[
constexpr int HOTKEY_ID_CLEAR_STORED = 9;  // Win+Shift+]
constexpr int HOTKEY_ID_EXCHANGE = 10;     // Win+Shift+,
constexpr int HOTKEY_ID_MOVE = 11;         // Win+Shift+.

void registerHotkey(const std::string& text, int id) {
  auto hotkey = winapi::create_hotkey(text, id);
  if (hotkey) {
    winapi::register_hotkey(*hotkey);
  }
}

void registerNavigationHotkeys() {
  registerHotkey("super+shift+h", HOTKEY_ID_LEFT);
  registerHotkey("super+shift+j", HOTKEY_ID_DOWN);
  registerHotkey("super+shift+k", HOTKEY_ID_UP);
  registerHotkey("super+shift+l", HOTKEY_ID_RIGHT);
  registerHotkey("super+shift+y", HOTKEY_ID_TOGGLE_SPLIT);
  registerHotkey("super+shift+escape", HOTKEY_ID_EXIT);
  registerHotkey("super+shift+;", HOTKEY_ID_TOGGLE_GLOBAL);
  registerHotkey("super+shift+[", HOTKEY_ID_STORE_CELL);
  registerHotkey("super+shift+]", HOTKEY_ID_CLEAR_STORED);
  registerHotkey("super+shift+,", HOTKEY_ID_EXCHANGE);
  registerHotkey("super+shift+.", HOTKEY_ID_MOVE);
  spdlog::info("Registered hotkeys: Win+Shift+H/J/K/L/Y/ESC/;/[/]/,/.");
}

void unregisterNavigationHotkeys() {
  winapi::unregister_hotkey(HOTKEY_ID_LEFT);
  winapi::unregister_hotkey(HOTKEY_ID_DOWN);
  winapi::unregister_hotkey(HOTKEY_ID_UP);
  winapi::unregister_hotkey(HOTKEY_ID_RIGHT);
  winapi::unregister_hotkey(HOTKEY_ID_TOGGLE_SPLIT);
  winapi::unregister_hotkey(HOTKEY_ID_EXIT);
  winapi::unregister_hotkey(HOTKEY_ID_TOGGLE_GLOBAL);
  winapi::unregister_hotkey(HOTKEY_ID_STORE_CELL);
  winapi::unregister_hotkey(HOTKEY_ID_CLEAR_STORED);
  winapi::unregister_hotkey(HOTKEY_ID_EXCHANGE);
  winapi::unregister_hotkey(HOTKEY_ID_MOVE);
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
  spdlog::debug("Total windows: {}", totalWindows);

  for (const auto& pc : system.clusters) {
    spdlog::debug("--- Monitor {} ---", pc.id);

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

      spdlog::debug("  Window: \"{}\"", title);
      spdlog::debug("    Position: x={}, y={}", static_cast<int>(globalRect.x),
                    static_cast<int>(globalRect.y));
      spdlog::debug("    Size: {}x{}", static_cast<int>(globalRect.width),
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

} // namespace

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

  auto system = timed("createSystem",
                      [&clusterInfos] { return multi_cell_logic::createSystem(clusterInfos); });

  auto initEnd = std::chrono::high_resolution_clock::now();
  spdlog::trace("total initialization: {}us",
                std::chrono::duration_cast<std::chrono::microseconds>(initEnd - initStart).count());

  // 2. Print initial layout and apply
  spdlog::info("=== Initial Tile Layout ===");
  printTileLayout(system, hwndToTitle);

  timedVoid("initial applyTileLayout", [&system] { applyTileLayout(system); });

  // Register keyboard hotkeys
  registerNavigationHotkeys();

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  // Store cell for swap/move operations (clusterId, leafId)
  std::optional<std::pair<multi_cell_logic::ClusterId, size_t>> storedCell;

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto loopStart = std::chrono::high_resolution_clock::now();

    // Check for keyboard hotkeys
    if (auto hotkeyId = winapi::check_keyboard_action()) {
      if (auto dir = hotkeyIdToDirection(*hotkeyId)) {
        handleKeyboardNavigation(system, *dir);
      } else if (*hotkeyId == HOTKEY_ID_TOGGLE_SPLIT) {
        if (multi_cell_logic::toggleSelectedSplitDir(system)) {
          spdlog::info("Toggled split direction");
        }
      } else if (*hotkeyId == HOTKEY_ID_EXIT) {
        spdlog::info("Exit hotkey pressed, shutting down...");
        break;
      } else if (*hotkeyId == HOTKEY_ID_TOGGLE_GLOBAL) {
        if (multi_cell_logic::toggleClusterGlobalSplitDir(system)) {
          // Get the current global split direction after toggle
          if (system.selection.has_value()) {
            const auto* pc = multi_cell_logic::getCluster(system, system.selection->clusterId);
            if (pc != nullptr) {
              const char* dirStr = (pc->cluster.globalSplitDir == cell_logic::SplitDir::Vertical)
                  ? "vertical" : "horizontal";
              spdlog::info("Toggled cluster global split direction: {}", dirStr);
            }
          }
        }
      } else if (*hotkeyId == HOTKEY_ID_STORE_CELL) {
        if (system.selection.has_value()) {
          const auto* pc = multi_cell_logic::getCluster(system, system.selection->clusterId);
          if (pc != nullptr) {
            const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cellIndex)];
            if (cell.leafId.has_value()) {
              storedCell = {system.selection->clusterId, *cell.leafId};
              spdlog::info("Stored cell for operation: cluster={}, leafId={}",
                           system.selection->clusterId, *cell.leafId);
            }
          }
        }
      } else if (*hotkeyId == HOTKEY_ID_CLEAR_STORED) {
        storedCell.reset();
        spdlog::info("Cleared stored cell");
      } else if (*hotkeyId == HOTKEY_ID_EXCHANGE) {
        if (storedCell.has_value() && system.selection.has_value()) {
          const auto* pc = multi_cell_logic::getCluster(system, system.selection->clusterId);
          if (pc != nullptr) {
            const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cellIndex)];
            if (cell.leafId.has_value()) {
              auto result = multi_cell_logic::swapCells(
                  system, system.selection->clusterId, *cell.leafId, storedCell->first,
                  storedCell->second);
              if (result.success) {
                storedCell.reset();
                spdlog::info("Exchanged cells successfully");
              }
            }
          }
        }
      } else if (*hotkeyId == HOTKEY_ID_MOVE) {
        if (storedCell.has_value() && system.selection.has_value()) {
          const auto* pc = multi_cell_logic::getCluster(system, system.selection->clusterId);
          if (pc != nullptr) {
            const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cellIndex)];
            if (cell.leafId.has_value()) {
              auto result = multi_cell_logic::moveCell(system, storedCell->first, storedCell->second,
                                                       system.selection->clusterId, *cell.leafId);
              if (result.success) {
                storedCell.reset();
                spdlog::info("Moved cell successfully");
              }
            }
          }
        }
      }
    }

    // Re-gather window state
    auto currentState =
        timed("gatherCurrentWindowState", [] { return gatherCurrentWindowState(); });

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
      // One-line summary at info level
      spdlog::info("Window changes: +{} added, -{} removed",
                   result.addedLeafIds.size(), result.deletedLeafIds.size());

      // Detailed logging at debug level
      if (!result.deletedLeafIds.empty()) {
        spdlog::debug("Removed windows:");
        for (size_t id : result.deletedLeafIds) {
          auto titleIt = hwndToTitle.find(id);
          if (titleIt != hwndToTitle.end()) {
            spdlog::debug("  - \"{}\"", titleIt->second);
          }
        }
      }
      if (!result.addedLeafIds.empty()) {
        spdlog::debug("Added windows:");
        for (size_t id : result.addedLeafIds) {
          auto titleIt = hwndToTitle.find(id);
          if (titleIt != hwndToTitle.end()) {
            spdlog::debug("  + \"{}\"", titleIt->second);
          }
        }
      }

      spdlog::debug("=== Updated Tile Layout ===");
      printTileLayout(system, hwndToTitle);

      // Move mouse to center of the last added cell
      if (!result.addedLeafIds.empty()) {
        size_t lastAddedId = result.addedLeafIds.back();
        // Find which cluster contains this leaf
        for (const auto& pc : system.clusters) {
          auto cellIndexOpt = multi_cell_logic::findCellByLeafId(pc.cluster, lastAddedId);
          if (cellIndexOpt.has_value()) {
            cell_logic::Rect globalRect = multi_cell_logic::getCellGlobalRect(pc, *cellIndexOpt);
            long centerX = static_cast<long>(globalRect.x + globalRect.width / 2.0f);
            long centerY = static_cast<long>(globalRect.y + globalRect.height / 2.0f);
            winapi::set_cursor_pos(centerX, centerY);
            spdlog::debug("Moved cursor to center of new cell at ({}, {})", centerX, centerY);
            break;
          }
        }
      }
    }

    timedVoid("applyTileLayout", [&system] { applyTileLayout(system); });

    auto loopEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart).count());
  }

  // Cleanup hotkeys before exit
  unregisterNavigationHotkeys();
  spdlog::info("Hotkeys unregistered, exiting...");
}

} // namespace wintiler
