#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "raylib.h"

#include <cells.h>

using namespace wintiler;

const size_t PROCESS_ID_START = 10;

struct AppState
{
  cell_logic::WindowState windowState;
  size_t nextProcessId = PROCESS_ID_START;
  std::unordered_map<size_t, size_t> processToLeafIdMap;
  std::unordered_map<size_t, size_t> leafIdToProcessMap;
};

void addNewProcess(AppState &appState)
{
  auto newLeafIdOpt = cell_logic::splitSelectedLeaf(appState.windowState);
  if (!newLeafIdOpt.has_value())
  {
    return;
  }

  size_t processId = appState.nextProcessId++;
  size_t leafId = *newLeafIdOpt;
  appState.processToLeafIdMap[processId] = leafId;
  appState.leafIdToProcessMap[leafId] = processId;
}

void deleteSelectedCellsProcess(AppState &appState)
{
  auto selectedCell = appState.windowState.selectedIndex;
  if (!selectedCell.has_value())
  {
    return;
  }

  auto processIt = appState.leafIdToProcessMap.find(
      appState.windowState.cells[static_cast<std::size_t>(*selectedCell)].leafId.value());
  if (processIt == appState.leafIdToProcessMap.end())
  {
    return;
  }

  size_t selectedProcessId = processIt->second;

  if (!deleteSelectedLeaf(appState.windowState))
  {
    return;
  }

  auto it = appState.processToLeafIdMap.find(selectedProcessId);
  if (it != appState.processToLeafIdMap.end())
  {
    size_t leafId = it->second;
    appState.processToLeafIdMap.erase(it);
    appState.leafIdToProcessMap.erase(leafId);
  }
}

void resetAppState(AppState &appState, float width, float height)
{
  appState.windowState = cell_logic::createInitialState(width, height);
  appState.nextProcessId = PROCESS_ID_START;
  appState.processToLeafIdMap.clear();
  appState.leafIdToProcessMap.clear();
}

int main(void)
{
  AppState appState;

  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "win-tiler");

  resetAppState(appState, (float)screenWidth, (float)screenHeight);

  SetTargetFPS(60);

  auto centerMouseOnSelection = [&appState]()
  {
    if (appState.windowState.selectedIndex.has_value())
    {
      const auto &cell = appState.windowState.cells[*appState.windowState.selectedIndex];
      SetMousePosition((int)(cell.rect.x + cell.rect.width / 2), (int)(cell.rect.y + cell.rect.height / 2));
    }
  };

  while (!WindowShouldClose())
  {
    Vector2 mousePos = GetMousePosition();
    for (int i = 0; i < static_cast<int>(appState.windowState.cells.size()); ++i)
    {
      if (!isLeaf(appState.windowState, i))
      {
        continue;
      }

      const auto &cell = appState.windowState.cells[static_cast<std::size_t>(i)];
      Rectangle rr{cell.rect.x, cell.rect.y, cell.rect.width, cell.rect.height};

      if (CheckCollisionPointRec(mousePos, rr))
      {
        appState.windowState.selectedIndex = i;
        break;
      }
    }

    if (IsKeyPressed(KEY_H))
    {
      appState.windowState.globalSplitDir = cell_logic::SplitDir::Horizontal;
    }

    if (IsKeyPressed(KEY_V))
    {
      appState.windowState.globalSplitDir = cell_logic::SplitDir::Vertical;
    }

    if (IsKeyPressed(KEY_T))
    {
      toggleSelectedSplitDir(appState.windowState);
    }

    if (IsKeyPressed(KEY_R))
    {
      resetAppState(appState, (float)screenWidth, (float)screenHeight);
    }

    if (IsKeyPressed(KEY_SPACE))
    {
      addNewProcess(appState);
    }

    if (IsKeyPressed(KEY_D))
    {
      deleteSelectedCellsProcess(appState);
    }

    if (IsKeyPressed(KEY_I))
    {
      // Debug dump of the whole state to the console.
      debugPrintState(appState.windowState);
    }

    if (IsKeyPressed(KEY_C))
    {
      // Validate internal invariants of the state and print result.
      validateState(appState.windowState);
    }

    if (IsKeyPressed(KEY_LEFT))
    {
      if (moveSelection(appState.windowState, cell_logic::Direction::Left))
      {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
      if (moveSelection(appState.windowState, cell_logic::Direction::Right))
      {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_UP))
    {
      if (moveSelection(appState.windowState, cell_logic::Direction::Up))
      {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_DOWN))
    {
      if (moveSelection(appState.windowState, cell_logic::Direction::Down))
      {
        centerMouseOnSelection();
      }
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    for (int i = 0; i < static_cast<int>(appState.windowState.cells.size()); ++i)
    {
      if (!isLeaf(appState.windowState, i))
      {
        continue;
      }

      const auto &cell = appState.windowState.cells[static_cast<std::size_t>(i)];
      const auto &r = cell.rect;
      Rectangle rr{r.x, r.y, r.width, r.height};

      bool isSelected = appState.windowState.selectedIndex.has_value() && *appState.windowState.selectedIndex == i;
      Color borderColor = isSelected ? RED : BLACK;

      DrawRectangleLinesEx(rr, isSelected ? 3.0f : 1.0f, borderColor);

      if (cell.leafId.has_value())
      {
        auto processIt = appState.leafIdToProcessMap.find(*cell.leafId);
        if (processIt != appState.leafIdToProcessMap.end())
        {
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

  return 0;
}