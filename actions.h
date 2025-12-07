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

} // namespace wintiler
