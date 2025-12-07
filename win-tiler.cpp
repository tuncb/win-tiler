#include "raylib.h"

#include "actions.h"
#include "state.h"

using namespace wintiler;

int main(void)
{
  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "win-tiler");

  AppState appState = createInitialState((float)screenWidth, (float)screenHeight);

  SetTargetFPS(60);

  while (!WindowShouldClose())
  {
    // Input handling
    if (IsKeyPressed(KEY_H))
    {
      if (appState.selectedIndex.has_value() && isLeaf(appState, *appState.selectedIndex))
      {
        Cell &cell = appState.cells[*appState.selectedIndex];
        cell.splitDir = SplitDir::Horizontal;
      }
    }

    if (IsKeyPressed(KEY_V))
    {
      if (appState.selectedIndex.has_value() && isLeaf(appState, *appState.selectedIndex))
      {
        Cell &cell = appState.cells[*appState.selectedIndex];
        cell.splitDir = SplitDir::Vertical;
      }
    }

    if (IsKeyPressed(KEY_T))
    {
      toggleSelectedSplitDir(appState);
    }

    if (IsKeyPressed(KEY_R))
    {
      appState = createInitialState((float)screenWidth, (float)screenHeight);
    }

    if (IsKeyPressed(KEY_SPACE))
    {
      splitSelectedLeaf(appState);
    }

    if (IsKeyPressed(KEY_D))
    {
      deleteSelectedLeaf(appState);
    }

    if (IsKeyPressed(KEY_I))
    {
      // Debug dump of the whole state to the console.
      debugPrintState(appState);
    }

    if (IsKeyPressed(KEY_LEFT))
    {
      moveSelection(appState, Direction::Left);
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
      moveSelection(appState, Direction::Right);
    }
    if (IsKeyPressed(KEY_UP))
    {
      moveSelection(appState, Direction::Up);
    }
    if (IsKeyPressed(KEY_DOWN))
    {
      moveSelection(appState, Direction::Down);
    }

    BeginDrawing();
    ClearBackground(RAYWHITE);

    for (int i = 0; i < static_cast<int>(appState.cells.size()); ++i)
    {
      if (!isLeaf(appState, i))
      {
        continue;
      }

      const Cell &cell = appState.cells[static_cast<std::size_t>(i)];
      const Rect &r = cell.rect;
      Rectangle rr{r.x, r.y, r.width, r.height};

      bool isSelected = appState.selectedIndex.has_value() && *appState.selectedIndex == i;
      Color borderColor = isSelected ? RED : BLACK;

      DrawRectangleLinesEx(rr, isSelected ? 3.0f : 1.0f, borderColor);
    }

    EndDrawing();
  }

  CloseWindow();

  return 0;
}