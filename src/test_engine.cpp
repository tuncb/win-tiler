#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>

#include "engine.h"
#include "save_layout.h"

using namespace wintiler;
using namespace wintiler::ctrl;

namespace {

// Create a simple 2-cluster system for testing
// Cluster 0: 800x600 at (0,0) with 2 windows (leaf_ids 1 and 2)
// Cluster 1: 800x600 at (800,0) with 1 window (leaf_id 3)
Engine create_test_engine() {
  Engine engine;
  std::vector<ClusterInitInfo> infos = {
      {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}},   // Cluster 0
      {800.0f, 0.0f, 800.0f, 600.0f, 800.0f, 0.0f, 800.0f, 600.0f, {3}}}; // Cluster 1
  engine.init(infos);
  return engine;
}

Engine create_test_engine_with_secondary_taskbar() {
  Engine engine;
  std::vector<ClusterInitInfo> infos = {
      {0.0f, 0.0f, 800.0f, 560.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}},
      {800.0f, 0.0f, 800.0f, 560.0f, 800.0f, 0.0f, 800.0f, 600.0f, {3}}};
  engine.init(infos);
  return engine;
}

Engine create_engine_with_empty_second_cluster() {
  Engine engine;
  std::vector<ClusterInitInfo> infos = {
      {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}},
      {800.0f, 0.0f, 800.0f, 600.0f, 800.0f, 0.0f, 800.0f, 600.0f, {}}};
  engine.init(infos);
  return engine;
}

// Create engine with single cluster with one window
Engine create_single_cluster_engine() {
  Engine engine;
  std::vector<ClusterInitInfo> infos = {
      {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1}}};
  engine.init(infos);
  return engine;
}

// Create engine with single cluster with two windows (for sibling tests)
Engine create_two_window_engine() {
  Engine engine;
  std::vector<ClusterInitInfo> infos = {
      {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}}};
  engine.init(infos);
  return engine;
}

LayoutRule create_two_window_vertical_layout_rule(float ratio) {
  LayoutRule rule;
  rule.window_count = 2;
  rule.tree.split_dir = LayoutSplitDir::Vertical;
  rule.tree.split_ratio = ratio;
  return rule;
}

LayoutOptions create_two_window_vertical_layout_options(float ratio) {
  LayoutOptions options;
  options.rules.push_back(create_two_window_vertical_layout_rule(ratio));
  return options;
}

LayoutRule create_three_window_vertical_right_horizontal_layout_rule() {
  LayoutRule rule;
  rule.window_count = 3;
  rule.tree.split_dir = LayoutSplitDir::Vertical;
  rule.tree.split_ratio = 0.5f;

  auto right = std::make_shared<LayoutTreeNode>();
  right->split_dir = LayoutSplitDir::Horizontal;
  right->split_ratio = 0.5f;
  rule.tree.second = right;
  return rule;
}

LayoutOptions create_three_window_vertical_right_horizontal_layout_options() {
  LayoutOptions options;
  options.rules.push_back(create_three_window_vertical_right_horizontal_layout_rule());
  return options;
}

Engine create_three_window_engine() {
  Engine engine;
  std::vector<ClusterInitInfo> infos = {
      {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2, 3}}};
  engine.init(infos);
  return engine;
}

// Create empty engine
Engine create_empty_engine() {
  Engine engine;
  std::vector<ClusterInitInfo> infos = {};
  engine.init(infos);
  return engine;
}

// Compute geometries with default gaps (10, 10) and no zen
std::vector<std::vector<Rect>> compute_default_geometries(const Engine& engine) {
  return engine.compute_geometries(10.0f, 10.0f, 0.0f);
}

std::vector<size_t> collect_cluster_leaf_ids(const Cluster& cluster) {
  std::vector<size_t> leaf_ids;
  for (int index = 0; index < cluster.tree.size(); ++index) {
    if (cluster.tree.is_leaf(index) && cluster.tree[index].leaf_id.has_value()) {
      leaf_ids.push_back(*cluster.tree[index].leaf_id);
    }
  }
  return leaf_ids;
}

std::optional<int> find_cluster_leaf_index(const Cluster& cluster, size_t leaf_id) {
  for (int index = 0; index < cluster.tree.size(); ++index) {
    if (cluster.tree.is_leaf(index) && cluster.tree[index].leaf_id.has_value() &&
        *cluster.tree[index].leaf_id == leaf_id) {
      return index;
    }
  }
  return std::nullopt;
}

Point compute_rect_center(const Rect& rect) {
  return Point{static_cast<long>(rect.x + rect.width / 2.0f),
               static_cast<long>(rect.y + rect.height / 2.0f)};
}

std::vector<ClusterCellUpdateInfo> build_current_cluster_updates(const Engine& engine) {
  std::vector<ClusterCellUpdateInfo> updates;
  updates.reserve(engine.system.clusters.size());
  for (const auto& cluster : engine.system.clusters) {
    updates.push_back({collect_cluster_leaf_ids(cluster), cluster.has_fullscreen_cell});
  }
  return updates;
}

bool rects_equal(const Rect& lhs, const Rect& rhs) {
  return lhs.x == doctest::Approx(rhs.x) && lhs.y == doctest::Approx(rhs.y) &&
         lhs.width == doctest::Approx(rhs.width) && lhs.height == doctest::Approx(rhs.height);
}

bool geometries_equal(const std::vector<std::vector<Rect>>& lhs,
                      const std::vector<std::vector<Rect>>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (size_t cluster_index = 0; cluster_index < lhs.size(); ++cluster_index) {
    if (lhs[cluster_index].size() != rhs[cluster_index].size()) {
      return false;
    }

    for (size_t rect_index = 0; rect_index < lhs[cluster_index].size(); ++rect_index) {
      if (!rects_equal(lhs[cluster_index][rect_index], rhs[cluster_index][rect_index])) {
        return false;
      }
    }
  }

  return true;
}

// Helper to set selection on a system
void set_selection(Engine& engine, int cluster_index, int cell_index) {
  engine.system.selection = CellIndicatorByIndex{cluster_index, cell_index};
}

std::optional<CellIndicatorByIndex> find_cell_for_leaf_id(const Engine& engine, size_t leaf_id) {
  for (size_t cluster_index = 0; cluster_index < engine.system.clusters.size(); ++cluster_index) {
    auto cell_index = find_cluster_leaf_index(engine.system.clusters[cluster_index], leaf_id);
    if (cell_index.has_value()) {
      return CellIndicatorByIndex{static_cast<int>(cluster_index), *cell_index};
    }
  }
  return std::nullopt;
}

std::optional<Point>
get_leaf_center_from_geometries(const Engine& engine,
                                const std::vector<std::vector<Rect>>& geometries, size_t leaf_id) {
  auto cell = find_cell_for_leaf_id(engine, leaf_id);
  if (!cell.has_value()) {
    return std::nullopt;
  }

  int cluster_index = cell->cluster_index;
  int cell_index = cell->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= geometries.size()) {
    return std::nullopt;
  }
  if (cell_index < 0 ||
      static_cast<size_t>(cell_index) >= geometries[static_cast<size_t>(cluster_index)].size()) {
    return std::nullopt;
  }

  return compute_rect_center(
      geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(cell_index)]);
}

} // namespace

TEST_SUITE("Save layout rule serialization") {
  TEST_CASE("builds layout rule from cluster split tree without window ids") {
    Cluster cluster;
    CellData root;
    root.split_dir = SplitDir::Vertical;
    root.split_ratio = 0.30f;
    int root_index = cluster.tree.add_node(root);

    CellData first_leaf;
    first_leaf.leaf_id = 101;
    int first_index = cluster.tree.add_node(first_leaf, root_index);

    CellData second_split;
    second_split.split_dir = SplitDir::Horizontal;
    second_split.split_ratio = 0.60f;
    int second_index = cluster.tree.add_node(second_split, root_index);
    cluster.tree.set_children(root_index, first_index, second_index);

    CellData second_first_leaf;
    second_first_leaf.leaf_id = 202;
    int second_first_index = cluster.tree.add_node(second_first_leaf, second_index);

    CellData second_second_leaf;
    second_second_leaf.leaf_id = 303;
    int second_second_index = cluster.tree.add_node(second_second_leaf, second_index);
    cluster.tree.set_children(second_index, second_first_index, second_second_index);

    auto rule = build_layout_rule_from_cluster(cluster);

    REQUIRE(rule.has_value());
    CHECK(rule->window_count == 3);
    CHECK(count_layout_windows(rule->tree) == 3);
    CHECK(rule->tree.split_dir == LayoutSplitDir::Vertical);
    CHECK(rule->tree.split_ratio == doctest::Approx(0.30f));
    CHECK_FALSE(rule->tree.first);
    REQUIRE(rule->tree.second);
    CHECK(rule->tree.second->split_dir == LayoutSplitDir::Horizontal);
    CHECK(rule->tree.second->split_ratio == doctest::Approx(0.60f));
    CHECK_FALSE(rule->tree.second->first);
    CHECK_FALSE(rule->tree.second->second);
  }

  TEST_CASE("rejects single window cluster") {
    Cluster cluster;
    CellData leaf;
    leaf.leaf_id = 101;
    cluster.tree.add_node(leaf);

    auto rule = build_layout_rule_from_cluster(cluster);

    CHECK(!rule.has_value());
    CHECK(rule.error() == "Need at least 2 tiled windows");
  }
}

// =============================================================================
// Engine::init Tests
// =============================================================================

TEST_SUITE("Engine::init") {
  TEST_CASE("init with empty clusters") {
    Engine engine = create_empty_engine();

    CHECK(engine.system.clusters.empty());
    CHECK_FALSE(engine.system.selection.has_value());
  }

  TEST_CASE("init with single cluster") {
    Engine engine = create_single_cluster_engine();

    CHECK(engine.system.clusters.size() == 1);
    CHECK(engine.system.clusters[0].window_width == 800.0f);
    CHECK(engine.system.clusters[0].window_height == 600.0f);
    // Single window means tree has 1 node (root leaf)
    CHECK(engine.system.clusters[0].tree.size() == 1);
  }

  TEST_CASE("init with multiple clusters") {
    Engine engine = create_test_engine();

    CHECK(engine.system.clusters.size() == 2);

    // Cluster 0: 2 windows creates tree with 3 nodes (parent + 2 leaves)
    CHECK(engine.system.clusters[0].tree.size() == 3);
    CHECK(engine.system.clusters[0].global_x == 0.0f);

    // Cluster 1: 1 window creates tree with 1 node
    CHECK(engine.system.clusters[1].tree.size() == 1);
    CHECK(engine.system.clusters[1].global_x == 800.0f);
  }

  TEST_CASE("init replaces existing state") {
    Engine engine = create_test_engine();
    CHECK(engine.system.clusters.size() == 2);

    // Re-init with single cluster
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 400.0f, 300.0f, 0.0f, 0.0f, 400.0f, 300.0f, {100}}};
    engine.init(infos);

    CHECK(engine.system.clusters.size() == 1);
    CHECK(engine.system.clusters[0].window_width == 400.0f);
  }

  TEST_CASE("init applies configured layout rule for matching window count") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {{0.0f,
                                           0.0f,
                                           800.0f,
                                           600.0f,
                                           0.0f,
                                           0.0f,
                                           800.0f,
                                           600.0f,
                                           {1, 2},
                                           create_two_window_vertical_layout_rule(0.30f)}};

    engine.init(infos);

    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 3);
    CHECK(cluster.tree[0].split_dir == SplitDir::Vertical);
    CHECK(cluster.tree[0].split_ratio == doctest::Approx(0.30f));

    auto geometries = engine.compute_geometries(0.0f, 0.0f, 0.0f);
    auto first_leaf = find_cluster_leaf_index(cluster, 1);
    auto second_leaf = find_cluster_leaf_index(cluster, 2);
    REQUIRE(first_leaf.has_value());
    REQUIRE(second_leaf.has_value());

    CHECK(geometries[0][static_cast<size_t>(*first_leaf)].width == doctest::Approx(240.0f));
    CHECK(geometries[0][static_cast<size_t>(*second_leaf)].x == doctest::Approx(240.0f));
    CHECK(geometries[0][static_cast<size_t>(*second_leaf)].width == doctest::Approx(560.0f));
  }
}

// =============================================================================
// Engine leaf operation tests
// =============================================================================

TEST_SUITE("Engine leaf operations") {
  TEST_CASE("find_leaf returns cluster and cell for an existing leaf") {
    Engine engine = create_test_engine();

    auto cell = engine.find_leaf(3);

    REQUIRE(cell.has_value());
    CHECK(cell->cluster_index == 1);
    CHECK(cell->cell_index == 0);
  }

  TEST_CASE("selected_leaf_id returns selected managed leaf") {
    Engine engine = create_two_window_engine();
    set_selection(engine, 0, 2);

    auto leaf_id = engine.selected_leaf_id();

    REQUIRE(leaf_id.has_value());
    CHECK(*leaf_id == 2);
  }

  TEST_CASE("select_leaf updates system selection") {
    Engine engine = create_test_engine();

    bool selected = engine.select_leaf(2);

    CHECK(selected);
    REQUIRE(engine.system.selection.has_value());
    CHECK(engine.system.selection->cluster_index == 0);
    CHECK(engine.system.selection->cell_index == 2);
  }

  TEST_CASE("swap_leaves swaps managed leaves across clusters") {
    Engine engine = create_test_engine();

    bool swapped = engine.swap_leaves(1, 3);

    CHECK(swapped);
    auto leaf1 = engine.find_leaf(1);
    auto leaf3 = engine.find_leaf(3);
    REQUIRE(leaf1.has_value());
    REQUIRE(leaf3.has_value());
    CHECK(leaf1->cluster_index == 1);
    CHECK(leaf3->cluster_index == 0);
  }

  TEST_CASE("move_leaf_to_cell moves a managed leaf into the target cluster") {
    Engine engine = create_test_engine();

    bool moved = engine.move_leaf_to_cell(1, 1, 0);

    CHECK(moved);
    auto moved_leaf = engine.find_leaf(1);
    auto remaining_leaf = engine.find_leaf(2);
    REQUIRE(moved_leaf.has_value());
    REQUIRE(remaining_leaf.has_value());
    CHECK(moved_leaf->cluster_index == 1);
    CHECK(remaining_leaf->cluster_index == 0);
  }

  TEST_CASE("move_leaf_to_cell can empty the source cluster") {
    Engine engine = create_test_engine();

    bool moved = engine.move_leaf_to_cell(3, 0, 2);

    CHECK(moved);
    auto moved_leaf = engine.find_leaf(3);
    REQUIRE(moved_leaf.has_value());
    CHECK(moved_leaf->cluster_index == 0);
    CHECK(engine.system.clusters[1].tree.empty());
  }

  TEST_CASE("move_leaf_to_cell clears zen when the target leaf is split") {
    Engine engine = create_test_engine();
    engine.system.clusters[1].zen_cell_index = 0;

    bool moved = engine.move_leaf_to_cell(1, 1, 0);

    CHECK(moved);
    CHECK_FALSE(engine.system.clusters[1].zen_cell_index.has_value());
    auto moved_leaf = engine.find_leaf(1);
    REQUIRE(moved_leaf.has_value());
    CHECK(moved_leaf->cluster_index == 1);
  }

  TEST_CASE("select_leaf returns false when leaf is missing") {
    Engine engine = create_test_engine();

    bool selected = engine.select_leaf(999);

    CHECK_FALSE(selected);
  }

  TEST_CASE("swap_leaves returns false when a leaf is missing") {
    Engine engine = create_test_engine();

    bool swapped = engine.swap_leaves(1, 999);

    CHECK_FALSE(swapped);
  }

  TEST_CASE("move_leaf_to_cell returns false for an invalid target cluster") {
    Engine engine = create_test_engine();

    bool moved = engine.move_leaf_to_cell(1, 9, 0);

    CHECK_FALSE(moved);
  }
}

// =============================================================================
// Engine::compute_geometries Tests
// =============================================================================

TEST_SUITE("Engine::compute_geometries") {
  TEST_CASE("returns correct cluster count") {
    Engine engine = create_test_engine();
    auto geoms = engine.compute_geometries(10.0f, 10.0f, 0.0f);

    CHECK(geoms.size() == 2);
  }

  TEST_CASE("returns correct cell count per cluster") {
    Engine engine = create_test_engine();
    auto geoms = engine.compute_geometries(10.0f, 10.0f, 0.0f);

    // Cluster 0: 3 nodes (parent + 2 leaves)
    CHECK(geoms[0].size() == 3);
    // Cluster 1: 1 node
    CHECK(geoms[1].size() == 1);
  }

  TEST_CASE("applies horizontal and vertical gaps") {
    Engine engine = create_single_cluster_engine();
    auto geoms_with_gaps = engine.compute_geometries(20.0f, 30.0f, 0.0f);
    auto geoms_no_gaps = engine.compute_geometries(0.0f, 0.0f, 0.0f);

    // With gaps, the cell should be smaller
    CHECK(geoms_with_gaps[0][0].width < geoms_no_gaps[0][0].width);
    CHECK(geoms_with_gaps[0][0].height < geoms_no_gaps[0][0].height);
  }

  TEST_CASE("applies per-cluster gap and zen settings") {
    Engine engine = create_test_engine();
    engine.system.clusters[1].zen_cell_index = 0;

    std::vector<ClusterTilingOptions> cluster_options(2);
    cluster_options[0].gapOptions.horizontal = 0.0f;
    cluster_options[0].gapOptions.vertical = 0.0f;
    cluster_options[0].zen_percentage = 0.90f;
    cluster_options[1].gapOptions.horizontal = 20.0f;
    cluster_options[1].gapOptions.vertical = 20.0f;
    cluster_options[1].zen_percentage = 0.50f;

    auto geoms = engine.compute_geometries(cluster_options);

    REQUIRE(geoms.size() == 2);
    REQUIRE(geoms[0].size() == 3);
    REQUIRE(geoms[1].size() == 1);
    CHECK(geoms[0][0].x == doctest::Approx(0.0f));
    CHECK(geoms[0][0].width == doctest::Approx(800.0f));
    CHECK(geoms[1][0].x == doctest::Approx(1000.0f));
    CHECK(geoms[1][0].width == doctest::Approx(400.0f));
  }

  TEST_CASE("zero gaps produce full-size cells") {
    Engine engine = create_single_cluster_engine();
    auto geoms = engine.compute_geometries(0.0f, 0.0f, 0.0f);

    // Single cell should fill the entire cluster
    CHECK(geoms[0][0].width == 800.0f);
    CHECK(geoms[0][0].height == 600.0f);
  }

  TEST_CASE("empty system returns empty geometries") {
    Engine engine = create_empty_engine();
    auto geoms = engine.compute_geometries(10.0f, 10.0f, 0.0f);

    CHECK(geoms.empty());
  }
}

// =============================================================================
// Engine::get_hover_info Tests
// =============================================================================

TEST_SUITE("Engine::get_hover_info") {
  TEST_CASE("returns cluster when over empty area") {
    // Create a cluster with window in only part of the area
    Engine engine = create_single_cluster_engine();
    auto geoms = compute_default_geometries(engine);

    // Point inside cluster bounds
    HoverInfo info = engine.get_hover_info(100.0f, 100.0f, geoms);

    CHECK(info.cluster_index.has_value());
    CHECK(*info.cluster_index == 0);
  }

  TEST_CASE("returns cell when over leaf") {
    Engine engine = create_single_cluster_engine();
    auto geoms = compute_default_geometries(engine);

    // Get the cell rect and hit inside it
    const auto& rect = geoms[0][0];
    float center_x = rect.x + rect.width / 2;
    float center_y = rect.y + rect.height / 2;

    HoverInfo info = engine.get_hover_info(center_x, center_y, geoms);

    CHECK(info.cluster_index.has_value());
    CHECK(info.cell.has_value());
    CHECK(info.cell->cluster_index == 0);
    CHECK(info.cell->cell_index == 0);
  }

  TEST_CASE("returns nullopt when outside all clusters") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    // Point far outside both clusters
    HoverInfo info = engine.get_hover_info(-100.0f, -100.0f, geoms);

    CHECK_FALSE(info.cluster_index.has_value());
    CHECK_FALSE(info.cell.has_value());
  }

  TEST_CASE("handles multiple clusters") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    // Point in cluster 1 (starts at x=800)
    HoverInfo info = engine.get_hover_info(900.0f, 300.0f, geoms);

    CHECK(info.cluster_index.has_value());
    CHECK(*info.cluster_index == 1);
  }

  TEST_CASE("returns cluster for taskbar area outside the work area") {
    Engine engine = create_test_engine_with_secondary_taskbar();
    auto geoms = compute_default_geometries(engine);

    HoverInfo info = engine.get_hover_info(900.0f, 580.0f, geoms);

    CHECK(info.cluster_index.has_value());
    CHECK(*info.cluster_index == 1);
    CHECK_FALSE(info.cell.has_value());
  }

  TEST_CASE("zen hover prefers zen cell over overlapped background cell") {
    Engine engine = create_two_window_engine();
    auto geoms = engine.compute_geometries(10.0f, 10.0f, 0.90f);

    engine.system.clusters[0].zen_cell_index = 1;

    geoms = engine.compute_geometries(10.0f, 10.0f, 0.90f);

    const Rect& zen_rect = geoms[0][1];
    const Rect& other_rect = geoms[0][2];

    float overlap_left = std::max(zen_rect.x, other_rect.x);
    float overlap_top = std::max(zen_rect.y, other_rect.y);
    float overlap_right = std::min(zen_rect.x + zen_rect.width, other_rect.x + other_rect.width);
    float overlap_bottom = std::min(zen_rect.y + zen_rect.height, other_rect.y + other_rect.height);

    REQUIRE(overlap_left < overlap_right);
    REQUIRE(overlap_top < overlap_bottom);

    float hover_x = overlap_left + (overlap_right - overlap_left) / 2.0f;
    float hover_y = overlap_top + (overlap_bottom - overlap_top) / 2.0f;

    HoverInfo info = engine.get_hover_info(hover_x, hover_y, geoms);

    REQUIRE(info.cell.has_value());
    CHECK(info.cell->cluster_index == 0);
    CHECK(info.cell->cell_index == 1);
  }
}

// =============================================================================
// Engine::update_selection_from_hover Tests
// =============================================================================

TEST_SUITE("Engine::update_selection_from_hover") {
  TEST_CASE("changes selection when hovering a different cell") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);
    const auto& target_rect = geoms[0][2];
    float hover_x = target_rect.x + target_rect.width / 2.0f;
    float hover_y = target_rect.y + target_rect.height / 2.0f;

    HoverSelectionResult result = engine.update_selection_from_hover(hover_x, hover_y, geoms);

    CHECK(result.selection_changed == true);
    REQUIRE(engine.system.selection.has_value());
    CHECK(engine.system.selection->cluster_index == 0);
    CHECK(engine.system.selection->cell_index == 2);
  }

  TEST_CASE("does nothing when hovering outside all cells") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);
    set_selection(engine, 0, 1);

    HoverSelectionResult result = engine.update_selection_from_hover(-100.0f, -100.0f, geoms);

    CHECK(result.selection_changed == false);
    REQUIRE(engine.system.selection.has_value());
    CHECK(engine.system.selection->cluster_index == 0);
    CHECK(engine.system.selection->cell_index == 1);
  }
}

// =============================================================================
// Engine::update Tests
// =============================================================================

TEST_SUITE("Engine::update") {
  TEST_CASE("returns topology_changed when changes applied") {
    Engine engine = create_test_engine();

    // Add a new window to cluster 0
    std::vector<ClusterCellUpdateInfo> updates = {
        {{1, 2, 4}, false}, // Cluster 0: added window 4
        {{3}, false}        // Cluster 1: unchanged
    };

    UpdateResult result = engine.update(updates);
    CHECK(result.topology_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
  }

  TEST_CASE("returns no-op result when no changes") {
    Engine engine = create_test_engine();

    // Same windows as initial state
    std::vector<ClusterCellUpdateInfo> updates = {
        {{1, 2}, false}, // Cluster 0: same
        {{3}, false}     // Cluster 1: same
    };

    UpdateResult result = engine.update(updates);
    CHECK(result.topology_changed == false);
    CHECK(result.selection_changed == false);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
  }

  TEST_CASE("reports selection change when update retargets selection") {
    Engine engine = create_test_engine();
    set_selection(engine, 0, 1);

    std::vector<ClusterCellUpdateInfo> updates = {
        {{1, 2, 4}, false}, // Cluster 0: added window 4
        {{3}, false}        // Cluster 1: unchanged
    };

    UpdateResult result = engine.update(updates);
    CHECK(result.topology_changed == true);
    CHECK(result.selection_changed == true);
  }

  TEST_CASE("updates fullscreen state") {
    Engine engine = create_test_engine();

    std::vector<ClusterCellUpdateInfo> updates = {{{1, 2}, true}, // Cluster 0: has fullscreen
                                                  {{3}, false}};

    UpdateResult result = engine.update(updates);
    CHECK(result.topology_changed == false);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(engine.system.clusters[0].has_fullscreen_cell == true);
    CHECK(engine.system.clusters[1].has_fullscreen_cell == false);
  }

  TEST_CASE("can move a managed window into an empty cluster via update") {
    Engine engine = create_engine_with_empty_second_cluster();
    REQUIRE(engine.find_leaf(1).has_value());
    CHECK(engine.system.clusters[1].tree.empty());

    std::vector<ClusterCellUpdateInfo> updates = {
        {{2}, false},
        {{1}, false},
    };

    UpdateResult result = engine.update(updates);

    CHECK(result.topology_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);

    auto moved_leaf = engine.find_leaf(1);
    auto remaining_leaf = engine.find_leaf(2);
    REQUIRE(moved_leaf.has_value());
    REQUIRE(remaining_leaf.has_value());
    CHECK(moved_leaf->cluster_index == 1);
    CHECK(remaining_leaf->cluster_index == 0);
    CHECK_FALSE(engine.system.clusters[1].tree.empty());
  }

  TEST_CASE("applies configured layout rule when topology changes") {
    Engine engine = create_single_cluster_engine();
    LayoutOptions layout_options = create_two_window_vertical_layout_options(0.30f);

    std::vector<ClusterCellUpdateInfo> updates = {{{1, 2}, false}};

    UpdateResult result = engine.update(updates, std::nullopt, &layout_options);

    CHECK(result.topology_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);

    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 3);
    CHECK(cluster.tree[0].split_dir == SplitDir::Vertical);
    CHECK(cluster.tree[0].split_ratio == doctest::Approx(0.30f));
  }

  TEST_CASE("layout rule rebuild preserves existing leaf order before appending new windows") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {40, 30, 10, 20}}};
    engine.init(infos);

    LayoutOptions layout_options = create_three_window_vertical_right_horizontal_layout_options();
    std::vector<ClusterCellUpdateInfo> updates = {{{10, 20, 30}, false}};

    UpdateResult result = engine.update(updates, std::nullopt, &layout_options);

    CHECK(result.topology_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);

    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 5);

    auto left_child = cluster.tree.get_first_child(0);
    auto right_child = cluster.tree.get_second_child(0);
    REQUIRE(left_child.has_value());
    REQUIRE(right_child.has_value());
    REQUIRE(cluster.tree[*left_child].leaf_id.has_value());
    CHECK(*cluster.tree[*left_child].leaf_id == 30);

    auto right_top_child = cluster.tree.get_first_child(*right_child);
    auto right_bottom_child = cluster.tree.get_second_child(*right_child);
    REQUIRE(right_top_child.has_value());
    REQUIRE(right_bottom_child.has_value());
    REQUIRE(cluster.tree[*right_top_child].leaf_id.has_value());
    REQUIRE(cluster.tree[*right_bottom_child].leaf_id.has_value());
    CHECK(*cluster.tree[*right_top_child].leaf_id == 10);
    CHECK(*cluster.tree[*right_bottom_child].leaf_id == 20);
  }
}

// =============================================================================
// Engine::process_frame Tests
// =============================================================================

TEST_SUITE("Engine::process_frame") {
  TEST_CASE("reapplies configured layout templates when requested") {
    Engine engine = create_two_window_engine();
    LayoutOptions layout_options = create_two_window_vertical_layout_options(0.30f);

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.reapply_layout_templates = true;
    input.layout_options = &layout_options;
    input.gap_h = 0.0f;
    input.gap_v = 0.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    CHECK(engine.system.clusters[0].tree[0].split_ratio == doctest::Approx(0.30f));
  }

  TEST_CASE("reapplies per-cluster layout templates when requested") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 800.0f, 600.0f, {1, 2}},
        {800.0f, 0.0f, 800.0f, 600.0f, 800.0f, 0.0f, 800.0f, 600.0f, {3, 4}}};
    engine.init(infos);

    std::vector<ClusterTilingOptions> cluster_options(2);
    cluster_options[0].layoutOptions = create_two_window_vertical_layout_options(0.30f);
    cluster_options[1].layoutOptions = create_two_window_vertical_layout_options(0.70f);

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.reapply_layout_templates = true;
    input.cluster_options = cluster_options;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    CHECK(engine.system.clusters[0].tree[0].split_ratio == doctest::Approx(0.30f));
    CHECK(engine.system.clusters[1].tree[0].split_ratio == doctest::Approx(0.70f));
  }

  TEST_CASE("exit hotkey returns loop control and final geometries") {
    Engine engine = create_test_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.hotkey_action = HotkeyAction::Exit;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Exit);
    CHECK(output.geometries.size() == engine.system.clusters.size());
  }

  TEST_CASE("topology update returns final geometry and cursor position") {
    Engine engine = create_test_engine();

    EngineFrameInput input;
    input.cluster_updates = {{{1, 2, 4}, false}, {{3}, false}};
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.topology_changed == true);
    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    CHECK(output.cursor_pos.has_value());
    REQUIRE(output.geometries.size() == 2);
    CHECK(output.geometries[0].size() ==
          static_cast<size_t>(engine.system.clusters[0].tree.size()));
  }

  TEST_CASE("navigation hotkey returns focus and cursor from single frame call") {
    Engine engine = create_two_window_engine();
    set_selection(engine, 0, 1);

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.hotkey_action = HotkeyAction::NavigateRight;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.selection_changed == true);
    CHECK(output.focus_leaf_id.has_value());
    CHECK(output.cursor_pos.has_value());
  }

  TEST_CASE("dump hotkey returns a one-shot window management dump request") {
    Engine engine = create_test_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.hotkey_action = HotkeyAction::DumpWindowManagement;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.dump_window_management == true);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == false);
    CHECK_FALSE(output.toast_message.has_value());
  }

  TEST_CASE("restart hotkey returns a one-shot system restart request") {
    Engine engine = create_test_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.hotkey_action = HotkeyAction::RestartSystem;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.restart_system == true);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == false);
    REQUIRE(output.toast_message.has_value());
    CHECK(*output.toast_message == "System restarted");
  }

  TEST_CASE("hover selects and focuses a window inside process_frame") {
    Engine engine = create_two_window_engine();
    set_selection(engine, 0, 1);
    auto geoms = compute_default_geometries(engine);
    const auto& target_rect = geoms[0][2];

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.pointer_window_id = 2;
    input.cursor_pos = ctrl::Point{static_cast<long>(target_rect.x + target_rect.width / 2.0f),
                                   static_cast<long>(target_rect.y + target_rect.height / 2.0f)};
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.selection_changed == true);
    REQUIRE(engine.system.selection.has_value());
    CHECK(engine.system.selection->cell_index == 2);
    CHECK(output.focus_leaf_id == 2);
    CHECK(engine.system.focused_leaf_id == 2);
  }

  TEST_CASE("mouse movement focuses across monitors and within the selected tile") {
    Engine engine = create_test_engine();
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.foreground_leaf_id = 1;
    input.pointer_window_id = 1;
    input.cursor_pos = ctrl::Point{100, 100};
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());

    input.cursor_pos = ctrl::Point{1000, 100};
    input.pointer_window_id = 3;
    CHECK(engine.process_frame(input).focus_leaf_id == 3);
    CHECK(engine.selected_leaf_id() == 3);

    // Alt+Tab can change focus while the mouse stays on the same selected tile.
    input.foreground_leaf_id = 2;
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    input.cursor_pos->x += 1;
    const auto output = engine.process_frame(input);
    CHECK_FALSE(output.selection_changed);
    CHECK(output.focus_leaf_id == 3);
    CHECK(engine.system.focused_leaf_id == 3);

    input.foreground_leaf_id = 3;
    input.cursor_pos->x += 1;
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
  }

  TEST_CASE("hover focus respects disabled hover and non-window areas") {
    Engine engine = create_test_engine_with_secondary_taskbar();
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.foreground_leaf_id = 1;
    input.pointer_window_id = 3;
    input.cursor_pos = ctrl::Point{1000, 100};
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    SUBCASE("hover disabled") { input.update_hover_selection = false; }
    SUBCASE("taskbar") { input.cursor_pos = ctrl::Point{1000, 580}; }
    SUBCASE("gap") { input.cursor_pos = ctrl::Point{0, 0}; }
    SUBCASE("outside monitors") { input.cursor_pos = ctrl::Point{-100, -100}; }
    SUBCASE("missing cursor") { input.cursor_pos.reset(); }
    SUBCASE("missing window under pointer") { input.pointer_window_id.reset(); }
    SUBCASE("floating window or menu covers tile") { input.pointer_window_id = 99; }
    SUBCASE("another tiled window covers tile") { input.pointer_window_id = 2; }
    SUBCASE("fullscreen monitor") {
      engine.system.clusters[1].has_fullscreen_cell = true;
      input.cluster_updates[1].has_fullscreen_cell = true;
    }

    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
  }

  TEST_CASE("an untiled foreground dialog protects focus until closed or switched away") {
    Engine engine = create_two_window_engine();
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.foreground_leaf_id = 1;
    input.cursor_pos = ctrl::Point{100, 100};
    input.pointer_window_id = 1;
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());

    input.foreground_leaf_id = 99;
    input.foreground_is_dialog = true;
    input.cursor_pos->x += 1;
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    CHECK(engine.system.focused_leaf_id == 1);

    const auto geoms = compute_default_geometries(engine);
    const auto& other = geoms[0][2];
    input.cursor_pos = compute_rect_center(other);
    input.pointer_window_id = 2;
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    input.pointer_window_id = 99; // The pointer reaches the dialog itself.
    input.cursor_pos->x += 1;
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());

    SUBCASE("dialog closes") { input.foreground_leaf_id = 1; }
    SUBCASE("user switches to an unmanaged ordinary window") { input.foreground_leaf_id = 100; }
    input.foreground_is_dialog = false;
    input.pointer_window_id = 2;
    CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value()); // Still idle.
    input.cursor_pos->x += 1;
    CHECK(engine.process_frame(input).focus_leaf_id == 2);
  }

  TEST_CASE("hover redirects a disabled owner to its blocking dialog") {
    Engine engine = create_two_window_engine();
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.foreground_leaf_id = 1;
    input.pointer_window_id = 2;
    input.pointer_window_enabled = false;
    input.pointer_blocking_dialog_id = 99;
    input.cursor_pos = compute_rect_center(compute_default_geometries(engine)[0][2]);

    SUBCASE("blocking dialog receives focus without becoming a tiled leaf") {
      CHECK(engine.process_frame(input).focus_leaf_id == 99);
      CHECK_FALSE(engine.find_leaf(99).has_value());
      CHECK(engine.system.focused_leaf_id == 1);
      input.foreground_leaf_id = 99;
      input.foreground_is_dialog = true;
      input.cursor_pos->x += 1;
      CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    }
    SUBCASE("disabled owner without a usable dialog is not activated") {
      input.pointer_blocking_dialog_id.reset();
      CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    }
    SUBCASE("enabled owner is focused normally") {
      input.pointer_window_enabled = true;
      CHECK(engine.process_frame(input).focus_leaf_id == 2);
    }
    SUBCASE("covering unmanaged window still prevents hover activation") {
      input.pointer_window_id = 100;
      CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    }
  }

  TEST_CASE("dialog protection leaves explicit navigation and managed dialogs usable") {
    Engine engine = create_two_window_engine();
    set_selection(engine, 0, 1);
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.foreground_is_dialog = true;
    input.foreground_leaf_id = 99;
    input.pointer_window_id = 1;
    input.cursor_pos = ctrl::Point{100, 100};

    SUBCASE("keyboard navigation can leave an untiled dialog") {
      input.hotkey_action = HotkeyAction::NavigateRight;
      CHECK(engine.process_frame(input).focus_leaf_id == 2);
    }
    SUBCASE("a tiled dialog does not suspend ordinary hover focus") {
      input.foreground_leaf_id = 2;
      CHECK(engine.process_frame(input).focus_leaf_id == 1);
    }
  }

  TEST_CASE("hover focus respects zen and explicit frame actions") {
    Engine engine = create_two_window_engine();
    set_selection(engine, 0, 1);
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.foreground_leaf_id = 1;
    input.pointer_window_id = 2;
    input.cursor_pos = ctrl::Point{600, 300};

    SUBCASE("zen window takes precedence over background tile") {
      engine.system.clusters[0].zen_cell_index = 1;
      input.zen_pct = 0.90f;
      input.foreground_leaf_id = 2;
      input.pointer_window_id = 1;
      CHECK(engine.process_frame(input).focus_leaf_id == 1);
    }
    SUBCASE("navigation takes precedence over hovered window") {
      input.cursor_pos = ctrl::Point{100, 100};
      input.pointer_window_id = 1;
      input.hotkey_action = HotkeyAction::NavigateRight;
      const auto output = engine.process_frame(input);
      CHECK(output.focus_leaf_id == 2);
      REQUIRE(output.cursor_pos.has_value());
      input.cursor_pos = output.cursor_pos;
      input.pointer_window_id = 2;
      input.hotkey_action.reset();
      // Even if focus has not caught up, our own warp must not request focus again.
      CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    }
    SUBCASE("new window keeps foreground focus during topology update") {
      input.cluster_updates = {{{1, 2, 4}, false}};
      input.foreground_leaf_id = 4;
      const auto output = engine.process_frame(input);
      CHECK(output.topology_changed);
      CHECK_FALSE(output.focus_leaf_id.has_value());
      REQUIRE(output.cursor_pos.has_value());
      input.cursor_pos = output.cursor_pos;
      CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    }
    SUBCASE("completed drag does not focus the hovered window") {
      CompletedDragRequest drag;
      drag.leaf_id = 1;
      drag.cursor_pos = input.cursor_pos;
      input.completed_drag = drag;
      CHECK_FALSE(engine.process_frame(input).focus_leaf_id.has_value());
    }
  }

  TEST_CASE("redirects new windows to the hovered monitor taskbar area") {
    Engine engine = create_test_engine_with_secondary_taskbar();
    set_selection(engine, 0, 1);

    EngineFrameInput input;
    input.cluster_updates = {{{1, 2, 4}, false}, {{3}, false}};
    input.cursor_pos = ctrl::Point{900, 580};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.topology_changed == true);
    auto new_leaf = engine.find_leaf(4);
    REQUIRE(new_leaf.has_value());
    CHECK(new_leaf->cluster_index == 1);
  }

  TEST_CASE("keeps newly observed fullscreen windows on their reported monitor") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 800.0f, 560.0f, 0.0f, 0.0f, 800.0f, 600.0f, {10}},
        {800.0f, 0.0f, 800.0f, 560.0f, 800.0f, 0.0f, 800.0f, 600.0f, {20}}};
    engine.init(infos);
    set_selection(engine, 1, 0);

    EngineFrameInput input;
    input.cluster_updates = {{{30}, true}, {{20}, false}};
    input.cursor_pos = ctrl::Point{900, 300};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.topology_changed == true);
    auto fullscreen_leaf = engine.find_leaf(30);
    auto right_leaf = engine.find_leaf(20);
    REQUIRE(fullscreen_leaf.has_value());
    REQUIRE(right_leaf.has_value());
    CHECK(fullscreen_leaf->cluster_index == 0);
    CHECK(right_leaf->cluster_index == 1);
    CHECK(engine.system.clusters[0].has_fullscreen_cell == true);
    CHECK(engine.system.clusters[1].has_fullscreen_cell == false);
  }

  TEST_CASE("completed drag move is handled inside process_frame") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    size_t source_leaf_id = *engine.system.clusters[0].tree[1].leaf_id;
    const auto& target_rect = geoms[0][2];

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.completed_drag = CompletedDragRequest{
        source_leaf_id,
        ctrl::Point{static_cast<long>(target_rect.x + target_rect.width / 2.0f),
                    static_cast<long>(target_rect.y + target_rect.height / 2.0f)},
        std::nullopt, false};

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.clear_drag_ended == true);
    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    REQUIRE(output.cursor_pos.has_value());
    auto expected_center =
        get_leaf_center_from_geometries(engine, output.geometries, source_leaf_id);
    REQUIRE(expected_center.has_value());
    CHECK(output.cursor_pos->x == expected_center->x);
    CHECK(output.cursor_pos->y == expected_center->y);
  }

  TEST_CASE("completed drag from single-window monitor honors the hovered target cell") {
    Engine engine = create_test_engine();
    set_selection(engine, 1, 0);
    auto geoms = compute_default_geometries(engine);

    size_t source_leaf_id = 3;
    const auto& target_rect = geoms[0][2];

    EngineFrameInput input;
    input.cluster_updates = {{{1, 2, 3}, false}, {{}, false}};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.completed_drag = CompletedDragRequest{
        source_leaf_id,
        ctrl::Point{static_cast<long>(target_rect.x + target_rect.width / 2.0f),
                    static_cast<long>(target_rect.y + target_rect.height / 2.0f)},
        std::nullopt, false};

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.clear_drag_ended == true);
    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);

    auto moved_leaf = engine.find_leaf(source_leaf_id);
    REQUIRE(moved_leaf.has_value());
    CHECK(moved_leaf->cluster_index == 0);
    const auto& moved_rect = output.geometries[static_cast<size_t>(moved_leaf->cluster_index)]
                                              [static_cast<size_t>(moved_leaf->cell_index)];
    CHECK(moved_rect.x == doctest::Approx(target_rect.x));
    CHECK(moved_rect.width == doctest::Approx(target_rect.width));
    CHECK(engine.system.clusters[1].tree.empty());
  }

  TEST_CASE("completed drag split reapplies per-cluster layout rule") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    size_t source_leaf_id = 3;
    const auto& target_rect = geoms[0][2];

    std::vector<ClusterTilingOptions> cluster_options(2);
    cluster_options[0].layoutOptions =
        create_three_window_vertical_right_horizontal_layout_options();

    EngineFrameInput input;
    input.cluster_updates = {{{1, 2, 3}, false}, {{}, false}};
    input.has_completed_initial_tile_pass = true;
    input.cluster_options = cluster_options;
    input.completed_drag = CompletedDragRequest{
        source_leaf_id,
        ctrl::Point{static_cast<long>(target_rect.x + target_rect.width / 2.0f),
                    static_cast<long>(target_rect.y + target_rect.height / 2.0f)},
        std::nullopt, false};

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.clear_drag_ended == true);
    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);

    auto moved_leaf = engine.find_leaf(source_leaf_id);
    REQUIRE(moved_leaf.has_value());
    CHECK(moved_leaf->cluster_index == 0);

    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 5);
    auto right_child = cluster.tree.get_second_child(0);
    REQUIRE(right_child.has_value());
    CHECK(cluster.tree[0].split_dir == SplitDir::Vertical);
    CHECK(cluster.tree[*right_child].split_dir == SplitDir::Horizontal);
  }

  TEST_CASE("completed drag exchange across clusters is preserved until the next frame") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    size_t source_leaf_id = *engine.system.clusters[0].tree[1].leaf_id;
    size_t target_leaf_id = *engine.system.clusters[1].tree[0].leaf_id;
    const auto& target_rect = geoms[1][0];

    EngineFrameInput input;
    input.cluster_updates = {
        {{2}, false},
        {{1, 3}, false},
    };
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.completed_drag = CompletedDragRequest{
        source_leaf_id,
        ctrl::Point{static_cast<long>(target_rect.x + target_rect.width / 2.0f),
                    static_cast<long>(target_rect.y + target_rect.height / 2.0f)},
        std::nullopt, true};

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.clear_drag_ended == true);
    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);

    auto moved_source = engine.find_leaf(source_leaf_id);
    auto moved_target = engine.find_leaf(target_leaf_id);
    REQUIRE(moved_source.has_value());
    REQUIRE(moved_target.has_value());
    CHECK(moved_source->cluster_index == 1);
    CHECK(moved_target->cluster_index == 0);
  }

  TEST_CASE("completed drag resize is handled inside process_frame") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);
    size_t source_leaf_id = *engine.system.clusters[0].tree[1].leaf_id;
    float original_ratio = engine.system.clusters[0].tree[0].split_ratio;

    CompletedDragRequest request;
    request.leaf_id = source_leaf_id;
    request.actual_window_rect = geoms[0][1];
    request.actual_window_rect->width += 30.0f;

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.completed_drag = request;
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.clear_drag_ended == true);
    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    CHECK(engine.system.clusters[0].tree[0].split_ratio != original_ratio);
  }

  TEST_CASE("completed drag without valid drop target reapplies current tiles") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);
    size_t source_leaf_id = *engine.system.clusters[0].tree[2].leaf_id;
    const auto& source_rect = geoms[0][2];

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.completed_drag =
        CompletedDragRequest{source_leaf_id, ctrl::Point{-100, -100},
                             ctrl::Rect{source_rect.x + 120.0f, source_rect.y + 40.0f,
                                        source_rect.width, source_rect.height},
                             false};

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.clear_drag_ended == true);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == true);
    CHECK(geometries_equal(output.geometries, geoms));
  }

  TEST_CASE("unmanaged completed drag still clears drag state") {
    Engine engine = create_test_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.completed_drag = CompletedDragRequest{999, std::nullopt, std::nullopt, false};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.clear_drag_ended == true);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == false);
  }

  TEST_CASE("auto zen waits for initial tile pass inside process_frame") {
    Engine engine = create_two_window_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{{1, false, true}}};
    input.auto_zen_on_maximize = true;
    input.has_completed_initial_tile_pass = false;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.zen_pct = 0.90f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.has_completed_initial_tile_pass == true);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == true);
    CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());

    input.has_completed_initial_tile_pass = true;
    EngineFrameOutput steady_output = engine.process_frame(input);

    CHECK(steady_output.layout_changed == false);
    CHECK(steady_output.apply_tiles == false);
    CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());
  }

  TEST_CASE("auto zen is applied inside process_frame") {
    Engine engine = create_two_window_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{{1, false, true}}};
    input.auto_zen_on_maximize = true;
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.zen_pct = 0.90f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    CHECK(output.has_completed_initial_tile_pass == true);
    CHECK(output.focus_leaf_id == std::optional<size_t>{1});
    REQUIRE(engine.system.clusters[0].zen_cell_index.has_value());
    CHECK(*engine.system.clusters[0].zen_cell_index == 1);
  }

  TEST_CASE("auto zen remains after tiling clears the maximized flag on the next frame") {
    Engine engine = create_two_window_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{{1, false, true}}};
    input.auto_zen_on_maximize = true;
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.zen_pct = 0.90f;

    EngineFrameOutput first_output = engine.process_frame(input);

    CHECK(first_output.layout_changed == true);
    CHECK(first_output.apply_tiles == true);
    REQUIRE(engine.system.clusters[0].zen_cell_index.has_value());
    CHECK(*engine.system.clusters[0].zen_cell_index == 1);

    input.managed_windows = {{{1, false, false}}};

    EngineFrameOutput second_output = engine.process_frame(input);

    CHECK(second_output.layout_changed == false);
    CHECK(second_output.apply_tiles == false);
    REQUIRE(engine.system.clusters[0].zen_cell_index.has_value());
    CHECK(*engine.system.clusters[0].zen_cell_index == 1);
  }

  TEST_CASE("a new maximize edge toggles zen back off") {
    Engine engine = create_two_window_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{{1, false, true}}};
    input.auto_zen_on_maximize = true;
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.zen_pct = 0.90f;

    EngineFrameOutput maximized_output = engine.process_frame(input);

    CHECK(maximized_output.layout_changed == true);
    CHECK(maximized_output.apply_tiles == true);
    REQUIRE(engine.system.clusters[0].zen_cell_index.has_value());
    CHECK(*engine.system.clusters[0].zen_cell_index == 1);

    // Simulate the real loop: tiling restores the maximized window to normal state.
    input.managed_windows = {{{1, false, false}}};
    EngineFrameOutput steady_output = engine.process_frame(input);

    CHECK(steady_output.layout_changed == false);
    CHECK(steady_output.apply_tiles == false);
    REQUIRE(engine.system.clusters[0].zen_cell_index.has_value());
    CHECK(*engine.system.clusters[0].zen_cell_index == 1);

    // A later maximize of the same window is a fresh edge and toggles zen back off.
    input.managed_windows = {{{1, false, true}}};
    EngineFrameOutput toggled_off_output = engine.process_frame(input);

    CHECK(toggled_off_output.layout_changed == true);
    CHECK(toggled_off_output.apply_tiles == true);
    CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());
  }

  TEST_CASE("returned geometries match final engine state after multiple layout mutations") {
    Engine engine = create_test_engine();
    set_selection(engine, 0, 1);

    EngineFrameInput input;
    input.cluster_updates = {{{1, 2, 4}, false}, {{3}, false}};
    input.managed_windows = {{{1, false, true}}};
    input.hotkey_action = HotkeyAction::ToggleSplit;
    input.auto_zen_on_maximize = true;
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;
    input.zen_pct = 0.90f;

    EngineFrameOutput output = engine.process_frame(input);
    auto expected_geometries = engine.compute_geometries(10.0f, 10.0f, 0.90f);

    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    CHECK(geometries_equal(output.geometries, expected_geometries));
  }

  TEST_CASE("first frame requests initial tile apply") {
    Engine engine = create_test_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == true);
    CHECK(output.has_completed_initial_tile_pass == true);
    CHECK(output.geometries.size() == engine.system.clusters.size());
  }

  TEST_CASE("steady frame after initial pass does not request tile apply") {
    Engine engine = create_test_engine();

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == false);
    CHECK(output.has_completed_initial_tile_pass == true);
    CHECK(output.geometries.size() == engine.system.clusters.size());
    CHECK(output.placement_correction_leaf_ids.empty());
  }

  TEST_CASE("steady frame requests targeted placement correction for mismatched window") {
    Engine engine = create_test_engine();
    auto expected_geometries = compute_default_geometries(engine);
    auto leaf_cell = engine.find_leaf(1);
    REQUIRE(leaf_cell.has_value());
    const auto& target_rect = expected_geometries[static_cast<size_t>(leaf_cell->cluster_index)]
                                                 [static_cast<size_t>(leaf_cell->cell_index)];

    ManagedWindowState mismatched_window;
    mismatched_window.leaf_id = 1;
    mismatched_window.actual_rect =
        Rect{target_rect.x + 20.0f, target_rect.y, target_rect.width, target_rect.height};

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{mismatched_window}, {}};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.layout_changed == false);
    CHECK(output.apply_tiles == false);
    REQUIRE(output.placement_correction_leaf_ids.size() == 1);
    CHECK(output.placement_correction_leaf_ids[0] == 1);
  }

  TEST_CASE("steady placement correction ignores small drift and minimized windows") {
    Engine engine = create_test_engine();
    auto expected_geometries = compute_default_geometries(engine);
    auto leaf_cell = engine.find_leaf(1);
    REQUIRE(leaf_cell.has_value());
    const auto& target_rect = expected_geometries[static_cast<size_t>(leaf_cell->cluster_index)]
                                                 [static_cast<size_t>(leaf_cell->cell_index)];

    ManagedWindowState close_window;
    close_window.leaf_id = 1;
    close_window.actual_rect =
        Rect{target_rect.x + 1.0f, target_rect.y, target_rect.width, target_rect.height};

    ManagedWindowState minimized_window;
    minimized_window.leaf_id = 2;
    minimized_window.is_minimized = true;
    minimized_window.actual_rect =
        Rect{target_rect.x + 40.0f, target_rect.y, target_rect.width, target_rect.height};

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{close_window, minimized_window}, {}};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.control == LoopControl::Continue);
    CHECK(output.apply_tiles == false);
    CHECK(output.placement_correction_leaf_ids.empty());
  }

  TEST_CASE("steady placement correction skips impossible minimum track size") {
    Engine engine = create_test_engine();
    auto expected_geometries = compute_default_geometries(engine);
    auto leaf_cell = engine.find_leaf(1);
    REQUIRE(leaf_cell.has_value());
    const auto& target_rect = expected_geometries[static_cast<size_t>(leaf_cell->cluster_index)]
                                                 [static_cast<size_t>(leaf_cell->cell_index)];

    ManagedWindowState constrained_window;
    constrained_window.leaf_id = 1;
    constrained_window.actual_rect =
        Rect{target_rect.x, target_rect.y, target_rect.width + 40.0f, target_rect.height};
    constrained_window.min_track_width = 801;
    constrained_window.min_track_height = static_cast<int>(target_rect.height);

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{constrained_window}, {}};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.apply_tiles == false);
    CHECK(output.placement_correction_leaf_ids.empty());
    CHECK(engine.placement_correction_failures.empty());
  }

  TEST_CASE("steady placement correction suppresses repeated failed target after three attempts") {
    Engine engine = create_test_engine();
    auto expected_geometries = compute_default_geometries(engine);
    auto leaf_cell = engine.find_leaf(1);
    REQUIRE(leaf_cell.has_value());
    const auto& target_rect = expected_geometries[static_cast<size_t>(leaf_cell->cluster_index)]
                                                 [static_cast<size_t>(leaf_cell->cell_index)];

    ManagedWindowState mismatched_window;
    mismatched_window.leaf_id = 1;
    mismatched_window.actual_rect =
        Rect{target_rect.x + 20.0f, target_rect.y, target_rect.width, target_rect.height};

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.managed_windows = {{mismatched_window}, {}};
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    for (int i = 0; i < 3; ++i) {
      EngineFrameOutput output = engine.process_frame(input);
      REQUIRE(output.placement_correction_leaf_ids.size() == 1);
      CHECK(output.placement_correction_leaf_ids[0] == 1);
    }

    EngineFrameOutput suppressed_output = engine.process_frame(input);
    CHECK(suppressed_output.placement_correction_leaf_ids.empty());
    REQUIRE(engine.placement_correction_failures.size() == 1);
    CHECK(engine.placement_correction_failures[0].attempts == 3);

    mismatched_window.actual_rect = target_rect;
    input.managed_windows = {{mismatched_window}, {}};
    EngineFrameOutput matched_output = engine.process_frame(input);
    CHECK(matched_output.placement_correction_leaf_ids.empty());
    CHECK(engine.placement_correction_failures.empty());

    mismatched_window.actual_rect =
        Rect{target_rect.x + 20.0f, target_rect.y, target_rect.width, target_rect.height};
    input.managed_windows = {{mismatched_window}, {}};
    EngineFrameOutput retried_output = engine.process_frame(input);
    REQUIRE(retried_output.placement_correction_leaf_ids.size() == 1);
    CHECK(retried_output.placement_correction_leaf_ids[0] == 1);
  }
}

// =============================================================================
// Engine::process_action Tests - Navigation
// =============================================================================

TEST_SUITE("Engine::process_action - Navigation") {
  TEST_CASE("navigate returns cursor position on success") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    // Select first leaf
    set_selection(engine, 0, 1);

    // Navigate to the other cell
    ActionResult result =
        engine.process_action(HotkeyAction::NavigateRight, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
    CHECK(result.cursor_pos.has_value());
    REQUIRE(result.focus_leaf_id.has_value());
    REQUIRE(engine.system.selection.has_value());
    const auto& selected_cluster =
        engine.system.clusters[static_cast<size_t>(engine.system.selection->cluster_index)];
    CHECK(*result.focus_leaf_id ==
          *selected_cluster.tree[engine.system.selection->cell_index].leaf_id);
  }

  TEST_CASE("navigate fails at boundary") {
    Engine engine = create_single_cluster_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 0);

    // Try to navigate left when there's nowhere to go
    ActionResult result =
        engine.process_action(HotkeyAction::NavigateLeft, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
    CHECK(result.selection_changed == false);
    CHECK_FALSE(result.cursor_pos.has_value());
    CHECK_FALSE(result.focus_leaf_id.has_value());
  }

  TEST_CASE("navigate works across clusters") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    // Select a cell in cluster 0
    set_selection(engine, 0, 1);

    // Navigate right should eventually reach cluster 1
    ActionResult result =
        engine.process_action(HotkeyAction::NavigateRight, geoms, 10.0f, 10.0f, 0.0f);

    // Should succeed (moves within or across clusters)
    if (result.success) {
      CHECK(result.selection_changed == true);
    }
  }
}

// =============================================================================
// Engine::process_action Tests - ToggleSplit
// =============================================================================

TEST_SUITE("Engine::process_action - ToggleSplit") {
  TEST_CASE("toggles split direction") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    // Select a leaf cell
    set_selection(engine, 0, 1);

    // Get initial split direction of parent
    SplitDir initial_dir = engine.system.clusters[0].tree[0].split_dir;

    ActionResult result =
        engine.process_action(HotkeyAction::ToggleSplit, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);

    // Split direction should have changed
    SplitDir new_dir = engine.system.clusters[0].tree[0].split_dir;
    CHECK(new_dir != initial_dir);
  }

  TEST_CASE("toggles parent split when selected leaf has sibling subtree") {
    Engine engine = create_three_window_engine();
    auto geoms = compute_default_geometries(engine);

    auto& tree = engine.system.clusters[0].tree;
    REQUIRE(tree.get_first_child(0) == std::optional<int>{1});
    REQUIRE(tree.get_second_child(0) == std::optional<int>{2});
    REQUIRE(tree.is_leaf(1));
    REQUIRE_FALSE(tree.is_leaf(2));

    set_selection(engine, 0, 1);
    SplitDir initial_parent_dir = tree[0].split_dir;
    SplitDir initial_subtree_dir = tree[2].split_dir;

    ActionResult result =
        engine.process_action(HotkeyAction::ToggleSplit, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(tree[0].split_dir != initial_parent_dir);
    CHECK(tree[2].split_dir == initial_subtree_dir);
    CHECK(tree.get_first_child(0) == std::optional<int>{1});
    CHECK(tree.get_second_child(0) == std::optional<int>{2});
  }

  TEST_CASE("toggles parent split when selected leaf follows sibling subtree") {
    Engine engine = create_three_window_engine();
    auto geoms = compute_default_geometries(engine);

    auto& tree = engine.system.clusters[0].tree;
    REQUIRE(tree.get_first_child(0) == std::optional<int>{1});
    REQUIRE(tree.get_second_child(0) == std::optional<int>{2});

    tree.swap_children(0);
    REQUIRE(tree.get_first_child(0) == std::optional<int>{2});
    REQUIRE(tree.get_second_child(0) == std::optional<int>{1});
    REQUIRE_FALSE(tree.is_leaf(2));
    REQUIRE(tree.is_leaf(1));

    set_selection(engine, 0, 1);
    SplitDir initial_parent_dir = tree[0].split_dir;
    SplitDir initial_subtree_dir = tree[2].split_dir;

    ActionResult result =
        engine.process_action(HotkeyAction::ToggleSplit, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(tree[0].split_dir != initial_parent_dir);
    CHECK(tree[2].split_dir == initial_subtree_dir);
    CHECK(tree.get_first_child(0) == std::optional<int>{2});
    CHECK(tree.get_second_child(0) == std::optional<int>{1});
  }

  TEST_CASE("returns failure when no selection") {
    Engine engine = create_empty_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result =
        engine.process_action(HotkeyAction::ToggleSplit, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
  }
}

// =============================================================================
// Engine::process_action Tests - SplitIncrease / SplitDecrease
// =============================================================================

TEST_SUITE("Engine::process_action - SplitRatio") {
  TEST_CASE("SplitIncrease adjusts ratio") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    float initial_ratio = engine.system.clusters[0].tree[0].split_ratio;

    ActionResult result =
        engine.process_action(HotkeyAction::SplitIncrease, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(result.cursor_pos.has_value());
    CHECK(result.focus_leaf_id.has_value());

    float new_ratio = engine.system.clusters[0].tree[0].split_ratio;
    // Ratio should have changed (direction depends on which child is selected)
    CHECK(new_ratio != initial_ratio);
  }

  TEST_CASE("SplitDecrease adjusts ratio") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    float initial_ratio = engine.system.clusters[0].tree[0].split_ratio;

    ActionResult result =
        engine.process_action(HotkeyAction::SplitDecrease, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);

    float new_ratio = engine.system.clusters[0].tree[0].split_ratio;
    CHECK(new_ratio != initial_ratio);
  }
}

// =============================================================================
// Engine::process_action Tests - ExchangeSiblings
// =============================================================================

TEST_SUITE("Engine::process_action - ExchangeSiblings") {
  TEST_CASE("exchanges leaf siblings structurally") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    auto& tree = engine.system.clusters[0].tree;
    REQUIRE(tree.get_first_child(0) == std::optional<int>{1});
    REQUIRE(tree.get_second_child(0) == std::optional<int>{2});
    size_t selected_leaf = *tree[1].leaf_id;
    size_t sibling_leaf = *tree[2].leaf_id;

    ActionResult result =
        engine.process_action(HotkeyAction::ExchangeSiblings, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(result.cursor_pos.has_value());
    CHECK(result.focus_leaf_id.has_value());

    CHECK(tree.get_first_child(0) == std::optional<int>{2});
    CHECK(tree.get_second_child(0) == std::optional<int>{1});
    CHECK(*tree[1].leaf_id == selected_leaf);
    CHECK(*tree[2].leaf_id == sibling_leaf);
    CHECK(result.focus_leaf_id == std::optional<size_t>{selected_leaf});
  }

  TEST_CASE("exchanges selected leaf with sibling subtree") {
    Engine engine = create_three_window_engine();
    auto geoms = compute_default_geometries(engine);

    auto& tree = engine.system.clusters[0].tree;
    REQUIRE(tree.get_first_child(0) == std::optional<int>{1});
    REQUIRE(tree.get_second_child(0) == std::optional<int>{2});
    REQUIRE(tree.is_leaf(1));
    REQUIRE_FALSE(tree.is_leaf(2));

    set_selection(engine, 0, 1);
    size_t selected_leaf = *tree[1].leaf_id;

    ActionResult result =
        engine.process_action(HotkeyAction::ExchangeSiblings, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(result.cursor_pos.has_value());
    CHECK(result.focus_leaf_id == std::optional<size_t>{selected_leaf});

    CHECK(tree.get_first_child(0) == std::optional<int>{2});
    CHECK(tree.get_second_child(0) == std::optional<int>{1});
    CHECK(tree.get_parent(1) == std::optional<int>{0});
    CHECK(tree.get_parent(2) == std::optional<int>{0});
    CHECK(tree.get_first_child(2) == std::optional<int>{3});
    CHECK(tree.get_second_child(2) == std::optional<int>{4});
    CHECK(*tree[1].leaf_id == selected_leaf);
  }

  TEST_CASE("fails when no sibling exists") {
    Engine engine = create_single_cluster_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 0);

    ActionResult result =
        engine.process_action(HotkeyAction::ExchangeSiblings, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
  }

  TEST_CASE("clears zen when swapping a zen cell with its sibling") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);
    ActionResult zen_result =
        engine.process_action(HotkeyAction::ToggleZen, geoms, 10.0f, 10.0f, 0.0f);
    REQUIRE(zen_result.success == true);
    REQUIRE(engine.system.clusters[0].zen_cell_index == std::optional<int>{1});

    ActionResult result =
        engine.process_action(HotkeyAction::ExchangeSiblings, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());
  }

  TEST_CASE("clears zen when sibling subtree contains zen cell") {
    Engine engine = create_three_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);
    engine.system.clusters[0].zen_cell_index = 3;

    ActionResult result =
        engine.process_action(HotkeyAction::ExchangeSiblings, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());
  }
}

// =============================================================================
// Engine::process_action Tests - ToggleZen
// =============================================================================

TEST_SUITE("Engine::process_action - ToggleZen") {
  TEST_CASE("enables zen mode for selected cell") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());

    ActionResult result = engine.process_action(HotkeyAction::ToggleZen, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    REQUIRE(result.focus_leaf_id.has_value());
    REQUIRE(engine.system.selection.has_value());
    const auto& selected_cluster =
        engine.system.clusters[static_cast<size_t>(engine.system.selection->cluster_index)];
    CHECK(*result.focus_leaf_id ==
          *selected_cluster.tree[engine.system.selection->cell_index].leaf_id);
    CHECK(engine.system.clusters[0].zen_cell_index.has_value());
    CHECK(*engine.system.clusters[0].zen_cell_index == 1);
  }

  TEST_CASE("disables zen mode when already zen") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    // Enable zen
    [[maybe_unused]] auto _ =
        engine.process_action(HotkeyAction::ToggleZen, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(engine.system.clusters[0].zen_cell_index.has_value());

    // Toggle again to disable
    ActionResult result = engine.process_action(HotkeyAction::ToggleZen, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());
  }
}

// =============================================================================
// Engine::process_action Tests - CycleSplitMode
// =============================================================================

TEST_SUITE("Engine::process_action - CycleSplitMode") {
  TEST_CASE("cycles through split modes") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    CHECK(engine.system.split_mode == SplitMode::Dwindle);

    ActionResult result1 =
        engine.process_action(HotkeyAction::CycleSplitMode, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(result1.success == true);
    CHECK(result1.layout_changed == false);
    CHECK(result1.apply_tiles == false);
    CHECK(result1.toast_message == std::optional<std::string>{"Split mode: Vertical"});
    CHECK(engine.system.split_mode == SplitMode::Vertical);

    ActionResult result2 =
        engine.process_action(HotkeyAction::CycleSplitMode, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(result2.success == true);
    CHECK(result2.toast_message == std::optional<std::string>{"Split mode: Horizontal"});
    CHECK(engine.system.split_mode == SplitMode::Horizontal);

    ActionResult result3 =
        engine.process_action(HotkeyAction::CycleSplitMode, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(result3.success == true);
    CHECK(result3.toast_message == std::optional<std::string>{"Split mode: Dwindle"});
    CHECK(engine.system.split_mode == SplitMode::Dwindle);
  }

  TEST_CASE("dwindle split mode uses selected cell aspect ratio") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 400.0f, 1000.0f, 0.0f, 0.0f, 400.0f, 1000.0f, {1, 2, 3}}};
    engine.init(infos, SplitMode::Dwindle);

    REQUIRE(engine.system.clusters.size() == 1);
    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 5);
    CHECK(cluster.tree[0].split_dir == SplitDir::Horizontal);
    CHECK(cluster.tree[2].split_dir == SplitDir::Horizontal);

    auto geoms = engine.compute_geometries(0.0f, 0.0f, 0.0f);
    REQUIRE(geoms.size() == 1);
    REQUIRE(geoms[0].size() == 5);
    CHECK(geoms[0][1].width == doctest::Approx(400.0f));
    CHECK(geoms[0][1].height == doctest::Approx(500.0f));
    CHECK(geoms[0][3].width == doctest::Approx(400.0f));
    CHECK(geoms[0][3].height == doctest::Approx(250.0f));
    CHECK(geoms[0][4].width == doctest::Approx(400.0f));
    CHECK(geoms[0][4].height == doctest::Approx(250.0f));
  }

  TEST_CASE("dwindle split mode splits wide selected cells vertically") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 1000.0f, 400.0f, 0.0f, 0.0f, 1000.0f, 400.0f, {1, 2, 3}}};
    engine.init(infos, SplitMode::Dwindle);

    REQUIRE(engine.system.clusters.size() == 1);
    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 5);
    CHECK(cluster.tree[0].split_dir == SplitDir::Vertical);
    CHECK(cluster.tree[2].split_dir == SplitDir::Vertical);

    auto geoms = engine.compute_geometries(0.0f, 0.0f, 0.0f);
    REQUIRE(geoms.size() == 1);
    REQUIRE(geoms[0].size() == 5);
    CHECK(geoms[0][1].width == doctest::Approx(500.0f));
    CHECK(geoms[0][1].height == doctest::Approx(400.0f));
    CHECK(geoms[0][3].width == doctest::Approx(250.0f));
    CHECK(geoms[0][3].height == doctest::Approx(400.0f));
    CHECK(geoms[0][4].width == doctest::Approx(250.0f));
    CHECK(geoms[0][4].height == doctest::Approx(400.0f));
  }

  TEST_CASE("dwindle split mode uses split width multiplier") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 1000.0f, 400.0f, 0.0f, 0.0f, 1000.0f, 400.0f, {1, 2, 3}, std::nullopt, 0.10f}};
    engine.init(infos, SplitMode::Dwindle);

    REQUIRE(engine.system.clusters.size() == 1);
    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 5);
    CHECK(cluster.split_width_multiplier == doctest::Approx(0.10f));
    CHECK(cluster.tree[0].split_dir == SplitDir::Horizontal);
    CHECK(cluster.tree[2].split_dir == SplitDir::Horizontal);
  }

  TEST_CASE("process frame applies cluster split width multiplier before insertion") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 1000.0f, 400.0f, 0.0f, 0.0f, 1000.0f, 400.0f, {1, 2}}};
    engine.init(infos, SplitMode::Dwindle);
    set_selection(engine, 0, 2);

    ClusterTilingOptions cluster_options;
    cluster_options.layoutOptions.split_width_multiplier = 0.75f;

    EngineFrameInput input;
    input.cluster_options = {cluster_options};
    input.cluster_updates = {{std::vector<size_t>{1, 2, 3}, false}};
    input.managed_windows = {{{1, false, false, false, std::nullopt},
                              {2, false, false, false, std::nullopt},
                              {3, false, false, false, std::nullopt}}};
    input.has_completed_initial_tile_pass = true;

    auto output = engine.process_frame(input);

    CHECK(output.topology_changed == true);
    REQUIRE(engine.system.clusters.size() == 1);
    const auto& cluster = engine.system.clusters[0];
    CHECK(cluster.split_width_multiplier == doctest::Approx(0.75f));
    REQUIRE(cluster.tree.size() == 5);
    CHECK(cluster.tree[2].split_dir == SplitDir::Horizontal);
  }

  TEST_CASE("dwindle uses hovered cell for new window insertion") {
    Engine engine;
    std::vector<ClusterInitInfo> infos = {
        {0.0f, 0.0f, 800.0f, 1000.0f, 0.0f, 0.0f, 800.0f, 1000.0f, {1, 2, 3}}};
    engine.init(infos, SplitMode::Dwindle);
    set_selection(engine, 0, 1);

    auto before_geoms = engine.compute_geometries(0.0f, 0.0f, 0.0f);
    REQUIRE(before_geoms.size() == 1);
    REQUIRE(before_geoms[0].size() == 5);
    const auto& hovered_rect = before_geoms[0][4];

    EngineFrameInput input;
    input.cluster_updates = {{std::vector<size_t>{1, 2, 3, 4}, false}};
    input.managed_windows = {{{1, false, false, false, std::nullopt},
                              {2, false, false, false, std::nullopt},
                              {3, false, false, false, std::nullopt},
                              {4, false, false, false, std::nullopt}}};
    input.cursor_pos = compute_rect_center(hovered_rect);
    input.has_completed_initial_tile_pass = true;

    auto output = engine.process_frame(input);

    CHECK(output.topology_changed == true);
    CHECK(output.layout_changed == true);
    CHECK(output.apply_tiles == true);
    REQUIRE(engine.system.clusters.size() == 1);
    const auto& cluster = engine.system.clusters[0];
    REQUIRE(cluster.tree.size() == 7);
    CHECK(cluster.tree[4].split_dir == SplitDir::Horizontal);

    auto new_window_cell = find_cluster_leaf_index(cluster, 4);
    REQUIRE(new_window_cell.has_value());
    CHECK(*new_window_cell == 6);
    REQUIRE(output.geometries.size() == 1);
    REQUIRE(output.geometries[0].size() == 7);
    CHECK(output.geometries[0][5].width == doctest::Approx(400.0f));
    CHECK(output.geometries[0][5].height == doctest::Approx(250.0f));
    CHECK(output.geometries[0][6].width == doctest::Approx(400.0f));
    CHECK(output.geometries[0][6].height == doctest::Approx(250.0f));
  }
}

// =============================================================================
// Engine::process_action Tests - ResetSplitRatio
// =============================================================================

TEST_SUITE("Engine::process_action - ResetSplitRatio") {
  TEST_CASE("resets ratio to 0.5") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    // First change the ratio
    [[maybe_unused]] auto _ =
        engine.process_action(HotkeyAction::SplitIncrease, geoms, 10.0f, 10.0f, 0.0f);

    float changed_ratio = engine.system.clusters[0].tree[0].split_ratio;
    CHECK(changed_ratio != 0.5f);

    // Now reset
    ActionResult result =
        engine.process_action(HotkeyAction::ResetSplitRatio, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(result.cursor_pos.has_value());
    CHECK(result.focus_leaf_id.has_value());
    CHECK(engine.system.clusters[0].tree[0].split_ratio == 0.5f);
  }
}

// =============================================================================
// Engine::process_action Tests - Exit
// =============================================================================

TEST_SUITE("Engine::process_action - Exit") {
  TEST_CASE("Exit action returns loop control") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result = engine.process_action(HotkeyAction::Exit, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.control == LoopControl::Exit);
    CHECK(result.selection_changed == false);
    CHECK_FALSE(result.cursor_pos.has_value());
  }

  TEST_CASE("TogglePause action returns loop control") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result =
        engine.process_action(HotkeyAction::TogglePause, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.control == LoopControl::EnterManualPause);
  }

  TEST_CASE("DumpWindowManagement action requests a one-shot dump without changing layout") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result =
        engine.process_action(HotkeyAction::DumpWindowManagement, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.control == LoopControl::Continue);
    CHECK(result.dump_window_management == true);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
    CHECK_FALSE(result.toast_message.has_value());
  }

  TEST_CASE("RestartSystem action requests a full system restart without changing layout") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result =
        engine.process_action(HotkeyAction::RestartSystem, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.control == LoopControl::Continue);
    CHECK(result.restart_system == true);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
    REQUIRE(result.toast_message.has_value());
    CHECK(*result.toast_message == "System restarted");
  }

  TEST_CASE("ToggleFloating action requests selected leaf floating toggle") {
    Engine engine = create_test_engine();
    auto selected_leaf_id = engine.selected_leaf_id();
    auto geoms = compute_default_geometries(engine);

    ActionResult result =
        engine.process_action(HotkeyAction::ToggleFloating, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.control == LoopControl::Continue);
    CHECK(result.toggle_floating == true);
    CHECK(result.floating_leaf_id == selected_leaf_id);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
  }

  TEST_CASE("ToggleVerboseLogging action requests runtime logging toggle") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result =
        engine.process_action(HotkeyAction::ToggleVerboseLogging, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.control == LoopControl::Continue);
    CHECK(result.toggle_verbose_logging == true);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
  }
}

// =============================================================================
// Edge Cases and Error Conditions
// =============================================================================

TEST_SUITE("Engine - Edge Cases") {
  TEST_CASE("empty system handles all actions gracefully") {
    Engine engine = create_empty_engine();
    auto geoms = compute_default_geometries(engine);

    // None of these should crash
    ActionResult r1 = engine.process_action(HotkeyAction::NavigateLeft, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(r1.success == false);

    ActionResult r2 = engine.process_action(HotkeyAction::ToggleSplit, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(r2.success == false);

    ActionResult r3 = engine.process_action(HotkeyAction::MoveLeft, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(r3.success == false);

    ActionResult r4 = engine.process_action(HotkeyAction::ToggleZen, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(r4.success == false);
  }

  TEST_CASE("actions work with zen mode active") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);
    [[maybe_unused]] auto _ =
        engine.process_action(HotkeyAction::ToggleZen, geoms, 10.0f, 10.0f, 0.0f);

    // Recompute geometries with zen percentage
    auto zen_geoms = engine.compute_geometries(10.0f, 10.0f, 0.90f);

    // Navigation should still work (only zen cell is visible)
    ActionResult result =
        engine.process_action(HotkeyAction::NavigateRight, zen_geoms, 10.0f, 10.0f, 0.90f);
    // May succeed or fail depending on geometry, but shouldn't crash
    CHECK(result.control == LoopControl::Continue);
  }

  TEST_CASE("get_hover_info with empty geometries") {
    Engine engine = create_test_engine();
    std::vector<std::vector<Rect>> empty_geoms;

    HoverInfo info = engine.get_hover_info(100.0f, 100.0f, empty_geoms);

    // Should still find cluster (based on cluster bounds, not geometry)
    CHECK(info.cluster_index.has_value());
    // But no cell (no geometry to hit)
    CHECK_FALSE(info.cell.has_value());
  }
}


TEST_SUITE("Automatic split targets") {

  TEST_CASE("largest retains the dwindle width multiplier") {
    Engine engine = create_single_cluster_engine();
    EngineFrameInput input;
    input.cluster_options.resize(1);
    input.cluster_options[0].layoutOptions.split_target = LayoutSplitTarget::Largest;
    input.cluster_options[0].layoutOptions.split_width_multiplier = 0.5f;
    input.cluster_updates = {{{1, 2}, false}};
    auto output = engine.process_frame(input);
    CHECK(output.topology_changed);
    // 800x600 would normally split left/right; scaled width 400 selects top/bottom.
    CHECK(engine.system.clusters[0].tree[0].split_dir == SplitDir::Horizontal);
  }


  TEST_CASE("largest splits the half-sized cell regardless of hovered quarter") {
    Engine engine;
    engine.init({{0, 0, 800, 1000, 0, 0, 800, 1000, {1, 2, 3}}});
    LayoutOptions options;
    options.split_target = LayoutSplitTarget::Largest;
    EngineFrameInput input;
    input.layout_options = &options;
    input.cluster_updates = {{{1, 2, 3, 4}, false}};
    input.cursor_pos = Point{700, 900};
    input.foreground_leaf_id = 3;
    auto output = engine.process_frame(input);
    CHECK(output.topology_changed);
    for (size_t id : {1u, 2u, 3u, 4u}) {
      auto cell = engine.find_leaf(id);
      REQUIRE(cell.has_value());
      const auto& rect = output.geometries[0][cell->cell_index];
      CHECK(rect.width * rect.height == doctest::Approx(200000.0f));
    }
    auto old_cell = engine.find_leaf(1);
    auto new_cell = engine.find_leaf(4);
    REQUIRE(old_cell.has_value());
    REQUIRE(new_cell.has_value());
    CHECK(engine.system.clusters[0].tree.get_parent(old_cell->cell_index) ==
          engine.system.clusters[0].tree.get_parent(new_cell->cell_index));
  }

  TEST_CASE("largest uses resized area rather than shortest branch") {
    Engine engine;
    engine.init({{0, 0, 800, 1000, 0, 0, 800, 1000, {1, 2, 3}}});
    engine.system.clusters[0].tree[0].split_ratio = 0.1f;
    LayoutOptions options;
    options.split_target = LayoutSplitTarget::Largest;
    auto result = engine.update({{{1, 2, 3, 4}, false}}, std::nullopt, &options);
    CHECK(result.topology_changed);
    auto second = engine.find_leaf(2);
    auto added = engine.find_leaf(4);
    REQUIRE(second.has_value());
    REQUIRE(added.has_value());
    CHECK(engine.system.clusters[0].tree.get_parent(second->cell_index) ==
          engine.system.clusters[0].tree.get_parent(added->cell_index));
    auto geoms = engine.compute_geometries(0, 0, 0);
    auto first = engine.find_leaf(1);
    REQUIRE(first.has_value());
    CHECK(geoms[0][first->cell_index].height == doctest::Approx(100.0f));
  }

  TEST_CASE("largest recalculates for batches and supports every split direction") {
    for (auto mode : {SplitMode::Dwindle, SplitMode::Vertical, SplitMode::Horizontal}) {
      Engine engine;
      engine.init({{0, 0, 800, 800, 0, 0, 800, 800, {1}}}, mode);
      LayoutOptions options;
      options.split_target = LayoutSplitTarget::Largest;
      auto result = engine.update({{{1, 2, 3, 4}, false}}, std::nullopt, &options);
      CHECK(result.topology_changed);
      auto geoms = engine.compute_geometries(0, 0, 0);
      for (size_t id : {1u, 2u, 3u, 4u}) {
        auto cell = engine.find_leaf(id);
        REQUIRE(cell.has_value());
        const auto& rect = geoms[0][cell->cell_index];
        CHECK(rect.width * rect.height == doctest::Approx(160000.0f));
        if (mode == SplitMode::Vertical) {
          CHECK(rect.width == doctest::Approx(200.0f));
        } else if (mode == SplitMode::Horizontal) {
          CHECK(rect.height == doctest::Approx(200.0f));
        }
      }
      // Equal halves choose the first subtree, independent of insertion selection.
      auto first = engine.find_leaf(1);
      auto third = engine.find_leaf(3);
      REQUIRE(first.has_value());
      REQUIRE(third.has_value());
      CHECK(engine.system.clusters[0].tree.get_parent(first->cell_index) ==
            engine.system.clusters[0].tree.get_parent(third->cell_index));
    }
  }

  TEST_CASE("largest startup and insertion into an empty cluster produce equal quarters") {
    for (bool startup : {false, true}) {
      ClusterInitInfo info{0, 0, 800, 800, 0, 0, 800, 800, {}};
      info.split_target = LayoutSplitTarget::Largest;
      if (startup) {
        info.initial_cell_ids = {1, 2, 3, 4};
      }
      Engine engine;
      engine.init({info});
      LayoutOptions options;
      options.split_target = LayoutSplitTarget::Largest;
      if (!startup) {
        CHECK(engine.update({{{1, 2, 3, 4}, false}}, std::nullopt, &options).topology_changed);
      }
      auto geoms = engine.compute_geometries(0, 0, 0);
      for (size_t id : {1u, 2u, 3u, 4u}) {
        auto cell = engine.find_leaf(id);
        REQUIRE(cell.has_value());
        CHECK(geoms[0][cell->cell_index].width == doctest::Approx(400.0f));
        CHECK(geoms[0][cell->cell_index].height == doctest::Approx(400.0f));
      }
    }
  }

  TEST_CASE("non-pointer targets remember tiled focus when a new window takes OS focus") {
    for (auto target : {LayoutSplitTarget::Focused, LayoutSplitTarget::Largest}) {
      Engine engine = create_test_engine();
      LayoutOptions options;
      options.split_target = target;
      EngineFrameInput input;
      input.layout_options = &options;
      input.cluster_updates = {{{1, 2}, false}, {{3}, false}};
      input.cursor_pos = Point{1200, 300};
      input.foreground_leaf_id = 1;
      input.pointer_window_id = 3;
      engine.previous_cursor_pos = input.cursor_pos; // Keyboard focus with a stationary pointer.
      auto initial = engine.process_frame(input);
      CHECK_FALSE(initial.topology_changed);
      CHECK(engine.system.focused_leaf_id == 1);
      input.foreground_leaf_id = 4;
      input.cluster_updates[1].leaf_ids.push_back(4);
      auto output = engine.process_frame(input);
      CHECK(output.topology_changed);
      auto added = engine.find_leaf(4);
      auto first = engine.find_leaf(1);
      REQUIRE(added.has_value());
      REQUIRE(first.has_value());
      CHECK(added->cluster_index == 0);
      CHECK(engine.system.clusters[0].tree.get_parent(first->cell_index) ==
            engine.system.clusters[0].tree.get_parent(added->cell_index));
      CHECK(engine.system.focused_leaf_id == 4);
    }
  }

  TEST_CASE("focused targets a small active cell even after hovering a large cell") {
    Engine engine;
    engine.init({{0, 0, 800, 1000, 0, 0, 800, 1000, {1, 2, 3}}});
    LayoutOptions options;
    options.split_target = LayoutSplitTarget::Focused;
    EngineFrameInput input;
    input.layout_options = &options;
    input.cluster_updates = {{{1, 2, 3, 4}, false}};
    input.cursor_pos = Point{10, 10};
    input.foreground_leaf_id = 3;
    auto output = engine.process_frame(input);
    auto third = engine.find_leaf(3);
    auto added = engine.find_leaf(4);
    REQUIRE(third.has_value());
    REQUIRE(added.has_value());
    CHECK(engine.system.clusters[0].tree.get_parent(third->cell_index) ==
          engine.system.clusters[0].tree.get_parent(added->cell_index));
    const auto& rect = output.geometries[0][added->cell_index];
    CHECK(rect.width * rect.height == doctest::Approx(100000.0f));
  }

  TEST_CASE("without known focus non-pointer targets retain incoming monitor") {
    for (auto target : {LayoutSplitTarget::Focused, LayoutSplitTarget::Largest}) {
      Engine engine = create_test_engine();
      LayoutOptions options;
      options.split_target = target;
      EngineFrameInput input;
      input.layout_options = &options;
      input.cluster_updates = {{{1, 2}, false}, {{3, 4}, false}};
      input.cursor_pos = Point{10, 10};
      input.foreground_leaf_id = 4;
      auto output = engine.process_frame(input);
      CHECK(output.topology_changed);
      auto added = engine.find_leaf(4);
      REQUIRE(added.has_value());
      CHECK(added->cluster_index == 1);
    }
  }

  TEST_CASE("closed remembered focus is cleared and monitor profiles select target policy") {
    Engine engine = create_test_engine();
    EngineFrameInput input;
    input.cluster_options.resize(2);
    input.cluster_options[1].layoutOptions.split_target = LayoutSplitTarget::Largest;
    input.cluster_updates = {{{1, 2}, false}, {{3}, false}};
    input.foreground_leaf_id = 3;
    input.cursor_pos = Point{10, 10};
    input.pointer_window_id = 1;
    engine.previous_cursor_pos = input.cursor_pos; // Keyboard focus with a stationary pointer.
    auto initial = engine.process_frame(input);
    CHECK_FALSE(initial.topology_changed);
    input.foreground_leaf_id = 99; // Untiled dialog: retain the preceding tiled focus.
    input.cluster_updates[0].leaf_ids.push_back(4);
    auto added_output = engine.process_frame(input);
    CHECK(added_output.topology_changed);
    auto added = engine.find_leaf(4);
    REQUIRE(added.has_value());
    CHECK(added->cluster_index == 1);
    input.cluster_updates = {{{1, 2}, false}, {{4}, false}};
    auto removed = engine.process_frame(input);
    CHECK(removed.topology_changed);
    CHECK_FALSE(engine.system.focused_leaf_id.has_value());
  }

  TEST_CASE("matching layout rules override largest and explicit moves retain their target") {
    Engine engine = create_single_cluster_engine();
    auto options = create_two_window_vertical_layout_options(0.3f);
    options.split_target = LayoutSplitTarget::Largest;
    CHECK(engine.update({{{1, 2}, false}}, std::nullopt, &options).topology_changed);
    CHECK(engine.system.clusters[0].tree[0].split_ratio == doctest::Approx(0.3f));
    options.rules.clear();
    CHECK(engine.update({{{1, 2, 3}, false}}, std::nullopt, &options).topology_changed);
    auto destination = engine.find_leaf(1);
    REQUIRE(destination.has_value());
    CHECK(engine.move_leaf_to_cell(3, 0, destination->cell_index));
    auto first = engine.find_leaf(1);
    auto third = engine.find_leaf(3);
    REQUIRE(first.has_value());
    REQUIRE(third.has_value());
    CHECK(engine.system.clusters[0].tree.get_parent(first->cell_index) ==
          engine.system.clusters[0].tree.get_parent(third->cell_index));
  }
}

TEST_SUITE("Largest split target across monitors") {
  EngineFrameInput global_largest_input() {
    EngineFrameInput input;
    input.cluster_options.resize(2);
    for (auto& options : input.cluster_options) {
      options.layoutOptions.split_target = LayoutSplitTarget::LargestAllMonitors;
    }
    input.cluster_updates = {{{1, 2}, false}, {{3}, false}};
    input.has_completed_initial_tile_pass = true;
    return input;
  }

  TEST_CASE("arrival ignores focus and pointer and remains on destination next frame") {
    auto engine = create_test_engine();
    auto input = global_largest_input();
    input.foreground_leaf_id = 1;
    input.cursor_pos = Point{10, 10};
    CHECK_FALSE(engine.process_frame(input).topology_changed);
    input.foreground_leaf_id = 4;
    input.cluster_updates[0].leaf_ids.push_back(4);
    CHECK(engine.process_frame(input).apply_tiles);
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 1);
    input.cluster_updates = {{{1, 2}, false}, {{3, 4}, false}};
    CHECK_FALSE(engine.process_frame(input).topology_changed);
    CHECK(engine.find_leaf(4)->cluster_index == 1);
  }

  TEST_CASE("batch recalculates globally after each split for every direction") {
    for (auto mode : {SplitMode::Dwindle, SplitMode::Vertical, SplitMode::Horizontal}) {
      auto engine = create_test_engine();
      engine.system.split_mode = mode;
      auto input = global_largest_input();
      input.foreground_leaf_id = 1;
      input.cluster_updates[0].leaf_ids = {1, 2, 4, 5, 6};
      CHECK(engine.process_frame(input).topology_changed);
      REQUIRE(engine.find_leaf(4).has_value());
      REQUIRE(engine.find_leaf(5).has_value());
      REQUIRE(engine.find_leaf(6).has_value());
      CHECK(engine.find_leaf(4)->cluster_index == 1);
      CHECK(engine.find_leaf(5)->cluster_index == 0);
      CHECK(engine.find_leaf(6)->cluster_index == 0);
    }
  }

  TEST_CASE("ties prefer focused monitor then monitor order and first child") {
    for (bool focus_second : {false, true}) {
      Engine engine;
      engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {1}},
                   {800, 0, 800, 600, 800, 0, 800, 600, {3}}});
      auto input = global_largest_input();
      input.cluster_updates = {{{1, 4}, false}, {{3}, false}};
      if (focus_second) {
        input.foreground_leaf_id = 3;
      }
      CHECK(engine.process_frame(input).topology_changed);
      REQUIRE(engine.find_leaf(4).has_value());
      CHECK(engine.find_leaf(4)->cluster_index == (focus_second ? 1 : 0));
    }
    auto engine = create_test_engine();
    auto input = global_largest_input();
    input.foreground_leaf_id = 2;
    input.cluster_updates[0].leaf_ids.push_back(4);
    input.cluster_updates[1].has_fullscreen_cell = true;
    CHECK(engine.process_frame(input).topology_changed);
    const auto first = engine.find_leaf(1);
    const auto added = engine.find_leaf(4);
    REQUIRE(first.has_value());
    REQUIRE(added.has_value());
    CHECK(engine.system.clusters[0].tree.get_parent(first->cell_index) ==
          engine.system.clusters[0].tree.get_parent(added->cell_index));
  }

  TEST_CASE("removals on later monitors happen before choosing a destination") {
    Engine engine;
    engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {1, 2}},
                 {800, 0, 800, 600, 800, 0, 800, 600, {3, 4}}});
    auto input = global_largest_input();
    input.foreground_leaf_id = 1;
    input.cluster_updates = {{{1, 2, 5}, false}, {{3}, false}};
    CHECK(engine.process_frame(input).topology_changed);
    CHECK_FALSE(engine.find_leaf(4).has_value());
    REQUIRE(engine.find_leaf(5).has_value());
    CHECK(engine.find_leaf(5)->cluster_index == 1);
  }

  TEST_CASE("empty work areas compete with cells and all empty ties follow monitor order") {
    for (bool all_empty : {false, true}) {
      auto engine = create_engine_with_empty_second_cluster();
      auto input = global_largest_input();
      if (all_empty) {
        engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {}},
                     {800, 0, 800, 600, 800, 0, 800, 600, {}}});
        input.cluster_updates[0].leaf_ids.clear();
      }
      input.cluster_updates[1].leaf_ids = {4};
      CHECK(engine.process_frame(input).topology_changed);
      REQUIRE(engine.find_leaf(4).has_value());
      const auto added = *engine.find_leaf(4);
      CHECK(added.cluster_index == (all_empty ? 0 : 1));
      const auto geometry = engine.compute_geometries(0, 0, 0);
      CHECK(geometry[added.cluster_index][added.cell_index].width == doctest::Approx(800.0f));
      CHECK(geometry[added.cluster_index][added.cell_index].height == doctest::Approx(600.0f));
      input.cluster_updates = all_empty
          ? std::vector<ClusterCellUpdateInfo>{{{4}, false}, {{}, false}}
          : std::vector<ClusterCellUpdateInfo>{{{1, 2}, false}, {{4}, false}};
      CHECK_FALSE(engine.process_frame(input).topology_changed);
    }
  }

  TEST_CASE("smaller empty work areas lose to larger existing cells") {
    auto engine = create_engine_with_empty_second_cluster();
    engine.system.clusters[1].window_width = 300;
    engine.system.clusters[1].window_height = 300;
    auto input = global_largest_input();
    input.cluster_updates[1].leaf_ids = {4};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 0);
    CHECK(engine.system.clusters[1].tree.empty());
  }

  TEST_CASE("all empty monitors choose largest work area rather than incoming monitor") {
    Engine engine;
    engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {}},
                 {800, 0, 1200, 800, 800, 0, 1200, 800, {}}});
    auto input = global_largest_input();
    input.cluster_updates = {{{4}, false}, {{}, false}};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 1);
  }

  TEST_CASE("equal empty and occupied work areas prefer the focused monitor") {
    Engine engine;
    engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {}},
                 {800, 0, 800, 600, 800, 0, 800, 600, {3}}});
    auto input = global_largest_input();
    input.foreground_leaf_id = 3;
    input.cluster_updates = {{{4}, false}, {{3}, false}};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 1);
  }

  TEST_CASE("batch fills empty monitor then splits and reevaluates its cells") {
    auto engine = create_engine_with_empty_second_cluster();
    auto input = global_largest_input();
    input.foreground_leaf_id = 1;
    input.cluster_updates = {{{1, 2, 4, 5, 6}, false}, {{}, false}};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    REQUIRE(engine.find_leaf(5).has_value());
    REQUIRE(engine.find_leaf(6).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 1);
    CHECK(engine.find_leaf(5)->cluster_index == 1);
    CHECK(engine.find_leaf(6)->cluster_index == 0);
  }

  TEST_CASE("monitor emptied by a closure competes in the same frame") {
    auto engine = create_test_engine();
    auto input = global_largest_input();
    input.foreground_leaf_id = 1;
    input.cluster_updates = {{{1, 2, 4}, false}, {{}, false}};
    CHECK(engine.process_frame(input).topology_changed);
    CHECK_FALSE(engine.find_leaf(3).has_value());
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 1);
  }

  TEST_CASE("current fullscreen snapshot excludes targets and leaves fullscreen arrivals alone") {
    auto engine = create_test_engine();
    auto input = global_largest_input();
    input.cluster_updates = {{{1, 2, 4}, false}, {{3, 5}, true}};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    REQUIRE(engine.find_leaf(5).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 0);
    CHECK(engine.find_leaf(5)->cluster_index == 1);

    // An empty monitor remains eligible when every existing cell is fullscreen.
    auto fallback = create_engine_with_empty_second_cluster();
    input.cluster_updates = {{{1, 2}, true}, {{4}, false}};
    CHECK(fallback.process_frame(input).topology_changed);
    REQUIRE(fallback.find_leaf(4).has_value());
    CHECK(fallback.find_leaf(4)->cluster_index == 1);
  }

  TEST_CASE("work area and resized ratios determine area before gaps and zen") {
    Engine engine;
    engine.init({{0, 0, 1200, 800, 0, 0, 1200, 800, {1, 2}},
                 {1200, 0, 800, 600, 1200, 0, 800, 600, {3}}});
    engine.system.clusters[0].tree[0].split_ratio = 0.75f;
    auto input = global_largest_input();
    input.cluster_options[0].gapOptions.horizontal = 200;
    input.cluster_options[0].gapOptions.vertical = 200;
    engine.system.clusters[1].zen_cell_index = engine.find_leaf(3)->cell_index;
    input.cluster_updates[1].leaf_ids.push_back(4);
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 0);
    CHECK(engine.system.clusters[0].tree.get_parent(engine.find_leaf(1)->cell_index) ==
          engine.system.clusters[0].tree.get_parent(engine.find_leaf(4)->cell_index));
    CHECK(engine.system.clusters[1].zen_cell_index.has_value());
  }

  TEST_CASE("destination pointer policy cannot replace global largest selection") {
    Engine engine;
    engine.init({{0, 0, 400, 400, 0, 0, 400, 400, {1}},
                 {400, 0, 1200, 800, 400, 0, 1200, 800, {2, 3, 4}}});
    auto input = global_largest_input();
    input.foreground_leaf_id = 1;
    input.cluster_options[1].layoutOptions.split_target = LayoutSplitTarget::Pointer;
    input.cursor_pos = Point{1500, 700};
    input.cluster_updates = {{{1, 5}, false}, {{2, 3, 4}, false}};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(5).has_value());
    CHECK(engine.find_leaf(5)->cluster_index == 1);
    CHECK(engine.system.clusters[1].tree.get_parent(engine.find_leaf(2)->cell_index) ==
          engine.system.clusters[1].tree.get_parent(engine.find_leaf(5)->cell_index));
  }

  TEST_CASE("destination layout rules override splitting and insertion exits zen") {
    auto engine = create_test_engine();
    auto input = global_largest_input();
    input.foreground_leaf_id = 1;
    input.cluster_options[1].layoutOptions = create_two_window_vertical_layout_options(0.3f);
    engine.system.clusters[1].zen_cell_index = engine.find_leaf(3)->cell_index;
    input.cluster_updates[0].leaf_ids.push_back(4);
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 1);
    CHECK(engine.system.clusters[1].tree[0].split_ratio == doctest::Approx(0.3f));
    CHECK_FALSE(engine.system.clusters[1].zen_cell_index.has_value());
  }

  TEST_CASE("incoming policy is used without focus and other focused policies stay local") {
    for (bool local_focus : {false, true}) {
      auto engine = create_test_engine();
      auto input = global_largest_input();
      input.cluster_options[1].layoutOptions.split_target = LayoutSplitTarget::Focused;
      input.cluster_updates[0].leaf_ids.push_back(4);
      if (local_focus) {
        input.foreground_leaf_id = 1;
        input.cluster_options[0].layoutOptions.split_target = LayoutSplitTarget::Largest;
        input.cluster_options[1].layoutOptions.split_target = LayoutSplitTarget::LargestAllMonitors;
      }
      CHECK(engine.process_frame(input).topology_changed);
      REQUIRE(engine.find_leaf(4).has_value());
      CHECK(engine.find_leaf(4)->cluster_index == (local_focus ? 0 : 1));
    }
  }

  TEST_CASE("batch reevaluates area after destination layout rules") {
    auto engine = create_test_engine();
    auto input = global_largest_input();
    input.foreground_leaf_id = 1;
    input.cluster_options[1].layoutOptions = create_two_window_vertical_layout_options(0.9f);
    input.cluster_updates[0].leaf_ids = {1, 2, 4, 5};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    REQUIRE(engine.find_leaf(5).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 1);
    CHECK(engine.find_leaf(5)->cluster_index == 1);
    CHECK(engine.system.clusters[1].tree.get_parent(engine.find_leaf(3)->cell_index) ==
          engine.system.clusters[1].tree.get_parent(engine.find_leaf(5)->cell_index));
  }

  TEST_CASE("explicit redirection takes precedence over global policy") {
    auto engine = create_test_engine();
    LayoutOptions options;
    options.split_target = LayoutSplitTarget::LargestAllMonitors;
    CHECK(engine.update({{{1, 2}, false}, {{3, 4}, false}}, 0, &options).topology_changed);
    REQUIRE(engine.find_leaf(4).has_value());
    CHECK(engine.find_leaf(4)->cluster_index == 0);
  }

  TEST_CASE("existing external moves retain destination under global policy") {
    auto engine = create_test_engine();
    auto input = global_largest_input();
    input.cluster_updates = {{{1, 2, 3}, false}, {{}, false}};
    CHECK(engine.process_frame(input).topology_changed);
    REQUIRE(engine.find_leaf(3).has_value());
    CHECK(engine.find_leaf(3)->cluster_index == 0);
    CHECK_FALSE(engine.process_frame(input).topology_changed);
  }

  TEST_CASE("startup and reinitialization preserve membership and use local largest") {
    std::vector<ClusterInitInfo> infos = {
        {0, 0, 800, 800, 0, 0, 800, 800, {1, 2, 3, 4}},
        {800, 0, 1600, 1600, 800, 0, 1600, 1600, {5}}};
    for (auto& info : infos) {
      info.split_target = LayoutSplitTarget::LargestAllMonitors;
    }
    Engine engine;
    for (int pass = 0; pass < 2; ++pass) {
      engine.init(infos);
      const auto geometry = engine.compute_geometries(0, 0, 0);
      for (size_t id : {1u, 2u, 3u, 4u}) {
        const auto cell = engine.find_leaf(id);
        REQUIRE(cell.has_value());
        CHECK(cell->cluster_index == 0);
        CHECK(geometry[0][cell->cell_index].width == doctest::Approx(400.0f));
        CHECK(geometry[0][cell->cell_index].height == doctest::Approx(400.0f));
      }
      REQUIRE(engine.find_leaf(5).has_value());
      CHECK(engine.find_leaf(5)->cluster_index == 1);
    }
  }
}

TEST_SUITE("Directional movement") {
  TEST_CASE("both modes move onto empty monitors in all directions and survive the next frame") {
    for (auto mode : {MovementMode::Swap, MovementMode::Insert}) {
      for (auto action : {HotkeyAction::MoveLeft, HotkeyAction::MoveRight, HotkeyAction::MoveUp,
                          HotkeyAction::MoveDown}) {
        for (bool only_window : {false, true}) {
          CAPTURE(mode);
          CAPTURE(action);
          CAPTURE(only_window);
          const bool horizontal =
              action == HotkeyAction::MoveLeft || action == HotkeyAction::MoveRight;
          const bool backwards = action == HotkeyAction::MoveLeft || action == HotkeyAction::MoveUp;
          ClusterInitInfo source{0, 0, 800, 600, 0, 0, 800, 600, {1}};
          if (!only_window) {
            source.initial_cell_ids.push_back(2);
          }
          ClusterInitInfo target = source;
          target.x = target.monitor_x = horizontal ? (backwards ? -800.0f : 800.0f) : 0;
          target.y = target.monitor_y = horizontal ? 0 : (backwards ? -600.0f : 600.0f);
          target.initial_cell_ids.clear();
          Engine engine;
          engine.init({source, target}, horizontal ? SplitMode::Vertical : SplitMode::Horizontal);
          const size_t moved_id = only_window || backwards ? 1 : 2;
          REQUIRE(engine.select_leaf(moved_id));
          engine.system.clusters[0].zen_cell_index = engine.system.selection->cell_index;
          EngineFrameInput input;
          input.initial_movement_mode = mode;
          input.cluster_updates = build_current_cluster_updates(engine);
          input.has_completed_initial_tile_pass = true;
          input.hotkey_action = action;
          input.gap_h = 10;
          input.gap_v = 20;
          input.zen_pct = 0.9f;
          auto output = engine.process_frame(input);
          REQUIRE(output.apply_tiles);
          CHECK(output.layout_changed);
          CHECK(output.focus_leaf_id == moved_id);
          CHECK(engine.selected_leaf_id() == moved_id);
          REQUIRE(engine.find_leaf(moved_id).has_value());
          CHECK(engine.find_leaf(moved_id)->cluster_index == 1);
          CHECK(engine.system.clusters[0].tree.empty() == only_window);
          CHECK_FALSE(engine.system.clusters[0].zen_cell_index.has_value());
          CHECK(collect_cluster_leaf_ids(engine.system.clusters[1]) ==
                std::vector<size_t>{moved_id});
          const auto& rect = output.geometries[1][0];
          CHECK(rect.x == doctest::Approx(target.x + 10));
          CHECK(rect.y == doctest::Approx(target.y + 20));
          CHECK(rect.width == doctest::Approx(780));
          CHECK(rect.height == doctest::Approx(560));
          REQUIRE(output.cursor_pos.has_value());
          CHECK(output.cursor_pos->x == compute_rect_center(rect).x);
          CHECK(output.cursor_pos->y == compute_rect_center(rect).y);

          input.hotkey_action.reset();
          input.cluster_updates = build_current_cluster_updates(engine);
          auto next = engine.process_frame(input);
          CHECK_FALSE(next.topology_changed);
          CHECK_FALSE(next.apply_tiles);
          CHECK(engine.find_leaf(moved_id)->cluster_index == 1);
        }
      }
    }
  }

  TEST_CASE("an empty monitor wins over a farther occupied monitor but not a nearer window") {
    for (auto mode : {MovementMode::Swap, MovementMode::Insert}) {
      Engine engine;
      engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {1, 2}},
                   {800, 0, 800, 600, 800, 0, 800, 600, {}},
                   {1600, 0, 800, 600, 1600, 0, 800, 600, {3}}},
                  SplitMode::Vertical);
      engine.movement_mode = mode;
      REQUIRE(engine.select_leaf(1));
      auto result = engine.process_action(HotkeyAction::MoveRight,
                                          compute_default_geometries(engine), 10, 10, 0);
      REQUIRE(result.success);
      CHECK(engine.find_leaf(1)->cluster_index == 0);
      CHECK(engine.system.clusters[1].tree.empty());
      result = engine.process_action(HotkeyAction::MoveRight, compute_default_geometries(engine),
                                     10, 10, 0);
      REQUIRE(result.success);
      CHECK(engine.find_leaf(1)->cluster_index == 1);
      CHECK(engine.find_leaf(2)->cluster_index == 0);
      CHECK(engine.find_leaf(3)->cluster_index == 2);
    }
  }

  TEST_CASE("empty monitor candidates respect direction alignment and fullscreen exclusion") {
    for (auto mode : {MovementMode::Swap, MovementMode::Insert}) {
      Engine engine;
      engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {1}},
                   {-800, 0, 800, 600, -800, 0, 800, 600, {}},
                   {800, 1200, 800, 600, 800, 1200, 800, 600, {}},
                   {1600, 0, 800, 600, 1600, 0, 800, 600, {}}});
      engine.movement_mode = mode;
      REQUIRE(engine.select_leaf(1));
      auto navigation = engine.process_action(HotkeyAction::NavigateRight,
                                              compute_default_geometries(engine), 10, 10, 0);
      CHECK_FALSE(navigation.success);
      CHECK(engine.selected_leaf_id() == 1);
      auto result = engine.process_action(HotkeyAction::MoveRight,
                                          compute_default_geometries(engine), 10, 10, 0);
      REQUIRE(result.success);
      CHECK(engine.find_leaf(1)->cluster_index == 3);

      Engine blocked = create_engine_with_empty_second_cluster();
      blocked.movement_mode = mode;
      blocked.system.clusters[1].has_fullscreen_cell = true;
      REQUIRE(blocked.select_leaf(2));
      result = blocked.process_action(HotkeyAction::MoveRight, compute_default_geometries(blocked),
                                      10, 10, 0);
      CHECK_FALSE(result.success);
      CHECK_FALSE(result.apply_tiles);
      CHECK(blocked.find_leaf(2)->cluster_index == 0);
    }
  }

  TEST_CASE("all directions preserve the moved window and place it beyond the neighbor") {
    for (auto mode : {MovementMode::Swap, MovementMode::Insert}) {
      for (auto action : {HotkeyAction::MoveLeft, HotkeyAction::MoveRight, HotkeyAction::MoveUp,
                          HotkeyAction::MoveDown}) {
        CAPTURE(mode);
        CAPTURE(action);
        bool horizontal = action == HotkeyAction::MoveLeft || action == HotkeyAction::MoveRight;
        bool backwards = action == HotkeyAction::MoveLeft || action == HotkeyAction::MoveUp;
        Engine engine = create_two_window_engine();
        auto& cluster = engine.system.clusters[0];
        cluster.tree[0].split_dir = horizontal ? SplitDir::Vertical : SplitDir::Horizontal;
        REQUIRE(engine.select_leaf(backwards ? 2 : 1));
        auto source_id = *engine.selected_leaf_id();
        auto target_id = backwards ? 1u : 2u;
        engine.movement_mode = mode;
        auto result = engine.process_action(action, compute_default_geometries(engine), 10, 10, 0);
        REQUIRE(result.success);
        CHECK(result.apply_tiles);
        CHECK(result.layout_changed);
        CHECK(engine.selected_leaf_id() == source_id);
        CHECK(result.focus_leaf_id == source_id);
        REQUIRE(result.cursor_pos.has_value());
        auto geoms = compute_default_geometries(engine);
        auto source = *engine.find_leaf(source_id);
        auto target = *engine.find_leaf(target_id);
        auto sr = geoms[0][static_cast<size_t>(source.cell_index)];
        auto tr = geoms[0][static_cast<size_t>(target.cell_index)];
        CHECK((horizontal ? sr.x < tr.x : sr.y < tr.y) == backwards);
        CHECK(result.cursor_pos->x == compute_rect_center(sr).x);
        CHECK(result.cursor_pos->y == compute_rect_center(sr).y);
        // The moved window is now at the edge. Repeating must not wrap or change focus.
        auto edge = engine.process_action(action, geoms, 10, 10, 0);
        CHECK_FALSE(edge.success);
        CHECK_FALSE(edge.apply_tiles);
        CHECK(engine.selected_leaf_id() == source_id);
      }
    }
  }

  TEST_CASE("insert splits the target in the requested direction regardless of split mode") {
    for (auto action : {HotkeyAction::MoveLeft, HotkeyAction::MoveRight, HotkeyAction::MoveUp,
                        HotkeyAction::MoveDown}) {
      CAPTURE(action);
      bool horizontal = action == HotkeyAction::MoveLeft || action == HotkeyAction::MoveRight;
      bool backwards = action == HotkeyAction::MoveLeft || action == HotkeyAction::MoveUp;
      Engine engine;
      ClusterInitInfo source;
      source.width = 800;
      source.height = 600;
      source.initial_cell_ids = {1};
      ClusterInitInfo target = source;
      target.x = horizontal ? (backwards ? -800.0f : 800.0f) : 0;
      target.y = horizontal ? 0 : (backwards ? -600.0f : 600.0f);
      target.initial_cell_ids = {2, 3};
      engine.init({source, target}, horizontal ? SplitMode::Horizontal : SplitMode::Vertical);
      engine.movement_mode = MovementMode::Insert;
      REQUIRE(engine.select_leaf(1));
      auto result = engine.process_action(action, compute_default_geometries(engine), 10, 10, 0);
      REQUIRE(result.success);
      CHECK(engine.system.clusters[0].tree.size() == 0);
      auto moved = *engine.find_leaf(1);
      CHECK(moved.cluster_index == 1);
      const auto& cluster = engine.system.clusters[1];
      auto parent = cluster.tree.get_parent(moved.cell_index);
      REQUIRE(parent.has_value());
      CHECK(cluster.tree[*parent].split_dir ==
            (horizontal ? SplitDir::Vertical : SplitDir::Horizontal));
      CHECK((cluster.tree.get_first_child(*parent) == moved.cell_index) == backwards);
      CHECK(cluster.tree.size() == 5);
      CHECK(engine.selected_leaf_id() == 1);
    }
  }

  TEST_CASE("insert within a tree collapses the old split and preserves all windows") {
    Engine engine = create_three_window_engine();
    engine.movement_mode = MovementMode::Insert;
    REQUIRE(engine.select_leaf(1));
    auto result = engine.process_action(HotkeyAction::MoveRight, compute_default_geometries(engine),
                                        10, 10, 0);
    REQUIRE(result.success);
    auto ids = collect_cluster_leaf_ids(engine.system.clusters[0]);
    std::sort(ids.begin(), ids.end());
    CHECK(ids == std::vector<size_t>{1, 2, 3});
    CHECK(engine.selected_leaf_id() == 1);
    const auto& cluster = engine.system.clusters[0];
    auto moved = *engine.find_leaf(1);
    auto parent = cluster.tree.get_parent(moved.cell_index);
    REQUIRE(parent.has_value());
    CHECK(cluster.tree[*parent].split_dir == SplitDir::Vertical);
    CHECK(cluster.tree.get_second_child(*parent) == moved.cell_index);
  }

  TEST_CASE("cross-monitor frame ignores stale membership and keeps focus on the moved window") {
    for (auto mode : {MovementMode::Swap, MovementMode::Insert}) {
      Engine engine = create_test_engine();
      REQUIRE(engine.select_leaf(2));
      EngineFrameInput input;
      input.initial_movement_mode = mode;
      input.cluster_updates = {{{1, 2}, false}, {{3}, false}};
      input.hotkey_action = HotkeyAction::MoveRight;
      input.has_completed_initial_tile_pass = true;
      auto output = engine.process_frame(input);
      CHECK(output.apply_tiles);
      CHECK(output.focus_leaf_id == 2);
      CHECK(engine.selected_leaf_id() == 2);
      REQUIRE(engine.find_leaf(2).has_value());
      CHECK(engine.find_leaf(2)->cluster_index == 1);
      CHECK(output.movement_mode == mode);
      REQUIRE(output.cursor_pos.has_value());
      CHECK(output.cursor_pos->x > 800);
      input.hotkey_action.reset();
      input.cluster_updates =
          mode == MovementMode::Swap
              ? std::vector<ClusterCellUpdateInfo>{{{1, 3}, false}, {{2}, false}}
              : std::vector<ClusterCellUpdateInfo>{{{1}, false}, {{2, 3}, false}};
      auto next = engine.process_frame(input);
      CHECK_FALSE(next.topology_changed);
      CHECK(engine.find_leaf(2)->cluster_index == 1);
    }
  }

  TEST_CASE("repeated movement walks the same window across a row") {
    for (auto mode : {MovementMode::Swap, MovementMode::Insert}) {
      Engine engine;
      engine.init({{0, 0, 1200, 600, 0, 0, 1200, 600, {1, 2, 3}}}, SplitMode::Vertical);
      engine.movement_mode = mode;
      REQUIRE(engine.select_leaf(1));
      for (int step = 0; step < 2; ++step) {
        auto result = engine.process_action(HotkeyAction::MoveRight,
                                            compute_default_geometries(engine), 10, 10, 0);
        REQUIRE(result.success);
        CHECK(engine.selected_leaf_id() == 1);
      }
      auto geoms = compute_default_geometries(engine);
      auto moved = *engine.find_leaf(1);
      for (size_t id : {2u, 3u}) {
        auto other = *engine.find_leaf(id);
        CHECK(geoms[0][static_cast<size_t>(moved.cell_index)].x >
              geoms[0][static_cast<size_t>(other.cell_index)].x);
      }
    }
  }

  TEST_CASE("explicit insertion orientation survives layout templates and the following frame") {
    Engine engine = create_test_engine();
    REQUIRE(engine.select_leaf(2));
    EngineFrameInput input;
    input.initial_movement_mode = MovementMode::Insert;
    input.cluster_options.resize(2);
    LayoutOptions layout;
    auto rule = create_two_window_vertical_layout_rule(0.3f);
    rule.tree.split_dir = LayoutSplitDir::Horizontal;
    layout.rules.push_back(rule);
    input.cluster_options[1].layoutOptions = layout;
    input.cluster_updates = {{{1, 2}, false}, {{3}, false}};
    input.hotkey_action = HotkeyAction::MoveRight;
    input.has_completed_initial_tile_pass = true;
    auto output = engine.process_frame(input);
    REQUIRE(output.apply_tiles);
    CHECK(engine.system.clusters[1].tree[0].split_dir == SplitDir::Vertical);
    input.hotkey_action.reset();
    input.cluster_updates = {{{1}, false}, {{2, 3}, false}};
    output = engine.process_frame(input);
    CHECK_FALSE(output.layout_changed);
    CHECK(engine.system.clusters[1].tree[0].split_dir == SplitDir::Vertical);
    CHECK(engine.system.clusters[1].tree[0].split_ratio == doctest::Approx(0.5f));
  }

  TEST_CASE("mode toggle reports toast and survives subsequent frames and reinitialization") {
    Engine engine = create_two_window_engine();
    EngineFrameInput input;
    input.cluster_updates = {{{1, 2}, false}};
    input.hotkey_action = HotkeyAction::ToggleMovementMode;
    auto output = engine.process_frame(input);
    CHECK(output.movement_mode == MovementMode::Insert);
    CHECK(output.toast_message == "Movement: Insert");
    input.hotkey_action.reset();
    CHECK(engine.process_frame(input).movement_mode == MovementMode::Insert);
    engine.init({{0, 0, 800, 600, 0, 0, 800, 600, {1, 2}}});
    CHECK(engine.process_frame(input).movement_mode == MovementMode::Insert);
    input.hotkey_action = HotkeyAction::ToggleMovementMode;
    CHECK(engine.process_frame(input).toast_message == "Movement: Swap");
    input.hotkey_action.reset();
    input.initial_movement_mode = MovementMode::Insert;
    CHECK(engine.process_frame(input).movement_mode == MovementMode::Insert);
    input.initial_movement_mode = MovementMode::Swap;
    CHECK(engine.process_frame(input).movement_mode == MovementMode::Swap);
  }

  TEST_CASE("missing selection or missing neighbor leaves layout unchanged") {
    for (auto mode : {MovementMode::Swap, MovementMode::Insert}) {
      Engine engine = create_two_window_engine();
      engine.movement_mode = mode;
      engine.system.selection.reset();
      auto result = engine.process_action(HotkeyAction::MoveRight,
                                          compute_default_geometries(engine), 10, 10, 0);
      CHECK_FALSE(result.success);
      CHECK_FALSE(result.focus_leaf_id.has_value());
      REQUIRE(engine.select_leaf(1));
      result = engine.process_action(HotkeyAction::MoveLeft, compute_default_geometries(engine), 10,
                                     10, 0);
      CHECK_FALSE(result.success);
      CHECK_FALSE(result.apply_tiles);
      CHECK(engine.selected_leaf_id() == 1);
    }
  }
}

TEST_SUITE("minimum size layout") {
  TEST_CASE("reported limits move siblings on either axis without changing preferred ratios") {
    for (bool horizontal : {false, true}) {
      Engine engine = create_two_window_engine();
      engine.system.clusters[0].tree[0].split_dir =
          horizontal ? SplitDir::Horizontal : SplitDir::Vertical;
      EngineFrameInput input;
      input.cluster_updates = build_current_cluster_updates(engine);
      input.has_completed_initial_tile_pass = true;
      input.gap_h = 10;
      input.gap_v = 10;
      ManagedWindowState first;
      first.leaf_id = 1;
      first.min_track_width = horizontal ? 0 : 500;
      first.min_track_height = horizontal ? 400 : 0;
      ManagedWindowState second;
      second.leaf_id = 2;
      second.min_track_width = horizontal ? 0 : 200;
      second.min_track_height = horizontal ? 150 : 0;
      input.managed_windows = {{first, second}};
      auto output = engine.process_frame(input);
      REQUIRE(output.apply_tiles);
      CHECK(output.layout_changed);
      auto a_index = engine.find_leaf(1)->cell_index;
      auto b_index = engine.find_leaf(2)->cell_index;
      const auto& a = output.geometries[0][a_index];
      const auto& b = output.geometries[0][b_index];
      if (horizontal) {
        CHECK(a.height == doctest::Approx(400));
        CHECK(b.height == doctest::Approx(170));
        CHECK(b.y == doctest::Approx(a.y + a.height + 10));
      } else {
        CHECK(a.width == doctest::Approx(500));
        CHECK(b.width == doctest::Approx(270));
        CHECK(b.x == doctest::Approx(a.x + a.width + 10));
      }
      CHECK(engine.system.clusters[0].tree[0].split_ratio == doctest::Approx(0.5f));
      CHECK(geometries_equal(output.geometries, compute_default_geometries(engine)));
      CHECK_FALSE(engine.process_frame(input).apply_tiles);

      input.managed_windows[0][0].min_track_width = 0;
      input.managed_windows[0][0].min_track_height = 0;
      output = engine.process_frame(input);
      CHECK(output.apply_tiles);
      const auto& restored = output.geometries[0][a_index];
      CHECK((horizontal ? restored.height : restored.width) ==
            doctest::Approx(horizontal ? 285 : 385));
    }
  }

  TEST_CASE("second sibling minimum bounds the first sibling and exact fits stay feasible") {
    Engine engine = create_two_window_engine();
    engine.system.clusters[0].tree[0].split_ratio = 0.9f;
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10;
    input.gap_v = 10;
    ManagedWindowState a;
    a.leaf_id = 1;
    a.min_track_width = 270;
    ManagedWindowState b;
    b.leaf_id = 2;
    b.min_track_width = 500;
    input.managed_windows = {{a, b}};
    auto output = engine.process_frame(input);
    CHECK(output.apply_tiles);
    CHECK(output.geometries[0][engine.find_leaf(1)->cell_index].width == doctest::Approx(270));
    CHECK(output.geometries[0][engine.find_leaf(2)->cell_index].width == doctest::Approx(500));
    CHECK(engine.system.clusters[0].tree[0].split_ratio == doctest::Approx(0.9f));
  }

  TEST_CASE("nested subtree minimums propagate both axes including gaps") {
    Engine engine;
    ClusterInitInfo info{0, 0, 800, 600, 0, 0, 800, 600, {1, 2, 3}};
    info.initial_layout_rule = create_three_window_vertical_right_horizontal_layout_rule();
    engine.init({info});
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10;
    input.gap_v = 10;
    ManagedWindowState a;
    a.leaf_id = 1;
    a.min_track_width = 250;
    ManagedWindowState b;
    b.leaf_id = 2;
    b.min_track_width = 500;
    b.min_track_height = 400;
    ManagedWindowState c;
    c.leaf_id = 3;
    c.min_track_width = 450;
    c.min_track_height = 150;
    input.managed_windows = {{a, b, c}};
    auto output = engine.process_frame(input);
    REQUIRE(output.apply_tiles);
    auto ar = output.geometries[0][engine.find_leaf(1)->cell_index];
    auto br = output.geometries[0][engine.find_leaf(2)->cell_index];
    auto cr = output.geometries[0][engine.find_leaf(3)->cell_index];
    CHECK(ar.width == doctest::Approx(270));
    CHECK(br.width == doctest::Approx(500));
    CHECK(cr.width == doctest::Approx(500));
    CHECK(br.height == doctest::Approx(400));
    CHECK(cr.height == doctest::Approx(170));
    CHECK(br.x == doctest::Approx(ar.x + ar.width + 10));
    CHECK(cr.y == doctest::Approx(br.y + br.height + 10));
  }

  TEST_CASE("impossible combined minimums keep preferred split without repeated retiling") {
    Engine engine = create_two_window_engine();
    auto before = compute_default_geometries(engine);
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10;
    input.gap_v = 10;
    ManagedWindowState a;
    a.leaf_id = 1;
    a.min_track_width = 500;
    ManagedWindowState b = a;
    b.leaf_id = 2;
    input.managed_windows = {{a, b}};
    auto output = engine.process_frame(input);
    CHECK_FALSE(output.apply_tiles);
    CHECK(geometries_equal(before, output.geometries));
    CHECK_FALSE(engine.process_frame(input).apply_tiles);
  }

  TEST_CASE("same axis nested minimum sums constrain ancestors on both axes") {
    for (bool horizontal : {false, true}) {
      Engine engine;
      const float width = horizontal ? 600.0f : 1200.0f;
      const float height = horizontal ? 1200.0f : 600.0f;
      ClusterInitInfo info{0, 0, width, height, 0, 0, width, height, {1, 2, 3}};
      auto rule = create_three_window_vertical_right_horizontal_layout_rule();
      rule.tree.split_dir = horizontal ? LayoutSplitDir::Horizontal : LayoutSplitDir::Vertical;
      rule.tree.second->split_dir = rule.tree.split_dir;
      info.initial_layout_rule = rule;
      engine.init({info});
      EngineFrameInput input;
      input.cluster_updates = build_current_cluster_updates(engine);
      input.has_completed_initial_tile_pass = true;
      input.gap_h = 10;
      input.gap_v = 10;
      ManagedWindowState b;
      b.leaf_id = 2;
      b.min_track_width = horizontal ? 0 : 700;
      b.min_track_height = horizontal ? 700 : 0;
      ManagedWindowState c;
      c.leaf_id = 3;
      c.min_track_width = horizontal ? 0 : 200;
      c.min_track_height = horizontal ? 200 : 0;
      input.managed_windows = {{b, c}};
      auto output = engine.process_frame(input);
      REQUIRE(output.apply_tiles);
      auto ar = output.geometries[0][engine.find_leaf(1)->cell_index];
      auto br = output.geometries[0][engine.find_leaf(2)->cell_index];
      auto cr = output.geometries[0][engine.find_leaf(3)->cell_index];
      CHECK((horizontal ? ar.height : ar.width) == doctest::Approx(260));
      CHECK((horizontal ? br.height : br.width) == doctest::Approx(700));
      CHECK((horizontal ? cr.height : cr.width) == doctest::Approx(200));
    }
  }

  TEST_CASE("monitor changes discard inferred minimums and their old failure evidence") {
    Engine engine = create_two_window_engine();
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10;
    input.gap_v = 10;
    ManagedWindowState a;
    a.leaf_id = 1;
    a.actual_rect = compute_default_geometries(engine)[0][engine.find_leaf(1)->cell_index];
    a.actual_rect->width = 500;
    input.managed_windows = {{a}};
    for (int i = 0; i < 3; ++i) {
      CHECK_FALSE(engine.process_frame(input).apply_tiles);
    }
    REQUIRE(engine.process_frame(input).apply_tiles);
    REQUIRE(engine.minimum_sizes.at(1).observed_width == 500);
    SUBCASE("monitor work area changes") {
      engine.system.clusters[0].window_width = 900;
    }
    SUBCASE("DPI changes without changing work area") {
      input.managed_windows[0][0].dpi = 144;
    }
    auto output = engine.process_frame(input);
    CHECK(output.apply_tiles);
    CHECK(engine.minimum_sizes.at(1).observed_width == 0);
    CHECK(output.geometries[0][engine.find_leaf(1)->cell_index].width ==
          doctest::Approx((engine.system.clusters[0].window_width - 30) / 2));
    CHECK(engine.placement_correction_failures.empty());
  }

  TEST_CASE("three stable failed corrections learn only the oversized axis and move neighbors") {
    for (bool horizontal : {false, true}) {
      Engine engine = create_two_window_engine();
      engine.system.clusters[0].tree[0].split_dir =
          horizontal ? SplitDir::Horizontal : SplitDir::Vertical;
      EngineFrameInput input;
      input.cluster_updates = build_current_cluster_updates(engine);
      input.has_completed_initial_tile_pass = true;
      input.gap_h = 10;
      input.gap_v = 10;
      auto target = compute_default_geometries(engine)[0][engine.find_leaf(1)->cell_index];
      ManagedWindowState a;
      a.leaf_id = 1;
      a.actual_rect = target;
      if (horizontal) {
        a.actual_rect->height = 400;
      } else {
        a.actual_rect->width = 500;
      }
      input.managed_windows = {{a}};
      for (int attempt = 0; attempt < 3; ++attempt) {
        auto output = engine.process_frame(input);
        CHECK_FALSE(output.apply_tiles);
        CHECK(output.placement_correction_leaf_ids == std::vector<size_t>{1});
      }
      auto output = engine.process_frame(input);
      CHECK(output.apply_tiles);
      CHECK(output.layout_changed);
      const auto& constraint = engine.minimum_sizes.at(1);
      CHECK(constraint.observed_width == doctest::Approx(horizontal ? 0 : 500));
      CHECK(constraint.observed_height == doctest::Approx(horizontal ? 400 : 0));
      CHECK(rects_equal(output.geometries[0][engine.find_leaf(1)->cell_index], *a.actual_rect));
      CHECK_FALSE(engine.process_frame(input).apply_tiles);

      // An app accepting a smaller size disproves the inferred minimum.
      input.managed_windows[0][0].actual_rect = target;
      output = engine.process_frame(input);
      CHECK(output.apply_tiles);
      CHECK(rects_equal(output.geometries[0][engine.find_leaf(1)->cell_index], target));
    }
  }

  TEST_CASE("position drift and changing sizes do not establish minimum sizes") {
    for (bool moving : {false, true}) {
      Engine engine = create_two_window_engine();
      EngineFrameInput input;
      input.cluster_updates = build_current_cluster_updates(engine);
      input.has_completed_initial_tile_pass = true;
      input.gap_h = 10;
      input.gap_v = 10;
      ManagedWindowState a;
      a.leaf_id = 1;
      a.actual_rect = compute_default_geometries(engine)[0][engine.find_leaf(1)->cell_index];
      a.actual_rect->width = 500;
      if (moving) {
        a.actual_rect->x += 30;
      }
      input.managed_windows = {{a}};
      for (int i = 0; i < 6; ++i) {
        if (!moving) {
          input.managed_windows[0][0].actual_rect->width += 10;
        }
        CHECK_FALSE(engine.process_frame(input).apply_tiles);
      }
      CHECK(engine.minimum_sizes.at(1).observed_width == 0);
    }
  }

  TEST_CASE("moving a window with an inferred minimum exchanges it without resizing its split") {
    Engine engine = create_two_window_engine();
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10;
    input.gap_v = 10;
    ManagedWindowState a;
    a.leaf_id = 1;
    a.actual_rect = compute_default_geometries(engine)[0][engine.find_leaf(1)->cell_index];
    a.actual_rect->width = 500;
    input.managed_windows = {{a}};
    for (int i = 0; i < 3; ++i) {
      CHECK_FALSE(engine.process_frame(input).apply_tiles);
    }
    REQUIRE(engine.process_frame(input).apply_tiles);
    input.completed_drag = CompletedDragRequest{1, Point{655, 300}, *a.actual_rect, true};
    input.completed_drag->actual_window_rect->x = 520;
    input.managed_windows[0][0].actual_rect = input.completed_drag->actual_window_rect;
    auto output = engine.process_frame(input);
    CHECK(output.apply_tiles);
    CHECK(engine.system.clusters[0].tree[0].split_ratio == doctest::Approx(0.5f));
    const auto& rect = output.geometries[0][engine.find_leaf(1)->cell_index];
    CHECK(rect.width == doctest::Approx(500));
    CHECK(rect.x == doctest::Approx(290));
  }

  TEST_CASE("constraints follow window identity through exchanges and clear when windows leave") {
    Engine engine = create_two_window_engine();
    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.has_completed_initial_tile_pass = true;
    input.gap_h = 10;
    input.gap_v = 10;
    ManagedWindowState a;
    a.leaf_id = 1;
    a.min_track_width = 500;
    input.managed_windows = {{a}};
    REQUIRE(engine.process_frame(input).apply_tiles);
    REQUIRE(engine.select_leaf(1));
    input.hotkey_action = HotkeyAction::ExchangeSiblings;
    auto output = engine.process_frame(input);
    CHECK(output.apply_tiles);
    const auto rect = output.geometries[0][engine.find_leaf(1)->cell_index];
    CHECK(rect.width == doctest::Approx(500));
    CHECK(rect.x == doctest::Approx(290));
    input.hotkey_action.reset();
    input.cluster_updates = {{{2}, false}};
    input.managed_windows = {{}};
    output = engine.process_frame(input);
    CHECK(output.apply_tiles);
    CHECK(engine.minimum_sizes.empty());
  }
}

#endif // DOCTEST_CONFIG_DISABLE
