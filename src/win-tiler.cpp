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

  for (size_t monitorIndex = 0; monitorIndex < monitors.size(); ++monitorIndex) {
    const auto& monitor = monitors[monitorIndex];

    // Get windows for this monitor
    auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex);
    if (hwnds.empty()) {
      continue;
    }

    // Get window info to retrieve titles
    auto windowInfos = winapi::gather_raw_window_data(winapi::get_default_ignore_options());
    std::unordered_map<size_t, std::string> hwndToTitle;
    for (const auto& info : windowInfos) {
      hwndToTitle[reinterpret_cast<size_t>(info.handle)] = info.title;
    }

    // Convert HWNDs to size_t IDs for process_logic
    std::vector<size_t> cellIds;
    for (auto hwnd : hwnds) {
      cellIds.push_back(reinterpret_cast<size_t>(hwnd));
    }

    // Calculate workArea dimensions
    float width = static_cast<float>(monitor.workArea.right - monitor.workArea.left);
    float height = static_cast<float>(monitor.workArea.bottom - monitor.workArea.top);

    // Create and initialize app state
    process_logic::AppState appState;
    process_logic::resetAppState(appState, width, height);

    // Create cells for all windows
    process_logic::updateProcesses(appState, cellIds);

    // Collect tile results
    for (const auto& cell : appState.CellCluster.cells) {
      if (cell.isDead || !cell.leafId.has_value()) {
        continue;
      }

      // Find the processId (HWND) for this leaf
      auto it = appState.leafIdToProcessMap.find(*cell.leafId);
      if (it == appState.leafIdToProcessMap.end()) {
        continue;
      }

      size_t processId = it->second;
      winapi::HWND_T hwnd = reinterpret_cast<winapi::HWND_T>(processId);

      // Convert cell rect to window position (offset by workArea origin)
      winapi::WindowPosition pos;
      pos.x = static_cast<int>(cell.rect.x) + monitor.workArea.left;
      pos.y = static_cast<int>(cell.rect.y) + monitor.workArea.top;
      pos.width = static_cast<int>(cell.rect.width);
      pos.height = static_cast<int>(cell.rect.height);

      // Get window title
      std::string title;
      auto titleIt = hwndToTitle.find(processId);
      if (titleIt != hwndToTitle.end()) {
        title = titleIt->second;
      }

      results.push_back({hwnd, pos, title, monitorIndex});
    }
  }

  return results;
}

void runRaylibUI(const std::vector<winapi::HWND_T>& initialCellIds = {}) {
  process_logic::AppState appState;
  size_t nextCellId = CELL_ID_START;

  // Convert HWNDs to size_t for internal cell ID tracking
  std::vector<size_t> cellIds;
  cellIds.reserve(initialCellIds.size());
  for (const auto& hwnd : initialCellIds) {
    cellIds.push_back(reinterpret_cast<size_t>(hwnd));
  }

  if (!cellIds.empty()) {
    nextCellId = *std::max_element(cellIds.begin(), cellIds.end()) + 1;
  }

  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "win-tiler");

  process_logic::resetAppState(appState, (float)screenWidth, (float)screenHeight);
  if (!cellIds.empty()) {
    process_logic::updateProcesses(appState, cellIds);
  }

  SetTargetFPS(60);

  auto centerMouseOnSelection = [&appState]() {
    if (appState.CellCluster.selectedIndex.has_value()) {
      const auto& cell = appState.CellCluster.cells[*appState.CellCluster.selectedIndex];
      SetMousePosition((int)(cell.rect.x + cell.rect.width / 2),
                       (int)(cell.rect.y + cell.rect.height / 2));
    }
  };

  while (!WindowShouldClose()) {
    Vector2 mousePos = GetMousePosition();
    for (int i = 0; i < static_cast<int>(appState.CellCluster.cells.size()); ++i) {
      if (!isLeaf(appState.CellCluster, i)) {
        continue;
      }

      const auto& cell = appState.CellCluster.cells[static_cast<std::size_t>(i)];
      Rectangle rr{cell.rect.x, cell.rect.y, cell.rect.width, cell.rect.height};

      if (CheckCollisionPointRec(mousePos, rr)) {
        appState.CellCluster.selectedIndex = i;
        break;
      }
    }

    if (IsKeyPressed(KEY_H)) {
      appState.CellCluster.globalSplitDir = cell_logic::SplitDir::Horizontal;
    }

    if (IsKeyPressed(KEY_V)) {
      appState.CellCluster.globalSplitDir = cell_logic::SplitDir::Vertical;
    }

    if (IsKeyPressed(KEY_T)) {
      toggleSelectedSplitDir(appState.CellCluster);
    }

    if (IsKeyPressed(KEY_R)) {
      resetAppState(appState, (float)screenWidth, (float)screenHeight);
      nextCellId = CELL_ID_START;
    }

    if (IsKeyPressed(KEY_SPACE)) {
      addNewProcess(appState, nextCellId);
    }

    if (IsKeyPressed(KEY_D)) {
      deleteSelectedCellsProcess(appState);
    }

    if (IsKeyPressed(KEY_I)) {
      // Debug dump of the whole state to the console.
      debugPrintState(appState.CellCluster);
    }

    if (IsKeyPressed(KEY_C)) {
      // Validate internal invariants of the state and print result.
      validateState(appState.CellCluster);
    }

    if (IsKeyPressed(KEY_M)) {
      winapi::log_windows_per_monitor();
    }

    if (IsKeyPressed(KEY_LEFT)) {
      if (moveSelection(appState.CellCluster, cell_logic::Direction::Left)) {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_RIGHT)) {
      if (moveSelection(appState.CellCluster, cell_logic::Direction::Right)) {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_UP)) {
      if (moveSelection(appState.CellCluster, cell_logic::Direction::Up)) {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_DOWN)) {
      if (moveSelection(appState.CellCluster, cell_logic::Direction::Down)) {
        centerMouseOnSelection();
      }
    }

    for (int k = KEY_ONE; k <= KEY_NINE; ++k) {
      if (IsKeyPressed(k)) {
        int count = k - KEY_ONE + 1;
        std::vector<size_t> ids;
        for (int i = 0; i < count; ++i) {
          ids.push_back(10 + i);
        }
        process_logic::updateProcesses(appState, ids);
      }
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    for (int i = 0; i < static_cast<int>(appState.CellCluster.cells.size()); ++i) {
      if (!isLeaf(appState.CellCluster, i)) {
        continue;
      }

      const auto& cell = appState.CellCluster.cells[static_cast<std::size_t>(i)];
      const auto& r = cell.rect;
      Rectangle rr{r.x, r.y, r.width, r.height};

      bool isSelected = appState.CellCluster.selectedIndex.has_value() &&
                        *appState.CellCluster.selectedIndex == i;
      Color borderColor = isSelected ? RED : BLACK;

      DrawRectangleLinesEx(rr, isSelected ? 3.0f : 1.0f, borderColor);

      if (cell.leafId.has_value()) {
        auto processIt = appState.leafIdToProcessMap.find(*cell.leafId);
        if (processIt != appState.leafIdToProcessMap.end()) {
          size_t processId = processIt->second;
          std::string labelText = "P:" + std::to_string(processId);
          float fontSize = std::min(cell.rect.width, cell.rect.height) * 0.2f;
          if (fontSize < 10.0f)
            fontSize = 10.0f;

          int textWidth = MeasureText(labelText.c_str(), (int)fontSize);
          int textX = (int)(cell.rect.x + (cell.rect.width - textWidth) / 2);
          int textY = (int)(cell.rect.y + (cell.rect.height - fontSize) / 2) - 10;

          DrawText(labelText.c_str(), textX, textY, (int)fontSize, DARKGRAY);
        }
      }
    }

    EndDrawing();
  }

  CloseWindow();
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

    if (arg == "ui-test") {
      runRaylibUI();
      return 0;
    }

    if (arg == "ui-test-monitor" && i + 1 < argc) {
      size_t monitorIndex = std::stoul(argv[i + 1]);
      winapi::log_windows_per_monitor(monitorIndex);
      auto hwnds = winapi::get_hwnds_for_monitor(monitorIndex);
      runRaylibUI(hwnds);
      return 0;
    }

    if (arg == "ui-test-multi") {
      // Create test clusters simulating two monitors side by side
      std::vector<multi_cell_logic::ClusterInitInfo> infos;

      // Monitor 0: left side (0, 0) - 1920x1080
      infos.push_back({0, 0.0f, 0.0f, 1920.0f, 1080.0f, {}});

      // Monitor 1: right side (1920, 0) - 1920x1080
      infos.push_back({1, 1920.0f, 0.0f, 1920.0f, 1080.0f, {}});

      runRaylibUIMultiCluster(infos);
      return 0;
    }
  }

  winapi::log_windows_per_monitor();
  return 0;
}

#endif