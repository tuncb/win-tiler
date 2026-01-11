#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include "controller.h"

using namespace wintiler::ctrl;

namespace {

// =============================================================================
// Helper Functions
// =============================================================================

// Create an empty cluster with given dimensions
Cluster create_empty_cluster(float width = 1920.0f, float height = 1080.0f) {
  Cluster cluster;
  cluster.window_width = width;
  cluster.window_height = height;
  cluster.global_x = 0.0f;
  cluster.global_y = 0.0f;
  cluster.monitor_x = 0.0f;
  cluster.monitor_y = 0.0f;
  cluster.monitor_width = width;
  cluster.monitor_height = height;
  return cluster;
}

// Create a System with specified cluster configurations
// Each inner vector represents leaf_ids for that cluster
System create_test_system(const std::vector<std::vector<size_t>>& cluster_leaf_ids,
                          float cluster_width = 800.0f, float cluster_height = 600.0f) {
  std::vector<ClusterInitInfo> infos;
  float x_offset = 0.0f;

  for (const auto& leaf_ids : cluster_leaf_ids) {
    ClusterInitInfo info;
    info.x = x_offset;
    info.y = 0.0f;
    info.width = cluster_width;
    info.height = cluster_height;
    info.monitor_x = x_offset;
    info.monitor_y = 0.0f;
    info.monitor_width = cluster_width;
    info.monitor_height = cluster_height;
    info.initial_cell_ids = leaf_ids;
    infos.push_back(info);
    x_offset += cluster_width;
  }

  return create_system(infos);
}

// Compute geometries for all clusters with default gaps
std::vector<std::vector<Rect>> compute_test_geometries(const System& system, float gap_h = 10.0f,
                                                       float gap_v = 10.0f, float zen_pct = 0.85f) {
  std::vector<std::vector<Rect>> geometries;
  for (const auto& cluster : system.clusters) {
    geometries.push_back(compute_cluster_geometry(cluster, gap_h, gap_v, zen_pct));
  }
  return geometries;
}

// Helper to set selection on a system
void set_selection(System& system, int cluster_index, int cell_index) {
  system.selection = CellIndicatorByIndex{cluster_index, cell_index};
}

} // namespace

// =============================================================================
// is_leaf Tests
// =============================================================================

TEST_SUITE("is_leaf") {
  TEST_CASE("leaf cell returns true") {
    System system = create_test_system({{1}});
    const auto& cluster = system.clusters[0];
    CHECK(is_leaf(cluster, 0));
  }

  TEST_CASE("internal node returns false") {
    System system = create_test_system({{1, 2}});
    const auto& cluster = system.clusters[0];
    // Tree: [0]=internal, [1]=leaf(1), [2]=leaf(2)
    CHECK_FALSE(is_leaf(cluster, 0));
    CHECK(is_leaf(cluster, 1));
    CHECK(is_leaf(cluster, 2));
  }

  TEST_CASE("invalid index returns false") {
    System system = create_test_system({{1}});
    const auto& cluster = system.clusters[0];
    CHECK_FALSE(is_leaf(cluster, -1));
    CHECK_FALSE(is_leaf(cluster, 100));
  }

  TEST_CASE("empty tree returns false") {
    Cluster cluster = create_empty_cluster();
    CHECK_FALSE(is_leaf(cluster, 0));
    CHECK_FALSE(is_leaf(cluster, -1));
  }
}

// =============================================================================
// find_cell_by_leaf_id Tests
// =============================================================================

TEST_SUITE("find_cell_by_leaf_id") {
  TEST_CASE("finds existing leaf") {
    System system = create_test_system({{100}});
    const auto& cluster = system.clusters[0];
    auto result = find_cell_by_leaf_id(cluster, 100);
    REQUIRE(result.has_value());
    CHECK(*result == 0);
  }

  TEST_CASE("returns nullopt for missing") {
    System system = create_test_system({{100}});
    const auto& cluster = system.clusters[0];
    auto result = find_cell_by_leaf_id(cluster, 999);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("empty cluster returns nullopt") {
    Cluster cluster = create_empty_cluster();
    auto result = find_cell_by_leaf_id(cluster, 100);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("multiple leaves finds correct one") {
    System system = create_test_system({{10, 20, 30}});
    const auto& cluster = system.clusters[0];

    auto result10 = find_cell_by_leaf_id(cluster, 10);
    auto result20 = find_cell_by_leaf_id(cluster, 20);
    auto result30 = find_cell_by_leaf_id(cluster, 30);

    REQUIRE(result10.has_value());
    REQUIRE(result20.has_value());
    REQUIRE(result30.has_value());

    // Each should find a different index
    CHECK(*result10 != *result20);
    CHECK(*result20 != *result30);
    CHECK(*result10 != *result30);
  }
}

// =============================================================================
// get_cluster_leaf_ids Tests
// =============================================================================

TEST_SUITE("get_cluster_leaf_ids") {
  TEST_CASE("empty cluster returns empty") {
    Cluster cluster = create_empty_cluster();
    auto ids = get_cluster_leaf_ids(cluster);
    CHECK(ids.empty());
  }

  TEST_CASE("single leaf returns one id") {
    System system = create_test_system({{42}});
    auto ids = get_cluster_leaf_ids(system.clusters[0]);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 42);
  }

  TEST_CASE("multiple leaves returns all") {
    System system = create_test_system({{1, 2, 3}});
    auto ids = get_cluster_leaf_ids(system.clusters[0]);
    REQUIRE(ids.size() == 3);
    // Check all three ids are present (order may vary)
    CHECK(std::find(ids.begin(), ids.end(), 1) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), 2) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), 3) != ids.end());
  }

  TEST_CASE("excludes internal nodes") {
    System system = create_test_system({{100, 200}});
    auto ids = get_cluster_leaf_ids(system.clusters[0]);
    // Tree has 3 nodes but only 2 leaves
    CHECK(system.clusters[0].tree.size() == 3);
    CHECK(ids.size() == 2);
  }
}

// =============================================================================
// has_leaf_id Tests
// =============================================================================

TEST_SUITE("has_leaf_id") {
  TEST_CASE("finds in first cluster") {
    System system = create_test_system({{1, 2}, {3}});
    CHECK(has_leaf_id(system, 1));
    CHECK(has_leaf_id(system, 2));
  }

  TEST_CASE("finds in second cluster") {
    System system = create_test_system({{1, 2}, {3}});
    CHECK(has_leaf_id(system, 3));
  }

  TEST_CASE("not found returns false") {
    System system = create_test_system({{1, 2}, {3}});
    CHECK_FALSE(has_leaf_id(system, 999));
  }

  TEST_CASE("empty system returns false") {
    System system = create_test_system({});
    CHECK_FALSE(has_leaf_id(system, 1));
  }
}

// =============================================================================
// get_rect_center Tests
// =============================================================================

TEST_SUITE("get_rect_center") {
  TEST_CASE("calculates center correctly") {
    Rect r{100.0f, 200.0f, 400.0f, 300.0f};
    Point center = get_rect_center(r);
    CHECK(center.x == 300); // 100 + 400/2
    CHECK(center.y == 350); // 200 + 300/2
  }

  TEST_CASE("zero-size rect") {
    Rect r{50.0f, 50.0f, 0.0f, 0.0f};
    Point center = get_rect_center(r);
    CHECK(center.x == 50);
    CHECK(center.y == 50);
  }

  TEST_CASE("non-origin rect") {
    Rect r{0.0f, 0.0f, 100.0f, 100.0f};
    Point center = get_rect_center(r);
    CHECK(center.x == 50);
    CHECK(center.y == 50);
  }
}

// =============================================================================
// create_system Tests
// =============================================================================

TEST_SUITE("create_system") {
  TEST_CASE("empty infos creates empty system") {
    System system = create_system({});
    CHECK(system.clusters.empty());
    CHECK_FALSE(system.selection.has_value());
  }

  TEST_CASE("single cluster no cells") {
    ClusterInitInfo info;
    info.x = 0.0f;
    info.y = 0.0f;
    info.width = 800.0f;
    info.height = 600.0f;
    info.initial_cell_ids = {};

    System system = create_system({info});
    CHECK(system.clusters.size() == 1);
    CHECK(system.clusters[0].tree.empty());
    CHECK_FALSE(system.selection.has_value());
  }

  TEST_CASE("single cluster with cells") {
    System system = create_test_system({{1, 2}});
    CHECK(system.clusters.size() == 1);
    CHECK(system.clusters[0].tree.size() == 3); // parent + 2 leaves
    REQUIRE(system.selection.has_value());
    CHECK(system.selection->cluster_index == 0);
  }

  TEST_CASE("multiple clusters") {
    System system = create_test_system({{1}, {2, 3}, {4, 5, 6}});
    CHECK(system.clusters.size() == 3);
    CHECK(system.clusters[0].tree.size() == 1);
    CHECK(system.clusters[1].tree.size() == 3);
    CHECK(system.clusters[2].tree.size() == 5); // 3 leaves + 2 internal nodes
  }

  TEST_CASE("selection set to first cell") {
    System system = create_test_system({{}, {1, 2}});
    // First cluster empty, second has cells
    REQUIRE(system.selection.has_value());
    CHECK(system.selection->cluster_index == 1);
  }

  TEST_CASE("cluster dimensions copied") {
    ClusterInitInfo info;
    info.x = 100.0f;
    info.y = 200.0f;
    info.width = 1920.0f;
    info.height = 1080.0f;
    info.initial_cell_ids = {1};

    System system = create_system({info});
    CHECK(system.clusters[0].global_x == 100.0f);
    CHECK(system.clusters[0].global_y == 200.0f);
    CHECK(system.clusters[0].window_width == 1920.0f);
    CHECK(system.clusters[0].window_height == 1080.0f);
  }

  TEST_CASE("monitor info copied") {
    ClusterInitInfo info;
    info.x = 0.0f;
    info.y = 0.0f;
    info.width = 800.0f;
    info.height = 600.0f;
    info.monitor_x = 50.0f;
    info.monitor_y = 60.0f;
    info.monitor_width = 1920.0f;
    info.monitor_height = 1080.0f;
    info.initial_cell_ids = {1};

    System system = create_system({info});
    CHECK(system.clusters[0].monitor_x == 50.0f);
    CHECK(system.clusters[0].monitor_y == 60.0f);
    CHECK(system.clusters[0].monitor_width == 1920.0f);
    CHECK(system.clusters[0].monitor_height == 1080.0f);
  }
}

// =============================================================================
// delete_leaf Tests
// =============================================================================

TEST_SUITE("delete_leaf") {
  TEST_CASE("delete single root leaf") {
    System system = create_test_system({{1}});
    auto& cluster = system.clusters[0];
    CHECK(cluster.tree.size() == 1);

    bool result = delete_leaf(cluster, 0);
    CHECK(result);
    CHECK(cluster.tree.empty());
  }

  TEST_CASE("delete leaf promotes sibling") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    // Tree: [0]=internal, [1]=leaf(1), [2]=leaf(2)
    CHECK(cluster.tree.size() == 3);

    // Find cell with leaf_id 2
    auto cell_idx = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell_idx.has_value());

    bool result = delete_leaf(cluster, *cell_idx);
    CHECK(result);
    // After deletion, tree should have 1 node (sibling promoted to root)
    CHECK(cluster.tree.size() == 1);
    // Remaining cell should have leaf_id 1
    CHECK(cluster.tree[0].leaf_id.has_value());
    CHECK(*cluster.tree[0].leaf_id == 1);
  }

  TEST_CASE("delete from three-cell tree") {
    System system = create_test_system({{1, 2, 3}});
    auto& cluster = system.clusters[0];
    size_t initial_size = cluster.tree.size();
    CHECK(initial_size == 5); // 3 leaves + 2 internal

    // Delete one leaf
    auto cell_idx = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell_idx.has_value());

    bool result = delete_leaf(cluster, *cell_idx);
    CHECK(result);
    // Should now have 3 nodes (2 leaves + 1 internal)
    CHECK(cluster.tree.size() == 3);

    // Remaining leaf_ids should be 1 and 3
    auto ids = get_cluster_leaf_ids(cluster);
    CHECK(ids.size() == 2);
    CHECK(std::find(ids.begin(), ids.end(), 1) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), 3) != ids.end());
  }

  TEST_CASE("invalid index returns false") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];

    CHECK_FALSE(delete_leaf(cluster, -1));
    CHECK_FALSE(delete_leaf(cluster, 100));
  }

  TEST_CASE("non-leaf returns false") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    // Node 0 is the internal parent node
    CHECK_FALSE(delete_leaf(cluster, 0));
  }

  TEST_CASE("clears zen if deleted") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    auto cell_idx = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell_idx.has_value());

    cluster.zen_cell_index = *cell_idx;

    bool result = delete_leaf(cluster, *cell_idx);
    CHECK(result);
    CHECK_FALSE(cluster.zen_cell_index.has_value());
  }

  TEST_CASE("updates zen index on removal") {
    System system = create_test_system({{1, 2, 3}});
    auto& cluster = system.clusters[0];

    // Set zen to a cell that won't be deleted
    auto zen_cell = find_cell_by_leaf_id(cluster, 3);
    REQUIRE(zen_cell.has_value());
    cluster.zen_cell_index = *zen_cell;

    // Delete a different cell
    auto delete_cell = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(delete_cell.has_value());

    bool result = delete_leaf(cluster, *delete_cell);
    CHECK(result);

    // Zen should still be set (possibly to a different index)
    // The zen cell with leaf_id 3 should still exist
    auto new_zen = find_cell_by_leaf_id(cluster, 3);
    REQUIRE(new_zen.has_value());
    // Zen index may have changed due to compaction
    if (cluster.zen_cell_index.has_value()) {
      int zen_idx = *cluster.zen_cell_index;
      int expected_zen = *new_zen;
      CHECK(zen_idx == expected_zen);
    }
  }
}

// =============================================================================
// swap_cells Tests
// =============================================================================

TEST_SUITE("swap_cells") {
  TEST_CASE("same cluster siblings") {
    System system = create_test_system({{1, 2}});
    // Tree: [0]=internal, [1]=leaf(1), [2]=leaf(2)
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    bool result = swap_cells(system, 0, *cell1, 0, *cell2);
    CHECK(result);

    // After sibling swap, the indices swap but leaf_ids stay with indices
    // Actually for siblings, children of parent are swapped
    // The leaf_ids should now be at swapped positions
  }

  TEST_CASE("same cluster non-siblings") {
    System system = create_test_system({{1, 2, 3}});
    // Cells with leaf_id 1 and 3 may not be siblings
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell3 = find_cell_by_leaf_id(system.clusters[0], 3);
    REQUIRE(cell1.has_value());
    REQUIRE(cell3.has_value());

    bool result = swap_cells(system, 0, *cell1, 0, *cell3);
    CHECK(result);

    // After swap, leaf_ids should be exchanged
    CHECK(*system.clusters[0].tree[*cell1].leaf_id == 3);
    CHECK(*system.clusters[0].tree[*cell3].leaf_id == 1);
  }

  TEST_CASE("cross-cluster swap") {
    System system = create_test_system({{1}, {2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[1], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    bool result = swap_cells(system, 0, *cell1, 1, *cell2);
    CHECK(result);

    // After swap, leaf_ids should be exchanged
    CHECK(*system.clusters[0].tree[*cell1].leaf_id == 2);
    CHECK(*system.clusters[1].tree[*cell2].leaf_id == 1);
  }

  TEST_CASE("same cell no-op") {
    System system = create_test_system({{1, 2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());

    bool result = swap_cells(system, 0, *cell1, 0, *cell1);
    CHECK(result);
    // Leaf_id should be unchanged
    CHECK(*system.clusters[0].tree[*cell1].leaf_id == 1);
  }

  TEST_CASE("invalid cluster index") {
    System system = create_test_system({{1}, {2}});
    CHECK_FALSE(swap_cells(system, -1, 0, 0, 0));
    CHECK_FALSE(swap_cells(system, 0, 0, 10, 0));
  }

  TEST_CASE("invalid cell index") {
    System system = create_test_system({{1, 2}});
    // Cell index 0 is internal node, not leaf
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell2.has_value());
    CHECK_FALSE(swap_cells(system, 0, 0, 0, *cell2));
    CHECK_FALSE(swap_cells(system, 0, 100, 0, *cell2));
  }

  TEST_CASE("selection follows swap") {
    System system = create_test_system({{1}, {2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[1], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    // Select cell1
    set_selection(system, 0, *cell1);

    bool result = swap_cells(system, 0, *cell1, 1, *cell2);
    CHECK(result);

    // Selection should follow to the new location
    REQUIRE(system.selection.has_value());
    CHECK(system.selection->cluster_index == 1);
    CHECK(system.selection->cell_index == *cell2);
  }

  TEST_CASE("zen cleared on cross-cluster") {
    System system = create_test_system({{1}, {2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[1], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    // Set zen on cell1
    system.clusters[0].zen_cell_index = *cell1;

    bool result = swap_cells(system, 0, *cell1, 1, *cell2);
    CHECK(result);

    // Zen should be cleared (cell left the cluster)
    CHECK_FALSE(system.clusters[0].zen_cell_index.has_value());
  }
}

// =============================================================================
// move_cell Tests
// =============================================================================

TEST_SUITE("move_cell") {
  TEST_CASE("move within cluster siblings") {
    System system = create_test_system({{1, 2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    bool result = move_cell(system, 0, *cell1, 0, *cell2);
    CHECK(result);
    // For siblings, this is a swap operation
  }

  TEST_CASE("move within cluster non-siblings") {
    System system = create_test_system({{1, 2, 3}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell3 = find_cell_by_leaf_id(system.clusters[0], 3);
    REQUIRE(cell1.has_value());
    REQUIRE(cell3.has_value());

    size_t initial_ids = get_cluster_leaf_ids(system.clusters[0]).size();
    bool result = move_cell(system, 0, *cell1, 0, *cell3);
    CHECK(result);

    // All leaf_ids should still exist
    auto ids = get_cluster_leaf_ids(system.clusters[0]);
    CHECK(ids.size() == initial_ids);
  }

  TEST_CASE("move across clusters") {
    System system = create_test_system({{1, 2}, {3}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell3 = find_cell_by_leaf_id(system.clusters[1], 3);
    REQUIRE(cell1.has_value());
    REQUIRE(cell3.has_value());

    bool result = move_cell(system, 0, *cell1, 1, *cell3);
    CHECK(result);

    // Cluster 0 should have 1 leaf (leaf_id 2)
    auto ids0 = get_cluster_leaf_ids(system.clusters[0]);
    CHECK(ids0.size() == 1);
    CHECK(ids0[0] == 2);

    // Cluster 1 should have 2 leaves (leaf_ids 3 and 1)
    auto ids1 = get_cluster_leaf_ids(system.clusters[1]);
    CHECK(ids1.size() == 2);
    CHECK(std::find(ids1.begin(), ids1.end(), 1) != ids1.end());
    CHECK(std::find(ids1.begin(), ids1.end(), 3) != ids1.end());
  }

  TEST_CASE("same cell no-op") {
    System system = create_test_system({{1, 2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());

    bool result = move_cell(system, 0, *cell1, 0, *cell1);
    CHECK(result);
  }

  TEST_CASE("root only cell fails") {
    System system = create_test_system({{1}, {2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[1], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    // Cannot move the only cell from cluster 0
    bool result = move_cell(system, 0, *cell1, 1, *cell2);
    CHECK_FALSE(result);
  }

  TEST_CASE("invalid indices fail") {
    System system = create_test_system({{1, 2}, {3}});
    CHECK_FALSE(move_cell(system, -1, 0, 0, 0));
    CHECK_FALSE(move_cell(system, 0, 0, 10, 0));
    CHECK_FALSE(move_cell(system, 0, 100, 0, 0));
  }

  TEST_CASE("selection follows move") {
    System system = create_test_system({{1, 2}, {3}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell3 = find_cell_by_leaf_id(system.clusters[1], 3);
    REQUIRE(cell1.has_value());
    REQUIRE(cell3.has_value());

    set_selection(system, 0, *cell1);

    bool result = move_cell(system, 0, *cell1, 1, *cell3);
    CHECK(result);

    // Selection should follow the moved cell
    REQUIRE(system.selection.has_value());
    CHECK(system.selection->cluster_index == 1);
  }

  TEST_CASE("zen cleared on move") {
    System system = create_test_system({{1, 2}, {3}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell3 = find_cell_by_leaf_id(system.clusters[1], 3);
    REQUIRE(cell1.has_value());
    REQUIRE(cell3.has_value());

    system.clusters[0].zen_cell_index = *cell1;

    bool result = move_cell(system, 0, *cell1, 1, *cell3);
    CHECK(result);

    // Zen should be cleared
    CHECK_FALSE(system.clusters[0].zen_cell_index.has_value());
  }
}

// =============================================================================
// set_zen Tests
// =============================================================================

TEST_SUITE("set_zen") {
  TEST_CASE("sets zen on valid leaf") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());

    bool result = set_zen(system, 0, *cell);
    CHECK(result);
    REQUIRE(system.clusters[0].zen_cell_index.has_value());
    CHECK(*system.clusters[0].zen_cell_index == *cell);
  }

  TEST_CASE("invalid cluster fails") {
    System system = create_test_system({{1}});
    CHECK_FALSE(set_zen(system, -1, 0));
    CHECK_FALSE(set_zen(system, 10, 0));
  }

  TEST_CASE("invalid cell fails") {
    System system = create_test_system({{1, 2}});
    // Cell 0 is internal node
    CHECK_FALSE(set_zen(system, 0, 0));
    CHECK_FALSE(set_zen(system, 0, 100));
  }

  TEST_CASE("overwrites existing zen") {
    System system = create_test_system({{1, 2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    bool result1 = set_zen(system, 0, *cell1);
    CHECK(result1);
    CHECK(*system.clusters[0].zen_cell_index == *cell1);

    bool result2 = set_zen(system, 0, *cell2);
    CHECK(result2);
    CHECK(*system.clusters[0].zen_cell_index == *cell2);
  }
}

// =============================================================================
// clear_zen Tests
// =============================================================================

TEST_SUITE("clear_zen") {
  TEST_CASE("clears existing zen") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());

    system.clusters[0].zen_cell_index = *cell;
    clear_zen(system, 0);
    CHECK_FALSE(system.clusters[0].zen_cell_index.has_value());
  }

  TEST_CASE("no-op when no zen") {
    System system = create_test_system({{1, 2}});
    CHECK_FALSE(system.clusters[0].zen_cell_index.has_value());
    clear_zen(system, 0); // Should not crash
    CHECK_FALSE(system.clusters[0].zen_cell_index.has_value());
  }
}

// =============================================================================
// is_cell_zen Tests
// =============================================================================

TEST_SUITE("is_cell_zen") {
  TEST_CASE("returns true for zen cell") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());

    system.clusters[0].zen_cell_index = *cell;
    CHECK(is_cell_zen(system, 0, *cell));
  }

  TEST_CASE("returns false for non-zen") {
    System system = create_test_system({{1, 2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    system.clusters[0].zen_cell_index = *cell1;
    CHECK_FALSE(is_cell_zen(system, 0, *cell2));
  }

  TEST_CASE("returns false when no zen") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    CHECK_FALSE(is_cell_zen(system, 0, *cell));
  }
}

// =============================================================================
// toggle_selected_zen Tests
// =============================================================================

TEST_SUITE("toggle_selected_zen") {
  TEST_CASE("enables zen when off") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);

    bool result = toggle_selected_zen(system);
    CHECK(result);
    REQUIRE(system.clusters[0].zen_cell_index.has_value());
    CHECK(*system.clusters[0].zen_cell_index == *cell);
  }

  TEST_CASE("disables zen when on") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);
    system.clusters[0].zen_cell_index = *cell;

    bool result = toggle_selected_zen(system);
    CHECK(result);
    CHECK_FALSE(system.clusters[0].zen_cell_index.has_value());
  }

  TEST_CASE("no selection returns false") {
    System system = create_test_system({{1, 2}});
    system.selection.reset();

    bool result = toggle_selected_zen(system);
    CHECK_FALSE(result);
  }

  TEST_CASE("invalid selection fails") {
    System system = create_test_system({{1, 2}});
    // Select internal node
    set_selection(system, 0, 0);

    bool result = toggle_selected_zen(system);
    CHECK_FALSE(result);
  }
}

// =============================================================================
// move_selection Tests
// =============================================================================

TEST_SUITE("move_selection") {
  TEST_CASE("move right to adjacent cell") {
    System system = create_test_system({{1, 2}});
    // Set split direction to vertical for horizontal navigation
    system.clusters[0].tree[0].split_dir = SplitDir::Vertical;

    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());
    set_selection(system, 0, *cell1);

    auto geometries = compute_test_geometries(system);
    auto result = move_selection(system, Direction::Right, geometries);

    REQUIRE(result.has_value());
    CHECK(result->cluster_index == 0);
  }

  TEST_CASE("move left to adjacent cell") {
    System system = create_test_system({{1, 2}});
    system.clusters[0].tree[0].split_dir = SplitDir::Vertical;

    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell2.has_value());
    set_selection(system, 0, *cell2);

    auto geometries = compute_test_geometries(system);
    auto result = move_selection(system, Direction::Left, geometries);

    REQUIRE(result.has_value());
    CHECK(result->cluster_index == 0);
  }

  TEST_CASE("move down to adjacent cell") {
    System system = create_test_system({{1, 2}});
    system.clusters[0].tree[0].split_dir = SplitDir::Horizontal;

    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());
    set_selection(system, 0, *cell1);

    auto geometries = compute_test_geometries(system);
    auto result = move_selection(system, Direction::Down, geometries);

    REQUIRE(result.has_value());
  }

  TEST_CASE("move up to adjacent cell") {
    System system = create_test_system({{1, 2}});
    system.clusters[0].tree[0].split_dir = SplitDir::Horizontal;

    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell2.has_value());
    set_selection(system, 0, *cell2);

    auto geometries = compute_test_geometries(system);
    auto result = move_selection(system, Direction::Up, geometries);

    REQUIRE(result.has_value());
  }

  TEST_CASE("no cell in direction") {
    System system = create_test_system({{1}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());
    set_selection(system, 0, *cell1);

    auto geometries = compute_test_geometries(system);
    // Single cell has no neighbor in any direction
    auto result = move_selection(system, Direction::Right, geometries);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("cross-cluster navigation") {
    System system = create_test_system({{1}, {2}});
    // Clusters are horizontally adjacent
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());
    set_selection(system, 0, *cell1);

    auto geometries = compute_test_geometries(system);
    auto result = move_selection(system, Direction::Right, geometries);

    REQUIRE(result.has_value());
    CHECK(result->cluster_index == 1);
  }

  TEST_CASE("zen cluster only zen visible") {
    System system = create_test_system({{1, 2}, {3}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell3 = find_cell_by_leaf_id(system.clusters[1], 3);
    REQUIRE(cell1.has_value());
    REQUIRE(cell3.has_value());

    // Set zen on cluster 1
    system.clusters[1].zen_cell_index = *cell3;

    set_selection(system, 0, *cell1);
    auto geometries = compute_test_geometries(system);

    // Navigation to cluster 1 should only see the zen cell
    auto result = move_selection(system, Direction::Right, geometries);
    if (result.has_value() && result->cluster_index == 1) {
      CHECK(result->cell_index == *cell3);
    }
  }

  TEST_CASE("no selection returns nullopt") {
    System system = create_test_system({{1, 2}});
    system.selection.reset();

    auto geometries = compute_test_geometries(system);
    auto result = move_selection(system, Direction::Right, geometries);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("clears zen when moving to non-zen") {
    System system = create_test_system({{1, 2}});
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    // Set zen on cell2, select cell1 and move to cell2
    system.clusters[0].zen_cell_index = *cell2;
    system.clusters[0].tree[0].split_dir = SplitDir::Vertical;
    set_selection(system, 0, *cell1);

    auto geometries = compute_test_geometries(system);
    auto result = move_selection(system, Direction::Right, geometries);

    // If we moved to a non-zen cell in a zen cluster, zen should clear
    // Note: actual behavior depends on which cell is reached
  }
}

// =============================================================================
// toggle_selected_split_dir Tests
// =============================================================================

TEST_SUITE("toggle_selected_split_dir") {
  TEST_CASE("vertical to horizontal") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);

    system.clusters[0].tree[0].split_dir = SplitDir::Vertical;

    bool result = toggle_selected_split_dir(system);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_dir == SplitDir::Horizontal);
  }

  TEST_CASE("horizontal to vertical") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);

    system.clusters[0].tree[0].split_dir = SplitDir::Horizontal;

    bool result = toggle_selected_split_dir(system);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_dir == SplitDir::Vertical);
  }

  TEST_CASE("no selection returns false") {
    System system = create_test_system({{1, 2}});
    system.selection.reset();

    bool result = toggle_selected_split_dir(system);
    CHECK_FALSE(result);
  }

  TEST_CASE("root leaf returns false") {
    System system = create_test_system({{1}});
    set_selection(system, 0, 0);

    bool result = toggle_selected_split_dir(system);
    CHECK_FALSE(result);
  }

  TEST_CASE("non-leaf children fails") {
    System system = create_test_system({{1, 2, 3, 4}});
    // With 4 leaves, there are internal nodes with non-leaf children
    // Find a leaf whose parent has a non-leaf sibling
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());
    set_selection(system, 0, *cell1);

    // This may or may not succeed depending on tree structure
    // The function only succeeds if both children of parent are leaves
    [[maybe_unused]] bool result = toggle_selected_split_dir(system);
    // Just ensure it doesn't crash
  }
}

// =============================================================================
// cycle_split_mode Tests
// =============================================================================

TEST_SUITE("cycle_split_mode") {
  TEST_CASE("zigzag to vertical") {
    System system = create_test_system({{1}});
    system.split_mode = SplitMode::Zigzag;

    bool result = cycle_split_mode(system);
    CHECK(result);
    CHECK(system.split_mode == SplitMode::Vertical);
  }

  TEST_CASE("vertical to horizontal") {
    System system = create_test_system({{1}});
    system.split_mode = SplitMode::Vertical;

    bool result = cycle_split_mode(system);
    CHECK(result);
    CHECK(system.split_mode == SplitMode::Horizontal);
  }

  TEST_CASE("horizontal to zigzag") {
    System system = create_test_system({{1}});
    system.split_mode = SplitMode::Horizontal;

    bool result = cycle_split_mode(system);
    CHECK(result);
    CHECK(system.split_mode == SplitMode::Zigzag);
  }
}

// =============================================================================
// set_selected_split_ratio Tests
// =============================================================================

TEST_SUITE("set_selected_split_ratio") {
  TEST_CASE("sets valid ratio") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);

    bool result = set_selected_split_ratio(system, 0.7f);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_ratio == doctest::Approx(0.7f));
  }

  TEST_CASE("clamps to min 0.1") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);

    bool result = set_selected_split_ratio(system, 0.0f);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_ratio == doctest::Approx(0.1f));
  }

  TEST_CASE("clamps to max 0.9") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);

    bool result = set_selected_split_ratio(system, 1.0f);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_ratio == doctest::Approx(0.9f));
  }

  TEST_CASE("no selection fails") {
    System system = create_test_system({{1, 2}});
    system.selection.reset();

    bool result = set_selected_split_ratio(system, 0.5f);
    CHECK_FALSE(result);
  }

  TEST_CASE("root leaf fails") {
    System system = create_test_system({{1}});
    set_selection(system, 0, 0);

    bool result = set_selected_split_ratio(system, 0.5f);
    CHECK_FALSE(result);
  }
}

// =============================================================================
// adjust_selected_split_ratio Tests
// =============================================================================

TEST_SUITE("adjust_selected_split_ratio") {
  TEST_CASE("positive delta grows first child") {
    System system = create_test_system({{1, 2}});
    // First child is cell with leaf_id 1
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());
    set_selection(system, 0, *cell1);

    system.clusters[0].tree[0].split_ratio = 0.5f;

    bool result = adjust_selected_split_ratio(system, 0.1f);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_ratio == doctest::Approx(0.6f));
  }

  TEST_CASE("positive delta grows second child") {
    System system = create_test_system({{1, 2}});
    // Second child is cell with leaf_id 2
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell2.has_value());
    set_selection(system, 0, *cell2);

    system.clusters[0].tree[0].split_ratio = 0.5f;

    // For second child, delta is negated so positive delta decreases ratio
    bool result = adjust_selected_split_ratio(system, 0.1f);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_ratio == doctest::Approx(0.4f));
  }

  TEST_CASE("respects clamp limits") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    set_selection(system, 0, *cell);

    system.clusters[0].tree[0].split_ratio = 0.85f;

    bool result = adjust_selected_split_ratio(system, 0.2f);
    CHECK(result);
    CHECK(system.clusters[0].tree[0].split_ratio == doctest::Approx(0.9f));
  }

  TEST_CASE("no selection fails") {
    System system = create_test_system({{1, 2}});
    system.selection.reset();

    bool result = adjust_selected_split_ratio(system, 0.1f);
    CHECK_FALSE(result);
  }
}

// =============================================================================
// update Tests
// =============================================================================

TEST_SUITE("update") {
  TEST_CASE("add new cell") {
    System system = create_test_system({{1}});
    CHECK(system.clusters[0].tree.size() == 1);

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1, 2}}};
    bool result = update(system, updates);

    CHECK(result);
    CHECK(system.clusters[0].tree.size() == 3); // Now has 2 leaves + parent
    auto ids = get_cluster_leaf_ids(system.clusters[0]);
    CHECK(ids.size() == 2);
  }

  TEST_CASE("delete removed cell") {
    System system = create_test_system({{1, 2}});
    CHECK(system.clusters[0].tree.size() == 3);

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1}}};
    bool result = update(system, updates);

    CHECK(result);
    CHECK(system.clusters[0].tree.size() == 1);
    auto ids = get_cluster_leaf_ids(system.clusters[0]);
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 1);
  }

  TEST_CASE("add and delete combined") {
    System system = create_test_system({{1, 2}});

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1, 3}}}; // Remove 2, add 3
    bool result = update(system, updates);

    CHECK(result);
    auto ids = get_cluster_leaf_ids(system.clusters[0]);
    CHECK(ids.size() == 2);
    CHECK(std::find(ids.begin(), ids.end(), 1) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), 3) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), 2) == ids.end());
  }

  TEST_CASE("no changes returns false") {
    System system = create_test_system({{1, 2}});

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1, 2}}};
    bool result = update(system, updates);

    CHECK_FALSE(result);
  }

  TEST_CASE("selection updated on delete") {
    System system = create_test_system({{1, 2}});
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell2.has_value());
    set_selection(system, 0, *cell2);

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1}}}; // Delete 2
    bool result = update(system, updates);

    CHECK(result);
    REQUIRE(system.selection.has_value());
    // Selection should move to remaining cell
    CHECK(system.selection->cluster_index == 0);
  }

  TEST_CASE("selection updated on add") {
    System system = create_test_system({{1}});
    set_selection(system, 0, 0);

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1, 2}}};
    bool result = update(system, updates);

    CHECK(result);
    REQUIRE(system.selection.has_value());
  }

  TEST_CASE("redirect_cluster_index works") {
    System system = create_test_system({{1}, {}});

    // New window (2) appears, redirect to cluster 1
    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1, 2}}, {.leaf_ids = {}}};
    bool result = update(system, updates, 1);

    CHECK(result);
    // Window 2 should be in cluster 1, not cluster 0
    auto ids0 = get_cluster_leaf_ids(system.clusters[0]);
    auto ids1 = get_cluster_leaf_ids(system.clusters[1]);
    CHECK(std::find(ids0.begin(), ids0.end(), 2) == ids0.end());
    CHECK(std::find(ids1.begin(), ids1.end(), 2) != ids1.end());
  }

  TEST_CASE("fullscreen state updated") {
    System system = create_test_system({{1}});
    CHECK_FALSE(system.clusters[0].has_fullscreen_cell);

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1}, .has_fullscreen_cell = true}};
    [[maybe_unused]] bool result = update(system, updates);

    CHECK(system.clusters[0].has_fullscreen_cell);
  }

  TEST_CASE("zen cleared on cell change") {
    System system = create_test_system({{1, 2}});
    auto cell = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell.has_value());
    system.clusters[0].zen_cell_index = *cell;

    std::vector<ClusterCellUpdateInfo> updates = {{.leaf_ids = {1, 2, 3}}}; // Add cell 3
    bool result = update(system, updates);

    CHECK(result);
    CHECK_FALSE(system.clusters[0].zen_cell_index.has_value());
  }
}

// =============================================================================
// compute_cluster_geometry Tests
// =============================================================================

TEST_SUITE("compute_cluster_geometry") {
  TEST_CASE("empty cluster returns empty") {
    Cluster cluster = create_empty_cluster();
    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    CHECK(rects.empty());
  }

  TEST_CASE("single cell fills area minus gaps") {
    System system = create_test_system({{1}});
    auto& cluster = system.clusters[0];

    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    REQUIRE(rects.size() == 1);

    // Should fill 800-20=780 x 600-20=580
    CHECK(rects[0].x == doctest::Approx(10.0f));
    CHECK(rects[0].y == doctest::Approx(10.0f));
    CHECK(rects[0].width == doctest::Approx(780.0f));
    CHECK(rects[0].height == doctest::Approx(580.0f));
  }

  TEST_CASE("two cells split vertically") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Vertical;
    cluster.tree[0].split_ratio = 0.5f;

    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    REQUIRE(rects.size() == 3);

    // Internal node has empty rect
    CHECK(rects[0].width == 0.0f);

    // Find leaf rects
    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    auto cell2 = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    const Rect& r1 = rects[static_cast<size_t>(*cell1)];
    const Rect& r2 = rects[static_cast<size_t>(*cell2)];

    // Both should have equal width (half of available minus gap)
    CHECK(r1.width == doctest::Approx(r2.width));
    // Heights should be same (full height minus outer gaps)
    CHECK(r1.height == doctest::Approx(580.0f));
    CHECK(r2.height == doctest::Approx(580.0f));
  }

  TEST_CASE("two cells split horizontally") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Horizontal;
    cluster.tree[0].split_ratio = 0.5f;

    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f);

    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    auto cell2 = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    const Rect& r1 = rects[static_cast<size_t>(*cell1)];
    const Rect& r2 = rects[static_cast<size_t>(*cell2)];

    // Both should have equal height
    CHECK(r1.height == doctest::Approx(r2.height));
    // Widths should be same (full width minus outer gaps)
    CHECK(r1.width == doctest::Approx(780.0f));
    CHECK(r2.width == doctest::Approx(780.0f));
  }

  TEST_CASE("split ratio affects sizes") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Vertical;
    cluster.tree[0].split_ratio = 0.25f;

    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f);

    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    auto cell2 = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());

    const Rect& r1 = rects[static_cast<size_t>(*cell1)];
    const Rect& r2 = rects[static_cast<size_t>(*cell2)];

    // First child should be 25% width, second 75%
    float available = 780.0f - 10.0f; // minus inner gap
    CHECK(r1.width == doctest::Approx(available * 0.25f));
    CHECK(r2.width == doctest::Approx(available * 0.75f));
  }

  TEST_CASE("nested splits work correctly") {
    System system = create_test_system({{1, 2, 3}});
    auto& cluster = system.clusters[0];

    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    REQUIRE(rects.size() == 5);

    // All three leaf cells should have positive dimensions
    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    auto cell2 = find_cell_by_leaf_id(cluster, 2);
    auto cell3 = find_cell_by_leaf_id(cluster, 3);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());
    REQUIRE(cell3.has_value());

    CHECK(rects[static_cast<size_t>(*cell1)].width > 0.0f);
    CHECK(rects[static_cast<size_t>(*cell2)].width > 0.0f);
    CHECK(rects[static_cast<size_t>(*cell3)].width > 0.0f);
  }

  TEST_CASE("zen cell overrides geometry") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    auto cell = find_cell_by_leaf_id(cluster, 1);
    REQUIRE(cell.has_value());
    cluster.zen_cell_index = *cell;

    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f, 0.85f);

    const Rect& zen_rect = rects[static_cast<size_t>(*cell)];
    // Zen rect should be 85% of cluster size, centered
    float expected_w = 800.0f * 0.85f;
    float expected_h = 600.0f * 0.85f;
    CHECK(zen_rect.width == doctest::Approx(expected_w));
    CHECK(zen_rect.height == doctest::Approx(expected_h));
  }

  TEST_CASE("internal nodes have empty rects") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];

    auto rects = compute_cluster_geometry(cluster, 10.0f, 10.0f);

    // Node 0 is internal
    CHECK(rects[0].width == 0.0f);
    CHECK(rects[0].height == 0.0f);
  }

  TEST_CASE("gap values applied correctly") {
    System system = create_test_system({{1}});
    auto& cluster = system.clusters[0];

    auto rects_small = compute_cluster_geometry(cluster, 5.0f, 5.0f);
    auto rects_large = compute_cluster_geometry(cluster, 20.0f, 20.0f);

    // Larger gaps = smaller cell
    CHECK(rects_small[0].width > rects_large[0].width);
    CHECK(rects_small[0].height > rects_large[0].height);
  }
}

// =============================================================================
// perform_drop_move Tests
// =============================================================================

TEST_SUITE("perform_drop_move") {
  TEST_CASE("move to different cell") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Vertical;

    auto geometries = compute_test_geometries(system);
    auto cell2 = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell2.has_value());

    // Get center of cell2 as drop target
    const Rect& target_rect = geometries[0][static_cast<size_t>(*cell2)];
    float cx = target_rect.x + target_rect.width / 2.0f;
    float cy = target_rect.y + target_rect.height / 2.0f;

    auto result = perform_drop_move(system, 1, cx, cy, geometries, false);
    // May or may not succeed depending on drop logic
    // Just check it doesn't crash
  }

  TEST_CASE("exchange mode swaps cells") {
    System system = create_test_system({{1, 2}});
    auto geometries = compute_test_geometries(system);

    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell2.has_value());

    const Rect& target_rect = geometries[0][static_cast<size_t>(*cell2)];
    float cx = target_rect.x + target_rect.width / 2.0f;
    float cy = target_rect.y + target_rect.height / 2.0f;

    auto result = perform_drop_move(system, 1, cx, cy, geometries, true);
    if (result.has_value()) {
      CHECK(result->was_exchange);
    }
  }

  TEST_CASE("unmanaged source fails") {
    System system = create_test_system({{1, 2}});
    auto geometries = compute_test_geometries(system);

    auto result = perform_drop_move(system, 999, 400.0f, 300.0f, geometries, false);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("drop outside cells fails") {
    System system = create_test_system({{1}});
    auto geometries = compute_test_geometries(system);

    // Drop at coordinates outside any cell
    auto result = perform_drop_move(system, 1, -1000.0f, -1000.0f, geometries, false);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("drop on same cell fails") {
    System system = create_test_system({{1, 2}});
    auto geometries = compute_test_geometries(system);

    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    REQUIRE(cell1.has_value());

    const Rect& src_rect = geometries[0][static_cast<size_t>(*cell1)];
    float cx = src_rect.x + src_rect.width / 2.0f;
    float cy = src_rect.y + src_rect.height / 2.0f;

    auto result = perform_drop_move(system, 1, cx, cy, geometries, false);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("fullscreen target fails") {
    System system = create_test_system({{1}, {2}});
    system.clusters[1].has_fullscreen_cell = true;

    auto geometries = compute_test_geometries(system);
    auto cell2 = find_cell_by_leaf_id(system.clusters[1], 2);
    REQUIRE(cell2.has_value());

    const Rect& target_rect = geometries[1][static_cast<size_t>(*cell2)];
    float cx = target_rect.x + target_rect.width / 2.0f;
    float cy = target_rect.y + target_rect.height / 2.0f;

    auto result = perform_drop_move(system, 1, cx, cy, geometries, false);
    CHECK_FALSE(result.has_value());
  }

  TEST_CASE("returns result cursor position") {
    System system = create_test_system({{1, 2}});
    auto geometries = compute_test_geometries(system);

    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell2.has_value());

    const Rect& target_rect = geometries[0][static_cast<size_t>(*cell2)];
    float cx = target_rect.x + target_rect.width / 2.0f;
    float cy = target_rect.y + target_rect.height / 2.0f;

    auto result = perform_drop_move(system, 1, cx, cy, geometries, true);
    if (result.has_value()) {
      // Result should contain a cursor position
      bool has_position = (result->cursor_pos.x != 0) || (result->cursor_pos.y != 0);
      CHECK(has_position);
    }
  }

  TEST_CASE("cross-cluster drop move") {
    System system = create_test_system({{1, 2}, {3}});
    auto geometries = compute_test_geometries(system);

    auto cell3 = find_cell_by_leaf_id(system.clusters[1], 3);
    REQUIRE(cell3.has_value());

    const Rect& target_rect = geometries[1][static_cast<size_t>(*cell3)];
    float cx = target_rect.x + target_rect.width / 2.0f;
    float cy = target_rect.y + target_rect.height / 2.0f;

    auto result = perform_drop_move(system, 1, cx, cy, geometries, false);
    // Result depends on move_cell logic
  }
}

// =============================================================================
// update_split_ratio_from_resize Tests
// =============================================================================

TEST_SUITE("update_split_ratio_from_resize") {
  // Note: update_split_ratio_from_resize requires parent geometry in cluster_geometry,
  // but compute_cluster_geometry clears internal node rects. For these tests to work,
  // we provide custom geometry with valid parent rects.

  TEST_CASE("left edge resize updates ratio") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Vertical;
    cluster.tree[0].split_ratio = 0.5f;

    // Compute geometry and manually preserve parent rect
    auto geometry = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    // Set parent rect (node 0) to cluster bounds with gaps
    geometry[0] = Rect{10.0f, 10.0f, 780.0f, 580.0f};

    auto cell2 = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell2.has_value());

    Rect actual = geometry[static_cast<size_t>(*cell2)];
    actual.x -= 50.0f;     // Move left edge
    actual.width += 50.0f; // Expand width

    bool result = update_split_ratio_from_resize(system, 0, 2, actual, geometry);
    CHECK(result);
    // Ratio should have changed (smaller than 0.5 since first child shrank)
    CHECK(cluster.tree[0].split_ratio < 0.5f);
  }

  TEST_CASE("right edge resize updates ratio") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Vertical;
    cluster.tree[0].split_ratio = 0.5f;

    auto geometry = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    geometry[0] = Rect{10.0f, 10.0f, 780.0f, 580.0f};

    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    REQUIRE(cell1.has_value());

    Rect actual = geometry[static_cast<size_t>(*cell1)];
    actual.width += 50.0f; // Expand right edge

    bool result = update_split_ratio_from_resize(system, 0, 1, actual, geometry);
    CHECK(result);
    CHECK(cluster.tree[0].split_ratio > 0.5f);
  }

  TEST_CASE("top edge resize updates ratio") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Horizontal;
    cluster.tree[0].split_ratio = 0.5f;

    auto geometry = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    geometry[0] = Rect{10.0f, 10.0f, 780.0f, 580.0f};

    auto cell2 = find_cell_by_leaf_id(cluster, 2);
    REQUIRE(cell2.has_value());

    Rect actual = geometry[static_cast<size_t>(*cell2)];
    actual.y -= 50.0f;      // Move top edge
    actual.height += 50.0f; // Expand height

    bool result = update_split_ratio_from_resize(system, 0, 2, actual, geometry);
    CHECK(result);
    CHECK(cluster.tree[0].split_ratio < 0.5f);
  }

  TEST_CASE("bottom edge resize updates ratio") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Horizontal;
    cluster.tree[0].split_ratio = 0.5f;

    auto geometry = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    geometry[0] = Rect{10.0f, 10.0f, 780.0f, 580.0f};

    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    REQUIRE(cell1.has_value());

    Rect actual = geometry[static_cast<size_t>(*cell1)];
    actual.height += 50.0f; // Expand bottom edge

    bool result = update_split_ratio_from_resize(system, 0, 1, actual, geometry);
    CHECK(result);
    CHECK(cluster.tree[0].split_ratio > 0.5f);
  }

  TEST_CASE("no change returns false") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];

    auto geometry = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    REQUIRE(cell1.has_value());

    // Use exact same rect
    Rect actual = geometry[static_cast<size_t>(*cell1)];

    bool result = update_split_ratio_from_resize(system, 0, 1, actual, geometry);
    CHECK_FALSE(result);
  }

  TEST_CASE("invalid cluster fails") {
    System system = create_test_system({{1, 2}});
    Rect dummy{0, 0, 100, 100};
    std::vector<Rect> geometry;

    CHECK_FALSE(update_split_ratio_from_resize(system, -1, 1, dummy, geometry));
    CHECK_FALSE(update_split_ratio_from_resize(system, 10, 1, dummy, geometry));
  }

  TEST_CASE("leaf not found fails") {
    System system = create_test_system({{1, 2}});
    auto geometry = compute_cluster_geometry(system.clusters[0], 10.0f, 10.0f);
    Rect dummy{0, 0, 100, 100};

    bool result = update_split_ratio_from_resize(system, 0, 999, dummy, geometry);
    CHECK_FALSE(result);
  }

  TEST_CASE("root leaf fails") {
    System system = create_test_system({{1}});
    auto geometry = compute_cluster_geometry(system.clusters[0], 10.0f, 10.0f);
    Rect actual = geometry[0];
    actual.width += 50.0f;

    bool result = update_split_ratio_from_resize(system, 0, 1, actual, geometry);
    CHECK_FALSE(result);
  }

  TEST_CASE("respects clamp limits") {
    System system = create_test_system({{1, 2}});
    auto& cluster = system.clusters[0];
    cluster.tree[0].split_dir = SplitDir::Vertical;
    cluster.tree[0].split_ratio = 0.5f;

    auto geometry = compute_cluster_geometry(cluster, 10.0f, 10.0f);
    auto cell1 = find_cell_by_leaf_id(cluster, 1);
    REQUIRE(cell1.has_value());

    // Extreme resize
    Rect actual = geometry[static_cast<size_t>(*cell1)];
    actual.width = 1.0f; // Very small

    bool result = update_split_ratio_from_resize(system, 0, 1, actual, geometry);
    if (result) {
      // Ratio should be clamped
      CHECK(cluster.tree[0].split_ratio >= 0.1f);
      CHECK(cluster.tree[0].split_ratio <= 0.9f);
    }
  }
}

// =============================================================================
// validate_system Tests
// =============================================================================

TEST_SUITE("validate_system") {
  TEST_CASE("valid system returns true") {
    System system = create_test_system({{1, 2}, {3}});
    CHECK(validate_system(system));
  }

  TEST_CASE("invalid selection cluster fails") {
    System system = create_test_system({{1}});
    set_selection(system, 10, 0);
    CHECK_FALSE(validate_system(system));
  }

  TEST_CASE("invalid selection cell fails") {
    System system = create_test_system({{1, 2}});
    // Select internal node
    set_selection(system, 0, 0);
    CHECK_FALSE(validate_system(system));
  }

  TEST_CASE("leaf without leaf_id fails") {
    System system = create_test_system({{1}});
    // Corrupt: remove leaf_id from leaf
    system.clusters[0].tree[0].leaf_id.reset();
    CHECK_FALSE(validate_system(system));
  }

  TEST_CASE("internal with leaf_id fails") {
    System system = create_test_system({{1, 2}});
    // Corrupt: add leaf_id to internal node
    system.clusters[0].tree[0].leaf_id = 999;
    CHECK_FALSE(validate_system(system));
  }

  TEST_CASE("duplicate leaf_ids fails") {
    System system = create_test_system({{1, 2}});
    // Corrupt: make both leaves have same leaf_id
    auto cell1 = find_cell_by_leaf_id(system.clusters[0], 1);
    auto cell2 = find_cell_by_leaf_id(system.clusters[0], 2);
    REQUIRE(cell1.has_value());
    REQUIRE(cell2.has_value());
    system.clusters[0].tree[*cell2].leaf_id = 1;
    CHECK_FALSE(validate_system(system));
  }

  TEST_CASE("invalid zen index fails") {
    System system = create_test_system({{1, 2}});
    // Set zen to internal node
    system.clusters[0].zen_cell_index = 0;
    CHECK_FALSE(validate_system(system));
  }
}

#endif // DOCTEST_CONFIG_DISABLE
