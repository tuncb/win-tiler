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

// Helper to set selection on a system
void set_selection(Engine& engine, int cluster_index, int cell_index) {
  engine.system.selection = CellIndicatorByIndex{cluster_index, cell_index};
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
}

// =============================================================================
// Engine::update Tests
// =============================================================================

TEST_SUITE("Engine::update") {
  TEST_CASE("returns true when changes applied") {
    Engine engine = create_test_engine();

    // Add a new window to cluster 0
    std::vector<ClusterCellUpdateInfo> updates = {
        {{1, 2, 4}, false}, // Cluster 0: added window 4
        {{3}, false}        // Cluster 1: unchanged
    };

    bool changed = engine.update(updates);
    CHECK(changed == true);
  }

  TEST_CASE("returns false when no changes") {
    Engine engine = create_test_engine();

    // Same windows as initial state
    std::vector<ClusterCellUpdateInfo> updates = {
        {{1, 2}, false}, // Cluster 0: same
        {{3}, false}     // Cluster 1: same
    };

    bool changed = engine.update(updates);
    CHECK(changed == false);
  }

  TEST_CASE("updates fullscreen state") {
    Engine engine = create_test_engine();

    std::vector<ClusterCellUpdateInfo> updates = {{{1, 2}, true}, // Cluster 0: has fullscreen
                                                  {{3}, false}};

    [[maybe_unused]] bool _ = engine.update(updates);
    CHECK(engine.system.clusters[0].has_fullscreen_cell == true);
    CHECK(engine.system.clusters[1].has_fullscreen_cell == false);
  }
}

// =============================================================================
// Engine::store_selected_cell and clear_stored_cell Tests
// =============================================================================

TEST_SUITE("Engine::store_selected_cell and clear_stored_cell") {
  TEST_CASE("stores selected leaf cell") {
    Engine engine = create_test_engine();

    // Select leaf cell in cluster 0 (cell index 1 or 2 are leaves)
    set_selection(engine, 0, 1);
    engine.store_selected_cell();

    CHECK(engine.stored_cell.has_value());
    CHECK(engine.stored_cell->cluster_index == 0);
    // leaf_id should be from the selected cell
    CHECK(engine.stored_cell->leaf_id > 0);
  }

  TEST_CASE("does nothing when no selection") {
    Engine engine = create_empty_engine();

    engine.store_selected_cell();

    CHECK_FALSE(engine.stored_cell.has_value());
  }

  TEST_CASE("does nothing for non-leaf selection") {
    Engine engine = create_test_engine();

    // Select parent node (cell index 0 in a 2-window cluster is the parent)
    set_selection(engine, 0, 0);
    engine.store_selected_cell();

    // Parent nodes don't have leaf_id, so stored_cell should remain empty
    CHECK_FALSE(engine.stored_cell.has_value());
  }

  TEST_CASE("clear_stored_cell resets stored_cell") {
    Engine engine = create_test_engine();

    set_selection(engine, 0, 1);
    engine.store_selected_cell();
    CHECK(engine.stored_cell.has_value());

    engine.clear_stored_cell();
    CHECK_FALSE(engine.stored_cell.has_value());
  }

  TEST_CASE("clear on already-empty is safe") {
    Engine engine = create_test_engine();

    CHECK_FALSE(engine.stored_cell.has_value());
    engine.clear_stored_cell();
    CHECK_FALSE(engine.stored_cell.has_value());
  }
}

// =============================================================================
// Engine::get_selected_sibling_index Tests
// =============================================================================

TEST_SUITE("Engine::get_selected_sibling_index") {
  TEST_CASE("returns nullopt when no selection") {
    Engine engine = create_empty_engine();

    auto sibling = engine.get_selected_sibling_index();
    CHECK_FALSE(sibling.has_value());
  }

  TEST_CASE("returns sibling index for leaf with sibling") {
    Engine engine = create_two_window_engine();

    // Select first leaf (cell index 1)
    set_selection(engine, 0, 1);

    auto sibling = engine.get_selected_sibling_index();
    CHECK(sibling.has_value());
    CHECK(*sibling == 2); // Sibling is at index 2
  }

  TEST_CASE("returns nullopt for root node") {
    Engine engine = create_single_cluster_engine();

    // Select root (only node in single-window cluster)
    set_selection(engine, 0, 0);

    auto sibling = engine.get_selected_sibling_index();
    CHECK_FALSE(sibling.has_value());
  }

  TEST_CASE("handles invalid cluster_index") {
    Engine engine = create_test_engine();

    // Set invalid cluster index
    engine.system.selection = CellIndicatorByIndex{99, 0};

    auto sibling = engine.get_selected_sibling_index();
    CHECK_FALSE(sibling.has_value());
  }
}

// =============================================================================
// Engine::get_selected_sibling_leaf_id Tests
// =============================================================================

TEST_SUITE("Engine::get_selected_sibling_leaf_id") {
  TEST_CASE("returns nullopt when no selection") {
    Engine engine = create_empty_engine();

    auto leaf_id = engine.get_selected_sibling_leaf_id();
    CHECK_FALSE(leaf_id.has_value());
  }

  TEST_CASE("returns leaf_id when sibling is leaf") {
    Engine engine = create_two_window_engine();

    // Select first leaf (cell index 1)
    set_selection(engine, 0, 1);

    auto leaf_id = engine.get_selected_sibling_leaf_id();
    CHECK(leaf_id.has_value());
    // Sibling should have leaf_id 2 (assuming initial order preserved)
    CHECK(*leaf_id > 0);
  }

  TEST_CASE("returns nullopt when no sibling") {
    Engine engine = create_single_cluster_engine();

    set_selection(engine, 0, 0);

    auto leaf_id = engine.get_selected_sibling_leaf_id();
    CHECK_FALSE(leaf_id.has_value());
  }
}

// =============================================================================
// Engine::perform_drop_move Tests
// =============================================================================

TEST_SUITE("Engine::perform_drop_move") {
  TEST_CASE("move within same cluster") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    // Get leaf_id from cluster 0, cell 1
    size_t source_leaf_id = *engine.system.clusters[0].tree[1].leaf_id;

    // Drop on cluster 0, cell 2
    const auto& target_rect = geoms[0][2];
    float drop_x = target_rect.x + target_rect.width / 2;
    float drop_y = target_rect.y + target_rect.height / 2;

    auto result = engine.perform_drop_move(source_leaf_id, drop_x, drop_y, geoms, false);

    CHECK(result.has_value());
    CHECK(result->was_exchange == false);
  }

  TEST_CASE("exchange mode swaps cells") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    size_t source_leaf_id = *engine.system.clusters[0].tree[1].leaf_id;

    const auto& target_rect = geoms[0][2];
    float drop_x = target_rect.x + target_rect.width / 2;
    float drop_y = target_rect.y + target_rect.height / 2;

    auto result = engine.perform_drop_move(source_leaf_id, drop_x, drop_y, geoms, true);

    CHECK(result.has_value());
    CHECK(result->was_exchange == true);
  }

  TEST_CASE("returns nullopt for cursor outside all cells") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    size_t source_leaf_id = *engine.system.clusters[0].tree[1].leaf_id;

    // Cursor way outside
    auto result = engine.perform_drop_move(source_leaf_id, -1000.0f, -1000.0f, geoms, false);

    CHECK_FALSE(result.has_value());
  }
}

// =============================================================================
// Engine::handle_resize Tests
// =============================================================================

TEST_SUITE("Engine::handle_resize") {
  TEST_CASE("returns false when no change needed") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    // Use same rect as computed - no change
    size_t leaf_id = *engine.system.clusters[0].tree[1].leaf_id;

    bool changed = engine.handle_resize(0, leaf_id, geoms[0][1], geoms[0]);
    // Ratio shouldn't change if window matches expected size
    CHECK(changed == false);
  }
}

// =============================================================================
// Engine::get_selected_center Tests
// =============================================================================

TEST_SUITE("Engine::get_selected_center") {
  TEST_CASE("returns nullopt when no selection") {
    Engine engine = create_empty_engine();
    auto geoms = compute_default_geometries(engine);

    auto center = engine.get_selected_center(geoms);
    CHECK_FALSE(center.has_value());
  }

  TEST_CASE("returns center of selected cell") {
    Engine engine = create_single_cluster_engine();
    auto geoms = compute_default_geometries(engine);

    set_selection(engine, 0, 0);

    auto center = engine.get_selected_center(geoms);
    CHECK(center.has_value());

    // Center should be roughly in the middle of the cell
    const auto& rect = geoms[0][0];
    long expected_x = static_cast<long>(rect.x + rect.width / 2);
    long expected_y = static_cast<long>(rect.y + rect.height / 2);

    CHECK(center->x == expected_x);
    CHECK(center->y == expected_y);
  }

  TEST_CASE("handles invalid cluster index") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    engine.system.selection = CellIndicatorByIndex{99, 0};

    auto center = engine.get_selected_center(geoms);
    CHECK_FALSE(center.has_value());
  }

  TEST_CASE("handles invalid cell index") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    engine.system.selection = CellIndicatorByIndex{0, 99};

    auto center = engine.get_selected_center(geoms);
    CHECK_FALSE(center.has_value());
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
    CHECK(result.new_cursor_pos.has_value());
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
    CHECK(engine.stored_cell.has_value());
  }

  TEST_CASE("ClearStored clears stored cell") {
    Engine engine = create_two_window_engine();
    auto geoms = compute_default_geometries(engine);

    // First store a cell
    set_selection(engine, 0, 1);
    engine.store_selected_cell();
    CHECK(engine.stored_cell.has_value());

    // Clear it
    ActionResult result =
        engine.process_action(HotkeyAction::ClearStored, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
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
    engine.store_selected_cell();
    size_t stored_leaf_id = engine.stored_cell->leaf_id;

    // Select second cell
    set_selection(engine, 0, 2);
    size_t selected_leaf_id = *engine.system.clusters[0].tree[2].leaf_id;

    ActionResult result = engine.process_action(HotkeyAction::Exchange, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
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
    engine.store_selected_cell();
    set_selection(engine, 0, 2);

    ActionResult result = engine.process_action(HotkeyAction::Exchange, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == true);
    CHECK_FALSE(engine.stored_cell.has_value());
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
    CHECK(result.new_cursor_pos.has_value());

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
    CHECK(engine.system.split_mode == SplitMode::Vertical);

    ActionResult result2 =
        engine.process_action(HotkeyAction::CycleSplitMode, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(result2.success == true);
    CHECK(engine.system.split_mode == SplitMode::Horizontal);

    ActionResult result3 =
        engine.process_action(HotkeyAction::CycleSplitMode, geoms, 10.0f, 10.0f, 0.0f);
    CHECK(result3.success == true);
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
    CHECK(result.new_cursor_pos.has_value());
    CHECK(engine.system.clusters[0].tree[0].split_ratio == 0.5f);
  }
}

// =============================================================================
// Engine::process_action Tests - Exit
// =============================================================================

TEST_SUITE("Engine::process_action - Exit") {
  TEST_CASE("Exit action does nothing") {
    Engine engine = create_test_engine();
    auto geoms = compute_default_geometries(engine);

    ActionResult result = engine.process_action(HotkeyAction::Exit, geoms, 10.0f, 10.0f, 0.0f);

    CHECK(result.success == false);
    CHECK(result.selection_changed == false);
    CHECK_FALSE(result.new_cursor_pos.has_value());
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
    auto zen_geoms = engine.compute_geometries(10.0f, 10.0f, 0.85f);

    // Navigation should still work (only zen cell is visible)
    ActionResult result =
        engine.process_action(HotkeyAction::NavigateRight, zen_geoms, 10.0f, 10.0f, 0.85f);
    // May succeed or fail depending on geometry, but shouldn't crash
    (void)result;
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

  TEST_CASE("get_selected_center with mismatched geometries") {
    Engine engine = create_test_engine();
    std::vector<std::vector<Rect>> small_geoms = {{{0, 0, 100, 100}}}; // Only 1 cell

    set_selection(engine, 0, 2); // Cell index 2 doesn't exist in small_geoms

    auto center = engine.get_selected_center(small_geoms);
    CHECK_FALSE(center.has_value());
  }
}

#endif // DOCTEST_CONFIG_DISABLE
