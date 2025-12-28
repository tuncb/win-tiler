#ifdef DOCTEST_CONFIG_DISABLE

#include "multi_ui.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "options.h"
#include "raylib.h"

namespace wintiler {

namespace {

// Convert overlay::Color to Raylib Color
Color toRaylibColor(const overlay::Color& c) {
  return Color{c.r, c.g, c.b, c.a};
}

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
  cells::System system;
};

ViewTransform computeViewTransform(const cells::System& system, float screenW, float screenH,
                                   float margin) {
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

Rectangle toScreenRect(const ViewTransform& vt, const cells::Rect& globalRect) {
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
std::optional<std::pair<cells::ClusterId, int>>
findCellAtGlobalPoint(const cells::System& system, float globalX, float globalY) {
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (!cells::isLeaf(pc.cluster, i)) {
        continue;
      }

      cells::Rect globalRect = cells::getCellGlobalRect(pc, i);

      if (globalX >= globalRect.x && globalX < globalRect.x + globalRect.width &&
          globalY >= globalRect.y && globalY < globalRect.y + globalRect.height) {
        return std::make_pair(pc.id, i);
      }
    }
  }
  return std::nullopt;
}

void centerMouseOnSelection(const MultiClusterAppState& appState, const ViewTransform& vt) {
  auto selectedRect = cells::getSelectedCellGlobalRect(appState.system);
  if (selectedRect.has_value()) {
    float centerX = selectedRect->x + selectedRect->width / 2.0f;
    float centerY = selectedRect->y + selectedRect->height / 2.0f;

    float screenX, screenY;
    toScreenPoint(vt, centerX, centerY, screenX, screenY);

    SetMousePosition(static_cast<int>(screenX), static_cast<int>(screenY));
  }
}

std::vector<cells::ClusterCellIds> buildCurrentState(const cells::System& system) {
  std::vector<cells::ClusterCellIds> state;
  for (const auto& pc : system.clusters) {
    state.push_back({pc.id, cells::getClusterLeafIds(pc.cluster)});
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
  cells::updateSystem(appState.system, state, std::make_pair(selectedClusterId, newLeafId));
}

void deleteSelectedProcessMulti(MultiClusterAppState& appState) {
  auto selected = cells::getSelectedCell(appState.system);
  if (!selected.has_value()) {
    return;
  }

  auto [clusterId, cellIndex] = *selected;
  const auto* pc = cells::getCluster(appState.system, clusterId);
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
  cells::updateSystem(appState.system, state, std::nullopt);
}

Color getClusterColor(cells::ClusterId id) {
  return CLUSTER_COLORS[id % NUM_CLUSTER_COLORS];
}

std::optional<HotkeyAction> getKeyAction() {
  if (IsKeyPressed(KEY_H))
    return HotkeyAction::NavigateLeft;
  if (IsKeyPressed(KEY_J))
    return HotkeyAction::NavigateDown;
  if (IsKeyPressed(KEY_K))
    return HotkeyAction::NavigateUp;
  if (IsKeyPressed(KEY_L))
    return HotkeyAction::NavigateRight;
  if (IsKeyPressed(KEY_Y))
    return HotkeyAction::ToggleSplit;
  if (IsKeyPressed(KEY_LEFT_BRACKET))
    return HotkeyAction::StoreCell;
  if (IsKeyPressed(KEY_RIGHT_BRACKET))
    return HotkeyAction::ClearStored;
  if (IsKeyPressed(KEY_COMMA))
    return HotkeyAction::Exchange;
  if (IsKeyPressed(KEY_PERIOD))
    return HotkeyAction::Move;
  if (IsKeyPressed(KEY_PAGE_UP))
    return HotkeyAction::SplitIncrease;
  if (IsKeyPressed(KEY_PAGE_DOWN))
    return HotkeyAction::SplitDecrease;
  return std::nullopt;
}

} // namespace

void runRaylibUIMultiCluster(const std::vector<cells::ClusterInitInfo>& infos,
                             GlobalOptionsProvider& optionsProvider) {
  const auto& options = optionsProvider.options;

  MultiClusterAppState appState;
  appState.system =
      cells::createSystem(infos, options.gapOptions.horizontal, options.gapOptions.vertical);

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
  std::optional<std::pair<cells::ClusterId, size_t>> storedCell;

  while (!WindowShouldClose()) {
    // Check for config changes and hot-reload
    if (optionsProvider.refresh()) {
      cells::updateSystemGaps(appState.system, options.gapOptions.horizontal,
                              options.gapOptions.vertical);
    }

    // Mouse hover selection
    Vector2 mousePos = GetMousePosition();
    float globalX, globalY;
    toGlobalPoint(vt, mousePos.x, mousePos.y, globalX, globalY);

    auto cellAtMouse = findCellAtGlobalPoint(appState.system, globalX, globalY);
    if (cellAtMouse.has_value()) {
      auto [clusterId, cellIndex] = *cellAtMouse;

      // Update selection if different
      auto currentSel = cells::getSelectedCell(appState.system);
      if (!currentSel.has_value() || currentSel->first != clusterId ||
          currentSel->second != cellIndex) {
        // Set new selection
        appState.system.selection = cells::Selection{clusterId, cellIndex};
      }
    }
    // Note: Empty clusters no longer maintain "selected" state - selection requires a cell

    // Keyboard input (actions not in HotkeyAction enum)
    if (IsKeyPressed(KEY_SPACE)) {
      addNewProcessMulti(appState, nextProcessId);
    }

    if (IsKeyPressed(KEY_D)) {
      deleteSelectedProcessMulti(appState);
    }

    if (IsKeyPressed(KEY_I)) {
      cells::debugPrintSystem(appState.system);
    }

    if (IsKeyPressed(KEY_C)) {
      cells::validateSystem(appState.system);
    }

    // Keyboard input (HotkeyAction enum actions)
    auto action = getKeyAction();
    if (action.has_value()) {
      switch (*action) {
      case HotkeyAction::NavigateLeft:
        if (cells::moveSelection(appState.system, cells::Direction::Left)) {
          centerMouseOnSelection(appState, vt);
        }
        break;
      case HotkeyAction::NavigateDown:
        if (cells::moveSelection(appState.system, cells::Direction::Down)) {
          centerMouseOnSelection(appState, vt);
        }
        break;
      case HotkeyAction::NavigateUp:
        if (cells::moveSelection(appState.system, cells::Direction::Up)) {
          centerMouseOnSelection(appState, vt);
        }
        break;
      case HotkeyAction::NavigateRight:
        if (cells::moveSelection(appState.system, cells::Direction::Right)) {
          centerMouseOnSelection(appState, vt);
        }
        break;
      case HotkeyAction::ToggleSplit:
        cells::toggleSelectedSplitDir(appState.system);
        break;
      case HotkeyAction::StoreCell:
        if (appState.system.selection.has_value()) {
          auto* pc = cells::getCluster(appState.system, appState.system.selection->clusterId);
          if (pc) {
            auto& cell =
                pc->cluster.cells[static_cast<size_t>(appState.system.selection->cellIndex)];
            if (cell.leafId.has_value()) {
              storedCell = {appState.system.selection->clusterId, *cell.leafId};
            }
          }
        }
        break;
      case HotkeyAction::ClearStored:
        storedCell.reset();
        break;
      case HotkeyAction::Exchange:
        if (storedCell.has_value() && appState.system.selection.has_value()) {
          auto* pc = cells::getCluster(appState.system, appState.system.selection->clusterId);
          if (pc) {
            auto& cell =
                pc->cluster.cells[static_cast<size_t>(appState.system.selection->cellIndex)];
            if (cell.leafId.has_value()) {
              auto result = cells::swapCells(appState.system, appState.system.selection->clusterId,
                                             *cell.leafId, storedCell->first, storedCell->second);
              if (result.success) {
                storedCell.reset();
              }
            }
          }
        }
        break;
      case HotkeyAction::Move:
        if (storedCell.has_value() && appState.system.selection.has_value()) {
          auto* pc = cells::getCluster(appState.system, appState.system.selection->clusterId);
          if (pc) {
            auto& cell =
                pc->cluster.cells[static_cast<size_t>(appState.system.selection->cellIndex)];
            if (cell.leafId.has_value()) {
              auto result = cells::moveCell(appState.system, storedCell->first, storedCell->second,
                                            appState.system.selection->clusterId, *cell.leafId);
              if (result.success) {
                storedCell.reset();
              }
            }
          }
        }
        break;
      case HotkeyAction::SplitIncrease:
        if (cells::adjustSelectedSplitRatio(appState.system, 0.05f)) {
          centerMouseOnSelection(appState, vt);
        }
        break;
      case HotkeyAction::SplitDecrease:
        if (cells::adjustSelectedSplitRatio(appState.system, -0.05f)) {
          centerMouseOnSelection(appState, vt);
        }
        break;
      case HotkeyAction::Exit:
      case HotkeyAction::ToggleGlobal:
        // Not implemented in multi_ui
        break;
      }
    }

    // Drawing
    BeginDrawing();
    ClearBackground(RAYWHITE);

    // Draw cluster backgrounds first
    for (const auto& pc : appState.system.clusters) {
      cells::Rect clusterGlobalRect{pc.globalX, pc.globalY, pc.cluster.windowWidth,
                                    pc.cluster.windowHeight};
      Rectangle screenRect = toScreenRect(vt, clusterGlobalRect);
      DrawRectangleRec(screenRect, getClusterColor(pc.id));
      DrawRectangleLinesEx(screenRect, 2.0f, DARKGRAY);
    }

    // Draw cells
    auto selectedCell = cells::getSelectedCell(appState.system);

    for (const auto& pc : appState.system.clusters) {
      for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
        if (!cells::isLeaf(pc.cluster, i)) {
          continue;
        }

        const auto& cell = pc.cluster.cells[static_cast<size_t>(i)];
        cells::Rect globalRect = cells::getCellGlobalRect(pc, i);
        Rectangle screenRect = toScreenRect(vt, globalRect);

        bool isSelected =
            selectedCell.has_value() && selectedCell->first == pc.id && selectedCell->second == i;

        // Check if this cell is the stored cell
        bool isStoredCell = false;
        if (storedCell.has_value() && storedCell->first == pc.id) {
          auto storedIdx = cells::findCellByLeafId(pc.cluster, storedCell->second);
          if (storedIdx.has_value() && *storedIdx == i) {
            isStoredCell = true;
          }
        }

        // Determine border color and width from VisualizationOptions
        const auto& vizOpts = options.visualizationOptions;
        Color borderColor;
        float borderWidth;
        if (isSelected && isStoredCell) {
          borderColor = PURPLE;
          borderWidth = vizOpts.borderWidth + 1.0f;
        } else if (isStoredCell) {
          borderColor = toRaylibColor(vizOpts.storedColor);
          borderWidth = vizOpts.borderWidth;
        } else if (isSelected) {
          borderColor = toRaylibColor(vizOpts.selectedColor);
          borderWidth = vizOpts.borderWidth;
        } else {
          borderColor = toRaylibColor(vizOpts.normalColor);
          borderWidth = vizOpts.borderWidth;
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
