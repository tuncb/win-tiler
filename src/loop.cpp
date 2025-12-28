#include "loop.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "multi_cell_renderer.h"
#include "multi_cells.h"
#include "overlay.h"
#include "winapi.h"

namespace wintiler {

namespace {

// Result type for action handlers - signals whether the main loop should continue or exit
enum class ActionResult { Continue, Exit };

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
  return static_cast<int>(action) + 1; // Start from 1 to avoid 0
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
std::optional<cells::Direction> hotkeyActionToDirection(HotkeyAction action) {
  switch (action) {
  case HotkeyAction::NavigateLeft:
    return cells::Direction::Left;
  case HotkeyAction::NavigateDown:
    return cells::Direction::Down;
  case HotkeyAction::NavigateUp:
    return cells::Direction::Up;
  case HotkeyAction::NavigateRight:
    return cells::Direction::Right;
  default:
    return std::nullopt;
  }
}

// Handle keyboard navigation: move selection, set foreground, move mouse to center
void handleKeyboardNavigation(cells::System& system, cells::Direction dir) {
  // Try to move selection in the given direction
  if (!cells::moveSelection(system, dir)) {
    spdlog::trace("Cannot move selection in direction");
    return;
  }

  // Get the newly selected cell
  auto selectedCell = cells::getSelectedCell(system);
  if (!selectedCell.has_value()) {
    spdlog::error("No cell selected after moveSelection");
    return;
  }

  auto [clusterId, cellIndex] = *selectedCell;
  const auto* pc = cells::getCluster(system, clusterId);
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
  cells::Rect globalRect = cells::getCellGlobalRect(*pc, cellIndex);
  long centerX = static_cast<long>(globalRect.x + globalRect.width / 2.0f);
  long centerY = static_cast<long>(globalRect.y + globalRect.height / 2.0f);

  if (!winapi::set_cursor_pos(centerX, centerY)) {
    spdlog::error("Failed to set cursor position");
    return;
  }

  spdlog::trace("Navigated to cell {} in cluster {}", cellIndex, clusterId);
}

// Type alias for stored cell used in swap/move operations
using StoredCell = std::optional<std::pair<cells::ClusterId, size_t>>;

ActionResult handleToggleSplit(cells::System& system) {
  if (cells::toggleSelectedSplitDir(system)) {
    spdlog::info("Toggled split direction");
  }
  return ActionResult::Continue;
}

ActionResult handleExit() {
  spdlog::info("Exit hotkey pressed, shutting down...");
  return ActionResult::Exit;
}

ActionResult handleToggleGlobal(cells::System& system, std::string& outMessage) {
  if (cells::toggleClusterGlobalSplitDir(system)) {
    if (system.selection.has_value()) {
      const auto* pc = cells::getCluster(system, system.selection->clusterId);
      if (pc != nullptr) {
        const char* dirStr =
            (pc->cluster.globalSplitDir == cells::SplitDir::Vertical) ? "vertical" : "horizontal";
        spdlog::info("Toggled cluster global split direction: {}", dirStr);
        outMessage = std::string("Toggled: ") + dirStr;
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handleStoreCell(cells::System& system, StoredCell& storedCell) {
  if (system.selection.has_value()) {
    const auto* pc = cells::getCluster(system, system.selection->clusterId);
    if (pc != nullptr) {
      const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cellIndex)];
      if (cell.leafId.has_value()) {
        storedCell = {system.selection->clusterId, *cell.leafId};
        spdlog::info("Stored cell for operation: cluster={}, leafId={}",
                     system.selection->clusterId, *cell.leafId);
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handleClearStored(StoredCell& storedCell) {
  storedCell.reset();
  spdlog::info("Cleared stored cell");
  return ActionResult::Continue;
}

ActionResult handleExchange(cells::System& system, StoredCell& storedCell) {
  if (storedCell.has_value() && system.selection.has_value()) {
    const auto* pc = cells::getCluster(system, system.selection->clusterId);
    if (pc != nullptr) {
      const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cellIndex)];
      if (cell.leafId.has_value()) {
        auto result = cells::swapCells(system, system.selection->clusterId, *cell.leafId,
                                       storedCell->first, storedCell->second);
        if (result.success) {
          storedCell.reset();
          spdlog::info("Exchanged cells successfully");
        }
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult handleMove(cells::System& system, StoredCell& storedCell) {
  if (storedCell.has_value() && system.selection.has_value()) {
    const auto* pc = cells::getCluster(system, system.selection->clusterId);
    if (pc != nullptr) {
      const auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cellIndex)];
      if (cell.leafId.has_value()) {
        auto result = cells::moveCell(system, storedCell->first, storedCell->second,
                                      system.selection->clusterId, *cell.leafId);
        if (result.success) {
          storedCell.reset();
          spdlog::info("Moved cell successfully");
        }
      }
    }
  }
  return ActionResult::Continue;
}

ActionResult dispatchHotkeyAction(HotkeyAction action, cells::System& system,
                                  StoredCell& storedCell, std::string& outMessage) {
  // Handle navigation actions
  if (auto dir = hotkeyActionToDirection(action)) {
    handleKeyboardNavigation(system, *dir);
    return ActionResult::Continue;
  }

  // Handle other actions
  switch (action) {
  case HotkeyAction::ToggleSplit:
    return handleToggleSplit(system);
  case HotkeyAction::Exit:
    return handleExit();
  case HotkeyAction::ToggleGlobal:
    return handleToggleGlobal(system, outMessage);
  case HotkeyAction::StoreCell:
    return handleStoreCell(system, storedCell);
  case HotkeyAction::ClearStored:
    return handleClearStored(storedCell);
  case HotkeyAction::Exchange:
    return handleExchange(system, storedCell);
  case HotkeyAction::Move:
    return handleMove(system, storedCell);
  default:
    return ActionResult::Continue;
  }
}

void updateForegroundSelectionFromMousePosition(cells::System& system) {
  auto foregroundHwnd = winapi::get_foreground_window();

  if (foregroundHwnd == nullptr ||
      !cells::hasLeafId(system, reinterpret_cast<size_t>(foregroundHwnd))) {
    return;
  }

  auto cursorPosOpt = winapi::get_cursor_pos();
  if (!cursorPosOpt.has_value()) {
    spdlog::error("Failed to get cursor position");
    return;
  }

  float cursorX = static_cast<float>(cursorPosOpt->x);
  float cursorY = static_cast<float>(cursorPosOpt->y);

  auto cellAtCursor = cells::findCellAtPoint(system, cursorX, cursorY);

  if (!cellAtCursor.has_value()) {
    return;
  }

  auto [clusterId, cellIndex] = *cellAtCursor;

  bool needsUpdate = !system.selection.has_value() || system.selection->clusterId != clusterId ||
                     system.selection->cellIndex != cellIndex;

  if (!needsUpdate) {
    return;
  }

  system.selection = cells::Selection{clusterId, cellIndex};

  const auto* pc = cells::getCluster(system, clusterId);
  if (pc != nullptr) {
    const auto& cell = pc->cluster.cells[static_cast<size_t>(cellIndex)];
    if (cell.leafId.has_value()) {
      winapi::HWND_T cellHwnd = reinterpret_cast<winapi::HWND_T>(*cell.leafId);
      if (!winapi::set_foreground_window(cellHwnd)) {
        spdlog::error("Failed to set foreground window for HWND {}", cellHwnd);
      }
      spdlog::trace("======================Selection updated: cluster={}, cell={}", clusterId,
                    cellIndex);
    }
  }
}

// Helper: Print tile layout from a multi-cluster system
void printTileLayout(const cells::System& system) {
  size_t totalWindows = cells::countTotalLeaves(system);
  spdlog::debug("Total windows: {}", totalWindows);

  for (const auto& pc : system.clusters) {
    spdlog::debug("--- Monitor {} ---", pc.id);

    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
      if (cell.isDead || !cell.leafId.has_value()) {
        continue;
      }

      size_t hwndValue = *cell.leafId;
      cells::Rect globalRect = cells::getCellGlobalRect(pc, i);

      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(hwndValue);
      auto windowInfo = winapi::get_window_info(hwnd);

      spdlog::debug("  Window: \"{}\" ({})", windowInfo.title, windowInfo.processName);
      spdlog::debug("    Position: x={}, y={}", static_cast<int>(globalRect.x),
                    static_cast<int>(globalRect.y));
      spdlog::debug("    Size: {}x{}", static_cast<int>(globalRect.width),
                    static_cast<int>(globalRect.height));
    }
  }
}

// Helper: Gather current window state for all monitors
std::vector<cells::ClusterCellIds> gatherCurrentWindowState(const IgnoreOptions& ignoreOptions) {
  std::vector<cells::ClusterCellIds> result;
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
void applyTileLayout(const cells::System& system) {
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
      if (cell.isDead || !cell.leafId.has_value()) {
        continue;
      }

      size_t hwndValue = *cell.leafId;
      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(hwndValue);
      cells::Rect globalRect = cells::getCellGlobalRect(pc, i);

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

cells::System createInitialSystem(const GlobalOptions& options) {
  auto monitors = winapi::get_monitors();
  winapi::log_monitors(monitors);

  std::vector<cells::ClusterInitInfo> clusterInfos;
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& monitor = monitors[i];
    float x = static_cast<float>(monitor.workArea.left);
    float y = static_cast<float>(monitor.workArea.top);
    float w = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float h = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    auto hwnds = winapi::get_hwnds_for_monitor(i, options.ignoreOptions);
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }
    clusterInfos.push_back({i, x, y, w, h, cellIds});
  }

  return cells::createSystem(clusterInfos, options.gapOptions.horizontal,
                             options.gapOptions.vertical);
}

} // namespace

void runLoopMode(GlobalOptionsProvider& provider) {
  const auto& options = provider.options;

  auto system = timed("createInitialSystem", [&options] { return createInitialSystem(options); });

  // Print initial layout and apply
  spdlog::info("=== Initial Tile Layout ===");
  printTileLayout(system);

  timedVoid("initial applyTileLayout", [&system] { applyTileLayout(system); });

  // Register keyboard hotkeys
  registerNavigationHotkeys(options.keyboardOptions);

  // Initialize overlay for rendering
  overlay::init();

  // Print keyboard shortcuts
  spdlog::info("=== Keyboard Shortcuts ===");
  for (const auto& binding : options.keyboardOptions.bindings) {
    spdlog::info("  {}: {}", hotkeyActionToString(binding.action), binding.hotkey);
  }

  // 3. Enter monitoring loop
  spdlog::info("Monitoring for window changes... (Ctrl+C to exit)");

  // Store cell for swap/move operations
  StoredCell storedCell;

  // Toast message state
  std::string toastMessage;
  auto toastExpiry = std::chrono::steady_clock::now();
  auto toastDuration = std::chrono::milliseconds(options.visualizationOptions.toastDurationMs);

  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto loopStart = std::chrono::high_resolution_clock::now();

    // Check for config file changes and hot-reload
    if (provider.refresh()) {
      // Re-register hotkeys with new bindings (options is ref to provider.options, already updated)
      unregisterNavigationHotkeys(options.keyboardOptions);
      registerNavigationHotkeys(options.keyboardOptions);

      // Update gap settings and recompute cell rects
      cells::updateSystemGaps(system, options.gapOptions.horizontal, options.gapOptions.vertical);

      // Update toast duration
      toastDuration = std::chrono::milliseconds(options.visualizationOptions.toastDurationMs);

      spdlog::info("Config hot-reloaded");
    }

    // Check for keyboard hotkeys
    if (auto hotkeyId = winapi::check_keyboard_action()) {
      auto actionOpt = idToHotkeyAction(*hotkeyId);
      if (!actionOpt.has_value()) {
        continue; // Unknown hotkey ID
      }
      std::string actionMessage;
      if (dispatchHotkeyAction(*actionOpt, system, storedCell, actionMessage) ==
          ActionResult::Exit) {
        break;
      }
      if (!actionMessage.empty()) {
        toastMessage = actionMessage;
        toastExpiry = std::chrono::steady_clock::now() + toastDuration;
      }
    }

    // Re-gather window state
    auto currentState = timed("gatherCurrentWindowState", [&options] {
      return gatherCurrentWindowState(options.ignoreOptions);
    });

    // Use updateSystem to sync
    auto result = timed("updateSystem", [&system, &currentState] {
      return cells::updateSystem(system, currentState, std::nullopt);
    });

    updateForegroundSelectionFromMousePosition(system);

    // If changes detected, log and apply
    if (!result.deletedLeafIds.empty() || !result.addedLeafIds.empty()) {
      // One-line summary at info level
      spdlog::info("Window changes: +{} added, -{} removed", result.addedLeafIds.size(),
                   result.deletedLeafIds.size());

      // Detailed logging at debug level for added windows
      if (!result.addedLeafIds.empty()) {
        spdlog::debug("Added windows:");
        for (size_t id : result.addedLeafIds) {
          winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(id);
          std::string title = winapi::get_window_info(hwnd).title;
          spdlog::debug("  + \"{}\"", title);
        }
      }

      spdlog::debug("=== Updated Tile Layout ===");
      printTileLayout(system);

      // Move mouse to center of the last added cell
      if (!result.addedLeafIds.empty()) {
        size_t lastAddedId = result.addedLeafIds.back();
        // Find which cluster contains this leaf
        for (const auto& pc : system.clusters) {
          auto cellIndexOpt = cells::findCellByLeafId(pc.cluster, lastAddedId);
          if (cellIndexOpt.has_value()) {
            cells::Rect globalRect = cells::getCellGlobalRect(pc, *cellIndexOpt);
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

    // Render cell system overlay
    std::string currentToast = (std::chrono::steady_clock::now() < toastExpiry) ? toastMessage : "";
    renderer::RenderOptions renderOpts{
        options.visualizationOptions.normalColor,   options.visualizationOptions.selectedColor,
        options.visualizationOptions.storedColor,   options.visualizationOptions.borderWidth,
        options.visualizationOptions.toastFontSize,
    };
    renderer::render(system, renderOpts, storedCell, currentToast);

    auto loopEnd = std::chrono::high_resolution_clock::now();
    spdlog::trace(
        "loop iteration total: {}us",
        std::chrono::duration_cast<std::chrono::microseconds>(loopEnd - loopStart).count());
  }

  // Cleanup hotkeys and overlay before exit
  unregisterNavigationHotkeys(options.keyboardOptions);
  overlay::shutdown();
  spdlog::info("Hotkeys unregistered, overlay shutdown, exiting...");
}

} // namespace wintiler
