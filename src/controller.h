#pragma once

#include <optional>
#include <vector>

#include "binary_tree.h"

namespace wintiler::ctrl {

// Basic geometric type
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Integer point coordinates (for cursor positioning)
struct Point {
  long x = 0;
  long y = 0;
};

// Split direction for binary space partitioning
enum class SplitDir { Vertical, Horizontal };

// Split mode determines how new splits are oriented
enum class SplitMode {
  Zigzag,    // Alternate direction based on parent
  Vertical,  // Always split vertically
  Horizontal // Always split horizontally
};

// Navigation direction
enum class Direction { Left, Right, Up, Down };

// Cell data stored in BinaryTree nodes
// Tree structure (parent/children) is managed by BinaryTree itself
struct CellData {
  SplitDir split_dir = SplitDir::Vertical;
  float split_ratio = 0.5f;
  std::optional<size_t> leaf_id; // Only set for leaf cells (windows)
};

// A cluster of cells forming a binary tree with position and monitor info
struct Cluster {
  BinaryTree<CellData> tree;
  float window_width = 0.0f;
  float window_height = 0.0f;
  std::optional<int> zen_cell_index;
  bool has_fullscreen_cell = false;
  float global_x = 0.0f;
  float global_y = 0.0f;
  float monitor_x = 0.0f;
  float monitor_y = 0.0f;
  float monitor_width = 0.0f;
  float monitor_height = 0.0f;
};

// Selection indicator using indices
struct CellIndicatorByIndex {
  int cluster_index = 0;
  int cell_index = 0;
};

// The top-level system managing all clusters
struct System {
  std::vector<Cluster> clusters;
  std::optional<CellIndicatorByIndex> selection;
  SplitMode split_mode = SplitMode::Zigzag;
};

// Initialization info for creating a cluster
struct ClusterInitInfo {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float monitor_x = 0.0f;
  float monitor_y = 0.0f;
  float monitor_width = 0.0f;
  float monitor_height = 0.0f;
  std::vector<size_t> initial_cell_ids;
};

// Update info for a single cluster (index is implicit from vector position)
struct ClusterCellUpdateInfo {
  std::vector<size_t> leaf_ids;
  bool has_fullscreen_cell = false;
};

// Result of a drop-move operation
struct DropMoveResult {
  Point cursor_pos;
  bool was_exchange = false;
};

// ============================================================================
// Query Functions
// ============================================================================

// Returns true if the cell at cell_index is a leaf (has no children)
[[nodiscard]] bool is_leaf(const Cluster& cluster, int cell_index);

// Find cell index by leaf ID. Returns nullopt if not found.
[[nodiscard]] std::optional<int> find_cell_by_leaf_id(const Cluster& cluster, size_t leaf_id);

// Get all leaf IDs from a cluster.
[[nodiscard]] std::vector<size_t> get_cluster_leaf_ids(const Cluster& cluster);

// Check if a leaf_id exists in any cluster of the system
[[nodiscard]] bool has_leaf_id(const System& system, size_t leaf_id);

// Get center point of a rectangle
[[nodiscard]] Point get_rect_center(const Rect& rect);

// Compute geometry for all cells in a cluster (in global coordinates).
// Returns vector where index = cell_index:
// - Leaf cells: computed rectangle with gaps applied (global coordinates)
// - Internal nodes: empty Rect (0,0,0,0)
// - Zen cell (if active): centered rect at zen_percentage of cluster size
[[nodiscard]] std::vector<Rect> compute_cluster_geometry(const Cluster& cluster, float gap_h,
                                                         float gap_v, float zen_percentage = 0.85f);

// ============================================================================
// Initialization
// ============================================================================

// Create a multi-cluster system from cluster initialization info
[[nodiscard]] System create_system(const std::vector<ClusterInitInfo>& infos);

// ============================================================================
// Cell Operations
// ============================================================================

// Delete a leaf cell and promote its sibling
// Returns true on success, false if cell is not a valid leaf or is root with no sibling
[[nodiscard]] bool delete_leaf(Cluster& cluster, int cell_index);

// Swap two cells (exchange positions or leaf_ids)
// Returns true on success, false on failure (invalid indices, not leaves)
// Updates selection to follow the swapped cell if selected
// Handles zen mode (clears zen when cells leave a zen cluster)
[[nodiscard]] bool swap_cells(System& system, int cluster_index1, int cell_index1,
                              int cluster_index2, int cell_index2);

// Move a cell from source to target (delete + split)
// Returns true on success, false on failure
// Updates selection to follow the moved cell if selected
// Handles zen mode (clears zen when source cell is in zen mode)
[[nodiscard]] bool move_cell(System& system, int source_cluster_index, int source_cell_index,
                             int target_cluster_index, int target_cell_index);

// Perform a drag-drop move or exchange operation
// source_leaf_id: leaf_id of the window being dragged
// cursor_x, cursor_y: drop location in global coordinates
// geometries: precomputed geometries from compute_cluster_geometry for all clusters
// do_exchange: if true, swap source and target; if false, move source to target
// Returns cursor position at result cell center, or nullopt on failure
[[nodiscard]] std::optional<DropMoveResult>
perform_drop_move(System& system, size_t source_leaf_id, float cursor_x, float cursor_y,
                  const std::vector<std::vector<Rect>>& geometries, bool do_exchange);

// ============================================================================
// Zen Mode
// ============================================================================

// Set zen mode for a cell
[[nodiscard]] bool set_zen(System& system, int cluster_index, int cell_index);

// Clear zen mode for a cluster
void clear_zen(System& system, int cluster_index);

// Check if a cell is in zen mode
[[nodiscard]] bool is_cell_zen(const System& system, int cluster_index, int cell_index);

// Toggle zen mode for selected cell
[[nodiscard]] bool toggle_selected_zen(System& system);

// ============================================================================
// Selection Navigation
// ============================================================================

// Move selection to adjacent cell using geometric navigation
// cell_geometries: 2D vector where:
//   - Outer index = cluster_index
//   - Inner index = cell_index
//   - Value = global Rect for that cell (empty rect for non-leaf/invalid cells)
// Handles zen mode: when a cluster has zen, only the zen cell is considered for navigation
// Returns the new selection if navigation succeeded
[[nodiscard]] std::optional<CellIndicatorByIndex>
move_selection(System& system, Direction dir,
               const std::vector<std::vector<Rect>>& cell_geometries);

// ============================================================================
// Split Operations
// ============================================================================

// Toggle split direction of selected cell's parent
[[nodiscard]] bool toggle_selected_split_dir(System& system);

// Cycle through split modes (Zigzag -> Vertical -> Horizontal -> Zigzag)
[[nodiscard]] bool cycle_split_mode(System& system);

// Set split ratio of selected cell's parent (clamped to 0.1-0.9)
[[nodiscard]] bool set_selected_split_ratio(System& system, float new_ratio);

// Adjust split ratio of selected cell's parent by delta
// Delta is negated if selected cell is second child (so positive = grow selected cell)
[[nodiscard]] bool adjust_selected_split_ratio(System& system, float delta);

// Update split ratio based on window resize
// actual_window_rect: the resized window's actual rect in global coordinates
// cluster_geometry: precomputed geometry for the cluster containing the cell
// Returns true if ratio was updated
[[nodiscard]] bool update_split_ratio_from_resize(System& system, int cluster_index, size_t leaf_id,
                                                  const Rect& actual_window_rect,
                                                  const std::vector<Rect>& cluster_geometry);

// ============================================================================
// System State Updates
// ============================================================================

// Update system state with new window configuration
// If redirect_cluster_index is provided, new windows are added to that cluster
// Returns true if any cells were added or deleted
[[nodiscard]] bool update(System& system, const std::vector<ClusterCellUpdateInfo>& cluster_updates,
                          std::optional<int> redirect_cluster_index = std::nullopt);

// ============================================================================
// Utilities
// ============================================================================

// Validate the entire multi-cluster system
[[nodiscard]] bool validate_system(const System& system);

// Debug: print the entire multi-cluster system to stdout
void debug_print_system(const System& system);

} // namespace wintiler::ctrl
