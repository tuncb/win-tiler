#ifndef DOCTEST_CONFIG_DISABLE

#include <doctest/doctest.h>

#include "engine.h"

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
}

// =============================================================================
// Engine::process_frame Tests
// =============================================================================

TEST_SUITE("Engine::process_frame") {
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

  TEST_CASE("hover selection happens inside process_frame") {
    Engine engine = create_two_window_engine();
    set_selection(engine, 0, 1);
    auto geoms = compute_default_geometries(engine);
    const auto& target_rect = geoms[0][2];

    EngineFrameInput input;
    input.cluster_updates = build_current_cluster_updates(engine);
    input.cursor_pos = ctrl::Point{static_cast<long>(target_rect.x + target_rect.width / 2.0f),
                                   static_cast<long>(target_rect.y + target_rect.height / 2.0f)};
    input.gap_h = 10.0f;
    input.gap_v = 10.0f;

    EngineFrameOutput output = engine.process_frame(input);

    CHECK(output.selection_changed == true);
    REQUIRE(engine.system.selection.has_value());
    CHECK(engine.system.selection->cell_index == 2);
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

  TEST_CASE("returns failure when no selection") {
    Engine engine = create_empty_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result =
        engine.process_action(HotkeyAction::ToggleSplit, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
  }
}

// =============================================================================
// Engine::process_action Tests - StoreCell / ClearStored
// =============================================================================

TEST_SUITE("Engine::process_action - StoreCell/ClearStored") {
  TEST_CASE("StoreCell stores selected cell") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    ActionResult result = engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
    CHECK(engine.stored_cell.has_value());
  }

  TEST_CASE("StoreCell fails when no current selection was captured") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    engine.stored_cell = StoredCell{0, 1};
    engine.system.selection.reset();

    ActionResult result = engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
    REQUIRE(engine.stored_cell.has_value());
    CHECK(engine.stored_cell->cluster_index == 0);
    CHECK(engine.stored_cell->leaf_id == 1);
  }

  TEST_CASE("ClearStored clears stored cell") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    // First store a cell
    set_selection(engine, 0, 1);
    ActionResult store_result =
        engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(store_result.success == true);
    CHECK(engine.stored_cell.has_value());

    // Clear it
    ActionResult result =
        engine.process_action(HotkeyAction::ClearStored, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == false);
    CHECK(result.apply_tiles == false);
    CHECK_FALSE(engine.stored_cell.has_value());
  }
}

// =============================================================================
// Engine::process_action Tests - Exchange / Move
// =============================================================================

TEST_SUITE("Engine::process_action - Exchange/Move") {
  TEST_CASE("Exchange fails without stored cell") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    ActionResult result = engine.process_action(HotkeyAction::Exchange, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
  }

  TEST_CASE("Move fails without stored cell") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    ActionResult result = engine.process_action(HotkeyAction::Move, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
  }

  TEST_CASE("Exchange swaps stored and selected") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    // Store first cell
    set_selection(engine, 0, 1);
    ActionResult store_result =
        engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(store_result.success == true);
    size_t stored_leaf_id = engine.stored_cell->leaf_id;

    // Select second cell
    set_selection(engine, 0, 2);
    size_t selected_leaf_id = *engine.system.clusters[0].tree[2].leaf_id;

    ActionResult result = engine.process_action(HotkeyAction::Exchange, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    // Stored cell should be cleared after exchange
    CHECK_FALSE(engine.stored_cell.has_value());

    // Verify swap occurred - leaf_ids should have swapped positions
    CHECK(*engine.system.clusters[0].tree[1].leaf_id == selected_leaf_id);
    CHECK(*engine.system.clusters[0].tree[2].leaf_id == stored_leaf_id);
  }

  TEST_CASE("Exchange clears stored on success") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);
    ActionResult store_result =
        engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(store_result.success == true);
    set_selection(engine, 0, 2);

    ActionResult result = engine.process_action(HotkeyAction::Exchange, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK_FALSE(engine.stored_cell.has_value());
  }

  TEST_CASE("Exchange across clusters reports selection change and focus") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);
    ActionResult store_result =
        engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);
    REQUIRE(store_result.success == true);

    set_selection(engine, 1, 0);
    ActionResult result = engine.process_action(HotkeyAction::Exchange, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(result.focus_leaf_id == std::optional<size_t>{3});
    REQUIRE(engine.system.selection.has_value());
    CHECK(engine.system.selection->cluster_index == 0);
    CHECK(engine.system.selection->cell_index == 1);
  }

  TEST_CASE("Move across clusters reports selection change and focus") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);
    ActionResult store_result =
        engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);
    REQUIRE(store_result.success == true);

    set_selection(engine, 1, 0);
    ActionResult result = engine.process_action(HotkeyAction::Move, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(result.focus_leaf_id == std::optional<size_t>{1});
    REQUIRE(engine.system.selection.has_value());
    CHECK(engine.system.selection->cluster_index == 1);
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
  TEST_CASE("swaps cell with its sibling") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 1);

    size_t cell1_leaf = *engine.system.clusters[0].tree[1].leaf_id;
    size_t cell2_leaf = *engine.system.clusters[0].tree[2].leaf_id;

    ActionResult result =
        engine.process_action(HotkeyAction::ExchangeSiblings, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK(result.selection_changed == true);
    CHECK(result.layout_changed == true);
    CHECK(result.apply_tiles == true);
    CHECK(result.cursor_pos.has_value());
    CHECK(result.focus_leaf_id.has_value());

    // Verify swap
    CHECK(*engine.system.clusters[0].tree[1].leaf_id == cell2_leaf);
    CHECK(*engine.system.clusters[0].tree[2].leaf_id == cell1_leaf);
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

    CHECK(engine.system.split_mode == SplitMode::Zigzag);

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
    CHECK(result3.toast_message == std::optional<std::string>{"Split mode: Zigzag"});
    CHECK(engine.system.split_mode == SplitMode::Zigzag);
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

    ActionResult r3 = engine.process_action(HotkeyAction::StoreCell, geoms, 10.0f, 10.0f, 0.0f);
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

#endif // DOCTEST_CONFIG_DISABLE
