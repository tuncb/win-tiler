#include <cells.h>

#include <iostream>
#include <algorithm>

namespace wintiler
{
  namespace cell_logic
  {

    WindowState createInitialState(float width, float height)
    {
      WindowState state{};

      state.cells.clear();
      state.nextLeafId = 1;

      // Store the logical window size for lazy creation of the first cell.
      state.windowWidth = width;
      state.windowHeight = height;

      // Start global split direction as Vertical, as requested.
      state.globalSplitDir = SplitDir::Vertical;
      state.gapHorizontal = 10.0f;
      state.gapVertical = 10.0f;

      // Do not create any cells yet. The first call to splitSelectedLeaf
      // on an empty state will create the initial root leaf.
      state.selectedIndex = std::nullopt;

      return state;
    }

    bool isLeaf(const WindowState &state, int cellIndex)
    {
      if (cellIndex < 0 || static_cast<std::size_t>(cellIndex) >= state.cells.size())
      {
        return false;
      }

      const Cell &cell = state.cells[static_cast<std::size_t>(cellIndex)];
      if (cell.isDead)
      {
        return false;
      }

      return !cell.firstChild.has_value() && !cell.secondChild.has_value();
    }

    int addCell(WindowState &state, const Cell &cell)
    {
      state.cells.push_back(cell);
      return static_cast<int>(state.cells.size() - 1);
    }

    static void recomputeChildrenRects(WindowState &state, int nodeIndex)
    {
      Cell &node = state.cells[static_cast<std::size_t>(nodeIndex)];

      if (node.isDead)
      {
        return;
      }

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
        float availableWidth = parentRect.width - state.gapHorizontal;
        float childWidth = availableWidth > 0.0f ? availableWidth * 0.5f : 0.0f;
        first = Rect{parentRect.x, parentRect.y, childWidth, parentRect.height};
        second = Rect{parentRect.x + childWidth + state.gapHorizontal, parentRect.y, childWidth, parentRect.height};
      }
      else // Horizontal
      {
        float availableHeight = parentRect.height - state.gapVertical;
        float childHeight = availableHeight > 0.0f ? availableHeight * 0.5f : 0.0f;
        first = Rect{parentRect.x, parentRect.y, parentRect.width, childHeight};
        second = Rect{parentRect.x, parentRect.y + childHeight + state.gapVertical, parentRect.width, childHeight};
      }

      Cell &firstChild = state.cells[static_cast<std::size_t>(*node.firstChild)];
      Cell &secondChild = state.cells[static_cast<std::size_t>(*node.secondChild)];

      firstChild.rect = first;
      secondChild.rect = second;
    }

    void recomputeSubtreeRects(WindowState &state, int nodeIndex)
    {
      if (nodeIndex < 0 || static_cast<std::size_t>(nodeIndex) >= state.cells.size())
      {
        return;
      }

      Cell &node = state.cells[static_cast<std::size_t>(nodeIndex)];

      if (node.isDead)
      {
        return;
      }

      // If this is a split node, first update its immediate children based on its rect.
      if (node.firstChild.has_value() && node.secondChild.has_value())
      {
        recomputeChildrenRects(state, nodeIndex);

        // Recursively propagate to the subtree.
        recomputeSubtreeRects(state, *node.firstChild);
        recomputeSubtreeRects(state, *node.secondChild);
      }
    }

    bool deleteSelectedLeaf(WindowState &state)
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
      if (state.cells.empty())
      {
        return false;
      }

      Cell &selectedCell = state.cells[static_cast<std::size_t>(selected)];
      if (selectedCell.isDead)
      {
        return false;
      }

      // Allow deleting the root leaf when it is the only live cell.
      if (selected == 0)
      {
        // Root is a leaf here, so by construction it is the only live
        // node in the tree. Transition back to an empty layout.
        state.cells.clear();
        state.selectedIndex.reset();
        return true;
      }

      if (!selectedCell.parent.has_value())
      {
        return false;
      }

      int parentIndex = *selectedCell.parent;
      Cell &parent = state.cells[static_cast<std::size_t>(parentIndex)];

      if (parent.isDead)
      {
        return false;
      }

      if (!parent.firstChild.has_value() || !parent.secondChild.has_value())
      {
        return false; // malformed tree
      }

      int firstIdx = *parent.firstChild;
      int secondIdx = *parent.secondChild;
      int siblingIndex = (selected == firstIdx) ? secondIdx : firstIdx;

      // Promote sibling into the parent's slot by copying it over.
      Cell &sibling = state.cells[static_cast<std::size_t>(siblingIndex)];
      if (sibling.isDead)
      {
        return false;
      }

      // Capture the parent's rect before we overwrite the parent cell.
      Rect newRect = parent.rect;

      Cell promoted = sibling;         // copy (includes leafId)
      promoted.rect = newRect;         // Adopt parent's geometry
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

      // Recompute the layout for the promoted subtree.
      recomputeSubtreeRects(state, parentIndex);

      // Mark the deleted leaf and its sibling as dead.
      selectedCell.isDead = true;
      sibling.isDead = true;
      selectedCell.parent.reset();
      sibling.parent.reset();

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

    std::optional<int> findNextLeafInDirection(const WindowState &state, int currentIndex, Direction dir)
    {
      if (currentIndex < 0 || static_cast<std::size_t>(currentIndex) >= state.cells.size())
      {
        return std::nullopt;
      }

      const Cell &currentCell = state.cells[static_cast<std::size_t>(currentIndex)];
      if (currentCell.isDead || !isLeaf(state, currentIndex))
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

        const Cell &candidateCell = state.cells[static_cast<std::size_t>(i)];
        if (candidateCell.isDead)
        {
          continue;
        }

        const Rect &candidateRect = candidateCell.rect;

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

    bool moveSelection(WindowState &state, Direction dir)
    {
      if (!state.selectedIndex.has_value())
      {
        return false;
      }
      int current = *state.selectedIndex;
      if (current < 0 || static_cast<std::size_t>(current) >= state.cells.size())
      {
        return false;
      }

      if (state.cells[static_cast<std::size_t>(current)].isDead)
      {
        return false;
      }
      std::optional<int> next = findNextLeafInDirection(state, current, dir);
      if (!next.has_value())
      {
        return false;
      }

      state.selectedIndex = *next;
      return true;
    }

    std::optional<size_t> splitSelectedLeaf(WindowState &state)
    {
      // Special case: if there is no selection and the state has no
      // cells yet, lazily create the initial root leaf covering the
      // window. This mirrors the old eager initialization behavior.
      if (!state.selectedIndex.has_value())
      {
        if (state.cells.empty())
        {
          Cell root{};
          root.kind = CellKind::Leaf;
          root.splitDir = state.globalSplitDir;
          root.isDead = false;
          root.parent = std::nullopt;
          root.firstChild = std::nullopt;
          root.secondChild = std::nullopt;
          root.leafId = state.nextLeafId++;

          float rootW = state.windowWidth - 2.0f * state.gapHorizontal;
          float rootH = state.windowHeight - 2.0f * state.gapVertical;
          root.rect = Rect{
              state.gapHorizontal,
              state.gapVertical,
              rootW > 0.0f ? rootW : 0.0f,
              rootH > 0.0f ? rootH : 0.0f};

          int index = addCell(state, root);
          state.selectedIndex = index;

          return root.leafId;
        }

        // No selection but we do have cells; we cannot determine which
        // leaf to split, so do nothing.
        return std::nullopt;
      }

      int selected = *state.selectedIndex;
      if (!isLeaf(state, selected))
      {
        return std::nullopt;
      }

      Cell &leaf = state.cells[static_cast<std::size_t>(selected)];
      if (leaf.isDead)
      {
        return std::nullopt;
      }
      Rect r = leaf.rect;

      // Store the parent's leafId before converting to Split node.
      size_t parentLeafId = *leaf.leafId;

      // Create two child cells based on the *global* split direction,
      // independent of the leaf's stored splitDir.
      Rect firstRect{};
      Rect secondRect{};

      if (state.globalSplitDir == SplitDir::Vertical)
      {
        float availableWidth = r.width - state.gapHorizontal;
        float childWidth = availableWidth > 0.0f ? availableWidth * 0.5f : 0.0f;
        firstRect = Rect{r.x, r.y, childWidth, r.height};
        secondRect = Rect{r.x + childWidth + state.gapHorizontal, r.y, childWidth, r.height};
      }
      else // Horizontal
      {
        float availableHeight = r.height - state.gapVertical;
        float childHeight = availableHeight > 0.0f ? availableHeight * 0.5f : 0.0f;
        firstRect = Rect{r.x, r.y, r.width, childHeight};
        secondRect = Rect{r.x, r.y + childHeight + state.gapVertical, r.width, childHeight};
      }

      // Convert leaf into a split node. Its kind becomes Split, and its
      // own splitDir is now the direction used for this split (global).
      leaf.kind = CellKind::Split;
      leaf.splitDir = state.globalSplitDir;
      leaf.leafId = std::nullopt; // Split nodes don't have leafId

      Cell firstChild{};
      firstChild.kind = CellKind::Leaf;
      // Use global split direction for new leaves, so future splits alternate.
      firstChild.splitDir = state.globalSplitDir;
      firstChild.isDead = false;
      firstChild.parent = selected;
      firstChild.firstChild = std::nullopt;
      firstChild.secondChild = std::nullopt;
      firstChild.rect = firstRect;
      firstChild.leafId = parentLeafId; // Reuse parent's ID

      Cell secondChild{};
      secondChild.kind = CellKind::Leaf;
      // Use global split direction for new leaves.
      secondChild.splitDir = state.globalSplitDir;
      secondChild.isDead = false;
      secondChild.parent = selected;
      secondChild.firstChild = std::nullopt;
      secondChild.secondChild = std::nullopt;
      secondChild.rect = secondRect;
      secondChild.leafId = state.nextLeafId++; // Generate new ID

      int firstIndex = addCell(state, firstChild);
      int secondIndex = addCell(state, secondChild);

      // Re-fetch parent by index after possible reallocation.
      {
        Cell &parent = state.cells[static_cast<std::size_t>(selected)];
        parent.firstChild = firstIndex;
        parent.secondChild = secondIndex;
      }

      // Select the first child by default
      state.selectedIndex = firstIndex;

      // Alternate the global split direction once per split operation.
      state.globalSplitDir = (state.globalSplitDir == SplitDir::Vertical)
                                 ? SplitDir::Horizontal
                                 : SplitDir::Vertical;

      return secondChild.leafId;
    }

    bool toggleSelectedSplitDir(WindowState &state)
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
      if (leaf.isDead)
      {
        return false;
      }

      // Find the parent split node and its sibling leaf.
      if (!leaf.parent.has_value())
      {
        return false;
      }

      int parentIndex = *leaf.parent;
      Cell &parent = state.cells[static_cast<std::size_t>(parentIndex)];

      if (parent.isDead)
      {
        return false;
      }

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
      if (sibling.isDead)
      {
        return false;
      }

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
    void debugPrintState(const WindowState &state)
    {
      std::cout << "===== WindowState =====" << std::endl;

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

      for (std::size_t i = 0; i < state.cells.size(); ++i)
      {
        const Cell &c = state.cells[i];
        if (c.isDead)
        {
          continue;
        }
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

      std::cout << "===== End WindowState =====" << std::endl;
    }

    bool validateState(const WindowState &state)
    {
      bool ok = true;

      // Handle the empty-state case explicitly: no cells means
      // selectedIndex should be empty, which is a valid state.
      if (state.cells.empty())
      {
        if (state.selectedIndex.has_value())
        {
          std::cout << "[validate] ERROR: empty state has non-null selectedIndex" << std::endl;
          ok = false;
        }

        if (ok)
        {
          std::cout << "[validate] State OK (empty)" << std::endl;
        }
        else
        {
          std::cout << "[validate] State has anomalies (empty)" << std::endl;
        }

        return ok;
      }

      // Basic container/root invariants for non-empty states.
      // Root is implicitly index 0.
      if (state.cells[0].parent.has_value())
      {
        std::cout << "[validate] ERROR: root cell (index 0) has a parent" << std::endl;
        ok = false;
      }

      // Track how many times each index is referenced as a child or parent.
      std::vector<int> parentRefCount(state.cells.size(), 0);
      std::vector<int> childRefCount(state.cells.size(), 0);

      for (int i = 0; i < static_cast<int>(state.cells.size()); ++i)
      {
        const Cell &c = state.cells[static_cast<std::size_t>(i)];

        if (c.isDead)
        {
          // Dead cells are allowed to have arbitrary parent/children; they are ignored
          // by all operations. We just skip structural checks for them.
          continue;
        }

        // Parent index validity.
        if (c.parent.has_value())
        {
          int p = *c.parent;
          if (p < 0 || static_cast<std::size_t>(p) >= state.cells.size())
          {
            std::cout << "[validate] ERROR: cell " << i << " has out-of-range parent index " << p << std::endl;
            ok = false;
          }
          else
          {
            parentRefCount[static_cast<std::size_t>(i)]++;
          }
        }

        // Child index validity and kind invariants (live cells only).
        if (c.kind == CellKind::Leaf)
        {
          if (c.firstChild.has_value() || c.secondChild.has_value())
          {
            std::cout << "[validate] ERROR: leaf cell " << i << " has children" << std::endl;
            ok = false;
          }
          // Leaf cells must have a leafId
          if (!c.leafId.has_value())
          {
            std::cout << "[validate] ERROR: leaf cell " << i << " does not have a leafId" << std::endl;
            ok = false;
          }
        }
        else // Split
        {
          if (!c.firstChild.has_value() || !c.secondChild.has_value())
          {
            std::cout << "[validate] ERROR: split cell " << i << " is missing children" << std::endl;
            ok = false;
          }
          // Split cells must NOT have a leafId
          if (c.leafId.has_value())
          {
            std::cout << "[validate] ERROR: split cell " << i << " has a leafId (should be null)" << std::endl;
            ok = false;
          }
        }

        auto checkChild = [&](const std::optional<int> &childOpt, const char *label)
        {
          if (!childOpt.has_value())
          {
            return;
          }

          int child = *childOpt;
          if (child < 0 || static_cast<std::size_t>(child) >= state.cells.size())
          {
            std::cout << "[validate] ERROR: cell " << i << " has out-of-range " << label << " index " << child << std::endl;
            ok = false;
            return;
          }

          const Cell &cc = state.cells[static_cast<std::size_t>(child)];
          if (cc.isDead)
          {
            // Live cell pointing to a dead child is suspicious but not fatal;
            // log it and continue.
            std::cout << "[validate] WARNING: cell " << i << "'s " << label << " (" << child
                      << ") is dead" << std::endl;
            ok = false;
          }
          if (!cc.parent.has_value() || *cc.parent != i)
          {
            std::cout << "[validate] ERROR: cell " << i << "'s " << label << " (" << child
                      << ") does not point back to parent " << i << std::endl;
            ok = false;
          }

          childRefCount[static_cast<std::size_t>(child)]++;
        };

        checkChild(c.firstChild, "firstChild");
        checkChild(c.secondChild, "secondChild");
      }

      // Check reference counters for anomalies.
      for (std::size_t i = 0; i < state.cells.size(); ++i)
      {
        if (parentRefCount[i] > 1)
        {
          std::cout << "[validate] WARNING: cell " << i << " has parent set more than once ("
                    << parentRefCount[i] << ")" << std::endl;
          ok = false;
        }

        if (childRefCount[i] > 2)
        {
          std::cout << "[validate] WARNING: cell " << i << " is referenced as a child more than twice ("
                    << childRefCount[i] << ")" << std::endl;
          ok = false;
        }
      }

      // Check for duplicate leafIds among leaf cells.
      std::vector<size_t> leafIds;
      for (int i = 0; i < static_cast<int>(state.cells.size()); ++i)
      {
        const Cell &c = state.cells[static_cast<std::size_t>(i)];
        if (c.isDead)
        {
          continue;
        }
        if (c.kind == CellKind::Leaf && c.leafId.has_value())
        {
          leafIds.push_back(*c.leafId);
        }
      }

      // Sort and check for duplicates
      std::sort(leafIds.begin(), leafIds.end());
      for (std::size_t i = 1; i < leafIds.size(); ++i)
      {
        if (leafIds[i] == leafIds[i - 1])
        {
          std::cout << "[validate] ERROR: duplicate leafId " << leafIds[i] << " found" << std::endl;
          ok = false;
        }
      }

      if (ok)
      {
        std::cout << "[validate] State OK (" << state.cells.size() << " cells)" << std::endl;
      }
      else
      {
        std::cout << "[validate] State has anomalies" << std::endl;
      }

      return ok;
    }
  }
} // namespace wintiler
