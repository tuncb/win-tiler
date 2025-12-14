#ifdef DOCTEST_CONFIG_DISABLE

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cells.h"
#include "process.h"
#include "raylib.h"
#include "winapi.h"

using namespace wintiler;

const size_t PROCESS_ID_START = 10;

void runRaylibUI() {
  process_logic::AppState appState;
  size_t nextProcessId = PROCESS_ID_START;

  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "win-tiler");

  cell_logic::Rect windowRect{0.0f, 0.0f, (float)screenWidth, (float)screenHeight};
  process_logic::resetAppState(appState, windowRect);

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
      cell_logic::Rect windowRect{0.0f, 0.0f, (float)screenWidth, (float)screenHeight};
      resetAppState(appState, windowRect);
      nextProcessId = PROCESS_ID_START;
    }

    if (IsKeyPressed(KEY_SPACE)) {
      addNewProcess(appState, nextProcessId);
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
          float fontSize = std::min(cell.rect.width, cell.rect.height) * 0.3f;
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

void executeMainLogic() {
  const auto windowsPerMonitor =
      winapi::gather_windows_per_monitor(winapi::get_default_ignore_options());

  for (const auto& wpm : windowsPerMonitor) {
    process_logic::AppState appState;
    auto left = wpm.monitor.rect.left;
    auto top = wpm.monitor.rect.top;
    auto width = wpm.monitor.rect.right - wpm.monitor.rect.left;
    auto height = wpm.monitor.rect.bottom - wpm.monitor.rect.top;

    // Create windowRect with monitor position as origin
    cell_logic::Rect windowRect{static_cast<float>(left), static_cast<float>(top),
                                static_cast<float>(width), static_cast<float>(height)};

    process_logic::resetAppState(appState, windowRect);
    std::vector<size_t> processIds;
    for (const auto& win : wpm.windows) {
      if (win.pid.has_value()) {
        processIds.push_back(static_cast<size_t>(win.pid.value()));
      }
    }

    process_logic::updateProcesses(appState, processIds);
    for (const auto& win : wpm.windows) {
      auto it = appState.processToLeafIdMap.find(static_cast<size_t>(win.pid.value_or(0)));
      if (it != appState.processToLeafIdMap.end()) {
        size_t leafId = it->second;
        auto cellIt =
            std::find_if(appState.CellCluster.cells.begin(), appState.CellCluster.cells.end(),
                         [leafId](const cell_logic::Cell& cell) {
                           return cell.leafId.has_value() && cell.leafId.value() == leafId;
                         });
        if (cellIt != appState.CellCluster.cells.end()) {
          const auto& rect = cellIt->rect;
          winapi::TileInfo tileInfo;
          tileInfo.handle = win.handle;
          // Cell rectangles are already in monitor coordinates
          tileInfo.window_position.x = static_cast<int>(rect.x);
          tileInfo.window_position.y = static_cast<int>(rect.y);
          tileInfo.window_position.width = static_cast<int>(rect.width);
          tileInfo.window_position.height = static_cast<int>(rect.height);

          winapi::update_window_position(tileInfo);
        }
      }
    }
  }
}

int main(int argc, char* argv[]) {
  bool runTests = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "tests") {
      runTests = true;
      break;
    }
  }

  if (runTests) {
    runRaylibUI();
  } else {
    executeMainLogic();
  }
  return 0;
}

#endif