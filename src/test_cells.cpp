#ifndef DOCTEST_CONFIG_DISABLE
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif // !DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <cmath>

#include "multi_cells.h"

using namespace wintiler;

// Default gap values for tests
constexpr float TEST_GAP_H = 10.0f;
constexpr float TEST_GAP_V = 10.0f;

// ============================================================================
// cell_logic Tests
// ============================================================================

TEST_SUITE("cell_logic") {

  TEST_CASE("createInitialState creates empty cluster with correct dimensions") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    CHECK(state.cells.empty());
    CHECK(state.windowWidth == 800.0f);
    CHECK(state.windowHeight == 600.0f);
    CHECK(state.globalSplitDir == cell_logic::SplitDir::Vertical);
    CHECK(state.nextLeafId == 1);
  }

  TEST_CASE("isLeaf returns false for empty state") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    CHECK(!cell_logic::isLeaf(state, 0));
    CHECK(!cell_logic::isLeaf(state, -1));
    CHECK(!cell_logic::isLeaf(state, 100));
  }

  TEST_CASE("splitLeaf creates root cell when cluster is empty") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    // Use -1 as selectedIndex for empty cluster
    auto result = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V);

    CHECK(result.has_value());
    CHECK(result->newLeafId == 1);
    CHECK(result->newSelectionIndex == 0);
    CHECK(state.cells.size() == 1);
    CHECK(cell_logic::isLeaf(state, 0));

    // Check root cell properties
    const auto& root = state.cells[0];
    CHECK(root.rect.x == 0.0f);
    CHECK(root.rect.y == 0.0f);
    CHECK(root.rect.width == 800.0f);
    CHECK(root.rect.height == 600.0f);
    CHECK(root.leafId.has_value());
    CHECK(*root.leafId == 1);
  }

  TEST_CASE("splitLeaf splits existing leaf") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    // Create root
    auto rootResult = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V);
    REQUIRE(rootResult.has_value());
    int selectionIndex = rootResult->newSelectionIndex;

    // Split root - should create two children
    auto splitResult = cell_logic::splitLeaf(state, selectionIndex, TEST_GAP_H, TEST_GAP_V);

    CHECK(splitResult.has_value());
    CHECK(splitResult->newLeafId == 2);
    CHECK(state.cells.size() == 3);

    // Root should no longer be a leaf
    CHECK(!cell_logic::isLeaf(state, 0));
    CHECK(!state.cells[0].leafId.has_value());

    // Children should be leaves
    CHECK(cell_logic::isLeaf(state, 1));
    CHECK(cell_logic::isLeaf(state, 2));

    // Selection should point to first child
    CHECK(splitResult->newSelectionIndex == 1);

    // First child keeps the parent's leafId
    CHECK(state.cells[1].leafId.has_value());
    CHECK(*state.cells[1].leafId == 1);

    // Second child gets the new leafId
    CHECK(state.cells[2].leafId.has_value());
    CHECK(*state.cells[2].leafId == 2);
  }

  TEST_CASE("splitLeaf alternates split direction") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    CHECK(state.globalSplitDir == cell_logic::SplitDir::Vertical);

    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V); // Create root (no toggle, just creates leaf)
    CHECK(state.globalSplitDir == cell_logic::SplitDir::Vertical);

    auto r2 = cell_logic::splitLeaf(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V); // First split - toggles to Horizontal
    CHECK(state.globalSplitDir == cell_logic::SplitDir::Horizontal);

    cell_logic::splitLeaf(state, r2->newSelectionIndex, TEST_GAP_H, TEST_GAP_V); // Second split - toggles back to Vertical
    CHECK(state.globalSplitDir == cell_logic::SplitDir::Vertical);
  }

  TEST_CASE("splitLeaf creates correct rects for vertical split") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V); // Create root (vertical split dir)
    cell_logic::splitLeaf(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V); // Split vertically

    // Children should be side by side
    const auto& first = state.cells[1];
    const auto& second = state.cells[2];

    float expectedWidth = (800.0f - TEST_GAP_H) / 2.0f; // (width - gap) / 2

    CHECK(first.rect.x == 0.0f);
    CHECK(first.rect.width == doctest::Approx(expectedWidth));
    CHECK(first.rect.height == 600.0f);

    CHECK(second.rect.x == doctest::Approx(expectedWidth + TEST_GAP_H));
    CHECK(second.rect.width == doctest::Approx(expectedWidth));
    CHECK(second.rect.height == 600.0f);
  }

  TEST_CASE("splitLeaf creates correct rects for horizontal split") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V); // Create root
    auto r2 = cell_logic::splitLeaf(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V); // First split (vertical)

    // Now globalSplitDir is Horizontal, split first child horizontally
    int selectedIndex = r2->newSelectionIndex;  // first child (index 1)
    state.globalSplitDir = cell_logic::SplitDir::Horizontal;
    cell_logic::splitLeaf(state, selectedIndex, TEST_GAP_H, TEST_GAP_V); // Split horizontally

    // Children should be stacked vertically
    const auto& first = state.cells[3];
    const auto& second = state.cells[4];

    float parentHeight = state.cells[1].rect.height;
    float expectedHeight = (parentHeight - TEST_GAP_V) / 2.0f;

    CHECK(first.rect.y == state.cells[1].rect.y);
    CHECK(first.rect.height == doctest::Approx(expectedHeight));

    CHECK(second.rect.y == doctest::Approx(first.rect.y + expectedHeight + TEST_GAP_V));
    CHECK(second.rect.height == doctest::Approx(expectedHeight));
  }

  TEST_CASE("deleteLeaf removes root cell") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V);

    auto result = cell_logic::deleteLeaf(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V);

    // Deleting root cell clears the cluster
    CHECK(!result.has_value());  // nullopt means cluster is empty
    CHECK(state.cells.empty());
  }

  TEST_CASE("deleteLeaf promotes sibling") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V); // Root
    cell_logic::splitLeaf(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V); // Split into 2 children

    // Delete second child
    auto result = cell_logic::deleteLeaf(state, 2, TEST_GAP_H, TEST_GAP_V);

    CHECK(result.has_value());
    // After deletion, the first child should be promoted to root position
    // and result should point to the promoted cell
    CHECK(cell_logic::isLeaf(state, *result));
  }

  TEST_CASE("deleteLeaf returns nullopt for invalid index") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    auto result = cell_logic::deleteLeaf(state, -1, TEST_GAP_H, TEST_GAP_V);
    CHECK(!result.has_value());
  }

  TEST_CASE("toggleSplitDir toggles parent's split direction") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V); // Root
    auto r2 = cell_logic::splitLeaf(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V); // Split

    // Parent (root) should have Vertical split dir initially
    CHECK(state.cells[0].splitDir == cell_logic::SplitDir::Vertical);

    bool result = cell_logic::toggleSplitDir(state, r2->newSelectionIndex, TEST_GAP_H, TEST_GAP_V);

    CHECK(result);
    CHECK(state.cells[0].splitDir == cell_logic::SplitDir::Horizontal);

    // Toggle back
    result = cell_logic::toggleSplitDir(state, r2->newSelectionIndex, TEST_GAP_H, TEST_GAP_V);
    CHECK(result);
    CHECK(state.cells[0].splitDir == cell_logic::SplitDir::Vertical);
  }

  TEST_CASE("toggleSplitDir returns false for root leaf") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V); // Create root leaf

    bool result = cell_logic::toggleSplitDir(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V);
    CHECK(!result); // Root has no parent to toggle
  }

  TEST_CASE("validateState returns true for valid empty state") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    CHECK(cell_logic::validateState(state));
  }

  TEST_CASE("validateState returns true for valid state with cells") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    auto r1 = cell_logic::splitLeaf(state, -1, TEST_GAP_H, TEST_GAP_V);
    auto r2 = cell_logic::splitLeaf(state, r1->newSelectionIndex, TEST_GAP_H, TEST_GAP_V);
    cell_logic::splitLeaf(state, r2->newSelectionIndex, TEST_GAP_H, TEST_GAP_V);

    CHECK(cell_logic::validateState(state));
  }

}

// ============================================================================
// multi_cell_logic Tests
// ============================================================================

TEST_SUITE("multi_cell_logic") {

  TEST_CASE("createSystem creates empty system") {
    auto system = multi_cell_logic::createSystem({});

    CHECK(system.clusters.empty());
    CHECK(!system.selection.has_value());
    CHECK(system.globalNextLeafId == 1);
  }

  TEST_CASE("createSystem creates system with single cluster") {
    multi_cell_logic::ClusterInitInfo info;
    info.id = 1;
    info.x = 0.0f;
    info.y = 0.0f;
    info.width = 800.0f;
    info.height = 600.0f;

    auto system = multi_cell_logic::createSystem({info});

    CHECK(system.clusters.size() == 1);
    CHECK(system.clusters[0].id == 1);
    CHECK(system.clusters[0].globalX == 0.0f);
    CHECK(system.clusters[0].globalY == 0.0f);
    CHECK(system.clusters[0].cluster.windowWidth == 800.0f);
    CHECK(system.clusters[0].cluster.windowHeight == 600.0f);
  }

  TEST_CASE("createSystem creates system with multiple clusters") {
    std::vector<multi_cell_logic::ClusterInitInfo> infos;

    multi_cell_logic::ClusterInitInfo info1;
    info1.id = 1;
    info1.x = 0.0f;
    info1.y = 0.0f;
    info1.width = 800.0f;
    info1.height = 600.0f;

    multi_cell_logic::ClusterInitInfo info2;
    info2.id = 2;
    info2.x = 800.0f;
    info2.y = 0.0f;
    info2.width = 400.0f;
    info2.height = 600.0f;

    infos.push_back(info1);
    infos.push_back(info2);

    auto system = multi_cell_logic::createSystem(infos);

    CHECK(system.clusters.size() == 2);
    CHECK(system.clusters[0].id == 1);
    CHECK(system.clusters[1].id == 2);
    CHECK(system.clusters[1].globalX == 800.0f);
  }

  TEST_CASE("createSystem with initialCellIds pre-creates leaves") {
    multi_cell_logic::ClusterInitInfo info;
    info.id = 1;
    info.x = 0.0f;
    info.y = 0.0f;
    info.width = 800.0f;
    info.height = 600.0f;
    info.initialCellIds = {10, 20};

    auto system = multi_cell_logic::createSystem({info});

    CHECK(system.clusters.size() == 1);
    CHECK(!system.clusters[0].cluster.cells.empty());
    CHECK(system.selection.has_value());
    CHECK(system.selection->clusterId == 1);

    // Count leaves
    size_t leafCount = multi_cell_logic::countTotalLeaves(system);
    CHECK(leafCount == 2);
  }

  TEST_CASE("addCluster adds cluster to existing system") {
    auto system = multi_cell_logic::createSystem({});

    multi_cell_logic::ClusterInitInfo info;
    info.id = 5;
    info.x = 100.0f;
    info.y = 200.0f;
    info.width = 400.0f;
    info.height = 300.0f;

    auto id = multi_cell_logic::addCluster(system, info);

    CHECK(id == 5);
    CHECK(system.clusters.size() == 1);
    CHECK(system.clusters[0].id == 5);
    CHECK(system.clusters[0].globalX == 100.0f);
    CHECK(system.clusters[0].globalY == 200.0f);
  }

  TEST_CASE("removeCluster removes existing cluster") {
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 800.0f, 600.0f, {}};
    multi_cell_logic::ClusterInitInfo info2{2, 800.0f, 0.0f, 400.0f, 600.0f, {}};

    auto system = multi_cell_logic::createSystem({info1, info2});
    CHECK(system.clusters.size() == 2);

    bool result = multi_cell_logic::removeCluster(system, 1);

    CHECK(result);
    CHECK(system.clusters.size() == 1);
    CHECK(system.clusters[0].id == 2);
  }

  TEST_CASE("removeCluster returns false for non-existent cluster") {
    auto system = multi_cell_logic::createSystem({});

    bool result = multi_cell_logic::removeCluster(system, 999);
    CHECK(!result);
  }

  TEST_CASE("getCluster returns correct cluster") {
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 800.0f, 600.0f, {}};
    multi_cell_logic::ClusterInitInfo info2{2, 800.0f, 0.0f, 400.0f, 600.0f, {}};

    auto system = multi_cell_logic::createSystem({info1, info2});

    auto* pc = multi_cell_logic::getCluster(system, 2);
    REQUIRE(pc != nullptr);
    CHECK(pc->id == 2);
    CHECK(pc->globalX == 800.0f);

    auto* notFound = multi_cell_logic::getCluster(system, 999);
    CHECK(notFound == nullptr);
  }

  TEST_CASE("localToGlobal converts coordinates correctly") {
    multi_cell_logic::PositionedCluster pc;
    pc.globalX = 100.0f;
    pc.globalY = 50.0f;

    cell_logic::Rect local{10.0f, 20.0f, 30.0f, 40.0f};
    auto global = multi_cell_logic::localToGlobal(pc, local);

    CHECK(global.x == 110.0f);
    CHECK(global.y == 70.0f);
    CHECK(global.width == 30.0f);
    CHECK(global.height == 40.0f);
  }

  TEST_CASE("globalToLocal converts coordinates correctly") {
    multi_cell_logic::PositionedCluster pc;
    pc.globalX = 100.0f;
    pc.globalY = 50.0f;

    cell_logic::Rect global{110.0f, 70.0f, 30.0f, 40.0f};
    auto local = multi_cell_logic::globalToLocal(pc, global);

    CHECK(local.x == 10.0f);
    CHECK(local.y == 20.0f);
    CHECK(local.width == 30.0f);
    CHECK(local.height == 40.0f);
  }

  TEST_CASE("getCellGlobalRect returns correct global rect") {
    multi_cell_logic::ClusterInitInfo info{1, 100.0f, 50.0f, 800.0f, 600.0f, {1}};
    auto system = multi_cell_logic::createSystem({info});

    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);
    REQUIRE(!pc->cluster.cells.empty());

    auto globalRect = multi_cell_logic::getCellGlobalRect(*pc, 0);

    CHECK(globalRect.x == 100.0f);
    CHECK(globalRect.y == 50.0f);
    CHECK(globalRect.width == 800.0f);
    CHECK(globalRect.height == 600.0f);
  }

  TEST_CASE("splitSelectedLeaf creates new leaf with global ID") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = multi_cell_logic::createSystem({info});

    auto newLeafId = multi_cell_logic::splitSelectedLeaf(system);

    CHECK(newLeafId.has_value());
    CHECK(multi_cell_logic::countTotalLeaves(system) == 2);
  }

  TEST_CASE("splitSelectedLeaf returns nullopt with no selection") {
    auto system = multi_cell_logic::createSystem({});

    auto result = multi_cell_logic::splitSelectedLeaf(system);
    CHECK(!result.has_value());
  }

  TEST_CASE("deleteSelectedLeaf removes leaf") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = multi_cell_logic::createSystem({info});

    CHECK(multi_cell_logic::countTotalLeaves(system) == 2);

    bool result = multi_cell_logic::deleteSelectedLeaf(system);

    CHECK(result);
    CHECK(multi_cell_logic::countTotalLeaves(system) == 1);
  }

  TEST_CASE("getSelectedCell returns current selection") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = multi_cell_logic::createSystem({info});

    auto selected = multi_cell_logic::getSelectedCell(system);

    CHECK(selected.has_value());
    CHECK(selected->first == 1);  // Cluster ID
    CHECK(selected->second == 0); // Cell index
  }

  TEST_CASE("getSelectedCell returns nullopt with no selection") {
    auto system = multi_cell_logic::createSystem({});

    auto selected = multi_cell_logic::getSelectedCell(system);
    CHECK(!selected.has_value());
  }

  TEST_CASE("getSelectedCellGlobalRect returns correct rect") {
    multi_cell_logic::ClusterInitInfo info{1, 100.0f, 50.0f, 800.0f, 600.0f, {1}};
    auto system = multi_cell_logic::createSystem({info});

    auto rect = multi_cell_logic::getSelectedCellGlobalRect(system);

    CHECK(rect.has_value());
    CHECK(rect->x == 100.0f);
    CHECK(rect->y == 50.0f);
    CHECK(rect->width == 800.0f);
    CHECK(rect->height == 600.0f);
  }

  TEST_CASE("countTotalLeaves counts correctly across clusters") {
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    multi_cell_logic::ClusterInitInfo info2{2, 800.0f, 0.0f, 400.0f, 600.0f, {3, 4, 5}};

    auto system = multi_cell_logic::createSystem({info1, info2});

    size_t count = multi_cell_logic::countTotalLeaves(system);
    CHECK(count == 5);
  }

  TEST_CASE("validateSystem returns true for valid system") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = multi_cell_logic::createSystem({info});

    CHECK(multi_cell_logic::validateSystem(system));
  }

  TEST_CASE("validateSystem returns true for empty system") {
    auto system = multi_cell_logic::createSystem({});
    CHECK(multi_cell_logic::validateSystem(system));
  }

}

// ============================================================================
// Cross-Cluster Navigation Tests
// ============================================================================

TEST_SUITE("multi_cell_logic navigation") {

  TEST_CASE("moveSelection moves within single cluster horizontally") {
    // Create a cluster with 2 cells side by side
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = multi_cell_logic::createSystem({info});

    // After split, first child is selected
    auto selected = multi_cell_logic::getSelectedCell(system);
    REQUIRE(selected.has_value());

    // Move right should go to sibling
    bool result = multi_cell_logic::moveSelection(system, cell_logic::Direction::Right);
    CHECK(result);

    auto newSelected = multi_cell_logic::getSelectedCell(system);
    REQUIRE(newSelected.has_value());
    CHECK(newSelected->first == 1); // Same cluster
    CHECK(newSelected->second != selected->second); // Different cell
  }

  TEST_CASE("moveSelection returns false when no selection") {
    auto system = multi_cell_logic::createSystem({});

    bool result = multi_cell_logic::moveSelection(system, cell_logic::Direction::Right);
    CHECK(!result);
  }

  TEST_CASE("moveSelection returns false when no cell in direction") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = multi_cell_logic::createSystem({info});

    // Only one cell, can't move anywhere
    bool result = multi_cell_logic::moveSelection(system, cell_logic::Direction::Left);
    CHECK(!result);
  }

  TEST_CASE("moveSelection moves across clusters") {
    // Create two clusters side by side
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 400.0f, 600.0f, {1}};
    multi_cell_logic::ClusterInitInfo info2{2, 400.0f, 0.0f, 400.0f, 600.0f, {2}};

    auto system = multi_cell_logic::createSystem({info1, info2});

    // Selection should be in first cluster
    CHECK(system.selection.has_value());
    CHECK(system.selection->clusterId == 1);

    // Move right should go to second cluster
    bool result = multi_cell_logic::moveSelection(system, cell_logic::Direction::Right);
    CHECK(result);
    CHECK(system.selection->clusterId == 2);

    // Move left should go back to first cluster
    result = multi_cell_logic::moveSelection(system, cell_logic::Direction::Left);
    CHECK(result);
    CHECK(system.selection->clusterId == 1);
  }

  TEST_CASE("findNextLeafInDirection finds correct cell left") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = multi_cell_logic::createSystem({info});

    // Get the cluster and find which cells are where
    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);

    // Find leaves
    int leftLeaf = -1, rightLeaf = -1;
    for (int i = 0; i < static_cast<int>(pc->cluster.cells.size()); ++i) {
      if (cell_logic::isLeaf(pc->cluster, i)) {
        auto rect = multi_cell_logic::getCellGlobalRect(*pc, i);
        if (leftLeaf < 0 || rect.x < multi_cell_logic::getCellGlobalRect(*pc, leftLeaf).x) {
          rightLeaf = leftLeaf;
          leftLeaf = i;
        } else {
          rightLeaf = i;
        }
      }
    }

    REQUIRE(leftLeaf >= 0);
    REQUIRE(rightLeaf >= 0);

    // From right leaf, find left should return left leaf
    auto result = multi_cell_logic::findNextLeafInDirection(system, 1, rightLeaf,
                                                            cell_logic::Direction::Left);
    CHECK(result.has_value());
    CHECK(result->first == 1);
    CHECK(result->second == leftLeaf);
  }

  TEST_CASE("findNextLeafInDirection finds correct cell right") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = multi_cell_logic::createSystem({info});

    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);

    // Find leaves
    int leftLeaf = -1, rightLeaf = -1;
    for (int i = 0; i < static_cast<int>(pc->cluster.cells.size()); ++i) {
      if (cell_logic::isLeaf(pc->cluster, i)) {
        auto rect = multi_cell_logic::getCellGlobalRect(*pc, i);
        if (leftLeaf < 0 || rect.x < multi_cell_logic::getCellGlobalRect(*pc, leftLeaf).x) {
          rightLeaf = leftLeaf;
          leftLeaf = i;
        } else {
          rightLeaf = i;
        }
      }
    }

    REQUIRE(leftLeaf >= 0);
    REQUIRE(rightLeaf >= 0);

    // From left leaf, find right should return right leaf
    auto result = multi_cell_logic::findNextLeafInDirection(system, 1, leftLeaf,
                                                            cell_logic::Direction::Right);
    CHECK(result.has_value());
    CHECK(result->first == 1);
    CHECK(result->second == rightLeaf);
  }

  TEST_CASE("findNextLeafInDirection crosses clusters") {
    // Two clusters side by side
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 400.0f, 600.0f, {1}};
    multi_cell_logic::ClusterInitInfo info2{2, 400.0f, 0.0f, 400.0f, 600.0f, {2}};

    auto system = multi_cell_logic::createSystem({info1, info2});

    // From cluster 1's leaf, find right should find cluster 2's leaf
    auto* pc1 = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc1 != nullptr);
    int leaf1 = 0; // The only cell in cluster 1
    for (int i = 0; i < static_cast<int>(pc1->cluster.cells.size()); ++i) {
      if (cell_logic::isLeaf(pc1->cluster, i)) {
        leaf1 = i;
        break;
      }
    }

    auto result = multi_cell_logic::findNextLeafInDirection(system, 1, leaf1,
                                                            cell_logic::Direction::Right);
    CHECK(result.has_value());
    CHECK(result->first == 2); // Should be in cluster 2
  }

  TEST_CASE("findNextLeafInDirection returns nullopt when no cell in direction") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = multi_cell_logic::createSystem({info});

    auto result = multi_cell_logic::findNextLeafInDirection(system, 1, 0,
                                                            cell_logic::Direction::Left);
    CHECK(!result.has_value());
  }

  TEST_CASE("toggleSelectedSplitDir works through multi_cell_logic") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = multi_cell_logic::createSystem({info});

    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);

    // Get initial split dir of parent
    cell_logic::SplitDir initialDir = pc->cluster.cells[0].splitDir;

    bool result = multi_cell_logic::toggleSelectedSplitDir(system);
    CHECK(result);

    // Check direction changed
    CHECK(pc->cluster.cells[0].splitDir != initialDir);
  }

  TEST_CASE("removeCluster updates selection to remaining cluster") {
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 400.0f, 600.0f, {1}};
    multi_cell_logic::ClusterInitInfo info2{2, 400.0f, 0.0f, 400.0f, 600.0f, {2}};

    auto system = multi_cell_logic::createSystem({info1, info2});

    // Selection should be in first cluster initially
    CHECK(system.selection->clusterId == 1);

    // Remove selected cluster
    multi_cell_logic::removeCluster(system, 1);

    // Selection should move to remaining cluster
    CHECK(system.selection.has_value());
    CHECK(system.selection->clusterId == 2);
  }

}

// ============================================================================
// System Update Tests
// ============================================================================

TEST_SUITE("multi_cell_logic updateSystem") {

  TEST_CASE("getClusterLeafIds returns all leaf IDs") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20, 30}};
    auto system = multi_cell_logic::createSystem({info});

    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);

    auto leafIds = multi_cell_logic::getClusterLeafIds(pc->cluster);

    CHECK(leafIds.size() == 3);
    // Check all IDs are present (order may vary)
    std::sort(leafIds.begin(), leafIds.end());
    CHECK(leafIds[0] == 10);
    CHECK(leafIds[1] == 20);
    CHECK(leafIds[2] == 30);
  }

  TEST_CASE("getClusterLeafIds returns empty for empty cluster") {
    auto cluster = cell_logic::createInitialState(800.0f, 600.0f);

    auto leafIds = multi_cell_logic::getClusterLeafIds(cluster);
    CHECK(leafIds.empty());
  }

  TEST_CASE("findCellByLeafId finds existing leaf") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = multi_cell_logic::createSystem({info});

    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);

    auto idx10 = multi_cell_logic::findCellByLeafId(pc->cluster, 10);
    auto idx20 = multi_cell_logic::findCellByLeafId(pc->cluster, 20);

    CHECK(idx10.has_value());
    CHECK(idx20.has_value());
    CHECK(*idx10 != *idx20);
  }

  TEST_CASE("findCellByLeafId returns nullopt for non-existent leaf") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = multi_cell_logic::createSystem({info});

    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);

    auto result = multi_cell_logic::findCellByLeafId(pc->cluster, 999);
    CHECK(!result.has_value());
  }

  TEST_CASE("updateSystem adds leaves to empty cluster") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {}};
    auto system = multi_cell_logic::createSystem({info});

    CHECK(multi_cell_logic::countTotalLeaves(system) == 0);

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {100, 200}}
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.empty());
    CHECK(result.addedLeafIds.size() == 2);
    CHECK(result.deletedLeafIds.empty());
    CHECK(multi_cell_logic::countTotalLeaves(system) == 2);
  }

  TEST_CASE("updateSystem adds leaves to existing cluster") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = multi_cell_logic::createSystem({info});

    CHECK(multi_cell_logic::countTotalLeaves(system) == 1);

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {10, 20, 30}}  // Keep 10, add 20 and 30
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.empty());
    CHECK(result.addedLeafIds.size() == 2);
    CHECK(result.deletedLeafIds.empty());
    CHECK(multi_cell_logic::countTotalLeaves(system) == 3);
  }

  TEST_CASE("updateSystem deletes leaves") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20, 30}};
    auto system = multi_cell_logic::createSystem({info});

    CHECK(multi_cell_logic::countTotalLeaves(system) == 3);

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {10}}  // Keep only 10, delete 20 and 30
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.empty());
    CHECK(result.deletedLeafIds.size() == 2);
    CHECK(result.addedLeafIds.empty());
    CHECK(multi_cell_logic::countTotalLeaves(system) == 1);
  }

  TEST_CASE("updateSystem handles mixed add and delete") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = multi_cell_logic::createSystem({info});

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {10, 30}}  // Keep 10, delete 20, add 30
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.empty());
    CHECK(result.deletedLeafIds.size() == 1);
    CHECK(result.addedLeafIds.size() == 1);
    CHECK(result.deletedLeafIds[0] == 20);
    CHECK(result.addedLeafIds[0] == 30);
    CHECK(multi_cell_logic::countTotalLeaves(system) == 2);
  }

  TEST_CASE("updateSystem updates selection") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = multi_cell_logic::createSystem({info});

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {10, 20}}  // No changes, just update selection
    };

    auto result = multi_cell_logic::updateSystem(system, updates, {{1, 20}});

    CHECK(result.errors.empty());
    CHECK(result.selectionUpdated);
    CHECK(system.selection.has_value());
    CHECK(system.selection->clusterId == 1);

    // Verify selection points to leaf with ID 20
    auto* pc = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc != nullptr);
    auto& cell = pc->cluster.cells[static_cast<size_t>(system.selection->cellIndex)];
    CHECK(cell.leafId.has_value());
    CHECK(*cell.leafId == 20);
  }

  TEST_CASE("updateSystem reports error for unknown cluster") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = multi_cell_logic::createSystem({info});

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {999, {10, 20}}  // Cluster 999 doesn't exist
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0].type == multi_cell_logic::UpdateError::Type::ClusterNotFound);
    CHECK(result.errors[0].clusterId == 999);
  }

  TEST_CASE("updateSystem reports error for invalid selection cluster") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = multi_cell_logic::createSystem({info});

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {10}}
    };

    auto result = multi_cell_logic::updateSystem(system, updates, {{999, 10}});

    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0].type == multi_cell_logic::UpdateError::Type::SelectionInvalid);
    CHECK(!result.selectionUpdated);
  }

  TEST_CASE("updateSystem reports error for invalid selection leaf") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = multi_cell_logic::createSystem({info});

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {10}}
    };

    auto result = multi_cell_logic::updateSystem(system, updates, {{1, 999}});

    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0].type == multi_cell_logic::UpdateError::Type::SelectionInvalid);
    CHECK(result.errors[0].leafId == 999);
    CHECK(!result.selectionUpdated);
  }

  TEST_CASE("updateSystem handles multiple clusters") {
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 400.0f, 600.0f, {10}};
    multi_cell_logic::ClusterInitInfo info2{2, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = multi_cell_logic::createSystem({info1, info2});

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {10, 11}},  // Add 11 to cluster 1
        {2, {20, 21}}   // Add 21 to cluster 2
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.empty());
    CHECK(result.addedLeafIds.size() == 2);
    CHECK(multi_cell_logic::countTotalLeaves(system) == 4);
  }

  TEST_CASE("updateSystem leaves unchanged cluster alone") {
    multi_cell_logic::ClusterInitInfo info1{1, 0.0f, 0.0f, 400.0f, 600.0f, {10, 11}};
    multi_cell_logic::ClusterInitInfo info2{2, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = multi_cell_logic::createSystem({info1, info2});

    // Only update cluster 2, leave cluster 1 alone
    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {2, {20, 21}}
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.empty());

    // Cluster 1 should still have its original leaves
    auto* pc1 = multi_cell_logic::getCluster(system, 1);
    REQUIRE(pc1 != nullptr);
    auto leafIds1 = multi_cell_logic::getClusterLeafIds(pc1->cluster);
    CHECK(leafIds1.size() == 2);

    // Cluster 2 should have the new leaf
    auto* pc2 = multi_cell_logic::getCluster(system, 2);
    REQUIRE(pc2 != nullptr);
    auto leafIds2 = multi_cell_logic::getClusterLeafIds(pc2->cluster);
    CHECK(leafIds2.size() == 2);
  }

  TEST_CASE("updateSystem can clear cluster to empty") {
    multi_cell_logic::ClusterInitInfo info{1, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = multi_cell_logic::createSystem({info});

    std::vector<multi_cell_logic::ClusterCellIds> updates = {
        {1, {}}  // Empty - delete all leaves
    };

    auto result = multi_cell_logic::updateSystem(system, updates, std::nullopt);

    CHECK(result.errors.empty());
    CHECK(result.deletedLeafIds.size() == 1);
    CHECK(multi_cell_logic::countTotalLeaves(system) == 0);
  }

}
