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

  process_logic::resetAppState(appState, (float)screenWidth, (float)screenHeight);

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
    winapi::log_windows_per_monitor();
  }
  return 0;
}

#endif