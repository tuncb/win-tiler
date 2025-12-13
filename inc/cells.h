#pragma once

#include <optional>
#include <vector>

namespace wintiler {
namespace cell_logic {

enum class SplitDir {
  Vertical,
  Horizontal,
};

struct Rect {
  float x;
  float y;
  float width;
  float height;
};

struct Cell {
  SplitDir splitDir;

  bool isDead = false;

  std::optional<int> parent;      // empty for root
  std::optional<int> firstChild;  // empty if none (leaf)
  std::optional<int> secondChild; // empty if none (leaf)

  Rect rect; // logical rectangle in window coordinates

  std::optional<size_t> leafId; // unique ID for leaf cells only
};

struct CellCluster {
  std::vector<Cell> cells;
  std::optional<int> selectedIndex; // always holds a Leaf index when set

  // Global split direction that alternates on each cell creation.
  SplitDir globalSplitDir;

  float gapHorizontal;
  float gapVertical;

  // Logical window size used to derive the initial root cell rect
  // when the first cell is created lazily on a split.
  float windowWidth = 0.0f;
  float windowHeight = 0.0f;

  size_t nextLeafId = 1; // Counter for unique leaf IDs
};

// Create initial CellCluster with a single root cell covering the given width/height.
CellCluster createInitialState(float width, float height);

// Returns true if the cell at cellIndex exists and has no children.
bool isLeaf(const CellCluster& state, int cellIndex);

// Append a cell to the state's cell array and return its index.
int addCell(CellCluster& state, const Cell& cell);

// Recompute rectangles for the subtree rooted at nodeIndex, assuming
// the root node's rect is already correctly set.
void recomputeSubtreeRects(CellCluster& state, int nodeIndex);

// Delete the currently selected leaf cell, promoting its sibling to
// take its place. Returns true if a deletion occurred.
bool deleteSelectedLeaf(CellCluster& state);

enum class Direction {
  Left,
  Right,
  Up,
  Down,
};

// Find the next leaf cell in the given direction relative to currentIndex.
// Returns empty optional if no suitable neighbor is found.
std::optional<int> findNextLeafInDirection(const CellCluster& state, int currentIndex,
                                           Direction dir);

// Move the selection in the given direction. Returns true if the
// selection changed.
bool moveSelection(CellCluster& state, Direction dir);

// Split the currently selected leaf cell into two children according
// to its splitDir. If there is no selection and the state has no cells,
// this will instead create a single root leaf covering the window and
// return its leafId. Returns id of the new leaf (or created root leaf)
// if the operation was performed.
std::optional<size_t> splitSelectedLeaf(CellCluster& state);

// Toggle the splitDir of the currently selected leaf between Vertical
// and Horizontal. Returns true if toggled.
bool toggleSelectedSplitDir(CellCluster& state);

// Debug: print the entire CellCluster to stdout.
void debugPrintState(const CellCluster& state);

// Validate internal invariants of the CellCluster and print any
// anomalies to stdout. Returns true if the state is valid.
bool validateState(const CellCluster& state);

} // namespace cell_logic
} // namespace wintiler
