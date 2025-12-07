#pragma once

#include <optional>
#include <vector>

namespace wintiler
{

  enum class CellKind
  {
    Leaf,
    Split,
  };

  enum class SplitDir
  {
    Vertical,
    Horizontal,
  };

  struct Rect
  {
    float x;
    float y;
    float width;
    float height;
  };

  struct Cell
  {
    CellKind kind;
    SplitDir splitDir;

    bool isDead = false;

    std::optional<int> parent;      // empty for root
    std::optional<int> firstChild;  // empty if none (leaf)
    std::optional<int> secondChild; // empty if none (leaf)

    Rect rect; // logical rectangle in window coordinates
  };

  struct AppState
  {
    std::vector<Cell> cells;
    std::optional<int> rootIndex;     // empty until initialized
    std::optional<int> selectedIndex; // always holds a Leaf index when set

    // Global split direction that alternates on each cell creation.
    SplitDir globalSplitDir;
  };

} // namespace wintiler
