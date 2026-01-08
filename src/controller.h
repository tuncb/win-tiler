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

// A cluster of cells forming a binary tree
struct CellCluster {
  BinaryTree<CellData> tree;
  float window_width = 0.0f;
  float window_height = 0.0f;
  std::optional<int> zen_cell_index;
  bool has_fullscreen_cell = false;
};

// A cluster with its global position and monitor info
struct PositionedCluster {
  CellCluster cluster;
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
  std::vector<PositionedCluster> clusters;
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

// ============================================================================
// Query Functions
// ============================================================================

// Returns true if the cell at cell_index is a leaf (has no children)
[[nodiscard]] bool is_leaf(const CellCluster& cluster, int cell_index);

// ============================================================================
// Initialization
// ============================================================================

// Create a multi-cluster system from cluster initialization info
[[nodiscard]] System create_system(const std::vector<ClusterInitInfo>& infos, float gap_horizontal,
                                   float gap_vertical);

// ============================================================================
// Cell Operations
// ============================================================================

// Delete a leaf cell and promote its sibling
// Returns true on success, false if cell is not a valid leaf or is root with no sibling
[[nodiscard]] bool delete_leaf(CellCluster& cluster, int cell_index);

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

} // namespace wintiler::ctrl
