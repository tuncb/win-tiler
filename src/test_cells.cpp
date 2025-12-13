#ifndef DOCTEST_CONFIG_DISABLE
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif // !DOCTEST_CONFIG_DISABLE

#include <cells.h>
#include <doctest/doctest.h>

#include <cmath>

using namespace wintiler::cell_logic;

TEST_CASE("Initial State") {
  CellCluster state = createInitialState(1920.0f, 1080.0f);
  CHECK(state.cells.empty());
  CHECK(state.windowWidth == 1920.0f);
  CHECK(state.windowHeight == 1080.0f);
  CHECK(!state.selectedIndex.has_value());
  CHECK(validateState(state));
}

TEST_CASE("Lazy Root Creation") {
  CellCluster state = createInitialState(100.0f, 100.0f);
  auto leafId = splitSelectedLeaf(state);

  REQUIRE(leafId.has_value());
  CHECK(state.cells.size() == 1);
  CHECK(state.selectedIndex == 0);
  CHECK(isLeaf(state, 0));

  const Cell& root = state.cells[0];
  CHECK(root.rect.width == 100.0f);
  CHECK(root.rect.height == 100.0f);
  CHECK(!root.parent.has_value());
  CHECK(validateState(state));
}

TEST_CASE("Splitting Leaf") {
  CellCluster state = createInitialState(100.0f, 100.0f);
  splitSelectedLeaf(state); // Create root

  // Root is selected. Split it.
  // Initial global split dir is Vertical.
  auto newLeafId = splitSelectedLeaf(state);

  REQUIRE(newLeafId.has_value());
  CHECK(state.cells.size() == 3); // Root (now split) + 2 children

  // Root should not be a leaf anymore
  CHECK(!isLeaf(state, 0));

  // Check children
  int rootIdx = 0;
  const Cell& root = state.cells[rootIdx];
  REQUIRE(root.firstChild.has_value());
  REQUIRE(root.secondChild.has_value());

  int child1Idx = *root.firstChild;
  int child2Idx = *root.secondChild;

  CHECK(isLeaf(state, child1Idx));
  CHECK(isLeaf(state, child2Idx));

  const Cell& child1 = state.cells[child1Idx];
  const Cell& child2 = state.cells[child2Idx];

  CHECK(child1.parent == rootIdx);
  CHECK(child2.parent == rootIdx);

  // Check geometry (Vertical split)
  // Gap is 10.0f by default.
  // Available width = 100 - 10 = 90. Child width = 45.
  CHECK(child1.rect.width == doctest::Approx(45.0f));
  CHECK(child2.rect.width == doctest::Approx(45.0f));
  CHECK(child1.rect.height == 100.0f);
  CHECK(child2.rect.height == 100.0f);

  CHECK(child1.rect.x == 0.0f);
  CHECK(child2.rect.x == doctest::Approx(55.0f)); // 45 + 10

  CHECK(validateState(state));
}

TEST_CASE("Navigation") {
  CellCluster state = createInitialState(100.0f, 100.0f);
  splitSelectedLeaf(state); // Root
  splitSelectedLeaf(state); // Split Vertical -> Left(0), Right(1). Selected is Left.

  // Current selection should be the first child (Left)
  // Note: splitSelectedLeaf implementation says: "Select the first child by default"
  // And "firstChild" is the left one in Vertical split.

  int leftChildIdx = *state.selectedIndex;

  // Move Right
  bool moved = moveSelection(state, Direction::Right);
  CHECK(moved);
  int rightChildIdx = *state.selectedIndex;
  CHECK(leftChildIdx != rightChildIdx);

  // Move Left
  moved = moveSelection(state, Direction::Left);
  CHECK(moved);
  CHECK(*state.selectedIndex == leftChildIdx);

  // Move Up (should fail)
  moved = moveSelection(state, Direction::Up);
  CHECK(!moved);
  CHECK(*state.selectedIndex == leftChildIdx);
}

TEST_CASE("Delete Leaf") {
  CellCluster state = createInitialState(100.0f, 100.0f);
  splitSelectedLeaf(state); // Root
  splitSelectedLeaf(state); // Split Vertical -> Left, Right. Selected is Left.

  int leftChildIdx = *state.selectedIndex;

  // Delete Left
  bool deleted = deleteSelectedLeaf(state);
  CHECK(deleted);

  // Should be back to 1 live cell (the root, which took the place of the sibling)
  // Actually, implementation promotes sibling to parent's slot.
  // Parent was index 0. Sibling was index 2.
  // Sibling is copied to index 0.
  // Index 1 (deleted) and 2 (sibling) are marked dead.

  CHECK(state.cells[0].isDead == false);
  CHECK(state.cells[1].isDead == true);
  CHECK(state.cells[2].isDead == true);

  CHECK(isLeaf(state, 0));
  CHECK(state.cells[0].rect.width == 100.0f); // Should occupy full window again

  CHECK(validateState(state));
}

TEST_CASE("Toggle Split Direction") {
  CellCluster state = createInitialState(100.0f, 100.0f);
  splitSelectedLeaf(state); // Root
  splitSelectedLeaf(state); // Split Vertical. Selected is Left.

  // Toggle split direction of the *parent* of the selected leaf.
  bool toggled = toggleSelectedSplitDir(state);
  CHECK(toggled);

  // Parent is root (0).
  CHECK(state.cells[0].splitDir == SplitDir::Horizontal);

  // Check children rects
  int child1Idx = *state.cells[0].firstChild;
  const Cell& child1 = state.cells[child1Idx];

  // Horizontal split: Width should be full, Height halved (minus gap)
  CHECK(child1.rect.width == 100.0f);
  CHECK(child1.rect.height == doctest::Approx(45.0f));

  CHECK(validateState(state));
}

TEST_CASE("Complex Scenario") {
  // Split multiple times and validate
  CellCluster state = createInitialState(100.0f, 100.0f);
  splitSelectedLeaf(state); // Root
  splitSelectedLeaf(state); // V-Split -> L, R. Sel: L

  moveSelection(state, Direction::Right); // Sel: R
  splitSelectedLeaf(state);               // H-Split R -> R_Top, R_Bottom. Sel: R_Top

  CHECK(state.cells.size() == 5); // Root, L, R(dead), R_Top, R_Bottom
  // Wait, R becomes a split node, so it's not dead, it's just not a leaf.
  // splitSelectedLeaf implementation:
  // "Convert leaf into a split node." -> It reuses the cell slot.
  // So R is at index 2. It is now a split node.
  // It adds 2 new children.
  // So cells size: Root(0), L(1), R(2), R_Top(3), R_Bottom(4). Total 5.

  CHECK(validateState(state));

  // Navigate
  // From R_Top (Right-Top), moving Left should go to L.
  moveSelection(state, Direction::Left);
  int current = *state.selectedIndex;
  CHECK(current == 1); // L

  // From L, moving Right should go to R_Top or R_Bottom (heuristic).
  moveSelection(state, Direction::Right);
  CHECK((*state.selectedIndex == 3 || *state.selectedIndex == 4));
}