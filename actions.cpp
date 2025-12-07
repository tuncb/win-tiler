#include "actions.h"

#include <iostream>

namespace wintiler
{

  AppState createInitialState(float width, float height)
  {
    AppState state;
    state.cells.clear();

    // Start global split direction as Vertical, as requested.
    state.globalSplitDir = SplitDir::Vertical;

    Cell root{};
    root.kind = CellKind::Leaf;
    // The root leaf starts with the current global split direction.
    root.splitDir = state.globalSplitDir;
    root.parent = std::nullopt;
    root.firstChild = std::nullopt;
    root.secondChild = std::nullopt;
    root.rect = Rect{0.0f, 0.0f, width, height};

    int index = addCell(state, root);
    state.rootIndex = index;
    state.selectedIndex = index;

    return state;
  }

  bool isLeaf(const AppState &state, int cellIndex)
  {
    if (cellIndex < 0 || static_cast<std::size_t>(cellIndex) >= state.cells.size())
    {
      return false;
    }

    const Cell &cell = state.cells[static_cast<std::size_t>(cellIndex)];
    return !cell.firstChild.has_value() && !cell.secondChild.has_value();
  }

  int addCell(AppState &state, const Cell &cell)
  {
    state.cells.push_back(cell);
    return static_cast<int>(state.cells.size() - 1);
  }

  static void recomputeChildrenRects(AppState &state, int nodeIndex)
  {
    Cell &node = state.cells[static_cast<std::size_t>(nodeIndex)];

    if (!node.firstChild.has_value() || !node.secondChild.has_value())
    {
      return; // nothing to do for leaves or incomplete splits
    }

    Rect parentRect = node.rect;

    // Compute child rects based on split direction.
    Rect first{};
    Rect second{};

    if (node.splitDir == SplitDir::Vertical)
    {
      float halfWidth = parentRect.width * 0.5f;
      first = Rect{parentRect.x, parentRect.y, halfWidth, parentRect.height};
      second = Rect{parentRect.x + halfWidth, parentRect.y, halfWidth, parentRect.height};
    }
    else // Horizontal
    {
      float halfHeight = parentRect.height * 0.5f;
      first = Rect{parentRect.x, parentRect.y, parentRect.width, halfHeight};
      second = Rect{parentRect.x, parentRect.y + halfHeight, parentRect.width, halfHeight};
    }

    Cell &firstChild = state.cells[static_cast<std::size_t>(*node.firstChild)];
    Cell &secondChild = state.cells[static_cast<std::size_t>(*node.secondChild)];

    firstChild.rect = first;
    secondChild.rect = second;
  }

  void recomputeSubtreeRects(AppState &state, int nodeIndex)
  {
    if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= state.cells.size())
    {
      return;
    }

    Cell &node = state.cells[static_cast<std::size_t>(nodeIndex)];

    // If this is a split node, first update its immediate children based on its rect.
    if (node.firstChild.has_value() && node.secondChild.has_value())
    {
      recomputeChildrenRects(state, nodeIndex);

      // Recursively propagate to the subtree.
      recomputeSubtreeRects(state, *node.firstChild);
      recomputeSubtreeRects(state, *node.secondChild);
    }
  }

  bool deleteSelectedLeaf(AppState &state)
  {
    if (!state.selectedIndex.has_value())
    {
      return false;
    }

    int selected = *state.selectedIndex;
    if (!isLeaf(state, selected))
    {
      return false; // only leaves can be deleted
    }

    if (!state.rootIndex.has_value() || selected == *state.rootIndex)
    {
      return false; // do not delete the root
    }

    Cell &selectedCell = state.cells[static_cast<std::size_t>(selected)];
    if (!selectedCell.parent.has_value())
    {
      return false;
    }

    int parentIndex = *selectedCell.parent;
    Cell &parent = state.cells[static_cast<std::size_t>(parentIndex)];

    if (!parent.firstChild.has_value() || !parent.secondChild.has_value())
    {
      return false; // malformed tree
    }

    int firstIdx = *parent.firstChild;
    int secondIdx = *parent.secondChild;
    int siblingIndex = (selected == firstIdx) ? secondIdx : firstIdx;

    // Promote sibling into the parent's slot by copying it over.
    Cell &sibling = state.cells[static_cast<std::size_t>(siblingIndex)];
    Cell promoted = sibling;         // copy
    promoted.parent = parent.parent; // take over parent's parent

    // If promoted node has children, fix their parent pointers to point to this node index (parentIndex).
    if (promoted.firstChild.has_value())
    {
      Cell &c1 = state.cells[static_cast<std::size_t>(*promoted.firstChild)];
      c1.parent = parentIndex;
    }
    if (promoted.secondChild.has_value())
    {
      Cell &c2 = state.cells[static_cast<std::size_t>(*promoted.secondChild)];
      c2.parent = parentIndex;
    }

    // Overwrite the parent cell with the promoted one.
    state.cells[static_cast<std::size_t>(parentIndex)] = promoted;

    // Mark the deleted leaf as unreachable by clearing its parent.
    selectedCell.parent.reset();

    // If needed later, we could also clear sibling.parent to mark it as dead,
    // but since its data has been copied into parentIndex, the original index
    // is effectively unused.

    // Ensure selectedIndex ends up on a leaf under parentIndex.
    int current = parentIndex;
    while (!isLeaf(state, current))
    {
      Cell &n = state.cells[static_cast<std::size_t>(current)];
      if (n.firstChild.has_value())
      {
        current = *n.firstChild;
      }
      else if (n.secondChild.has_value())
      {
        current = *n.secondChild;
      }
      else
      {
        break;
      }
    }

    state.selectedIndex = current;
    return true;
  }

  static bool isInDirection(const Rect &from, const Rect &to, Direction dir)
  {
    switch (dir)
    {
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

  static float directionalDistance(const Rect &from, const Rect &to, Direction dir)
  {
    // Basic heuristic: primary axis distance plus a smaller penalty for
    // offset on the orthogonal axis, to favor roughly aligned neighbors.
    float dxCenter = (to.x + to.width * 0.5f) - (from.x + from.width * 0.5f);
    float dyCenter = (to.y + to.height * 0.5f) - (from.y + from.height * 0.5f);

    switch (dir)
    {
    case Direction::Left:
    case Direction::Right:
      return (dir == Direction::Left ? -dxCenter : dxCenter) + 0.25f * std::abs(dyCenter);
    case Direction::Up:
    case Direction::Down:
      return (dir == Direction::Up ? -dyCenter : dyCenter) + 0.25f * std::abs(dxCenter);
    default:
      return std::numeric_limits<float>::max();
    }
  }

  std::optional<int> findNextLeafInDirection(const AppState &state, int currentIndex, Direction dir)
  {
    if (currentIndex < 0 || static_cast<std::size_t>(currentIndex) >= state.cells.size())
    {
      return std::nullopt;
    }

    if (!isLeaf(state, currentIndex))
    {
      return std::nullopt;
    }

    const Rect &currentRect = state.cells[static_cast<std::size_t>(currentIndex)].rect;

    std::optional<int> bestIndex;
    float bestScore = std::numeric_limits<float>::max();

    for (int i = 0; i < static_cast<int>(state.cells.size()); ++i)
    {
      if (i == currentIndex)
      {
        continue;
      }

      if (!isLeaf(state, i))
      {
        continue;
      }

      const Rect &candidateRect = state.cells[static_cast<std::size_t>(i)].rect;

      if (!isInDirection(currentRect, candidateRect, dir))
      {
        continue;
      }

      float score = directionalDistance(currentRect, candidateRect, dir);
      if (score < bestScore)
      {
        bestScore = score;
        bestIndex = i;
      }
    }

    return bestIndex;
  }

  bool moveSelection(AppState &state, Direction dir)
  {
    if (!state.selectedIndex.has_value())
    {
      return false;
    }

    int current = *state.selectedIndex;
    std::optional<int> next = findNextLeafInDirection(state, current, dir);
    if (!next.has_value())
    {
      return false;
    }

    state.selectedIndex = *next;
    return true;
  }

  bool splitSelectedLeaf(AppState &state)
  {
    if (!state.selectedIndex.has_value())
    {
      return false;
    }

    int selected = *state.selectedIndex;
    if (!isLeaf(state, selected))
    {
      return false;
    }

    Cell &leaf = state.cells[static_cast<std::size_t>(selected)];
    Rect r = leaf.rect;

    // Create two child cells based on the leaf's splitDir
    Rect firstRect{};
    Rect secondRect{};

    if (leaf.splitDir == SplitDir::Vertical)
    {
      float halfWidth = r.width * 0.5f;
      firstRect = Rect{r.x, r.y, halfWidth, r.height};
      secondRect = Rect{r.x + halfWidth, r.y, halfWidth, r.height};
    }
    else // Horizontal
    {
      float halfHeight = r.height * 0.5f;
      firstRect = Rect{r.x, r.y, r.width, halfHeight};
      secondRect = Rect{r.x, r.y + halfHeight, r.width, halfHeight};
    }

    // Convert leaf into a split node
    leaf.kind = CellKind::Split;

    Cell firstChild{};
    firstChild.kind = CellKind::Leaf;
    // Use global split direction for new leaves, so future splits alternate.
    firstChild.splitDir = state.globalSplitDir;
    firstChild.parent = selected;
    firstChild.firstChild = std::nullopt;
    firstChild.secondChild = std::nullopt;
    firstChild.rect = firstRect;

    Cell secondChild{};
    secondChild.kind = CellKind::Leaf;
    // Use global split direction for new leaves.
    secondChild.splitDir = state.globalSplitDir;
    secondChild.parent = selected;
    secondChild.firstChild = std::nullopt;
    secondChild.secondChild = std::nullopt;
    secondChild.rect = secondRect;

    int firstIndex = addCell(state, firstChild);
    int secondIndex = addCell(state, secondChild);

    leaf.firstChild = firstIndex;
    leaf.secondChild = secondIndex;

    // Select the first child by default
    state.selectedIndex = firstIndex;

    // Alternate the global split direction once per split operation.
    state.globalSplitDir = (state.globalSplitDir == SplitDir::Vertical)
                               ? SplitDir::Horizontal
                               : SplitDir::Vertical;

    return true;
  }

  bool toggleSelectedSplitDir(AppState &state)
  {
    if (!state.selectedIndex.has_value())
    {
      return false;
    }

    int selected = *state.selectedIndex;
    if (!isLeaf(state, selected))
    {
      return false;
    }

    Cell &leaf = state.cells[static_cast<std::size_t>(selected)];

    // Find the parent split node and its sibling leaf.
    if (!leaf.parent.has_value())
    {
      return false;
    }

    int parentIndex = *leaf.parent;
    Cell &parent = state.cells[static_cast<std::size_t>(parentIndex)];

    if (!parent.firstChild.has_value() || !parent.secondChild.has_value())
    {
      return false;
    }

    int firstIdx = *parent.firstChild;
    int secondIdx = *parent.secondChild;
    int siblingIndex = (selected == firstIdx) ? secondIdx : firstIdx;

    if (!isLeaf(state, siblingIndex))
    {
      return false;
    }

    Cell &sibling = state.cells[static_cast<std::size_t>(siblingIndex)];

    // Flip the parent split direction (horizontal <-> vertical).
    parent.splitDir = (parent.splitDir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;

    // Recompute rectangles for this subtree so the two children are
    // repositioned according to the new split orientation.
    recomputeSubtreeRects(state, parentIndex);

    // Keep the selected leaf index; visual layout is updated.
    (void)sibling; // sibling is not modified logically, only layout changes.

    return true;
  }

  // Debug helper: print a human-readable snapshot of the state.
  void debugPrintState(const AppState &state)
  {
    std::cout << "===== AppState =====" << std::endl;

    std::cout << "cells.size = " << state.cells.size() << std::endl;
    std::cout << "rootIndex = ";
    if (state.rootIndex.has_value())
      std::cout << *state.rootIndex;
    else
      std::cout << "null";
    std::cout << std::endl;

    std::cout << "selectedIndex = ";
    if (state.selectedIndex.has_value())
      std::cout << *state.selectedIndex;
    else
      std::cout << "null";
    std::cout << std::endl;

    std::cout << "globalSplitDir = "
              << (state.globalSplitDir == SplitDir::Vertical ? "Vertical" : "Horizontal")
              << std::endl;

    for (std::size_t i = 0; i < state.cells.size(); ++i)
    {
      const Cell &c = state.cells[i];
      std::cout << "-- Cell " << i << " --" << std::endl;
      std::cout << "  kind = " << (c.kind == CellKind::Leaf ? "Leaf" : "Split") << std::endl;
      std::cout << "  splitDir = " << (c.splitDir == SplitDir::Vertical ? "Vertical" : "Horizontal") << std::endl;

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

      std::cout << "  rect = { x=" << c.rect.x
                << ", y=" << c.rect.y
                << ", w=" << c.rect.width
                << ", h=" << c.rect.height
                << " }" << std::endl;
    }

    std::cout << "===== End AppState =====" << std::endl;
  }

} // namespace wintiler
