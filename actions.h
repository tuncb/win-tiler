#pragma once

#include "state.h"

namespace wintiler
{

  // Create initial AppState with a single root cell covering the given width/height.
  AppState createInitialState(float width, float height);

  // Returns true if the cell at cellIndex exists and has no children.
  bool isLeaf(const AppState &state, int cellIndex);

  // Append a cell to the state's cell array and return its index.
  int addCell(AppState &state, const Cell &cell);

  // Recompute rectangles for the subtree rooted at nodeIndex, assuming
  // the root node's rect is already correctly set.
  void recomputeSubtreeRects(AppState &state, int nodeIndex);

  // Delete the currently selected leaf cell, promoting its sibling to
  // take its place. Returns true if a deletion occurred.
  bool deleteSelectedLeaf(AppState &state);

  enum class Direction
  {
    Left,
    Right,
    Up,
    Down,
  };

  // Find the next leaf cell in the given direction relative to currentIndex.
  // Returns empty optional if no suitable neighbor is found.
  std::optional<int> findNextLeafInDirection(const AppState &state, int currentIndex, Direction dir);

  // Move the selection in the given direction. Returns true if the
  // selection changed.
  bool moveSelection(AppState &state, Direction dir);

  // Split the currently selected leaf cell into two children according
  // to its splitDir. Returns true if the split was performed.
  bool splitSelectedLeaf(AppState &state);

  // Toggle the splitDir of the currently selected leaf between Vertical
  // and Horizontal. Returns true if toggled.
  bool toggleSelectedSplitDir(AppState &state);

  // Debug: print the entire AppState to stdout.
  void debugPrintState(const AppState &state);

  // Validate internal invariants of the AppState and print any
  // anomalies to stdout. Returns true if the state is valid.
  bool validateState(const AppState &state);

} // namespace wintiler
