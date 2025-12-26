#include "multi_cells.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include <spdlog/spdlog.h>

namespace wintiler {

// ============================================================================
// Cell Logic Implementation
// ============================================================================
namespace cells {

CellCluster createInitialState(float width, float height) {
  CellCluster state{};

  state.cells.clear();

  state.windowWidth = width;
  state.windowHeight = height;

  state.globalSplitDir = SplitDir::Vertical;

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

std::optional<int> deleteLeaf(CellCluster& state, int selectedIndex, float gapHorizontal, float gapVertical) {
  if (!isLeaf(state, selectedIndex)) {
    return std::nullopt;
  }
  if (state.cells.empty()) {
    return std::nullopt;
  }

  Cell& selectedCell = state.cells[static_cast<std::size_t>(selectedIndex)];
  if (selectedCell.isDead) {
    return std::nullopt;
  }

  if (selectedIndex == 0) {
    state.cells.clear();
    return std::nullopt;  // Cluster is now empty
  }

  if (!selectedCell.parent.has_value()) {
    return std::nullopt;
  }

  int parentIndex = *selectedCell.parent;
  Cell& parent = state.cells[static_cast<std::size_t>(parentIndex)];

  if (parent.isDead) {
    return std::nullopt;
  }

  if (!parent.firstChild.has_value() || !parent.secondChild.has_value()) {
    return std::nullopt;
  }

  int firstIdx = *parent.firstChild;
  int secondIdx = *parent.secondChild;
  int siblingIndex = (selectedIndex == firstIdx) ? secondIdx : firstIdx;

  Cell& sibling = state.cells[static_cast<std::size_t>(siblingIndex)];
  if (sibling.isDead) {
    return std::nullopt;
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

  return current;  // New selection index
}

std::optional<SplitResult> splitLeaf(CellCluster& state, int selectedIndex, float gapHorizontal, float gapVertical, size_t& nextLeafId) {
  // Special case: if cluster is empty and selectedIndex is -1, create root
  if (state.cells.empty() && selectedIndex == -1) {
    Cell root{};
    root.splitDir = state.globalSplitDir;
    root.isDead = false;
    root.parent = std::nullopt;
    root.firstChild = std::nullopt;
    root.secondChild = std::nullopt;
    root.leafId = nextLeafId++;

    float rootW = state.windowWidth;
    float rootH = state.windowHeight;
    root.rect = Rect{0.0f, 0.0f, rootW > 0.0f ? rootW : 0.0f, rootH > 0.0f ? rootH : 0.0f};

    int index = addCell(state, root);

    return SplitResult{*root.leafId, index};
  }

  if (!isLeaf(state, selectedIndex)) {
    return std::nullopt;
  }

  Cell& leaf = state.cells[static_cast<std::size_t>(selectedIndex)];
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
  firstChild.parent = selectedIndex;
  firstChild.firstChild = std::nullopt;
  firstChild.secondChild = std::nullopt;
  firstChild.rect = firstRect;
  firstChild.leafId = parentLeafId;

  Cell secondChild{};
  secondChild.splitDir = state.globalSplitDir;
  secondChild.isDead = false;
  secondChild.parent = selectedIndex;
  secondChild.firstChild = std::nullopt;
  secondChild.secondChild = std::nullopt;
  secondChild.rect = secondRect;
  secondChild.leafId = nextLeafId++;

  int firstIndex = addCell(state, firstChild);
  int secondIndex = addCell(state, secondChild);

  {
    Cell& parent = state.cells[static_cast<std::size_t>(selectedIndex)];
    parent.firstChild = firstIndex;
    parent.secondChild = secondIndex;
  }

  state.globalSplitDir =
      (state.globalSplitDir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;

  return SplitResult{secondChild.leafId.value(), firstIndex};
}

bool toggleSplitDir(CellCluster& state, int selectedIndex, float gapHorizontal, float gapVertical) {
  if (!isLeaf(state, selectedIndex)) {
    return false;
  }

  Cell& leaf = state.cells[static_cast<std::size_t>(selectedIndex)];
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
  int siblingIndex = (selectedIndex == firstIdx) ? secondIdx : firstIdx;

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
  spdlog::debug("===== CellCluster =====");
  spdlog::debug("cells.size = {}", state.cells.size());
  spdlog::debug("globalSplitDir = {}",
                state.globalSplitDir == SplitDir::Vertical ? "Vertical" : "Horizontal");

  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    const Cell& c = state.cells[i];
    if (c.isDead) {
      continue;
    }
    spdlog::debug("-- Cell {} --", i);
    spdlog::debug("  kind = {}", c.leafId.has_value() ? "Leaf" : "Split");
    spdlog::debug("  splitDir = {}", c.splitDir == SplitDir::Vertical ? "Vertical" : "Horizontal");
    spdlog::debug("  parent = {}", c.parent.has_value() ? std::to_string(*c.parent) : "null");
    spdlog::debug("  firstChild = {}",
                  c.firstChild.has_value() ? std::to_string(*c.firstChild) : "null");
    spdlog::debug("  secondChild = {}",
                  c.secondChild.has_value() ? std::to_string(*c.secondChild) : "null");
    spdlog::debug("  rect = {{ x={}, y={}, w={}, h={} }}", c.rect.x, c.rect.y, c.rect.width,
                  c.rect.height);
  }

  spdlog::debug("===== End CellCluster =====");
}

bool validateState(const CellCluster& state) {
  bool ok = true;

  if (state.cells.empty()) {
    spdlog::debug("[validate] State OK (empty)");
    return ok;
  }

  if (state.cells[0].parent.has_value()) {
    spdlog::error("[validate] ERROR: root cell (index 0) has a parent");
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
        spdlog::error("[validate] ERROR: cell {} has out-of-range parent index {}", i, p);
        ok = false;
      } else {
        parentRefCount[static_cast<std::size_t>(i)]++;
      }
    }

    if (c.leafId.has_value()) {
      if (c.firstChild.has_value() || c.secondChild.has_value()) {
        spdlog::error("[validate] ERROR: leaf cell {} has children", i);
        ok = false;
      }
    } else {
      if (!c.firstChild.has_value() || !c.secondChild.has_value()) {
        spdlog::error("[validate] ERROR: split cell {} is missing children", i);
        ok = false;
      }
    }

    auto checkChild = [&](const std::optional<int>& childOpt, const char* label) {
      if (!childOpt.has_value()) {
        return;
      }

      int child = *childOpt;
      if (child < 0 || static_cast<std::size_t>(child) >= state.cells.size()) {
        spdlog::error("[validate] ERROR: cell {} has out-of-range {} index {}", i, label, child);
        ok = false;
        return;
      }

      const Cell& cc = state.cells[static_cast<std::size_t>(child)];
      if (cc.isDead) {
        spdlog::warn("[validate] WARNING: cell {}'s {} ({}) is dead", i, label, child);
        ok = false;
      }
      if (!cc.parent.has_value() || *cc.parent != i) {
        spdlog::error("[validate] ERROR: cell {}'s {} ({}) does not point back to parent {}",
                      i, label, child, i);
        ok = false;
      }

      childRefCount[static_cast<std::size_t>(child)]++;
    };

    checkChild(c.firstChild, "firstChild");
    checkChild(c.secondChild, "secondChild");
  }

  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    if (parentRefCount[i] > 1) {
      spdlog::warn("[validate] WARNING: cell {} has parent set more than once ({})",
                   i, parentRefCount[i]);
      ok = false;
    }

    if (childRefCount[i] > 2) {
      spdlog::warn("[validate] WARNING: cell {} is referenced as a child more than twice ({})",
                   i, childRefCount[i]);
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
      spdlog::error("[validate] ERROR: duplicate leafId {} found", leafIds[i]);
      ok = false;
    }
  }

  if (ok) {
    spdlog::debug("[validate] State OK ({} cells)", state.cells.size());
  } else {
    spdlog::warn("[validate] State has anomalies");
  }

  return ok;
}

// ============================================================================
// Multi-Cluster System Implementation
// ============================================================================

// ============================================================================
// Helper: Pre-create leaves in a cluster from initialCellIds
// Returns the selection index (or -1 if no cells created)
// ============================================================================

static int preCreateLeaves(PositionedCluster& pc, const std::vector<size_t>& cellIds,
                           size_t& globalNextLeafId, float gapHorizontal, float gapVertical) {
  int currentSelection = -1;

  for (size_t i = 0; i < cellIds.size(); ++i) {
    size_t cellId = cellIds[i];

    if (pc.cluster.cells.empty()) {
      // First cell: create root leaf (pass -1 for empty cluster)
      auto resultOpt = splitLeaf(pc.cluster, -1, gapHorizontal, gapVertical, globalNextLeafId);
      if (resultOpt.has_value()) {
        // The root leaf was just created. Overwrite its leafId with the provided one.
        int idx = resultOpt->newSelectionIndex;
        pc.cluster.cells[static_cast<size_t>(idx)].leafId = cellId;
        currentSelection = idx;
      }
    } else {
      // Subsequent cells: split current selection
      auto resultOpt = splitLeaf(pc.cluster, currentSelection, gapHorizontal, gapVertical, globalNextLeafId);
      if (resultOpt.has_value()) {
        // The new leaf (second child) was created. Find it and overwrite its leafId.
        // After split, selection moves to first child. The new leaf is the sibling.
        int firstChildIdx = resultOpt->newSelectionIndex;
        Cell& firstChild =
            pc.cluster.cells[static_cast<size_t>(firstChildIdx)];
        if (firstChild.parent.has_value()) {
          int parentIdx = *firstChild.parent;
          Cell& parent =
              pc.cluster.cells[static_cast<size_t>(parentIdx)];
          if (parent.secondChild.has_value()) {
            int secondChildIdx = *parent.secondChild;
            pc.cluster.cells[static_cast<size_t>(secondChildIdx)].leafId = cellId;
          }
        }
        currentSelection = firstChildIdx;
      }
    }
  }

  return currentSelection;
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
    pc.cluster = createInitialState(info.width, info.height);

    int selectionIndex = -1;
    // Pre-create leaves if initialCellIds provided
    if (!info.initialCellIds.empty()) {
      selectionIndex = preCreateLeaves(pc, info.initialCellIds, system.globalNextLeafId,
                                       system.gapHorizontal, system.gapVertical);
    }

    // If this is the first cluster with cells, make it the selected cluster
    if (!system.selection.has_value() && selectionIndex >= 0) {
      system.selection = Selection{pc.id, selectionIndex};
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
  pc.cluster = createInitialState(info.width, info.height);

  int selectionIndex = -1;
  if (!info.initialCellIds.empty()) {
    selectionIndex = preCreateLeaves(pc, info.initialCellIds, system.globalNextLeafId,
                                     system.gapHorizontal, system.gapVertical);
  }

  if (!system.selection.has_value() && selectionIndex >= 0) {
    system.selection = Selection{pc.id, selectionIndex};
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

  bool wasSelected = system.selection.has_value() && system.selection->clusterId == id;

  system.clusters.erase(it);

  // If the removed cluster was selected, move selection to another cluster with cells
  if (wasSelected) {
    system.selection.reset();
    for (auto& pc : system.clusters) {
      if (!pc.cluster.cells.empty()) {
        // Find the first leaf in this cluster
        for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
          if (isLeaf(pc.cluster, i)) {
            system.selection = Selection{pc.id, i};
            break;
          }
        }
        if (system.selection.has_value()) {
          break;
        }
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

Rect localToGlobal(const PositionedCluster& pc, const Rect& localRect) {
  return Rect{localRect.x + pc.globalX, localRect.y + pc.globalY, localRect.width,
                          localRect.height};
}

Rect globalToLocal(const PositionedCluster& pc, const Rect& globalRect) {
  return Rect{globalRect.x - pc.globalX, globalRect.y - pc.globalY, globalRect.width,
                          globalRect.height};
}

Rect getCellGlobalRect(const PositionedCluster& pc, int cellIndex) {
  if (cellIndex < 0 || static_cast<size_t>(cellIndex) >= pc.cluster.cells.size()) {
    return Rect{0.0f, 0.0f, 0.0f, 0.0f};
  }

  const Cell& cell = pc.cluster.cells[static_cast<size_t>(cellIndex)];
  return localToGlobal(pc, cell.rect);
}

// ============================================================================
// Cross-Cluster Navigation Helpers
// ============================================================================

static bool isInDirectionGlobal(const Rect& from, const Rect& to,
                                Direction dir) {
  switch (dir) {
  case Direction::Left:
    return to.x + to.width <= from.x;
  case Direction::Right:
    return to.x >= from.x + from.width;
  case Direction::Up:
    return to.y + to.height <= from.y;
  case Direction::Down:
    return to.y >= from.y + from.height;
  default:
    return false;
  }
}

static float directionalDistanceGlobal(const Rect& from, const Rect& to,
                                       Direction dir) {
  float dxCenter = (to.x + to.width * 0.5f) - (from.x + from.width * 0.5f);
  float dyCenter = (to.y + to.height * 0.5f) - (from.y + from.height * 0.5f);

  // Check for perpendicular overlap - cells that share vertical/horizontal space
  // are strongly preferred over cells that don't
  bool hasVerticalOverlap = (to.y < from.y + from.height) && (to.y + to.height > from.y);
  bool hasHorizontalOverlap = (to.x < from.x + from.width) && (to.x + to.width > from.x);

  switch (dir) {
  case Direction::Left:
  case Direction::Right: {
    float primaryDist = (dir == Direction::Left) ? -dxCenter : dxCenter;
    if (hasVerticalOverlap) {
      return primaryDist; // Overlapping cells get pure horizontal distance
    }
    // Non-overlapping cells get a large penalty
    float gap = std::min(std::abs(to.y - (from.y + from.height)),
                         std::abs(from.y - (to.y + to.height)));
    return primaryDist + 10000.0f + gap;
  }
  case Direction::Up:
  case Direction::Down: {
    float primaryDist = (dir == Direction::Up) ? -dyCenter : dyCenter;
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

static bool isClusterInDirection(const PositionedCluster& pc, const Rect& fromGlobalRect,
                                 Direction dir) {
  Rect clusterBounds{pc.globalX, pc.globalY, pc.cluster.windowWidth,
                                 pc.cluster.windowHeight};
  return isInDirectionGlobal(fromGlobalRect, clusterBounds, dir);
}

// ============================================================================
// Cross-Cluster Navigation
// ============================================================================

std::optional<std::pair<ClusterId, int>>
findNextLeafInDirection(const System& system, ClusterId currentClusterId, int currentCellIndex,
                        Direction dir) {
  const PositionedCluster* currentPC = getCluster(system, currentClusterId);
  if (!currentPC) {
    return std::nullopt;
  }

  if (!isLeaf(currentPC->cluster, currentCellIndex)) {
    return std::nullopt;
  }

  Rect currentGlobalRect = getCellGlobalRect(*currentPC, currentCellIndex);

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
      if (!isLeaf(pc.cluster, i)) {
        continue;
      }

      // Skip current cell
      if (pc.id == currentClusterId && i == currentCellIndex) {
        continue;
      }

      Rect candidateGlobalRect = getCellGlobalRect(pc, i);

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

bool moveSelection(System& system, Direction dir) {
  if (!system.selection.has_value()) {
    return false;
  }

  auto nextOpt = findNextLeafInDirection(system, system.selection->clusterId,
                                          system.selection->cellIndex, dir);
  if (!nextOpt.has_value()) {
    return false;
  }

  auto [nextClusterId, nextCellIndex] = *nextOpt;
  system.selection = Selection{nextClusterId, nextCellIndex};

  return true;
}

// ============================================================================
// Operations
// ============================================================================

std::optional<size_t> splitSelectedLeaf(System& system) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  PositionedCluster* pc = getCluster(system, system.selection->clusterId);
  if (!pc) {
    return std::nullopt;
  }

  auto resultOpt = splitLeaf(pc->cluster, system.selection->cellIndex,
                             system.gapHorizontal, system.gapVertical,
                             system.globalNextLeafId);

  if (resultOpt.has_value()) {
    // Update selection to the new selection index
    system.selection->cellIndex = resultOpt->newSelectionIndex;
    return resultOpt->newLeafId;
  }

  return std::nullopt;
}

bool deleteSelectedLeaf(System& system) {
  if (!system.selection.has_value()) {
    return false;
  }

  PositionedCluster* pc = getCluster(system, system.selection->clusterId);
  if (!pc) {
    return false;
  }

  auto newSelectionOpt = deleteLeaf(pc->cluster, system.selection->cellIndex,
                                                 system.gapHorizontal, system.gapVertical);

  if (newSelectionOpt.has_value()) {
    // Update selection to new cell in same cluster
    system.selection->cellIndex = *newSelectionOpt;
    return true;
  }

  // Cluster became empty (or deletion failed for root), find another cluster with cells
  system.selection.reset();
  for (auto& otherPc : system.clusters) {
    if (!otherPc.cluster.cells.empty()) {
      for (int i = 0; i < static_cast<int>(otherPc.cluster.cells.size()); ++i) {
        if (isLeaf(otherPc.cluster, i)) {
          system.selection = Selection{otherPc.id, i};
          return true;
        }
      }
    }
  }

  return true;  // Deletion occurred even if no new selection found
}

std::optional<std::pair<ClusterId, int>> getSelectedCell(const System& system) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  return std::make_pair(system.selection->clusterId, system.selection->cellIndex);
}

std::optional<Rect> getSelectedCellGlobalRect(const System& system) {
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
  if (!system.selection.has_value()) {
    return false;
  }

  PositionedCluster* pc = getCluster(system, system.selection->clusterId);
  if (!pc) {
    return false;
  }

  return toggleSplitDir(pc->cluster, system.selection->cellIndex,
                                     system.gapHorizontal, system.gapVertical);
}

bool toggleClusterGlobalSplitDir(System& system) {
  if (!system.selection.has_value()) {
    return false;
  }
  auto* pc = getCluster(system, system.selection->clusterId);
  if (pc == nullptr) {
    return false;
  }
  pc->cluster.globalSplitDir = (pc->cluster.globalSplitDir == SplitDir::Vertical)
                                   ? SplitDir::Horizontal
                                   : SplitDir::Vertical;
  return true;
}

SwapResult swapCells(System& system,
                     ClusterId clusterId1, size_t leafId1,
                     ClusterId clusterId2, size_t leafId2) {
  // Get clusters
  PositionedCluster* pc1 = getCluster(system, clusterId1);
  PositionedCluster* pc2 = getCluster(system, clusterId2);

  if (!pc1) {
    return {false, "Cluster 1 not found"};
  }
  if (!pc2) {
    return {false, "Cluster 2 not found"};
  }

  // Find cells by leafId
  auto idx1Opt = findCellByLeafId(pc1->cluster, leafId1);
  auto idx2Opt = findCellByLeafId(pc2->cluster, leafId2);

  if (!idx1Opt.has_value()) {
    return {false, "Leaf 1 not found"};
  }
  if (!idx2Opt.has_value()) {
    return {false, "Leaf 2 not found"};
  }

  int idx1 = *idx1Opt;
  int idx2 = *idx2Opt;

  // Check if same cell (no-op)
  if (clusterId1 == clusterId2 && leafId1 == leafId2) {
    return {true, ""};
  }

  // Validate both are leaves
  if (!isLeaf(pc1->cluster, idx1)) {
    return {false, "Cell 1 is not a leaf"};
  }
  if (!isLeaf(pc2->cluster, idx2)) {
    return {false, "Cell 2 is not a leaf"};
  }

  if (clusterId1 == clusterId2) {
    // Same-cluster swap: swap tree positions
    CellCluster& cluster = pc1->cluster;
    Cell& cell1 = cluster.cells[static_cast<size_t>(idx1)];
    Cell& cell2 = cluster.cells[static_cast<size_t>(idx2)];

    // Store original parent info
    auto parent1 = cell1.parent;
    auto parent2 = cell2.parent;

    // Swap parent pointers
    cell1.parent = parent2;
    cell2.parent = parent1;

    // Update parent's child pointers
    if (parent1.has_value()) {
      Cell& p1 = cluster.cells[static_cast<size_t>(*parent1)];
      if (p1.firstChild.has_value() && *p1.firstChild == idx1) {
        p1.firstChild = idx2;
      } else if (p1.secondChild.has_value() && *p1.secondChild == idx1) {
        p1.secondChild = idx2;
      }
    }
    if (parent2.has_value()) {
      Cell& p2 = cluster.cells[static_cast<size_t>(*parent2)];
      if (p2.firstChild.has_value() && *p2.firstChild == idx2) {
        p2.firstChild = idx1;
      } else if (p2.secondChild.has_value() && *p2.secondChild == idx2) {
        p2.secondChild = idx1;
      }
    }

    // Swap rects
    std::swap(cell1.rect, cell2.rect);

    // Note: Selection stays at the same cell index because the cells
    // still have the same leafIds - only their tree positions changed.
  } else {
    // Cross-cluster swap: exchange leafIds
    Cell& cell1 = pc1->cluster.cells[static_cast<size_t>(idx1)];
    Cell& cell2 = pc2->cluster.cells[static_cast<size_t>(idx2)];

    std::swap(cell1.leafId, cell2.leafId);

    // Note: Selection doesn't need updating for cross-cluster swap
    // because the selection tracks cell index, not leafId
  }

  return {true, ""};
}

MoveResult moveCell(System& system,
                    ClusterId sourceClusterId, size_t sourceLeafId,
                    ClusterId targetClusterId, size_t targetLeafId) {
  // Get clusters
  PositionedCluster* srcPC = getCluster(system, sourceClusterId);
  PositionedCluster* tgtPC = getCluster(system, targetClusterId);

  if (!srcPC) {
    return {false, -1, 0, "Source cluster not found"};
  }
  if (!tgtPC) {
    return {false, -1, 0, "Target cluster not found"};
  }

  // Find cells by leafId
  auto srcIdxOpt = findCellByLeafId(srcPC->cluster, sourceLeafId);
  auto tgtIdxOpt = findCellByLeafId(tgtPC->cluster, targetLeafId);

  if (!srcIdxOpt.has_value()) {
    return {false, -1, 0, "Source leaf not found"};
  }
  if (!tgtIdxOpt.has_value()) {
    return {false, -1, 0, "Target leaf not found"};
  }

  // Check if same cell (no-op)
  if (sourceClusterId == targetClusterId && sourceLeafId == targetLeafId) {
    return {true, *srcIdxOpt, sourceClusterId, ""};
  }

  // Validate both are leaves
  if (!isLeaf(srcPC->cluster, *srcIdxOpt)) {
    return {false, -1, 0, "Source cell is not a leaf"};
  }
  if (!isLeaf(tgtPC->cluster, *tgtIdxOpt)) {
    return {false, -1, 0, "Target cell is not a leaf"};
  }

  // Remember if source or target was selected
  bool sourceWasSelected = system.selection.has_value() &&
      system.selection->clusterId == sourceClusterId &&
      system.selection->cellIndex == *srcIdxOpt;

  bool targetWasSelected = system.selection.has_value() &&
      system.selection->clusterId == targetClusterId &&
      system.selection->cellIndex == *tgtIdxOpt;

  // Store source's leafId
  size_t savedLeafId = sourceLeafId;

  // Delete source
  auto deleteResult = deleteLeaf(srcPC->cluster, *srcIdxOpt,
                                              system.gapHorizontal, system.gapVertical);

  // Update srcPC pointer in case same-cluster operation moved things
  if (sourceClusterId == targetClusterId) {
    tgtPC = srcPC;
  }

  // Re-find target by leafId (index may have changed if same cluster)
  tgtIdxOpt = findCellByLeafId(tgtPC->cluster, targetLeafId);
  if (!tgtIdxOpt.has_value()) {
    return {false, -1, 0, "Target lost after delete"};
  }

  // Split target
  auto splitResult = splitLeaf(tgtPC->cluster, *tgtIdxOpt,
                               system.gapHorizontal, system.gapVertical,
                               system.globalNextLeafId);

  if (!splitResult.has_value()) {
    return {false, -1, 0, "Split failed"};
  }

  // Find the second child (new leaf) and set its leafId to savedLeafId
  int firstChildIdx = splitResult->newSelectionIndex;
  Cell& firstChild = tgtPC->cluster.cells[static_cast<size_t>(firstChildIdx)];

  if (!firstChild.parent.has_value()) {
    return {false, -1, 0, "Could not find parent after split"};
  }

  int parentIdx = *firstChild.parent;
  Cell& parent = tgtPC->cluster.cells[static_cast<size_t>(parentIdx)];

  if (!parent.secondChild.has_value()) {
    return {false, -1, 0, "Could not find new cell after split"};
  }

  int newCellIdx = *parent.secondChild;
  tgtPC->cluster.cells[static_cast<size_t>(newCellIdx)].leafId = savedLeafId;

  // Update selection if source or target was selected
  if (sourceWasSelected) {
    // Source was selected - follow it to its new position
    system.selection = Selection{targetClusterId, newCellIdx};
  } else if (targetWasSelected) {
    // Target was selected - it's now a parent, so select its first child
    // (which keeps the target's original leafId)
    system.selection = Selection{targetClusterId, firstChildIdx};
  }

  return {true, newCellIdx, targetClusterId, ""};
}

// ============================================================================
// Utilities
// ============================================================================

bool validateSystem(const System& system) {
  bool ok = true;

  spdlog::debug("===== Validating MultiClusterSystem =====");
  spdlog::debug("Total clusters: {}", system.clusters.size());
  if (system.selection.has_value()) {
    spdlog::debug("selection: cluster={}, cellIndex={}", system.selection->clusterId,
                  system.selection->cellIndex);
  } else {
    spdlog::debug("selection: null");
  }

  // Check that selection points to a valid cluster and cell
  if (system.selection.has_value()) {
    const PositionedCluster* selectedPc = getCluster(system, system.selection->clusterId);
    if (!selectedPc) {
      spdlog::error("[validate] ERROR: selection points to non-existent cluster");
      ok = false;
    } else if (!isLeaf(selectedPc->cluster, system.selection->cellIndex)) {
      spdlog::error("[validate] ERROR: selection points to non-leaf cell");
      ok = false;
    }
  }

  // Validate each cluster
  for (const auto& pc : system.clusters) {
    spdlog::debug("--- Cluster {} at ({}, {}) ---", pc.id, pc.globalX, pc.globalY);
    if (!validateState(pc.cluster)) {
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
      spdlog::error("[validate] ERROR: duplicate cluster ID {}", ids[i]);
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
      spdlog::error("[validate] ERROR: duplicate leafId {} across clusters", allLeafIds[i]);
      ok = false;
    }
  }

  if (ok) {
    spdlog::debug("[validate] System OK");
  } else {
    spdlog::warn("[validate] System has anomalies");
  }

  spdlog::debug("===== End Validation =====");

  return ok;
}

void debugPrintSystem(const System& system) {
  spdlog::debug("===== MultiClusterSystem =====");
  spdlog::debug("clusters.size = {}", system.clusters.size());
  spdlog::debug("globalNextLeafId = {}", system.globalNextLeafId);

  if (system.selection.has_value()) {
    spdlog::debug("selection = cluster={}, cellIndex={}", system.selection->clusterId,
                  system.selection->cellIndex);
  } else {
    spdlog::debug("selection = null");
  }

  for (const auto& pc : system.clusters) {
    spdlog::debug("--- Cluster {} ---", pc.id);
    spdlog::debug("  globalX = {}, globalY = {}", pc.globalX, pc.globalY);
    debugPrintState(pc.cluster);
  }

  spdlog::debug("===== End MultiClusterSystem =====");
}

size_t countTotalLeaves(const System& system) {
  size_t count = 0;
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (isLeaf(pc.cluster, i)) {
        ++count;
      }
    }
  }
  return count;
}

// ============================================================================
// Hit Testing
// ============================================================================

std::optional<std::pair<ClusterId, int>>
findCellAtPoint(const System& system, float globalX, float globalY) {
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (!isLeaf(pc.cluster, i)) {
        continue;
      }
      Rect globalRect = getCellGlobalRect(pc, i);
      if (globalX >= globalRect.x && globalX < globalRect.x + globalRect.width &&
          globalY >= globalRect.y && globalY < globalRect.y + globalRect.height) {
        return std::make_pair(pc.id, i);
      }
    }
  }
  return std::nullopt;
}

// ============================================================================
// System Update
// ============================================================================

std::vector<size_t> getClusterLeafIds(const CellCluster& cluster) {
  std::vector<size_t> leafIds;
  for (int i = 0; i < static_cast<int>(cluster.cells.size()); ++i) {
    const auto& cell = cluster.cells[static_cast<size_t>(i)];
    if (!cell.isDead && cell.leafId.has_value()) {
      leafIds.push_back(*cell.leafId);
    }
  }
  return leafIds;
}

std::optional<int> findCellByLeafId(const CellCluster& cluster, size_t leafId) {
  for (int i = 0; i < static_cast<int>(cluster.cells.size()); ++i) {
    const auto& cell = cluster.cells[static_cast<size_t>(i)];
    if (!cell.isDead && cell.leafId.has_value() && *cell.leafId == leafId) {
      return i;
    }
  }
  return std::nullopt;
}

UpdateResult updateSystem(
    System& system,
    const std::vector<ClusterCellIds>& clusterCellIds,
    std::optional<std::pair<ClusterId, size_t>> newSelection) {

  UpdateResult result;
  result.selectionUpdated = false;

  // Process each cluster update
  for (const auto& clusterUpdate : clusterCellIds) {
    PositionedCluster* pc = getCluster(system, clusterUpdate.clusterId);

    if (!pc) {
      // Cluster not found - add error
      result.errors.push_back({
          UpdateError::Type::ClusterNotFound,
          clusterUpdate.clusterId,
          0  // no specific leaf ID
      });
      continue;
    }

    // Get current leaf IDs
    std::vector<size_t> currentLeafIds = getClusterLeafIds(pc->cluster);

    // Compute set differences
    std::vector<size_t> sortedCurrent = currentLeafIds;
    std::vector<size_t> sortedDesired = clusterUpdate.leafIds;
    std::sort(sortedCurrent.begin(), sortedCurrent.end());
    std::sort(sortedDesired.begin(), sortedDesired.end());

    // toDelete = current - desired
    std::vector<size_t> toDelete;
    std::set_difference(sortedCurrent.begin(), sortedCurrent.end(),
                        sortedDesired.begin(), sortedDesired.end(),
                        std::back_inserter(toDelete));

    // toAdd = desired - current
    std::vector<size_t> toAdd;
    std::set_difference(sortedDesired.begin(), sortedDesired.end(),
                        sortedCurrent.begin(), sortedCurrent.end(),
                        std::back_inserter(toAdd));

    // Handle deletions
    for (size_t leafId : toDelete) {
      auto cellIndexOpt = findCellByLeafId(pc->cluster, leafId);
      if (!cellIndexOpt.has_value()) {
        result.errors.push_back({
            UpdateError::Type::LeafNotFound,
            clusterUpdate.clusterId,
            leafId
        });
        continue;
      }

      auto newSelectionOpt = deleteLeaf(pc->cluster, *cellIndexOpt,
                                                     system.gapHorizontal, system.gapVertical);
      result.deletedLeafIds.push_back(leafId);

      // If deletion succeeded and returned a new selection, update it if this was selected
      if (system.selection.has_value() &&
          system.selection->clusterId == clusterUpdate.clusterId &&
          system.selection->cellIndex == *cellIndexOpt) {
        if (newSelectionOpt.has_value()) {
          system.selection->cellIndex = *newSelectionOpt;
        } else {
          system.selection.reset();
        }
      }
    }

    // Determine starting split index for this cluster (prefer selection)
    int splitFromIndex = -1;
    if (system.selection.has_value() &&
        system.selection->clusterId == pc->id &&
        isLeaf(pc->cluster, system.selection->cellIndex)) {
      splitFromIndex = system.selection->cellIndex;
    }

    // Handle additions
    for (size_t leafId : toAdd) {
      // Find an existing leaf to split, or create root if empty
      int currentSelection = -1;

      if (pc->cluster.cells.empty()) {
        // Cluster is empty - will create root with splitLeaf(-1)
        currentSelection = -1;
      } else if (splitFromIndex >= 0 &&
                 isLeaf(pc->cluster, splitFromIndex)) {
        // Use tracked split point (follows selection)
        currentSelection = splitFromIndex;
      } else {
        // Fallback: find the first available leaf
        for (int i = 0; i < static_cast<int>(pc->cluster.cells.size()); ++i) {
          if (isLeaf(pc->cluster, i)) {
            currentSelection = i;
            break;
          }
        }
      }

      auto resultOpt = splitLeaf(pc->cluster, currentSelection,
                                 system.gapHorizontal, system.gapVertical,
                                 system.globalNextLeafId);

      if (resultOpt.has_value()) {
        // Override the new leaf's ID with the desired one
        // The new leaf is the second child of the split
        if (currentSelection == -1) {
          // Root was created - override its leafId
          int idx = resultOpt->newSelectionIndex;
          pc->cluster.cells[static_cast<size_t>(idx)].leafId = leafId;
        } else {
          // A split occurred - find the second child and override its leafId
          int firstChildIdx = resultOpt->newSelectionIndex;

          // Update splitFromIndex to follow the first child for subsequent additions
          splitFromIndex = firstChildIdx;

          Cell& firstChild =
              pc->cluster.cells[static_cast<size_t>(firstChildIdx)];
          if (firstChild.parent.has_value()) {
            int parentIdx = *firstChild.parent;
            Cell& parent =
                pc->cluster.cells[static_cast<size_t>(parentIdx)];
            if (parent.secondChild.has_value()) {
              int secondChildIdx = *parent.secondChild;
              pc->cluster.cells[static_cast<size_t>(secondChildIdx)].leafId = leafId;
            }
          }
        }
        result.addedLeafIds.push_back(leafId);
      }
    }
  }

  // Update selection
  if (newSelection.has_value()) {
    auto [clusterId, leafId] = *newSelection;

    PositionedCluster* pc = getCluster(system, clusterId);
    if (!pc) {
      result.errors.push_back({
          UpdateError::Type::SelectionInvalid,
          clusterId,
          leafId
      });
    } else {
      auto cellIndexOpt = findCellByLeafId(pc->cluster, leafId);
      if (!cellIndexOpt.has_value()) {
        result.errors.push_back({
            UpdateError::Type::SelectionInvalid,
            clusterId,
            leafId
        });
      } else {
        system.selection = Selection{clusterId, *cellIndexOpt};
        result.selectionUpdated = true;
      }
    }
  }

  return result;
}

} // namespace cells
} // namespace wintiler
