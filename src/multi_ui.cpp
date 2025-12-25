#ifdef DOCTEST_CONFIG_DISABLE

#include "multi_ui.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "raylib.h"

namespace wintiler {

namespace {

const size_t CELL_ID_START = 10;

// Semi-transparent cluster background colors
const Color CLUSTER_COLORS[] = {
    {100, 149, 237, 50}, // Cornflower blue
    {144, 238, 144, 50}, // Light green
    {255, 165, 0, 50},   // Orange
    {221, 160, 221, 50}, // Plum
    {255, 182, 193, 50}, // Light pink
    {255, 255, 0, 50},   // Yellow
    {0, 255, 255, 50},   // Cyan
    {255, 99, 71, 50},   // Tomato
};
const size_t NUM_CLUSTER_COLORS = sizeof(CLUSTER_COLORS) / sizeof(CLUSTER_COLORS[0]);

struct ViewTransform {
  float offsetX; // minX of bounding box
  float offsetY; // minY of bounding box
  float scale;   // uniform scale factor
  float margin;  // screen margin
  float screenWidth;
  float screenHeight;
};

struct MultiClusterAppState {
  multi_cell_logic::System system;
};

ViewTransform computeViewTransform(const multi_cell_logic::System& system, float screenW,
                                   float screenH, float margin) {
  if (system.clusters.empty()) {
    return ViewTransform{0.0f, 0.0f, 1.0f, margin, screenW, screenH};
  }

  float minX = std::numeric_limits<float>::max();
  float minY = std::numeric_limits<float>::max();
  float maxX = std::numeric_limits<float>::lowest();
  float maxY = std::numeric_limits<float>::lowest();

  for (const auto& pc : system.clusters) {
    minX = std::min(minX, pc.globalX);
    minY = std::min(minY, pc.globalY);
    maxX = std::max(maxX, pc.globalX + pc.cluster.windowWidth);
    maxY = std::max(maxY, pc.globalY + pc.cluster.windowHeight);
  }

  float worldW = maxX - minX;
  float worldH = maxY - minY;

  if (worldW <= 0.0f)
    worldW = 1.0f;
  if (worldH <= 0.0f)
    worldH = 1.0f;

  float availW = screenW - 2.0f * margin;
  float availH = screenH - 2.0f * margin;

  float scaleX = availW / worldW;
  float scaleY = availH / worldH;
  float scale = std::min(scaleX, scaleY);

  return ViewTransform{minX, minY, scale, margin, screenW, screenH};
}

Rectangle toScreenRect(const ViewTransform& vt, const cell_logic::Rect& globalRect) {
  return Rectangle{vt.margin + (globalRect.x - vt.offsetX) * vt.scale,
                   vt.margin + (globalRect.y - vt.offsetY) * vt.scale, globalRect.width * vt.scale,
                   globalRect.height * vt.scale};
}

void toGlobalPoint(const ViewTransform& vt, float screenX, float screenY, float& globalX,
                   float& globalY) {
  globalX = (screenX - vt.margin) / vt.scale + vt.offsetX;
  globalY = (screenY - vt.margin) / vt.scale + vt.offsetY;
}

void toScreenPoint(const ViewTransform& vt, float globalX, float globalY, float& screenX,
                   float& screenY) {
  screenX = vt.margin + (globalX - vt.offsetX) * vt.scale;
  screenY = vt.margin + (globalY - vt.offsetY) * vt.scale;
}

// Find the cluster and cell index at a global point
std::optional<std::pair<multi_cell_logic::ClusterId, int>>
findCellAtGlobalPoint(const multi_cell_logic::System& system, float globalX, float globalY) {
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (!cell_logic::isLeaf(pc.cluster, i)) {
        continue;
      }

      cell_logic::Rect globalRect = multi_cell_logic::getCellGlobalRect(pc, i);

      if (globalX >= globalRect.x && globalX < globalRect.x + globalRect.width &&
          globalY >= globalRect.y && globalY < globalRect.y + globalRect.height) {
        return std::make_pair(pc.id, i);
      }
    }
  }
  return std::nullopt;
}

void centerMouseOnSelection(const MultiClusterAppState& appState, const ViewTransform& vt) {
  auto selectedRect = multi_cell_logic::getSelectedCellGlobalRect(appState.system);
  if (selectedRect.has_value()) {
    float centerX = selectedRect->x + selectedRect->width / 2.0f;
    float centerY = selectedRect->y + selectedRect->height / 2.0f;

    float screenX, screenY;
    toScreenPoint(vt, centerX, centerY, screenX, screenY);

    SetMousePosition(static_cast<int>(screenX), static_cast<int>(screenY));
  }
}

std::vector<multi_cell_logic::ClusterCellIds> buildCurrentState(const multi_cell_logic::System& system) {
  std::vector<multi_cell_logic::ClusterCellIds> state;
  for (const auto& pc : system.clusters) {
    state.push_back({pc.id, multi_cell_logic::getClusterLeafIds(pc.cluster)});
  }
  return state;
}

void addNewProcessMulti(MultiClusterAppState& appState, size_t& nextProcessId) {
  if (!appState.system.selection.has_value()) {
    return;
  }

  auto selectedClusterId = appState.system.selection->clusterId;
  auto state = buildCurrentState(appState.system);

  // Add the new process ID (which becomes the leaf ID) to the selected cluster
  size_t newLeafId = nextProcessId++;
  for (auto& clusterCellIds : state) {
    if (clusterCellIds.clusterId == selectedClusterId) {
      clusterCellIds.leafIds.push_back(newLeafId);
      break;
    }
  }

  // Update system and select the newly added cell
  multi_cell_logic::updateSystem(appState.system, state,
                                 std::make_pair(selectedClusterId, newLeafId));
}

void deleteSelectedProcessMulti(MultiClusterAppState& appState) {
  auto selected = multi_cell_logic::getSelectedCell(appState.system);
  if (!selected.has_value()) {
    return;
  }

  auto [clusterId, cellIndex] = *selected;
  const auto* pc = multi_cell_logic::getCluster(appState.system, clusterId);
  if (!pc || cellIndex < 0 || static_cast<size_t>(cellIndex) >= pc->cluster.cells.size()) {
    return;
  }

  const auto& cell = pc->cluster.cells[static_cast<size_t>(cellIndex)];
  if (!cell.leafId.has_value()) {
    return;
  }

  size_t leafIdToRemove = *cell.leafId;
  auto state = buildCurrentState(appState.system);

  // Remove the leaf ID from the appropriate cluster's list
  for (auto& clusterCellIds : state) {
    if (clusterCellIds.clusterId == clusterId) {
      auto& ids = clusterCellIds.leafIds;
      ids.erase(std::remove(ids.begin(), ids.end(), leafIdToRemove), ids.end());
      break;
    }
  }

  // Update system (selection will auto-update)
  multi_cell_logic::updateSystem(appState.system, state, std::nullopt);
}

Color getClusterColor(multi_cell_logic::ClusterId id) {
  return CLUSTER_COLORS[id % NUM_CLUSTER_COLORS];
}

} // namespace

void runRaylibUIMultiCluster(const std::vector<multi_cell_logic::ClusterInitInfo>& infos,
                              std::optional<GapOptions> gapOptions) {
  MultiClusterAppState appState;
  appState.system = multi_cell_logic::createSystem(infos);
  if (gapOptions.has_value()) {
    appState.system.gapHorizontal = gapOptions->horizontal;
    appState.system.gapVertical = gapOptions->vertical;
  }

  // Set nextProcessId to avoid collisions with any pre-existing leaf IDs
  size_t nextProcessId = CELL_ID_START;
  for (const auto& pc : appState.system.clusters) {
    for (const auto& cell : pc.cluster.cells) {
      if (!cell.isDead && cell.leafId.has_value()) {
        nextProcessId = std::max(nextProcessId, *cell.leafId + 1);
      }
    }
  }

  const int screenWidth = 1600;
  const int screenHeight = 900;
  const float margin = 20.0f;

  InitWindow(screenWidth, screenHeight, "win-tiler multi-cluster");

  ViewTransform vt =
      computeViewTransform(appState.system, (float)screenWidth, (float)screenHeight, margin);

  SetTargetFPS(60);

  // Store cell for swap/move operations (clusterId, leafId)
  std::optional<std::pair<multi_cell_logic::ClusterId, size_t>> storedCell;

  while (!WindowShouldClose()) {
    // Mouse hover selection
    Vector2 mousePos = GetMousePosition();
    float globalX, globalY;
    toGlobalPoint(vt, mousePos.x, mousePos.y, globalX, globalY);

    auto cellAtMouse = findCellAtGlobalPoint(appState.system, globalX, globalY);
    if (cellAtMouse.has_value()) {
      auto [clusterId, cellIndex] = *cellAtMouse;

      // Update selection if different
      auto currentSel = multi_cell_logic::getSelectedCell(appState.system);
      if (!currentSel.has_value() || currentSel->first != clusterId ||
          currentSel->second != cellIndex) {
        // Set new selection
        appState.system.selection = multi_cell_logic::Selection{clusterId, cellIndex};
      }
    }
    // Note: Empty clusters no longer maintain "selected" state - selection requires a cell

    // Keyboard input
    if (IsKeyPressed(KEY_Y)) {
      multi_cell_logic::toggleSelectedSplitDir(appState.system);
    }

    if (IsKeyPressed(KEY_SPACE)) {
      addNewProcessMulti(appState, nextProcessId);
    }

    if (IsKeyPressed(KEY_D)) {
      deleteSelectedProcessMulti(appState);
    }

    if (IsKeyPressed(KEY_I)) {
      multi_cell_logic::debugPrintSystem(appState.system);
    }

    if (IsKeyPressed(KEY_C)) {
      multi_cell_logic::validateSystem(appState.system);
    }

    // Vim-style navigation: h=left, j=down, k=up, l=right
    if (IsKeyPressed(KEY_H)) {
      if (multi_cell_logic::moveSelection(appState.system, cell_logic::Direction::Left)) {
        centerMouseOnSelection(appState, vt);
      }
    }
    if (IsKeyPressed(KEY_L)) {
      if (multi_cell_logic::moveSelection(appState.system, cell_logic::Direction::Right)) {
        centerMouseOnSelection(appState, vt);
      }
    }
    if (IsKeyPressed(KEY_K)) {
      if (multi_cell_logic::moveSelection(appState.system, cell_logic::Direction::Up)) {
        centerMouseOnSelection(appState, vt);
      }
    }
    if (IsKeyPressed(KEY_J)) {
      if (multi_cell_logic::moveSelection(appState.system, cell_logic::Direction::Down)) {
        centerMouseOnSelection(appState, vt);
      }
    }

    // [ - Store currently selected cell for operation
    if (IsKeyPressed(KEY_LEFT_BRACKET)) {
      if (appState.system.selection.has_value()) {
        auto* pc =
            multi_cell_logic::getCluster(appState.system, appState.system.selection->clusterId);
        if (pc) {
          auto& cell = pc->cluster.cells[static_cast<size_t>(appState.system.selection->cellIndex)];
          if (cell.leafId.has_value()) {
            storedCell = {appState.system.selection->clusterId, *cell.leafId};
          }
        }
      }
    }

    // ] - Clear stored cell
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) {
      storedCell.reset();
    }

    // . - Move selected cell to stored cell
    if (IsKeyPressed(KEY_PERIOD)) {
      if (storedCell.has_value() && appState.system.selection.has_value()) {
        auto* pc =
            multi_cell_logic::getCluster(appState.system, appState.system.selection->clusterId);
        if (pc) {
          auto& cell = pc->cluster.cells[static_cast<size_t>(appState.system.selection->cellIndex)];
          if (cell.leafId.has_value()) {
            auto result =
                multi_cell_logic::moveCell(appState.system, storedCell->first, storedCell->second,
                                           appState.system.selection->clusterId, *cell.leafId);
            if (result.success) {
              storedCell.reset();
            }
          }
        }
      }
    }

    // , - Exchange/swap selected cell with stored cell
    if (IsKeyPressed(KEY_COMMA)) {
      if (storedCell.has_value() && appState.system.selection.has_value()) {
        auto* pc =
            multi_cell_logic::getCluster(appState.system, appState.system.selection->clusterId);
        if (pc) {
          auto& cell = pc->cluster.cells[static_cast<size_t>(appState.system.selection->cellIndex)];
          if (cell.leafId.has_value()) {
            auto result =
                multi_cell_logic::swapCells(appState.system, appState.system.selection->clusterId,
                                            *cell.leafId, storedCell->first, storedCell->second);
            if (result.success) {
              storedCell.reset();
            }
          }
        }
      }
    }

    // Drawing
    BeginDrawing();
    ClearBackground(RAYWHITE);

    // Draw cluster backgrounds first
    for (const auto& pc : appState.system.clusters) {
      cell_logic::Rect clusterGlobalRect{pc.globalX, pc.globalY, pc.cluster.windowWidth,
                                         pc.cluster.windowHeight};
      Rectangle screenRect = toScreenRect(vt, clusterGlobalRect);
      DrawRectangleRec(screenRect, getClusterColor(pc.id));
      DrawRectangleLinesEx(screenRect, 2.0f, DARKGRAY);
    }

    // Draw cells
    auto selectedCell = multi_cell_logic::getSelectedCell(appState.system);

    for (const auto& pc : appState.system.clusters) {
      for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
        if (!cell_logic::isLeaf(pc.cluster, i)) {
          continue;
        }

        const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
        cell_logic::Rect globalRect = multi_cell_logic::getCellGlobalRect(pc, i);
        Rectangle screenRect = toScreenRect(vt, globalRect);

        bool isSelected =
            selectedCell.has_value() && selectedCell->first == pc.id && selectedCell->second == i;

        // Check if this cell is the stored cell
        bool isStoredCell = false;
        if (storedCell.has_value() && storedCell->first == pc.id) {
          auto storedIdx = multi_cell_logic::findCellByLeafId(pc.cluster, storedCell->second);
          if (storedIdx.has_value() && *storedIdx == i) {
            isStoredCell = true;
          }
        }

        // Determine border color and width
        Color borderColor;
        float borderWidth;
        if (isSelected && isStoredCell) {
          borderColor = PURPLE;
          borderWidth = 4.0f;
        } else if (isStoredCell) {
          borderColor = BLUE;
          borderWidth = 3.0f;
        } else if (isSelected) {
          borderColor = RED;
          borderWidth = 3.0f;
        } else {
          borderColor = BLACK;
          borderWidth = 1.0f;
        }

        DrawRectangleLinesEx(screenRect, borderWidth, borderColor);

        // Draw process ID (leafId is the process ID)
        if (cell.leafId.has_value()) {
          std::string labelText = "P:" + std::to_string(*cell.leafId);
          float fontSize = std::min(screenRect.width, screenRect.height) * 0.2f;
          if (fontSize < 10.0f)
            fontSize = 10.0f;

          int textWidth = MeasureText(labelText.c_str(), (int)fontSize);
          int textX = (int)(screenRect.x + (screenRect.width - textWidth) / 2);
          int textY = (int)(screenRect.y + (screenRect.height - fontSize) / 2);

          DrawText(labelText.c_str(), textX, textY, (int)fontSize, DARKGRAY);
        }
      }
    }

    EndDrawing();
  }

  CloseWindow();
}

} // namespace wintiler

#endif
