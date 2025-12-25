#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
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

// Convert HotkeyAction to integer ID for Windows hotkey registration
int hotkeyActionToId(HotkeyAction action) {
  return static_cast<int>(action) + 1;  // Start from 1 to avoid 0
}

// Convert integer ID back to HotkeyAction
std::optional<HotkeyAction> idToHotkeyAction(int id) {
  int index = id - 1;
  if (index >= 0 && index <= static_cast<int>(HotkeyAction::Move)) {
    return static_cast<HotkeyAction>(index);
  }
  return std::nullopt;
}

void registerNavigationHotkeys(const KeyboardOptions& keyboardOptions) {
  for (const auto& binding : keyboardOptions.bindings) {
    int id = hotkeyActionToId(binding.action);
    auto hotkey = winapi::create_hotkey(binding.hotkey, id);
    if (hotkey) {
      winapi::register_hotkey(*hotkey);
    }
  }
  spdlog::info("Registered {} hotkeys", keyboardOptions.bindings.size());
}

void unregisterNavigationHotkeys(const KeyboardOptions& keyboardOptions) {
  for (const auto& binding : keyboardOptions.bindings) {
    int id = hotkeyActionToId(binding.action);
    winapi::unregister_hotkey(id);
  }
}

// Convert HotkeyAction to human-readable string
const char* hotkeyActionToString(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return "Navigate Left";
  case HotkeyAction::NavigateDown:
    return "Navigate Down";
  case HotkeyAction::NavigateUp:
    return "Navigate Up";
  case HotkeyAction::NavigateRight:
    return "Navigate Right";
  case HotkeyAction::ToggleSplit:
    return "Toggle Split";
  case HotkeyAction::Exit:
    return "Exit";
  case HotkeyAction::ToggleGlobal:
    return "Toggle Global";
  case HotkeyAction::StoreCell:
    return "Store Cell";
  case HotkeyAction::ClearStored:
    return "Clear Stored";
  case HotkeyAction::Exchange:
    return "Exchange";
  case HotkeyAction::Move:
    return "Move";
  default:
    return "Unknown";
  }
}

// Convert HotkeyAction to direction (for navigation actions)
std::optional<cell_logic::Direction> hotkeyActionToDirection(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return cell_logic::Direction::Left;
  case HotkeyAction::NavigateDown:
    return cell_logic::Direction::Down;
  case HotkeyAction::NavigateUp:
    return cell_logic::Direction::Up;
  case HotkeyAction::NavigateRight:
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
std::vector<multi_cell_logic::ClusterCellIds> gatherCurrentWindowState(
    const IgnoreOptions& ignoreOptions) {
  std::vector<multi_cell_logic::ClusterCellIds> result;
  auto monitors = winapi::get_monitors();

  for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
    auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex, ignoreOptions);
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

// Redirect new windows to the selected cluster so they split from the selected cell
void redirectNewWindowsToSelection(
    const multi_cell_logic::System& system,
    std::vector<multi_cell_logic::ClusterCellIds>& clusterCellIds) {

  if (!system.selection.has_value()) {
    return;  // No selection, keep default behavior
  }

  multi_cell_logic::ClusterId selectedClusterId = system.selection->clusterId;

  // Collect new windows (not in any cluster)
  std::vector<size_t> newWindows;
  for (const auto& update : clusterCellIds) {
    for (size_t leafId : update.leafIds) {
      bool isNew = true;
      for (const auto& pc : system.clusters) {
        if (multi_cell_logic::findCellByLeafId(pc.cluster, leafId).has_value()) {
          isNew = false;
          break;
        }
      }
      if (isNew) {
        newWindows.push_back(leafId);
      }
    }
  }

  if (newWindows.empty()) {
    return;  // No new windows to redirect
  }

  spdlog::debug("Redirecting {} new window(s) to selected cluster {}",
                newWindows.size(), selectedClusterId);

  // Remove new windows from their detected clusters
  for (auto& update : clusterCellIds) {
    auto& ids = update.leafIds;
    ids.erase(std::remove_if(ids.begin(), ids.end(),
      [&newWindows](size_t id) {
        return std::find(newWindows.begin(), newWindows.end(), id) != newWindows.end();
      }), ids.end());
  }

  // Add new windows to the selected cluster
  for (auto& update : clusterCellIds) {
    if (update.clusterId == selectedClusterId) {
      for (size_t id : newWindows) {
        update.leafIds.push_back(id);
      }
      break;
    }
  }
}

} // namespace

void runLoopTestMode(const GlobalOptions& options) {
  const auto& ignoreOptions = options.ignoreOptions;

  // 1. Build initial state (like computeTileLayout)
  auto monitors = winapi::get_monitors();

  // Build hwnd -> title lookup
  auto windowInfos = winapi::gather_raw_window_data(ignoreOptions);
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

    auto hwnds = winapi::get_hwnds_for_monitor(i, ignoreOptions);
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
    auto currentState = gatherCurrentWindowState(ignoreOptions);

    // Update title lookup for any new windows
    auto newWindowInfos = winapi::gather_raw_window_data(ignoreOptions);
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

void runLoopMode(const GlobalOptions& options) {
  const auto& ignoreOptions = options.ignoreOptions;
  const auto& keyboardOptions = options.keyboardOptions;

  auto initStart = std::chrono::high_resolution_clock::now();

  // 1. Build initial state (like computeTileLayout)
  auto monitors = timed("get_monitors", [] { return winapi::get_monitors(); });

  // Build hwnd -> title lookup
  auto hwndToTitle = timed("gather_raw_window_data + build lookup", [&ignoreOptions] {
    auto windowInfos = winapi::gather_raw_window_data(ignoreOptions);
    std::unordered_map<size_t, std::string> result;
    for (const auto& info : windowInfos) {
      result[reinterpret_cast<size_t>(info.handle)] = info.title;
    }
    return result;
  });

  // Create cluster init info
  auto clusterInfos = timed("build cluster infos", [&monitors, &ignoreOptions] {
    std::vector<multi_cell_logic::ClusterInitInfo> result;
    for (size_t i = 0; i < monitors.size(); ++i) {
      const auto& monitor = monitors[i];
      float x = static_cast<float>(monitor.workArea.left);
      float y = static_cast<float>(monitor.workArea.top);
      float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
      float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

      auto hwnds = winapi::get_hwnds_for_monitor(i, ignoreOptions);
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
  registerNavigationHotkeys(keyboardOptions);

  // Print keyboard shortcuts
  spdlog::info("=== Keyboard Shortcuts ===");
  for (const auto& binding : keyboardOptions.bindings) {
    spdlog::info("  {}: {}", hotkeyActionToString(binding.action), binding.hotkey);
  }

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  // Store cell for swap/move operations (clusterId, leafId)
  std::optional<std::pair<multi_cell_logic::ClusterId, size_t>> storedCell;

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto loopStart = std::chrono::high_resolution_clock::now();

    // Check for keyboard hotkeys
    if (auto hotkeyId = winapi::check_keyboard_action()) {
      auto actionOpt = idToHotkeyAction(*hotkeyId);
      if (!actionOpt.has_value()) {
        continue;  // Unknown hotkey ID
      }
      HotkeyAction action = *actionOpt;

      if (auto dir = hotkeyActionToDirection(action)) {
        handleKeyboardNavigation(system, *dir);
      } else if (action == HotkeyAction::ToggleSplit) {
        if (multi_cell_logic::toggleSelectedSplitDir(system)) {
          spdlog::info("Toggled split direction");
        }
      } else if (action == HotkeyAction::Exit) {
        spdlog::info("Exit hotkey pressed, shutting down...");
        break;
      } else if (action == HotkeyAction::ToggleGlobal) {
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
      } else if (action == HotkeyAction::StoreCell) {
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
      } else if (action == HotkeyAction::ClearStored) {
        storedCell.reset();
        spdlog::info("Cleared stored cell");
      } else if (action == HotkeyAction::Exchange) {
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
      } else if (action == HotkeyAction::Move) {
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
        timed("gatherCurrentWindowState", [&ignoreOptions] { return gatherCurrentWindowState(ignoreOptions); });

    // Update title lookup for any new windows
    timedVoid("update title lookup", [&hwndToTitle, &ignoreOptions] {
      auto newWindowInfos = winapi::gather_raw_window_data(ignoreOptions);
      for (const auto& info : newWindowInfos) {
        size_t key = reinterpret_cast<size_t>(info.handle);
        if (hwndToTitle.find(key) == hwndToTitle.end()) {
          hwndToTitle[key] = info.title;
        }
      }
    });

    // Redirect new windows to the selected cluster so they split from the selected cell
    redirectNewWindowsToSelection(system, currentState);

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
  unregisterNavigationHotkeys(keyboardOptions);
  spdlog::info("Hotkeys unregistered, exiting...");
}

} // namespace wintiler
