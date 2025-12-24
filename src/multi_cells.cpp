#include "multi_cells.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace wintiler {

// ============================================================================
// cell_logic Implementation (formerly in cells.cpp)
// ============================================================================
namespace cell_logic {

CellCluster createInitialState(float width, float height) {
  CellCluster state{};

  state.cells.clear();
  state.nextLeafId = 1;

  state.windowWidth = width;
  state.windowHeight = height;

  state.globalSplitDir = SplitDir::Vertical;

  state.selectedIndex = std::nullopt;

  return state;
}

bool isLeaf(const CellCluster& state, int cellIndex) {
  if (cellIndex < 0 || static_cast<std::size_t>(cellIndex) >= state.cells.size()) {
    return false;
  }

  const Cell& cell = state.cells[static_cast<std::size_t>(cellIndex)];
  if (cell.isDead) {
    return false;
  }

  return !cell.firstChild.has_value() && !cell.secondChild.has_value();
}

int addCell(CellCluster& state, const Cell& cell) {
  state.cells.push_back(cell);
  return static_cast<int>(state.cells.size() - 1);
}

static void recomputeChildrenRects(CellCluster& state, int nodeIndex,
                                   float gapHorizontal, float gapVertical) {
  Cell& node = state.cells[static_cast<std::size_t>(nodeIndex)];

  if (node.isDead) {
    return;
  }

  if (!node.firstChild.has_value() || !node.secondChild.has_value()) {
    return;
  }

  Rect parentRect = node.rect;

  Rect first{};
  Rect second{};

  if (node.splitDir == SplitDir::Vertical) {
    float availableWidth = parentRect.width - gapHorizontal;
    float childWidth = availableWidth > 0.0f ? availableWidth * 0.5f : 0.0f;
    first = Rect{parentRect.x, parentRect.y, childWidth, parentRect.height};
    second = Rect{parentRect.x + childWidth + gapHorizontal, parentRect.y, childWidth,
                  parentRect.height};
  } else {
    float availableHeight = parentRect.height - gapVertical;
    float childHeight = availableHeight > 0.0f ? availableHeight * 0.5f : 0.0f;
    first = Rect{parentRect.x, parentRect.y, parentRect.width, childHeight};
    second = Rect{parentRect.x, parentRect.y + childHeight + gapVertical, parentRect.width,
                  childHeight};
  }

  Cell& firstChild = state.cells[static_cast<std::size_t>(*node.firstChild)];
  Cell& secondChild = state.cells[static_cast<std::size_t>(*node.secondChild)];

  firstChild.rect = first;
  secondChild.rect = second;
}

static void recomputeSubtreeRects(CellCluster& state, int nodeIndex,
                                  float gapHorizontal, float gapVertical) {
  if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= state.cells.size()) {
    return;
  }

  Cell& node = state.cells[static_cast<std::size_t>(nodeIndex)];

  if (node.isDead) {
    return;
  }

  if (node.firstChild.has_value() && node.secondChild.has_value()) {
    recomputeChildrenRects(state, nodeIndex, gapHorizontal, gapVertical);
    recomputeSubtreeRects(state, *node.firstChild, gapHorizontal, gapVertical);
    recomputeSubtreeRects(state, *node.secondChild, gapHorizontal, gapVertical);
  }
}

bool deleteSelectedLeaf(CellCluster& state, float gapHorizontal, float gapVertical) {
  if (!state.selectedIndex.has_value()) {
    return false;
  }

  int selected = *state.selectedIndex;
  if (!isLeaf(state, selected)) {
    return false;
  }
  if (state.cells.empty()) {
    return false;
  }

  Cell& selectedCell = state.cells[static_cast<std::size_t>(selected)];
  if (selectedCell.isDead) {
    return false;
  }

  if (selected == 0) {
    state.cells.clear();
    state.selectedIndex.reset();
    return true;
  }

  if (!selectedCell.parent.has_value()) {
    return false;
  }

  int parentIndex = *selectedCell.parent;
  Cell& parent = state.cells[static_cast<std::size_t>(parentIndex)];

  if (parent.isDead) {
    return false;
  }

  if (!parent.firstChild.has_value() || !parent.secondChild.has_value()) {
    return false;
  }

  int firstIdx = *parent.firstChild;
  int secondIdx = *parent.secondChild;
  int siblingIndex = (selected == firstIdx) ? secondIdx : firstIdx;

  Cell& sibling = state.cells[static_cast<std::size_t>(siblingIndex)];
  if (sibling.isDead) {
    return false;
  }

  Rect newRect = parent.rect;

  Cell promoted = sibling;
  promoted.rect = newRect;
  promoted.parent = parent.parent;

  if (promoted.firstChild.has_value()) {
    Cell& c1 = state.cells[static_cast<std::size_t>(*promoted.firstChild)];
    c1.parent = parentIndex;
  }
  if (promoted.secondChild.has_value()) {
    Cell& c2 = state.cells[static_cast<std::size_t>(*promoted.secondChild)];
    c2.parent = parentIndex;
  }

  state.cells[static_cast<std::size_t>(parentIndex)] = promoted;

  recomputeSubtreeRects(state, parentIndex, gapHorizontal, gapVertical);

  selectedCell.isDead = true;
  sibling.isDead = true;
  selectedCell.parent.reset();
  sibling.parent.reset();

  int current = parentIndex;
  while (!isLeaf(state, current)) {
    Cell& n = state.cells[static_cast<std::size_t>(current)];
    if (n.firstChild.has_value()) {
      current = *n.firstChild;
    } else if (n.secondChild.has_value()) {
      current = *n.secondChild;
    } else {
      break;
    }
  }

  state.selectedIndex = current;
  return true;
}

std::optional<size_t> splitSelectedLeaf(CellCluster& state, float gapHorizontal, float gapVertical) {
  if (!state.selectedIndex.has_value()) {
    if (state.cells.empty()) {
      Cell root{};
      root.splitDir = state.globalSplitDir;
      root.isDead = false;
      root.parent = std::nullopt;
      root.firstChild = std::nullopt;
      root.secondChild = std::nullopt;
      root.leafId = state.nextLeafId++;

      float rootW = state.windowWidth;
      float rootH = state.windowHeight;
      root.rect = Rect{0.0f, 0.0f, rootW > 0.0f ? rootW : 0.0f, rootH > 0.0f ? rootH : 0.0f};

      int index = addCell(state, root);
      state.selectedIndex = index;

      return root.leafId;
    }

    return std::nullopt;
  }

  int selected = *state.selectedIndex;
  if (!isLeaf(state, selected)) {
    return std::nullopt;
  }

  Cell& leaf = state.cells[static_cast<std::size_t>(selected)];
  if (leaf.isDead) {
    return std::nullopt;
  }
  Rect r = leaf.rect;

  size_t parentLeafId = *leaf.leafId;

  Rect firstRect{};
  Rect secondRect{};

  if (state.globalSplitDir == SplitDir::Vertical) {
    float availableWidth = r.width - gapHorizontal;
    float childWidth = availableWidth > 0.0f ? availableWidth * 0.5f : 0.0f;
    firstRect = Rect{r.x, r.y, childWidth, r.height};
    secondRect = Rect{r.x + childWidth + gapHorizontal, r.y, childWidth, r.height};
  } else {
    float availableHeight = r.height - gapVertical;
    float childHeight = availableHeight > 0.0f ? availableHeight * 0.5f : 0.0f;
    firstRect = Rect{r.x, r.y, r.width, childHeight};
    secondRect = Rect{r.x, r.y + childHeight + gapVertical, r.width, childHeight};
  }

  leaf.splitDir = state.globalSplitDir;
  leaf.leafId = std::nullopt;

  Cell firstChild{};
  firstChild.splitDir = state.globalSplitDir;
  firstChild.isDead = false;
  firstChild.parent = selected;
  firstChild.firstChild = std::nullopt;
  firstChild.secondChild = std::nullopt;
  firstChild.rect = firstRect;
  firstChild.leafId = parentLeafId;

  Cell secondChild{};
  secondChild.splitDir = state.globalSplitDir;
  secondChild.isDead = false;
  secondChild.parent = selected;
  secondChild.firstChild = std::nullopt;
  secondChild.secondChild = std::nullopt;
  secondChild.rect = secondRect;
  secondChild.leafId = state.nextLeafId++;

  int firstIndex = addCell(state, firstChild);
  int secondIndex = addCell(state, secondChild);

  {
    Cell& parent = state.cells[static_cast<std::size_t>(selected)];
    parent.firstChild = firstIndex;
    parent.secondChild = secondIndex;
  }

  state.selectedIndex = firstIndex;

  state.globalSplitDir =
      (state.globalSplitDir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;

  return secondChild.leafId;
}

bool toggleSelectedSplitDir(CellCluster& state, float gapHorizontal, float gapVertical) {
  if (!state.selectedIndex.has_value()) {
    return false;
  }

  int selected = *state.selectedIndex;
  if (!isLeaf(state, selected)) {
    return false;
  }

  Cell& leaf = state.cells[static_cast<std::size_t>(selected)];
  if (leaf.isDead) {
    return false;
  }

  if (!leaf.parent.has_value()) {
    return false;
  }

  int parentIndex = *leaf.parent;
  Cell& parent = state.cells[static_cast<std::size_t>(parentIndex)];

  if (parent.isDead) {
    return false;
  }

  if (!parent.firstChild.has_value() || !parent.secondChild.has_value()) {
    return false;
  }

  int firstIdx = *parent.firstChild;
  int secondIdx = *parent.secondChild;
  int siblingIndex = (selected == firstIdx) ? secondIdx : firstIdx;

  if (!isLeaf(state, siblingIndex)) {
    return false;
  }

  Cell& sibling = state.cells[static_cast<std::size_t>(siblingIndex)];
  if (sibling.isDead) {
    return false;
  }

  parent.splitDir =
      (parent.splitDir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;

  recomputeSubtreeRects(state, parentIndex, gapHorizontal, gapVertical);

  (void)sibling;

  return true;
}

void debugPrintState(const CellCluster& state) {
  std::cout << "===== CellCluster =====" << std::endl;

  std::cout << "cells.size = " << state.cells.size() << std::endl;

  std::cout << "selectedIndex = ";
  if (state.selectedIndex.has_value())
    std::cout << *state.selectedIndex;
  else
    std::cout << "null";
  std::cout << std::endl;

  std::cout << "globalSplitDir = "
            << (state.globalSplitDir == SplitDir::Vertical ? "Vertical" : "Horizontal")
            << std::endl;

  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    const Cell& c = state.cells[i];
    if (c.isDead) {
      continue;
    }
    std::cout << "-- Cell " << i << " --" << std::endl;
    std::cout << "  kind = " << (c.leafId.has_value() ? "Leaf" : "Split") << std::endl;
    std::cout << "  splitDir = " << (c.splitDir == SplitDir::Vertical ? "Vertical" : "Horizontal")
              << std::endl;

    std::cout << "  parent = ";
    if (c.parent.has_value())
      std::cout << *c.parent;
    else
      std::cout << "null";
    std::cout << std::endl;

    std::cout << "  firstChild = ";
    if (c.firstChild.has_value())
      std::cout << *c.firstChild;
    else
      std::cout << "null";
    std::cout << std::endl;

    std::cout << "  secondChild = ";
    if (c.secondChild.has_value())
      std::cout << *c.secondChild;
    else
      std::cout << "null";
    std::cout << std::endl;

    std::cout << "  rect = { x=" << c.rect.x << ", y=" << c.rect.y << ", w=" << c.rect.width
              << ", h=" << c.rect.height << " }" << std::endl;
  }

  std::cout << "===== End CellCluster =====" << std::endl;
}

bool validateState(const CellCluster& state) {
  bool ok = true;

  if (state.cells.empty()) {
    if (state.selectedIndex.has_value()) {
      std::cout << "[validate] ERROR: empty state has non-null selectedIndex" << std::endl;
      ok = false;
    }

    if (ok) {
      std::cout << "[validate] State OK (empty)" << std::endl;
    } else {
      std::cout << "[validate] State has anomalies (empty)" << std::endl;
    }

    return ok;
  }

  if (state.cells[0].parent.has_value()) {
    std::cout << "[validate] ERROR: root cell (index 0) has a parent" << std::endl;
    ok = false;
  }

  std::vector<int> parentRefCount(state.cells.size(), 0);
  std::vector<int> childRefCount(state.cells.size(), 0);

  for (int i = 0; i < static_cast<int>(state.cells.size()); ++i) {
    const Cell& c = state.cells[static_cast<std::size_t>(i)];

    if (c.isDead) {
      continue;
    }

    if (c.parent.has_value()) {
      int p = *c.parent;
      if (p < 0 || static_cast<std::size_t>(p) >= state.cells.size()) {
        std::cout << "[validate] ERROR: cell " << i << " has out-of-range parent index " << p
                  << std::endl;
        ok = false;
      } else {
        parentRefCount[static_cast<std::size_t>(i)]++;
      }
    }

    if (c.leafId.has_value()) {
      if (c.firstChild.has_value() || c.secondChild.has_value()) {
        std::cout << "[validate] ERROR: leaf cell " << i << " has children" << std::endl;
        ok = false;
      }
    } else {
      if (!c.firstChild.has_value() || !c.secondChild.has_value()) {
        std::cout << "[validate] ERROR: split cell " << i << " is missing children" << std::endl;
        ok = false;
      }
    }

    auto checkChild = [&](const std::optional<int>& childOpt, const char* label) {
      if (!childOpt.has_value()) {
        return;
      }

      int child = *childOpt;
      if (child < 0 || static_cast<std::size_t>(child) >= state.cells.size()) {
        std::cout << "[validate] ERROR: cell " << i << " has out-of-range " << label << " index "
                  << child << std::endl;
        ok = false;
        return;
      }

      const Cell& cc = state.cells[static_cast<std::size_t>(child)];
      if (cc.isDead) {
        std::cout << "[validate] WARNING: cell " << i << "'s " << label << " (" << child
                  << ") is dead" << std::endl;
        ok = false;
      }
      if (!cc.parent.has_value() || *cc.parent != i) {
        std::cout << "[validate] ERROR: cell " << i << "'s " << label << " (" << child
                  << ") does not point back to parent " << i << std::endl;
        ok = false;
      }

      childRefCount[static_cast<std::size_t>(child)]++;
    };

    checkChild(c.firstChild, "firstChild");
    checkChild(c.secondChild, "secondChild");
  }

  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    if (parentRefCount[i] > 1) {
      std::cout << "[validate] WARNING: cell " << i << " has parent set more than once ("
                << parentRefCount[i] << ")" << std::endl;
      ok = false;
    }

    if (childRefCount[i] > 2) {
      std::cout << "[validate] WARNING: cell " << i << " is referenced as a child more than twice ("
                << childRefCount[i] << ")" << std::endl;
      ok = false;
    }
  }

  std::vector<size_t> leafIds;
  for (int i = 0; i < static_cast<int>(state.cells.size()); ++i) {
    const Cell& c = state.cells[static_cast<std::size_t>(i)];
    if (c.isDead) {
      continue;
    }
    if (c.leafId.has_value()) {
      leafIds.push_back(*c.leafId);
    }
  }

  std::sort(leafIds.begin(), leafIds.end());
  for (std::size_t i = 1; i < leafIds.size(); ++i) {
    if (leafIds[i] == leafIds[i - 1]) {
      std::cout << "[validate] ERROR: duplicate leafId " << leafIds[i] << " found" << std::endl;
      ok = false;
    }
  }

  if (ok) {
    std::cout << "[validate] State OK (" << state.cells.size() << " cells)" << std::endl;
  } else {
    std::cout << "[validate] State has anomalies" << std::endl;
  }

  return ok;
}

} // namespace cell_logic

// ============================================================================
// multi_cell_logic Implementation
// ============================================================================
namespace multi_cell_logic {

// ============================================================================
// Helper: Pre-create leaves in a cluster from initialCellIds
// ============================================================================

static void preCreateLeaves(PositionedCluster& pc, const std::vector<size_t>& cellIds,
                            size_t& globalNextLeafId, float gapHorizontal, float gapVertical) {
  for (size_t i = 0; i < cellIds.size(); ++i) {
    size_t cellId = cellIds[i];

    if (pc.cluster.cells.empty()) {
      // First cell: create root leaf
      pc.cluster.nextLeafId = globalNextLeafId;
      auto leafIdOpt = cell_logic::splitSelectedLeaf(pc.cluster, gapHorizontal, gapVertical);
      if (leafIdOpt.has_value()) {
        // The root leaf was just created. Overwrite its leafId with the provided one.
        if (pc.cluster.selectedIndex.has_value()) {
          int idx = *pc.cluster.selectedIndex;
          pc.cluster.cells[static_cast<size_t>(idx)].leafId = cellId;
        }
        globalNextLeafId = pc.cluster.nextLeafId;
      }
    } else {
      // Subsequent cells: split current selection
      pc.cluster.nextLeafId = globalNextLeafId;
      auto leafIdOpt = cell_logic::splitSelectedLeaf(pc.cluster, gapHorizontal, gapVertical);
      if (leafIdOpt.has_value()) {
        // The new leaf (second child) was created. Find it and overwrite its leafId.
        // After split, selection moves to first child. The new leaf is the sibling.
        if (pc.cluster.selectedIndex.has_value()) {
          int firstChildIdx = *pc.cluster.selectedIndex;
          cell_logic::Cell& firstChild =
              pc.cluster.cells[static_cast<size_t>(firstChildIdx)];
          if (firstChild.parent.has_value()) {
            int parentIdx = *firstChild.parent;
            cell_logic::Cell& parent =
                pc.cluster.cells[static_cast<size_t>(parentIdx)];
            if (parent.secondChild.has_value()) {
              int secondChildIdx = *parent.secondChild;
              pc.cluster.cells[static_cast<size_t>(secondChildIdx)].leafId = cellId;
            }
          }
        }
        globalNextLeafId = pc.cluster.nextLeafId;
      }
    }
  }
}

// ============================================================================
// Initialization
// ============================================================================

System createSystem(const std::vector<ClusterInitInfo>& infos) {
  System system;
  system.globalNextLeafId = 1;
  system.clusters.reserve(infos.size());

  for (const auto& info : infos) {
    PositionedCluster pc;
    pc.id = info.id;
    pc.globalX = info.x;
    pc.globalY = info.y;
    pc.cluster = cell_logic::createInitialState(info.width, info.height);

    // Pre-create leaves if initialCellIds provided
    if (!info.initialCellIds.empty()) {
      preCreateLeaves(pc, info.initialCellIds, system.globalNextLeafId,
                      system.gapHorizontal, system.gapVertical);
    }

    // If this is the first cluster with a selection, make it the selected cluster
    if (!system.selectedClusterId.has_value() && pc.cluster.selectedIndex.has_value()) {
      system.selectedClusterId = pc.id;
    }

    system.clusters.push_back(std::move(pc));
  }

  return system;
}

ClusterId addCluster(System& system, const ClusterInitInfo& info) {
  PositionedCluster pc;
  pc.id = info.id;
  pc.globalX = info.x;
  pc.globalY = info.y;
  pc.cluster = cell_logic::createInitialState(info.width, info.height);

  if (!info.initialCellIds.empty()) {
    preCreateLeaves(pc, info.initialCellIds, system.globalNextLeafId,
                    system.gapHorizontal, system.gapVertical);
  }

  if (!system.selectedClusterId.has_value() && pc.cluster.selectedIndex.has_value()) {
    system.selectedClusterId = pc.id;
  }

  system.clusters.push_back(std::move(pc));
  return info.id;
}

bool removeCluster(System& system, ClusterId id) {
  auto it = std::find_if(system.clusters.begin(), system.clusters.end(),
                         [id](const PositionedCluster& pc) { return pc.id == id; });

  if (it == system.clusters.end()) {
    return false;
  }

  bool wasSelected = system.selectedClusterId.has_value() && *system.selectedClusterId == id;

  system.clusters.erase(it);

  // If the removed cluster was selected, move selection to another cluster
  if (wasSelected) {
    system.selectedClusterId.reset();
    for (auto& pc : system.clusters) {
      if (pc.cluster.selectedIndex.has_value()) {
        system.selectedClusterId = pc.id;
        break;
      }
    }
  }

  return true;
}

PositionedCluster* getCluster(System& system, ClusterId id) {
  for (auto& pc : system.clusters) {
    if (pc.id == id) {
      return &pc;
    }
  }
  return nullptr;
}

const PositionedCluster* getCluster(const System& system, ClusterId id) {
  for (const auto& pc : system.clusters) {
    if (pc.id == id) {
      return &pc;
    }
  }
  return nullptr;
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

cell_logic::Rect localToGlobal(const PositionedCluster& pc, const cell_logic::Rect& localRect) {
  return cell_logic::Rect{localRect.x + pc.globalX, localRect.y + pc.globalY, localRect.width,
                          localRect.height};
}

cell_logic::Rect globalToLocal(const PositionedCluster& pc, const cell_logic::Rect& globalRect) {
  return cell_logic::Rect{globalRect.x - pc.globalX, globalRect.y - pc.globalY, globalRect.width,
                          globalRect.height};
}

cell_logic::Rect getCellGlobalRect(const PositionedCluster& pc, int cellIndex) {
  if (cellIndex < 0 || static_cast<size_t>(cellIndex) >= pc.cluster.cells.size()) {
    return cell_logic::Rect{0.0f, 0.0f, 0.0f, 0.0f};
  }

  const cell_logic::Cell& cell = pc.cluster.cells[static_cast<size_t>(cellIndex)];
  return localToGlobal(pc, cell.rect);
}

// ============================================================================
// Cross-Cluster Navigation Helpers
// ============================================================================

static bool isInDirectionGlobal(const cell_logic::Rect& from, const cell_logic::Rect& to,
                                cell_logic::Direction dir) {
  switch (dir) {
  case cell_logic::Direction::Left:
    return to.x + to.width <= from.x;
  case cell_logic::Direction::Right:
    return to.x >= from.x + from.width;
  case cell_logic::Direction::Up:
    return to.y + to.height <= from.y;
  case cell_logic::Direction::Down:
    return to.y >= from.y + from.height;
  default:
    return false;
  }
}

static float directionalDistanceGlobal(const cell_logic::Rect& from, const cell_logic::Rect& to,
                                       cell_logic::Direction dir) {
  float dxCenter = (to.x + to.width * 0.5f) - (from.x + from.width * 0.5f);
  float dyCenter = (to.y + to.height * 0.5f) - (from.y + from.height * 0.5f);

  // Check for perpendicular overlap - cells that share vertical/horizontal space
  // are strongly preferred over cells that don't
  bool hasVerticalOverlap = (to.y < from.y + from.height) && (to.y + to.height > from.y);
  bool hasHorizontalOverlap = (to.x < from.x + from.width) && (to.x + to.width > from.x);

  switch (dir) {
  case cell_logic::Direction::Left:
  case cell_logic::Direction::Right: {
    float primaryDist = (dir == cell_logic::Direction::Left) ? -dxCenter : dxCenter;
    if (hasVerticalOverlap) {
      return primaryDist; // Overlapping cells get pure horizontal distance
    }
    // Non-overlapping cells get a large penalty
    float gap = std::min(std::abs(to.y - (from.y + from.height)),
                         std::abs(from.y - (to.y + to.height)));
    return primaryDist + 10000.0f + gap;
  }
  case cell_logic::Direction::Up:
  case cell_logic::Direction::Down: {
    float primaryDist = (dir == cell_logic::Direction::Up) ? -dyCenter : dyCenter;
    if (hasHorizontalOverlap) {
      return primaryDist; // Overlapping cells get pure vertical distance
    }
    float gap = std::min(std::abs(to.x - (from.x + from.width)),
                         std::abs(from.x - (to.x + to.width)));
    return primaryDist + 10000.0f + gap;
  }
  default:
    return std::numeric_limits<float>::max();
  }
}

static bool isClusterInDirection(const PositionedCluster& pc, const cell_logic::Rect& fromGlobalRect,
                                 cell_logic::Direction dir) {
  cell_logic::Rect clusterBounds{pc.globalX, pc.globalY, pc.cluster.windowWidth,
                                 pc.cluster.windowHeight};
  return isInDirectionGlobal(fromGlobalRect, clusterBounds, dir);
}

// ============================================================================
// Cross-Cluster Navigation
// ============================================================================

std::optional<std::pair<ClusterId, int>>
findNextLeafInDirection(const System& system, ClusterId currentClusterId, int currentCellIndex,
                        cell_logic::Direction dir) {
  const PositionedCluster* currentPC = getCluster(system, currentClusterId);
  if (!currentPC) {
    return std::nullopt;
  }

  if (!cell_logic::isLeaf(currentPC->cluster, currentCellIndex)) {
    return std::nullopt;
  }

  cell_logic::Rect currentGlobalRect = getCellGlobalRect(*currentPC, currentCellIndex);

  std::optional<std::pair<ClusterId, int>> bestCandidate;
  float bestScore = std::numeric_limits<float>::max();

  // Search all clusters
  for (const auto& pc : system.clusters) {
    // Quick reject: skip clusters not in the desired direction
    // (except for current cluster, always search it for intra-cluster navigation)
    if (pc.id != currentClusterId && !isClusterInDirection(pc, currentGlobalRect, dir)) {
      continue;
    }

    // Search all leaves in this cluster
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      // Skip non-leaves and dead cells
      if (!cell_logic::isLeaf(pc.cluster, i)) {
        continue;
      }

      // Skip current cell
      if (pc.id == currentClusterId && i == currentCellIndex) {
        continue;
      }

      cell_logic::Rect candidateGlobalRect = getCellGlobalRect(pc, i);

      if (!isInDirectionGlobal(currentGlobalRect, candidateGlobalRect, dir)) {
        continue;
      }

      float score = directionalDistanceGlobal(currentGlobalRect, candidateGlobalRect, dir);
      if (score < bestScore) {
        bestScore = score;
        bestCandidate = std::make_pair(pc.id, i);
      }
    }
  }

  return bestCandidate;
}

bool moveSelection(System& system, cell_logic::Direction dir) {
  if (!system.selectedClusterId.has_value()) {
    return false;
  }

  PositionedCluster* currentPC = getCluster(system, *system.selectedClusterId);
  if (!currentPC || !currentPC->cluster.selectedIndex.has_value()) {
    return false;
  }

  int currentCellIndex = *currentPC->cluster.selectedIndex;

  auto nextOpt = findNextLeafInDirection(system, *system.selectedClusterId, currentCellIndex, dir);
  if (!nextOpt.has_value()) {
    return false;
  }

  auto [nextClusterId, nextCellIndex] = *nextOpt;

  // Update selection
  if (nextClusterId != *system.selectedClusterId) {
    // Cross-cluster move: clear selection in old cluster, set in new
    currentPC->cluster.selectedIndex.reset();

    PositionedCluster* nextPC = getCluster(system, nextClusterId);
    if (nextPC) {
      nextPC->cluster.selectedIndex = nextCellIndex;
      system.selectedClusterId = nextClusterId;
    }
  } else {
    // Same cluster move
    currentPC->cluster.selectedIndex = nextCellIndex;
  }

  return true;
}

// ============================================================================
// Operations
// ============================================================================

std::optional<size_t> splitSelectedLeaf(System& system) {
  if (!system.selectedClusterId.has_value()) {
    return std::nullopt;
  }

  PositionedCluster* pc = getCluster(system, *system.selectedClusterId);
  if (!pc) {
    return std::nullopt;
  }

  // Sync the global leaf ID counter
  pc->cluster.nextLeafId = system.globalNextLeafId;

  auto result = cell_logic::splitSelectedLeaf(pc->cluster, system.gapHorizontal, system.gapVertical);

  // Sync back after split
  system.globalNextLeafId = pc->cluster.nextLeafId;

  return result;
}

bool deleteSelectedLeaf(System& system) {
  if (!system.selectedClusterId.has_value()) {
    return false;
  }

  PositionedCluster* pc = getCluster(system, *system.selectedClusterId);
  if (!pc) {
    return false;
  }

  bool result = cell_logic::deleteSelectedLeaf(pc->cluster, system.gapHorizontal, system.gapVertical);

  // If the cluster became empty, move selection to another cluster
  if (result && pc->cluster.cells.empty()) {
    system.selectedClusterId.reset();
    for (auto& otherPc : system.clusters) {
      if (otherPc.cluster.selectedIndex.has_value()) {
        system.selectedClusterId = otherPc.id;
        break;
      }
    }
  }

  return result;
}

std::optional<std::pair<ClusterId, int>> getSelectedCell(const System& system) {
  if (!system.selectedClusterId.has_value()) {
    return std::nullopt;
  }

  const PositionedCluster* pc = getCluster(system, *system.selectedClusterId);
  if (!pc || !pc->cluster.selectedIndex.has_value()) {
    return std::nullopt;
  }

  return std::make_pair(*system.selectedClusterId, *pc->cluster.selectedIndex);
}

std::optional<cell_logic::Rect> getSelectedCellGlobalRect(const System& system) {
  auto selectedOpt = getSelectedCell(system);
  if (!selectedOpt.has_value()) {
    return std::nullopt;
  }

  auto [clusterId, cellIndex] = *selectedOpt;
  const PositionedCluster* pc = getCluster(system, clusterId);
  if (!pc) {
    return std::nullopt;
  }

  return getCellGlobalRect(*pc, cellIndex);
}

bool toggleSelectedSplitDir(System& system) {
  if (!system.selectedClusterId.has_value()) {
    return false;
  }

  PositionedCluster* pc = getCluster(system, *system.selectedClusterId);
  if (!pc) {
    return false;
  }

  return cell_logic::toggleSelectedSplitDir(pc->cluster, system.gapHorizontal, system.gapVertical);
}

// ============================================================================
// Utilities
// ============================================================================

bool validateSystem(const System& system) {
  bool ok = true;

  std::cout << "===== Validating MultiClusterSystem =====" << std::endl;
  std::cout << "Total clusters: " << system.clusters.size() << std::endl;
  std::cout << "selectedClusterId: ";
  if (system.selectedClusterId.has_value()) {
    std::cout << *system.selectedClusterId;
  } else {
    std::cout << "null";
  }
  std::cout << std::endl;

  // Check that selectedClusterId points to a valid cluster
  if (system.selectedClusterId.has_value()) {
    const PositionedCluster* selectedPc = getCluster(system, *system.selectedClusterId);
    if (!selectedPc) {
      std::cout << "[validate] ERROR: selectedClusterId points to non-existent cluster"
                << std::endl;
      ok = false;
    } else if (!selectedPc->cluster.selectedIndex.has_value()) {
      std::cout << "[validate] WARNING: selected cluster has no selectedIndex" << std::endl;
    }
  }

  // Validate each cluster
  for (const auto& pc : system.clusters) {
    std::cout << "--- Cluster " << pc.id << " at (" << pc.globalX << ", " << pc.globalY << ") ---"
              << std::endl;
    if (!cell_logic::validateState(pc.cluster)) {
      ok = false;
    }
  }

  // Check for duplicate cluster IDs
  std::vector<ClusterId> ids;
  for (const auto& pc : system.clusters) {
    ids.push_back(pc.id);
  }
  std::sort(ids.begin(), ids.end());
  for (size_t i = 1; i < ids.size(); ++i) {
    if (ids[i] == ids[i - 1]) {
      std::cout << "[validate] ERROR: duplicate cluster ID " << ids[i] << std::endl;
      ok = false;
    }
  }

  // Check for duplicate leafIds across all clusters
  std::vector<size_t> allLeafIds;
  for (const auto& pc : system.clusters) {
    for (const auto& cell : pc.cluster.cells) {
      if (!cell.isDead && cell.leafId.has_value()) {
        allLeafIds.push_back(*cell.leafId);
      }
    }
  }
  std::sort(allLeafIds.begin(), allLeafIds.end());
  for (size_t i = 1; i < allLeafIds.size(); ++i) {
    if (allLeafIds[i] == allLeafIds[i - 1]) {
      std::cout << "[validate] ERROR: duplicate leafId " << allLeafIds[i] << " across clusters"
                << std::endl;
      ok = false;
    }
  }

  if (ok) {
    std::cout << "[validate] System OK" << std::endl;
  } else {
    std::cout << "[validate] System has anomalies" << std::endl;
  }

  std::cout << "===== End Validation =====" << std::endl;

  return ok;
}

void debugPrintSystem(const System& system) {
  std::cout << "===== MultiClusterSystem =====" << std::endl;
  std::cout << "clusters.size = " << system.clusters.size() << std::endl;
  std::cout << "globalNextLeafId = " << system.globalNextLeafId << std::endl;

  std::cout << "selectedClusterId = ";
  if (system.selectedClusterId.has_value()) {
    std::cout << *system.selectedClusterId;
  } else {
    std::cout << "null";
  }
  std::cout << std::endl;

  for (const auto& pc : system.clusters) {
    std::cout << "--- Cluster " << pc.id << " ---" << std::endl;
    std::cout << "  globalX = " << pc.globalX << ", globalY = " << pc.globalY << std::endl;
    cell_logic::debugPrintState(pc.cluster);
  }

  std::cout << "===== End MultiClusterSystem =====" << std::endl;
}

size_t countTotalLeaves(const System& system) {
  size_t count = 0;
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (cell_logic::isLeaf(pc.cluster, i)) {
        ++count;
      }
    }
  }
  return count;
}

} // namespace multi_cell_logic
} // namespace wintiler
