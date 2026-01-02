#ifndef DOCTEST_CONFIG_DISABLE
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#endif // !DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "multi_cells.h"

using namespace wintiler;

// Default gap values for tests
constexpr float TEST_GAP_H = 10.0f;
constexpr float TEST_GAP_V = 10.0f;
constexpr float TEST_ZEN_PERCENTAGE = 0.8f;

// ============================================================================
// Multi-Cluster System Tests
// ============================================================================

TEST_SUITE("cells - multi-cluster") {
  TEST_CASE("createSystem creates empty system") {
    auto system = cells::create_system({});

    CHECK(system.clusters.empty());
    CHECK(!system.selection.has_value());
  }

  TEST_CASE("createSystem creates system with single cluster") {
    cells::ClusterInitInfo info;
    info.x = 0.0f;
    info.y = 0.0f;
    info.width = 800.0f;
    info.height = 600.0f;
    info.monitor_x = 0.0f;
    info.monitor_y = 0.0f;
    info.monitor_width = 800.0f;
    info.monitor_height = 600.0f;

    auto system = cells::create_system({info});

    CHECK(system.clusters.size() == 1);
    CHECK(system.clusters[0].global_x == 0.0f);
    CHECK(system.clusters[0].global_y == 0.0f);
    CHECK(system.clusters[0].cluster.window_width == 800.0f);
    CHECK(system.clusters[0].cluster.window_height == 600.0f);
  }

  TEST_CASE("createSystem creates system with multiple clusters") {
    std::vector<cells::ClusterInitInfo> infos;

    cells::ClusterInitInfo info1;
    info1.x = 0.0f;
    info1.y = 0.0f;
    info1.width = 800.0f;
    info1.height = 600.0f;
    info1.monitor_x = 0.0f;
    info1.monitor_y = 0.0f;
    info1.monitor_width = 800.0f;
    info1.monitor_height = 600.0f;

    cells::ClusterInitInfo info2;
    info2.x = 800.0f;
    info2.y = 0.0f;
    info2.width = 400.0f;
    info2.height = 600.0f;
    info2.monitor_x = 800.0f;
    info2.monitor_y = 0.0f;
    info2.monitor_width = 400.0f;
    info2.monitor_height = 600.0f;

    infos.push_back(info1);
    infos.push_back(info2);

    auto system = cells::create_system(infos);

    CHECK(system.clusters.size() == 2);
    CHECK(system.clusters[1].global_x == 800.0f);
  }

  TEST_CASE("createSystem with initialCellIds pre-creates leaves") {
    cells::ClusterInitInfo info;
    info.x = 0.0f;
    info.y = 0.0f;
    info.width = 800.0f;
    info.height = 600.0f;
    info.monitor_x = 0.0f;
    info.monitor_y = 0.0f;
    info.monitor_width = 800.0f;
    info.monitor_height = 600.0f;
    info.initial_cell_ids = {10, 20};

    auto system = cells::create_system({info});

    CHECK(system.clusters.size() == 1);
    CHECK(!system.clusters[0].cluster.cells.empty());
    CHECK(system.selection.has_value());
    CHECK(system.selection->cluster_index == 0);

    // Count leaves
    size_t leafCount = cells::count_total_leaves(system);
    CHECK(leafCount == 2);
  }

  TEST_CASE("direct cluster access works") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {}};
    cells::ClusterInitInfo info2{800.0f, 0.0f, 400.0f, 600.0f, 800.0f, 0.0f, 400.0f, 600.0f, {}};

    auto system = cells::create_system({info1, info2});

    REQUIRE(system.clusters.size() == 2);
    const auto& pc = system.clusters[1];
    CHECK(pc.global_x == 800.0f);
  }

  TEST_CASE("getCellGlobalRect returns correct global rect with edge margins") {
    cells::ClusterInitInfo info{100.0f, 50.0f, 800.0f, 600.0f, 100.0f, 50.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() == 1);
    auto& pc = system.clusters[0];
    REQUIRE(!pc.cluster.cells.empty());

    auto globalRect = cells::get_cell_global_rect(pc, 0);

    // Cell rect is inset by gapHorizontal/gapVertical (10px) on all edges
    CHECK(globalRect.x == 100.0f + TEST_GAP_H);
    CHECK(globalRect.y == 50.0f + TEST_GAP_V);
    CHECK(globalRect.width == 800.0f - 2.0f * TEST_GAP_H);
    CHECK(globalRect.height == 600.0f - 2.0f * TEST_GAP_V);
  }

  TEST_CASE("getSelectedCell returns current selection") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    auto selected = cells::get_selected_cell(system);

    CHECK(selected.has_value());
    CHECK(selected->first == 0);  // Cluster index
    CHECK(selected->second == 0); // Cell index
  }

  TEST_CASE("getSelectedCell returns nullopt with no selection") {
    auto system = cells::create_system({});

    auto selected = cells::get_selected_cell(system);
    CHECK(!selected.has_value());
  }

  TEST_CASE("countTotalLeaves counts correctly across clusters") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    cells::ClusterInitInfo info2{800.0f, 0.0f,   400.0f, 600.0f,   800.0f,
                                 0.0f,   400.0f, 600.0f, {3, 4, 5}};

    auto system = cells::create_system({info1, info2});

    size_t count = cells::count_total_leaves(system);
    CHECK(count == 5);
  }

  TEST_CASE("findCellAtPoint finds cell in single cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    // Point inside the cell
    auto result = cells::find_cell_at_point(system, 400.0f, 300.0f, TEST_ZEN_PERCENTAGE);

    REQUIRE(result.has_value());
    CHECK(result->first == 0);  // Cluster index
    CHECK(result->second == 0); // Cell index
  }

  TEST_CASE("findCellAtPoint returns nullopt for point outside all clusters") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    // Point outside the cluster
    auto result = cells::find_cell_at_point(system, 1000.0f, 1000.0f, TEST_ZEN_PERCENTAGE);

    CHECK(!result.has_value());
  }

  TEST_CASE("findCellAtPoint finds correct cell in split cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() == 1);
    auto& pc = system.clusters[0];

    // Find positions of both leaves
    auto idx1 = cells::find_cell_by_leaf_id(pc.cluster, 1);
    auto idx2 = cells::find_cell_by_leaf_id(pc.cluster, 2);
    REQUIRE(idx1.has_value());
    REQUIRE(idx2.has_value());

    auto rect1 = cells::get_cell_global_rect(pc, *idx1);
    auto rect2 = cells::get_cell_global_rect(pc, *idx2);

    // Test point in first cell
    auto result1 =
        cells::find_cell_at_point(system, rect1.x + 10.0f, rect1.y + 10.0f, TEST_ZEN_PERCENTAGE);
    REQUIRE(result1.has_value());
    CHECK(result1->first == 0);
    CHECK(result1->second == *idx1);

    // Test point in second cell
    auto result2 =
        cells::find_cell_at_point(system, rect2.x + 10.0f, rect2.y + 10.0f, TEST_ZEN_PERCENTAGE);
    REQUIRE(result2.has_value());
    CHECK(result2->first == 0);
    CHECK(result2->second == *idx2);
  }

  TEST_CASE("findCellAtPoint finds cell across multiple clusters") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {1}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {2}};
    auto system = cells::create_system({info1, info2});

    // Point in cluster 0
    auto result1 = cells::find_cell_at_point(system, 200.0f, 300.0f, TEST_ZEN_PERCENTAGE);
    REQUIRE(result1.has_value());
    CHECK(result1->first == 0);

    // Point in cluster 1
    auto result2 = cells::find_cell_at_point(system, 600.0f, 300.0f, TEST_ZEN_PERCENTAGE);
    REQUIRE(result2.has_value());
    CHECK(result2->first == 1);
  }

  TEST_CASE("findCellAtPoint returns nullopt for empty system") {
    auto system = cells::create_system({});

    auto result = cells::find_cell_at_point(system, 100.0f, 100.0f, TEST_ZEN_PERCENTAGE);

    CHECK(!result.has_value());
  }

  TEST_CASE("findCellAtPoint handles edge coordinates with margins") {
    cells::ClusterInitInfo info{100.0f, 50.0f, 800.0f, 600.0f, 100.0f, 50.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    // Cell rect is inset by gap (10px) on all edges
    // Cell starts at (110, 60), size (780, 580), ends at (890, 640)

    // Point in edge margin (outside cell)
    auto result0 = cells::find_cell_at_point(system, 100.0f, 50.0f, TEST_ZEN_PERCENTAGE);
    CHECK(!result0.has_value());

    // Point exactly at cell top-left corner (inclusive)
    auto result1 = cells::find_cell_at_point(system, 110.0f, 60.0f, TEST_ZEN_PERCENTAGE);
    REQUIRE(result1.has_value());
    CHECK(result1->first == 0);

    // Point just before cell bottom-right edge (exclusive)
    auto result2 = cells::find_cell_at_point(system, 889.0f, 639.0f, TEST_ZEN_PERCENTAGE);
    REQUIRE(result2.has_value());
    CHECK(result2->first == 0);

    // Point exactly at cell right edge (exclusive - outside)
    auto result3 = cells::find_cell_at_point(system, 890.0f, 300.0f, TEST_ZEN_PERCENTAGE);
    CHECK(!result3.has_value());

    // Point exactly at bottom edge (exclusive - outside)
    auto result4 = cells::find_cell_at_point(system, 500.0f, 650.0f, TEST_ZEN_PERCENTAGE);
    CHECK(!result4.has_value());
  }

  TEST_CASE("hasLeafId returns true for existing leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    CHECK(cells::has_leaf_id(system, 10));
    CHECK(cells::has_leaf_id(system, 20));
  }

  TEST_CASE("hasLeafId returns false for non-existent leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    CHECK(!cells::has_leaf_id(system, 999));
  }

  TEST_CASE("hasLeafId finds leaf across multiple clusters") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {10}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = cells::create_system({info1, info2});

    CHECK(cells::has_leaf_id(system, 10));
    CHECK(cells::has_leaf_id(system, 20));
    CHECK(!cells::has_leaf_id(system, 30));
  }
}

// ============================================================================
// Cross-Cluster Navigation Tests
// ============================================================================

TEST_SUITE("cells - navigation") {
  TEST_CASE("moveSelection moves within single cluster horizontally") {
    // Create a cluster with 2 cells side by side
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = cells::create_system({info});

    // After split, first child is selected
    auto selected = cells::get_selected_cell(system);
    REQUIRE(selected.has_value());

    // Move right should go to sibling
    bool result = system.move_selection(cells::Direction::Right);
    CHECK(result);

    auto newSelected = cells::get_selected_cell(system);
    REQUIRE(newSelected.has_value());
    CHECK(newSelected->first == 0);                 // Same cluster
    CHECK(newSelected->second != selected->second); // Different cell
  }

  TEST_CASE("moveSelection returns false when no selection") {
    auto system = cells::create_system({});

    bool result = system.move_selection(cells::Direction::Right);
    CHECK(!result);
  }

  TEST_CASE("moveSelection returns false when no cell in direction") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    // Only one cell, can't move anywhere
    bool result = system.move_selection(cells::Direction::Left);
    CHECK(!result);
  }

  TEST_CASE("moveSelection moves across clusters") {
    // Create two clusters side by side
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {1}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {2}};

    auto system = cells::create_system({info1, info2});

    // Selection should be in first cluster
    CHECK(system.selection.has_value());
    CHECK(system.selection->cluster_index == 0);

    // Move right should go to second cluster
    bool result = system.move_selection(cells::Direction::Right);
    CHECK(result);
    CHECK(system.selection->cluster_index == 1);

    // Move left should go back to first cluster
    result = system.move_selection(cells::Direction::Left);
    CHECK(result);
    CHECK(system.selection->cluster_index == 0);
  }

  TEST_CASE("toggleSelectedSplitDir works") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Get initial split dir of parent
    cells::SplitDir initialDir = pc.cluster.cells[0].split_dir;

    bool result = system.toggle_selected_split_dir();
    CHECK(result);

    // Check direction changed
    CHECK(pc.cluster.cells[0].split_dir != initialDir);
  }

  TEST_CASE("cycle_split_mode cycles through all modes") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {}};
    auto system = cells::create_system({info});

    // Default is Zigzag
    CHECK(system.split_mode == cells::SplitMode::Zigzag);

    // Cycle to Vertical
    CHECK(system.cycle_split_mode());
    CHECK(system.split_mode == cells::SplitMode::Vertical);

    // Cycle to Horizontal
    CHECK(system.cycle_split_mode());
    CHECK(system.split_mode == cells::SplitMode::Horizontal);

    // Cycle back to Zigzag
    CHECK(system.cycle_split_mode());
    CHECK(system.split_mode == cells::SplitMode::Zigzag);
  }

  TEST_CASE("Vertical mode creates vertical splits") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    // Set to Vertical
    system.split_mode = cells::SplitMode::Vertical;

    // Add second window
    cells::ClusterCellIds updates{0, {1, 2}};
    system.update({updates}, std::nullopt, {0.0f, 0.0f});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Root should be vertical split
    REQUIRE(pc.cluster.cells.size() >= 1);
    CHECK(pc.cluster.cells[0].split_dir == cells::SplitDir::Vertical);

    // Add third window - should still be vertical
    updates = cells::ClusterCellIds{0, {1, 2, 3}};
    system.update({updates}, std::nullopt, {0.0f, 0.0f});

    // Check that we have at least 3 cells and they all split vertically
    REQUIRE(pc.cluster.cells.size() >= 3);
    for (const auto& cell : pc.cluster.cells) {
      if (cell.first_child.has_value() && cell.second_child.has_value()) {
        CHECK(cell.split_dir == cells::SplitDir::Vertical);
      }
    }
  }

  TEST_CASE("Horizontal mode creates horizontal splits") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1}};
    auto system = cells::create_system({info});

    // Set to Horizontal
    system.split_mode = cells::SplitMode::Horizontal;

    // Add second window
    cells::ClusterCellIds updates{0, {1, 2}};
    system.update({updates}, std::nullopt, {0.0f, 0.0f});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Root should be horizontal split
    REQUIRE(pc.cluster.cells.size() >= 1);
    CHECK(pc.cluster.cells[0].split_dir == cells::SplitDir::Horizontal);
  }
}

// ============================================================================
// System Update Tests
// ============================================================================

TEST_SUITE("cells - updateSystem") {
  TEST_CASE("findCellByLeafId finds existing leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    auto idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);

    CHECK(idx10.has_value());
    CHECK(idx20.has_value());
    CHECK(*idx10 != *idx20);
  }

  TEST_CASE("findCellByLeafId returns nullopt for non-existent leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    auto result = cells::find_cell_by_leaf_id(pc.cluster, 999);
    CHECK(!result.has_value());
  }

  TEST_CASE("updateSystem adds leaves to empty cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {}};
    auto system = cells::create_system({info});

    CHECK(cells::count_total_leaves(system) == 0);

    std::vector<cells::ClusterCellIds> updates = {{0, {100, 200}}};

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.empty());
    CHECK(result.added_leaf_ids.size() == 2);
    CHECK(result.deleted_leaf_ids.empty());
    CHECK(cells::count_total_leaves(system) == 2);
  }

  TEST_CASE("updateSystem adds leaves to existing cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    CHECK(cells::count_total_leaves(system) == 1);

    std::vector<cells::ClusterCellIds> updates = {
        {0, {10, 20, 30}} // Keep 10, add 20 and 30
    };

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.empty());
    CHECK(result.added_leaf_ids.size() == 2);
    CHECK(result.deleted_leaf_ids.empty());
    CHECK(cells::count_total_leaves(system) == 3);
  }

  TEST_CASE("updateSystem deletes leaves") {
    cells::ClusterInitInfo info{0.0f, 0.0f,   800.0f, 600.0f,      0.0f,
                                0.0f, 800.0f, 600.0f, {10, 20, 30}};
    auto system = cells::create_system({info});

    CHECK(cells::count_total_leaves(system) == 3);

    std::vector<cells::ClusterCellIds> updates = {
        {0, {10}} // Keep only 10, delete 20 and 30
    };

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.empty());
    CHECK(result.deleted_leaf_ids.size() == 2);
    CHECK(result.added_leaf_ids.empty());
    CHECK(cells::count_total_leaves(system) == 1);
  }

  TEST_CASE("updateSystem handles mixed add and delete") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    std::vector<cells::ClusterCellIds> updates = {
        {0, {10, 30}} // Keep 10, delete 20, add 30
    };

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.empty());
    CHECK(result.deleted_leaf_ids.size() == 1);
    CHECK(result.added_leaf_ids.size() == 1);
    CHECK(result.deleted_leaf_ids[0] == 20);
    CHECK(result.added_leaf_ids[0] == 30);
    CHECK(cells::count_total_leaves(system) == 2);
  }

  TEST_CASE("updateSystem updates selection") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    std::vector<cells::ClusterCellIds> updates = {
        {0, {10, 20}} // No changes, just update selection
    };

    auto result = system.update(updates, {{0, 20}}, {0.0f, 0.0f});

    CHECK(result.errors.empty());
    CHECK(result.selection_updated);
    CHECK(system.selection.has_value());
    CHECK(system.selection->cluster_index == 0);

    // Verify selection points to leaf with ID 20
    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];
    auto& cell = pc.cluster.cells[static_cast<size_t>(system.selection->cell_index)];
    CHECK(cell.leaf_id.has_value());
    CHECK(*cell.leaf_id == 20);
  }

  TEST_CASE("updateSystem reports error for unknown cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    std::vector<cells::ClusterCellIds> updates = {
        {999, {10, 20}} // Cluster 999 doesn't exist
    };

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0].type == cells::UpdateError::Type::ClusterNotFound);
    CHECK(result.errors[0].cluster_index == 999);
  }

  TEST_CASE("updateSystem reports error for invalid selection cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    std::vector<cells::ClusterCellIds> updates = {{0, {10}}};

    auto result = system.update(updates, {{999, 10}}, {0.0f, 0.0f});

    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0].type == cells::UpdateError::Type::SelectionInvalid);
    CHECK(!result.selection_updated);
  }

  TEST_CASE("updateSystem reports error for invalid selection leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    std::vector<cells::ClusterCellIds> updates = {{0, {10}}};

    auto result = system.update(updates, {{0, 999}}, {0.0f, 0.0f});

    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0].type == cells::UpdateError::Type::SelectionInvalid);
    CHECK(result.errors[0].leaf_id == 999);
    CHECK(!result.selection_updated);
  }

  TEST_CASE("updateSystem handles multiple clusters") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {10}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = cells::create_system({info1, info2});

    std::vector<cells::ClusterCellIds> updates = {
        {0, {10, 11}}, // Add 11 to cluster 0
        {1, {20, 21}}  // Add 21 to cluster 1
    };

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.empty());
    CHECK(result.added_leaf_ids.size() == 2);
    CHECK(cells::count_total_leaves(system) == 4);
  }

  TEST_CASE("updateSystem leaves unchanged cluster alone") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {10, 11}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = cells::create_system({info1, info2});

    // Only update cluster 2, leave cluster 1 alone
    std::vector<cells::ClusterCellIds> updates = {{1, {20, 21}}};

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.empty());

    // Cluster 1 should still have its original leaves
    REQUIRE(system.clusters.size() >= 2);
    auto& pc1 = system.clusters[0];
    auto leafIds1 = cells::get_cluster_leaf_ids(pc1.cluster);
    CHECK(leafIds1.size() == 2);

    // Cluster 1 should have the new leaf
    auto& pc2 = system.clusters[1];
    auto leafIds2 = cells::get_cluster_leaf_ids(pc2.cluster);
    CHECK(leafIds2.size() == 2);
  }

  TEST_CASE("updateSystem can clear cluster to empty") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    std::vector<cells::ClusterCellIds> updates = {
        {0, {}} // Empty - delete all leaves
    };

    auto result = system.update(updates, std::nullopt, {0.0f, 0.0f});

    CHECK(result.errors.empty());
    CHECK(result.deleted_leaf_ids.size() == 1);
    CHECK(cells::count_total_leaves(system) == 0);
  }
}

// ============================================================================
// Swap and Move Cell Tests
// ============================================================================

TEST_SUITE("cells - swap and move") {
  TEST_CASE("swapCells swaps two cells in same cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Get initial positions
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    auto idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);
    REQUIRE(idx10.has_value());
    REQUIRE(idx20.has_value());

    auto rect10Before = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20Before = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    // Swap
    auto result = system.swap_cells(0, 10, 0, 20);

    CHECK(result.has_value());

    // Re-find cells (indices may have changed)
    idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);
    REQUIRE(idx10.has_value());
    REQUIRE(idx20.has_value());

    // Check rects were swapped
    auto rect10After = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20After = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    CHECK(rect10After.x == doctest::Approx(rect20Before.x));
    CHECK(rect10After.width == doctest::Approx(rect20Before.width));
    CHECK(rect20After.x == doctest::Approx(rect10Before.x));
    CHECK(rect20After.width == doctest::Approx(rect10Before.width));

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("swapCells is no-op for same cell") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.swap_cells(0, 10, 0, 10);

    CHECK(result.has_value());
    CHECK(cells::validate_system(system));
  }

  TEST_CASE("swapCells swaps cells across clusters") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {10}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = cells::create_system({info1, info2});

    REQUIRE(system.clusters.size() >= 2);
    auto& pc1 = system.clusters[0];
    auto& pc2 = system.clusters[1];

    // Swap cross-cluster
    auto result = system.swap_cells(0, 10, 1, 20);

    CHECK(result.has_value());

    // After cross-cluster swap, leafIds are exchanged
    // Cell in cluster 1 now has leafId 20, cell in cluster 2 has leafId 10
    auto idx1 = cells::find_cell_by_leaf_id(pc1.cluster, 20);
    auto idx2 = cells::find_cell_by_leaf_id(pc2.cluster, 10);

    CHECK(idx1.has_value());
    CHECK(idx2.has_value());

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("swapCells returns error for non-existent cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.swap_cells(0, 10, 999, 20);

    CHECK(!result.has_value());
    CHECK(!result.error().empty());
  }

  TEST_CASE("swapCells returns error for non-existent leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.swap_cells(0, 10, 0, 999);

    CHECK(!result.has_value());
    CHECK(!result.error().empty());
  }

  TEST_CASE("swapCells updates selection correctly in same cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    // Select the cell with leafId 10
    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    REQUIRE(idx10.has_value());
    system.selection = cells::CellIndicatorByIndex{0, *idx10};

    // Swap
    auto result = system.swap_cells(0, 10, 0, 20);
    CHECK(result.has_value());

    // Selection should still point to the cell with leafId 10 (now at different index)
    REQUIRE(system.selection.has_value());
    auto& selectedCell = pc.cluster.cells[static_cast<size_t>(system.selection->cell_index)];
    CHECK(selectedCell.leaf_id.has_value());
    CHECK(*selectedCell.leaf_id == 10);
  }

  TEST_CASE("moveCell moves cell within same cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    CHECK(cells::count_total_leaves(system) == 2);

    // Move 10 to 20
    auto result = system.move_cell(0, 10, 0, 20);

    CHECK(result.has_value());
    CHECK(result->new_cluster_index == 0);

    // Should now have 2 leaves (20 was split, creating a new leaf for 10)
    CHECK(cells::count_total_leaves(system) == 2);

    // Verify both leaves still exist
    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    auto idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);
    CHECK(idx10.has_value());
    CHECK(idx20.has_value());

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("moveCell is no-op for same cell") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.move_cell(0, 10, 0, 10);

    CHECK(result.has_value());
    CHECK(cells::count_total_leaves(system) == 1);
  }

  TEST_CASE("moveCell moves cell across clusters") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {10, 11}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = cells::create_system({info1, info2});

    CHECK(cells::count_total_leaves(system) == 3);

    // Move 10 from cluster 1 to cluster 2 (split from 20)
    auto result = system.move_cell(0, 10, 1, 20);

    CHECK(result.has_value());
    CHECK(result->new_cluster_index == 1);

    // Cluster 1 should now have 1 leaf (11)
    REQUIRE(system.clusters.size() >= 2);
    auto& pc1 = system.clusters[0];
    auto leafIds1 = cells::get_cluster_leaf_ids(pc1.cluster);
    CHECK(leafIds1.size() == 1);
    CHECK(leafIds1[0] == 11);

    // Cluster 1 should now have 2 leaves (20 and 10)
    auto& pc2 = system.clusters[1];
    auto leafIds2 = cells::get_cluster_leaf_ids(pc2.cluster);
    CHECK(leafIds2.size() == 2);
    std::sort(leafIds2.begin(), leafIds2.end());
    CHECK(leafIds2[0] == 10);
    CHECK(leafIds2[1] == 20);

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("moveCell preserves source leafId") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    auto result = system.move_cell(0, 10, 0, 20);

    CHECK(result.has_value());

    // The moved cell should still have leafId 10
    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    CHECK(idx10.has_value());
    CHECK(*idx10 == result->new_cell_index);
  }

  TEST_CASE("moveCell returns error for non-existent source cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.move_cell(999, 10, 1, 10);

    CHECK(!result.has_value());
    CHECK(!result.error().empty());
  }

  TEST_CASE("moveCell returns error for non-existent target cluster") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.move_cell(0, 10, 999, 20);

    CHECK(!result.has_value());
    CHECK(!result.error().empty());
  }

  TEST_CASE("moveCell returns error for non-existent source leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.move_cell(0, 999, 0, 10);

    CHECK(!result.has_value());
    CHECK(!result.error().empty());
  }

  TEST_CASE("moveCell returns error for non-existent target leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    auto result = system.move_cell(0, 10, 0, 999);

    CHECK(!result.has_value());
    CHECK(!result.error().empty());
  }

  TEST_CASE("moveCell updates selection when source was selected") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    // Select the cell with leafId 10
    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    REQUIRE(idx10.has_value());
    system.selection = cells::CellIndicatorByIndex{0, *idx10};

    // Move 10 to 20
    auto result = system.move_cell(0, 10, 0, 20);

    CHECK(result.has_value());

    // Selection should be updated to the new cell
    REQUIRE(system.selection.has_value());
    CHECK(system.selection->cell_index == result->new_cell_index);

    // The selected cell should have leafId 10
    auto& selectedCell = pc.cluster.cells[static_cast<size_t>(system.selection->cell_index)];
    CHECK(selectedCell.leaf_id.has_value());
    CHECK(*selectedCell.leaf_id == 10);
  }

  TEST_CASE("moveCell handles source cluster becoming empty") {
    cells::ClusterInitInfo info1{0.0f, 0.0f, 400.0f, 600.0f, 0.0f, 0.0f, 400.0f, 600.0f, {10}};
    cells::ClusterInitInfo info2{400.0f, 0.0f, 400.0f, 600.0f, 400.0f, 0.0f, 400.0f, 600.0f, {20}};
    auto system = cells::create_system({info1, info2});

    // Move the only cell from cluster 1 to cluster 2
    auto result = system.move_cell(0, 10, 1, 20);

    CHECK(result.has_value());

    // Cluster 0 should now be empty
    REQUIRE(system.clusters.size() >= 1);
    auto& pc1 = system.clusters[0];
    CHECK(pc1.cluster.cells.empty());

    // Cluster 2 should have 2 leaves
    CHECK(cells::count_total_leaves(system) == 2);
  }
}

// ============================================================================
// Split Ratio Tests
// ============================================================================

TEST_SUITE("cells - split ratio") {
  // -------------------------------------------------------------------------
  // setSplitRatio tests
  // -------------------------------------------------------------------------

  TEST_CASE("setSplitRatio sets ratio on vertical split") {
    // Create cluster with 2 leaves - initial split is vertical with 50/50
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];
    REQUIRE(pc.cluster.cells.size() >= 3); // Root (parent) + 2 children

    // Root cell (index 0) is the parent after split
    auto& parent = pc.cluster.cells[0];
    CHECK(parent.split_dir == cells::SplitDir::Vertical);
    CHECK(parent.split_ratio == doctest::Approx(0.5f));

    // Set ratio to 0.25
    bool result = cells::set_split_ratio(pc.cluster, 0, 0.25f, TEST_GAP_H, TEST_GAP_V);
    CHECK(result);
    CHECK(parent.split_ratio == doctest::Approx(0.25f));

    // Verify child widths
    // Parent rect: x=10, width=780 (800 - 2*10 margins)
    // Available = 780 - 10 (gap) = 770
    // First child: 770 * 0.25 = 192.5
    auto& firstChild = pc.cluster.cells[static_cast<size_t>(*parent.first_child)];
    auto& secondChild = pc.cluster.cells[static_cast<size_t>(*parent.second_child)];

    CHECK(firstChild.rect.width == doctest::Approx(192.5f));
    CHECK(secondChild.rect.width == doctest::Approx(577.5f)); // 770 * 0.75

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("setSplitRatio sets ratio on horizontal split") {
    // Create cluster and toggle to horizontal split
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Toggle split direction to horizontal
    CHECK(system.toggle_selected_split_dir());

    auto& parent = pc.cluster.cells[0];
    CHECK(parent.split_dir == cells::SplitDir::Horizontal);

    // Set ratio to 0.75
    bool result = cells::set_split_ratio(pc.cluster, 0, 0.75f, TEST_GAP_H, TEST_GAP_V);
    CHECK(result);
    CHECK(parent.split_ratio == doctest::Approx(0.75f));

    // Verify child heights
    // Parent rect: y=10, height=580 (600 - 2*10 margins)
    // Available = 580 - 10 (gap) = 570
    // First child: 570 * 0.75 = 427.5
    auto& firstChild = pc.cluster.cells[static_cast<size_t>(*parent.first_child)];
    auto& secondChild = pc.cluster.cells[static_cast<size_t>(*parent.second_child)];

    CHECK(firstChild.rect.height == doctest::Approx(427.5f));
    CHECK(secondChild.rect.height == doctest::Approx(142.5f)); // 570 * 0.25

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("setSplitRatio returns false for leaf cell") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Single leaf cell
    bool result = cells::set_split_ratio(pc.cluster, 0, 0.3f, TEST_GAP_H, TEST_GAP_V);
    CHECK(!result);
  }

  TEST_CASE("setSplitRatio returns false for invalid index") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Negative index
    bool result1 = cells::set_split_ratio(pc.cluster, -1, 0.3f, TEST_GAP_H, TEST_GAP_V);
    CHECK(!result1);

    // Out of bounds
    bool result2 = cells::set_split_ratio(pc.cluster, 999, 0.3f, TEST_GAP_H, TEST_GAP_V);
    CHECK(!result2);
  }

  TEST_CASE("setSplitRatio clamps 0.0 ratio to minimum") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    bool result = cells::set_split_ratio(pc.cluster, 0, 0.0f, TEST_GAP_H, TEST_GAP_V);
    CHECK(result);

    auto& parent = pc.cluster.cells[0];
    auto& firstChild = pc.cluster.cells[static_cast<size_t>(*parent.first_child)];

    // Clamped to minimum 0.1
    CHECK(parent.split_ratio == doctest::Approx(0.1f));
    CHECK(firstChild.rect.width == doctest::Approx(77.0f)); // 770 * 0.1
  }

  TEST_CASE("setSplitRatio clamps 1.0 ratio to maximum") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    bool result = cells::set_split_ratio(pc.cluster, 0, 1.0f, TEST_GAP_H, TEST_GAP_V);
    CHECK(result);

    auto& parent = pc.cluster.cells[0];
    auto& firstChild = pc.cluster.cells[static_cast<size_t>(*parent.first_child)];
    auto& secondChild = pc.cluster.cells[static_cast<size_t>(*parent.second_child)];

    // Clamped to maximum 0.9
    CHECK(parent.split_ratio == doctest::Approx(0.9f));
    CHECK(firstChild.rect.width == doctest::Approx(693.0f)); // 770 * 0.9
    CHECK(secondChild.rect.width == doctest::Approx(77.0f)); // 770 * 0.1
  }

  TEST_CASE("setSplitRatio recursively updates nested children") {
    // Create cluster with 3 leaves (nested splits)
    cells::ClusterInitInfo info{0.0f, 0.0f,   800.0f, 600.0f,      0.0f,
                                0.0f, 800.0f, 600.0f, {10, 20, 30}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];
    CHECK(cells::count_total_leaves(system) == 3);

    // Change root ratio - should update all descendants
    bool result = cells::set_split_ratio(pc.cluster, 0, 0.25f, TEST_GAP_H, TEST_GAP_V);
    CHECK(result);

    // Find leaf 30 and verify its rect was updated
    auto idx30 = cells::find_cell_by_leaf_id(pc.cluster, 30);
    REQUIRE(idx30.has_value());

    // The nested structure should have updated rects
    CHECK(cells::validate_system(system));
  }

  // -------------------------------------------------------------------------
  // setSelectedSplitRatio tests
  // -------------------------------------------------------------------------

  TEST_CASE("setSelectedSplitRatio works when leaf is selected") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    // Selection should be on a leaf
    REQUIRE(system.selection.has_value());

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Set ratio via selected leaf
    bool result = system.set_selected_split_ratio(0.3f);
    CHECK(result);

    // Parent's ratio should have changed
    auto& parent = pc.cluster.cells[0];
    CHECK(parent.split_ratio == doctest::Approx(0.3f));

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("setSelectedSplitRatio returns false with no selection") {
    auto system = cells::create_system({});

    bool result = system.set_selected_split_ratio(0.3f);
    CHECK(!result);
  }

  TEST_CASE("setSelectedSplitRatio returns false for root leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    // Single leaf has no parent
    bool result = system.set_selected_split_ratio(0.3f);
    CHECK(!result);
  }

  TEST_CASE("setSelectedSplitRatio updates rect sizes correctly") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    bool result = system.set_selected_split_ratio(0.25f);
    CHECK(result);

    auto& parent = pc.cluster.cells[0];
    auto& firstChild = pc.cluster.cells[static_cast<size_t>(*parent.first_child)];

    // Available width = 780 - 10 = 770
    // First child = 770 * 0.25 = 192.5
    CHECK(firstChild.rect.width == doctest::Approx(192.5f));

    CHECK(cells::validate_system(system));
  }

  // -------------------------------------------------------------------------
  // adjustSelectedSplitRatio tests
  // -------------------------------------------------------------------------

  TEST_CASE("adjustSelectedSplitRatio increases ratio") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Initial ratio is 0.5
    auto& parent = pc.cluster.cells[0];
    CHECK(parent.split_ratio == doctest::Approx(0.5f));

    // Increase by 0.1
    bool result = system.adjust_selected_split_ratio(0.1f);
    CHECK(result);
    CHECK(parent.split_ratio == doctest::Approx(0.6f));

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("adjustSelectedSplitRatio decreases ratio") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    auto& parent = pc.cluster.cells[0];

    // Decrease by 0.2
    bool result = system.adjust_selected_split_ratio(-0.2f);
    CHECK(result);
    CHECK(parent.split_ratio == doctest::Approx(0.3f));

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("adjustSelectedSplitRatio returns false with no selection") {
    auto system = cells::create_system({});

    bool result = system.adjust_selected_split_ratio(0.1f);
    CHECK(!result);
  }

  TEST_CASE("adjustSelectedSplitRatio returns false for root leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    bool result = system.adjust_selected_split_ratio(0.1f);
    CHECK(!result);
  }

  TEST_CASE("adjustSelectedSplitRatio clamps ratio at maximum") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Set to 0.9 first (the maximum)
    REQUIRE(system.set_selected_split_ratio(0.9f));

    auto& parent = pc.cluster.cells[0];

    // Increase by 0.2 - should stay clamped at 0.9
    bool result = system.adjust_selected_split_ratio(0.2f);
    CHECK(result);
    CHECK(parent.split_ratio == doctest::Approx(0.9f));
  }

  TEST_CASE("adjustSelectedSplitRatio clamps ratio at minimum") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Set to 0.1 first (the minimum)
    REQUIRE(system.set_selected_split_ratio(0.1f));

    auto& parent = pc.cluster.cells[0];

    // Decrease by 0.2 - should stay clamped at 0.1
    bool result = system.adjust_selected_split_ratio(-0.2f);
    CHECK(result);
    CHECK(parent.split_ratio == doctest::Approx(0.1f));
  }

  TEST_CASE("adjustSelectedSplitRatio updates rects after adjustment") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    auto& parent = pc.cluster.cells[0];
    auto& firstChild = pc.cluster.cells[static_cast<size_t>(*parent.first_child)];

    // Initial width at 0.5 ratio: 770 * 0.5 = 385
    CHECK(firstChild.rect.width == doctest::Approx(385.0f));

    // Adjust by -0.25 (new ratio = 0.25)
    bool result = system.adjust_selected_split_ratio(-0.25f);
    CHECK(result);

    // New width: 770 * 0.25 = 192.5
    CHECK(firstChild.rect.width == doctest::Approx(192.5f));

    CHECK(cells::validate_system(system));
  }
}

// ============================================================================
// Exchange Selected With Sibling Tests
// ============================================================================

TEST_SUITE("cells - exchange selected with sibling") {
  TEST_CASE("exchange_selected_with_sibling swaps two leaf siblings") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Get initial positions of leaves
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    auto idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);
    REQUIRE(idx10.has_value());
    REQUIRE(idx20.has_value());

    auto rect10Before = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20Before = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    // Exchange siblings
    bool result = system.exchange_selected_with_sibling();
    CHECK(result);

    // Re-find cells
    idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);
    REQUIRE(idx10.has_value());
    REQUIRE(idx20.has_value());

    // Check positions were swapped
    auto rect10After = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20After = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    CHECK(rect10After.x == doctest::Approx(rect20Before.x));
    CHECK(rect10After.width == doctest::Approx(rect20Before.width));
    CHECK(rect20After.x == doctest::Approx(rect10Before.x));
    CHECK(rect20After.width == doctest::Approx(rect10Before.width));

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("exchange_selected_with_sibling returns false with no selection") {
    auto system = cells::create_system({});

    bool result = system.exchange_selected_with_sibling();
    CHECK(!result);
  }

  TEST_CASE("exchange_selected_with_sibling returns false for root leaf") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}};
    auto system = cells::create_system({info});

    // Single leaf has no sibling
    bool result = system.exchange_selected_with_sibling();
    CHECK(!result);
  }

  TEST_CASE("exchange_selected_with_sibling updates cell positions correctly") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Initial: vertical split, leaf 10 on left, leaf 20 on right
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    auto idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);
    REQUIRE(idx10.has_value());
    REQUIRE(idx20.has_value());

    auto rect10Before = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20Before = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    // Leaf 10 should be on the left (smaller x)
    CHECK(rect10Before.x < rect20Before.x);

    // Exchange
    bool result = system.exchange_selected_with_sibling();
    CHECK(result);

    // After exchange: leaf 10 should now be on the right
    idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);

    auto rect10After = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20After = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    CHECK(rect10After.x > rect20After.x);

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("exchange_selected_with_sibling works with horizontal split") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    // Toggle to horizontal split first
    REQUIRE(system.toggle_selected_split_dir());

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    auto idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);
    REQUIRE(idx10.has_value());
    REQUIRE(idx20.has_value());

    auto rect10Before = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20Before = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    // After horizontal split toggle, leaf 10 should be on top (smaller y)
    CHECK(rect10Before.y < rect20Before.y);

    // Exchange
    bool result = system.exchange_selected_with_sibling();
    CHECK(result);

    // After exchange: leaf 10 should now be on the bottom
    idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    idx20 = cells::find_cell_by_leaf_id(pc.cluster, 20);

    auto rect10After = pc.cluster.cells[static_cast<size_t>(*idx10)].rect;
    auto rect20After = pc.cluster.cells[static_cast<size_t>(*idx20)].rect;

    CHECK(rect10After.y > rect20After.y);

    CHECK(cells::validate_system(system));
  }

  TEST_CASE("exchange_selected_with_sibling preserves selection") {
    cells::ClusterInitInfo info{0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10, 20}};
    auto system = cells::create_system({info});

    REQUIRE(system.clusters.size() >= 1);
    auto& pc = system.clusters[0];

    // Get initial selection
    auto idx10 = cells::find_cell_by_leaf_id(pc.cluster, 10);
    REQUIRE(idx10.has_value());

    // Set selection to leaf 10
    system.selection = cells::CellIndicatorByIndex{0, *idx10};

    // Exchange
    bool result = system.exchange_selected_with_sibling();
    CHECK(result);

    // Selection should still point to same cell index (which now has different position)
    REQUIRE(system.selection.has_value());
    CHECK(system.selection->cell_index == *idx10);
  }
}
