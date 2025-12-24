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
    CHECK(!state.selectedIndex.has_value());
    CHECK(state.nextLeafId == 1);
  }

  TEST_CASE("isLeaf returns false for empty state") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    CHECK(!cell_logic::isLeaf(state, 0));
    CHECK(!cell_logic::isLeaf(state, -1));
    CHECK(!cell_logic::isLeaf(state, 100));
  }

  TEST_CASE("splitSelectedLeaf creates root cell when cluster is empty") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    auto leafId = cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);

    CHECK(leafId.has_value());
    CHECK(*leafId == 1);
    CHECK(state.cells.size() == 1);
    CHECK(state.selectedIndex.has_value());
    CHECK(*state.selectedIndex == 0);
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

  TEST_CASE("splitSelectedLeaf splits existing leaf") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    // Create root
    auto rootLeafId = cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);
    REQUIRE(rootLeafId.has_value());

    // Split root - should create two children
    auto newLeafId = cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);

    CHECK(newLeafId.has_value());
    CHECK(*newLeafId == 2);
    CHECK(state.cells.size() == 3);

    // Root should no longer be a leaf
    CHECK(!cell_logic::isLeaf(state, 0));
    CHECK(!state.cells[0].leafId.has_value());

    // Children should be leaves
    CHECK(cell_logic::isLeaf(state, 1));
    CHECK(cell_logic::isLeaf(state, 2));

    // Selection should move to first child
    CHECK(state.selectedIndex.has_value());
    CHECK(*state.selectedIndex == 1);

    // First child keeps the parent's leafId
    CHECK(state.cells[1].leafId.has_value());
    CHECK(*state.cells[1].leafId == 1);

    // Second child gets the new leafId
    CHECK(state.cells[2].leafId.has_value());
    CHECK(*state.cells[2].leafId == 2);
  }

  TEST_CASE("splitSelectedLeaf alternates split direction") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    CHECK(state.globalSplitDir == cell_logic::SplitDir::Vertical);

    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Create root (no toggle, just creates leaf)
    CHECK(state.globalSplitDir == cell_logic::SplitDir::Vertical);

    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // First split - toggles to Horizontal
    CHECK(state.globalSplitDir == cell_logic::SplitDir::Horizontal);

    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Second split - toggles back to Vertical
    CHECK(state.globalSplitDir == cell_logic::SplitDir::Vertical);
  }

  TEST_CASE("splitSelectedLeaf creates correct rects for vertical split") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Create root (vertical split dir)
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Split vertically

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

  TEST_CASE("splitSelectedLeaf creates correct rects for horizontal split") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Create root
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // First split (vertical)

    // Now globalSplitDir is Horizontal, select first child for horizontal split
    state.selectedIndex = 1;
    state.globalSplitDir = cell_logic::SplitDir::Horizontal;
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Split horizontally

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

  TEST_CASE("deleteSelectedLeaf removes root cell") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);

    bool result = cell_logic::deleteSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);

    CHECK(result);
    CHECK(state.cells.empty());
    CHECK(!state.selectedIndex.has_value());
  }

  TEST_CASE("deleteSelectedLeaf promotes sibling") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Root
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Split into 2 children

    // Select second child and delete it
    state.selectedIndex = 2;
    bool result = cell_logic::deleteSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);

    CHECK(result);
    // After deletion, the first child should be promoted to root position
    // and selection should move to the promoted cell
    CHECK(state.selectedIndex.has_value());
    CHECK(cell_logic::isLeaf(state, *state.selectedIndex));
  }

  TEST_CASE("deleteSelectedLeaf returns false with no selection") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);

    bool result = cell_logic::deleteSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);
    CHECK(!result);
  }

  TEST_CASE("toggleSelectedSplitDir toggles parent's split direction") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Root
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Split

    // Parent (root) should have Vertical split dir initially
    CHECK(state.cells[0].splitDir == cell_logic::SplitDir::Vertical);

    bool result = cell_logic::toggleSelectedSplitDir(state, TEST_GAP_H, TEST_GAP_V);

    CHECK(result);
    CHECK(state.cells[0].splitDir == cell_logic::SplitDir::Horizontal);

    // Toggle back
    result = cell_logic::toggleSelectedSplitDir(state, TEST_GAP_H, TEST_GAP_V);
    CHECK(result);
    CHECK(state.cells[0].splitDir == cell_logic::SplitDir::Vertical);
  }

  TEST_CASE("toggleSelectedSplitDir returns false for root leaf") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V); // Create root leaf

    bool result = cell_logic::toggleSelectedSplitDir(state, TEST_GAP_H, TEST_GAP_V);
    CHECK(!result); // Root has no parent to toggle
  }

  TEST_CASE("validateState returns true for valid empty state") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    CHECK(cell_logic::validateState(state));
  }

  TEST_CASE("validateState returns true for valid state with cells") {
    auto state = cell_logic::createInitialState(800.0f, 600.0f);
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);
    cell_logic::splitSelectedLeaf(state, TEST_GAP_H, TEST_GAP_V);

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
    CHECK(!system.selectedClusterId.has_value());
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
    CHECK(system.selectedClusterId.has_value());
    CHECK(*system.selectedClusterId == 1);

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
    CHECK(system.selectedClusterId.has_value());
    CHECK(*system.selectedClusterId == 1);

    // Move right should go to second cluster
    bool result = multi_cell_logic::moveSelection(system, cell_logic::Direction::Right);
    CHECK(result);
    CHECK(*system.selectedClusterId == 2);

    // Move left should go back to first cluster
    result = multi_cell_logic::moveSelection(system, cell_logic::Direction::Left);
    CHECK(result);
    CHECK(*system.selectedClusterId == 1);
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
    CHECK(*system.selectedClusterId == 1);

    // Remove selected cluster
    multi_cell_logic::removeCluster(system, 1);

    // Selection should move to remaining cluster
    CHECK(system.selectedClusterId.has_value());
    CHECK(*system.selectedClusterId == 2);
  }

}
