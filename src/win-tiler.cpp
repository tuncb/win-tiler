#include "raylib.h"

#include "actions.h"
#include "state.h"

#include <string>
#include <vector>
#include <algorithm>

using namespace wintiler;

int main(void)
{
  const int screenWidth = 1600;
  const int screenHeight = 900;

  InitWindow(screenWidth, screenHeight, "win-tiler");

  AppState appState = createInitialState((float)screenWidth, (float)screenHeight);

  SetTargetFPS(60);

  auto centerMouseOnSelection = [&appState]()
  {
    if (appState.selectedIndex.has_value())
    {
      const Cell &cell = appState.cells[*appState.selectedIndex];
      SetMousePosition((int)(cell.rect.x + cell.rect.width / 2), (int)(cell.rect.y + cell.rect.height / 2));
    }
  };

  while (!WindowShouldClose())
  {
    Vector2 mousePos = GetMousePosition();
    for (int i = 0; i < static_cast<int>(appState.cells.size()); ++i)
    {
      if (!isLeaf(appState, i))
      {
        continue;
      }

      const Cell &cell = appState.cells[static_cast<std::size_t>(i)];
      Rectangle rr{cell.rect.x, cell.rect.y, cell.rect.width, cell.rect.height};

      if (CheckCollisionPointRec(mousePos, rr))
      {
        appState.selectedIndex = i;
        break;
      }
    }

    if (IsKeyPressed(KEY_H))
    {
      appState.globalSplitDir = SplitDir::Horizontal;
    }

    if (IsKeyPressed(KEY_V))
    {
      appState.globalSplitDir = SplitDir::Vertical;
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

    if (IsKeyPressed(KEY_C))
    {
      // Validate internal invariants of the state and print result.
      validateState(appState);
    }

    if (IsKeyPressed(KEY_LEFT))
    {
      if (moveSelection(appState, Direction::Left))
      {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_RIGHT))
    {
      if (moveSelection(appState, Direction::Right))
      {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_UP))
    {
      if (moveSelection(appState, Direction::Up))
      {
        centerMouseOnSelection();
      }
    }
    if (IsKeyPressed(KEY_DOWN))
    {
      if (moveSelection(appState, Direction::Down))
      {
        centerMouseOnSelection();
      }
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

      if (cell.leafId.has_value())
      {
        std::string labelText = std::to_string(*cell.leafId);
        float fontSize = std::min(cell.rect.width, cell.rect.height) * 0.5f;
        if (fontSize < 10.0f)
          fontSize = 10.0f;

        int textWidth = MeasureText(labelText.c_str(), (int)fontSize);
        int textX = (int)(cell.rect.x + (cell.rect.width - textWidth) / 2);
        int textY = (int)(cell.rect.y + (cell.rect.height - fontSize) / 2);

        DrawText(labelText.c_str(), textX, textY, (int)fontSize, BLACK);
      }
    }

    EndDrawing();
  }

  CloseWindow();

  return 0;
}