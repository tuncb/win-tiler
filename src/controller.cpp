#include "controller.h"

namespace wintiler::ctrl {

// ============================================================================
// Query Functions
// ============================================================================

bool is_leaf(const CellCluster& cluster, int cell_index) {
  return cluster.tree.is_leaf(cell_index);
}

// ============================================================================
// Internal Helpers
// ============================================================================

// Determine split direction based on split mode and current selection
static SplitDir determine_split_dir(const CellCluster& cluster, int selected_index,
                                    SplitMode mode) {
  switch (mode) {
  case SplitMode::Vertical:
    return SplitDir::Vertical;
  case SplitMode::Horizontal:
    return SplitDir::Horizontal;
  case SplitMode::Zigzag:
  default: {
    // If splitting an existing cell, use opposite of its parent's direction
    if (cluster.tree.is_valid_index(selected_index)) {
      auto parent_opt = cluster.tree.get_parent(selected_index);
      if (parent_opt.has_value()) {
        const CellData& parent_data = cluster.tree[*parent_opt];
        return (parent_data.split_dir == SplitDir::Vertical) ? SplitDir::Horizontal
                                                             : SplitDir::Vertical;
      }
    }
    // Fall back to Vertical for root-level splits
    return SplitDir::Vertical;
  }
  }
}

// Split result returned by split_leaf
struct SplitResult {
  size_t new_leaf_id;
  int new_selection_index;
};

// Split a leaf cell or create the root cell
// Returns the new selection index, or nullopt on failure
static std::optional<SplitResult> split_leaf(CellCluster& cluster, int selected_index,
                                             float gap_horizontal, float gap_vertical,
                                             size_t new_leaf_id, SplitDir split_dir,
                                             float split_ratio = 0.5f) {
  // Special case: if cluster is empty and selected_index is -1, create root
  if (cluster.tree.empty() && selected_index == -1) {
    CellData root_data;
    root_data.split_dir = split_dir;
    root_data.leaf_id = new_leaf_id;

    int index = cluster.tree.add_node(root_data);
    return SplitResult{new_leaf_id, index};
  }

  // Validate: must be a valid leaf
  if (!cluster.tree.is_leaf(selected_index)) {
    return std::nullopt;
  }

  CellData& leaf_data = cluster.tree[selected_index];
  size_t parent_leaf_id = *leaf_data.leaf_id;

  // Update parent: convert from leaf to internal node
  leaf_data.split_dir = split_dir;
  leaf_data.split_ratio = split_ratio;
  leaf_data.leaf_id = std::nullopt;

  // Create first child (keeps the original leaf_id)
  CellData first_child_data;
  first_child_data.split_dir = split_dir;
  first_child_data.leaf_id = parent_leaf_id;

  // Create second child (gets the new leaf_id)
  CellData second_child_data;
  second_child_data.split_dir = split_dir;
  second_child_data.leaf_id = new_leaf_id;

  // Add children to tree
  int first_child_index = cluster.tree.add_node(first_child_data, selected_index);
  int second_child_index = cluster.tree.add_node(second_child_data, selected_index);

  // Link children to parent
  cluster.tree.set_children(selected_index, first_child_index, second_child_index);

  // Return first child as new selection (matches original behavior)
  return SplitResult{new_leaf_id, first_child_index};
}

// Pre-create leaves in a cluster from initial cell IDs
// Returns the selection index (or -1 if no cells created)
static int pre_create_leaves(PositionedCluster& pc, const std::vector<size_t>& cell_ids,
                             float gap_horizontal, float gap_vertical, SplitMode mode) {
  int current_selection = -1;

  for (size_t cell_id : cell_ids) {
    // Determine split direction based on mode
    SplitDir split_dir = determine_split_dir(pc.cluster, current_selection, mode);

    if (pc.cluster.tree.empty()) {
      // First cell: create root leaf (pass -1 for empty cluster)
      auto result_opt =
          split_leaf(pc.cluster, -1, gap_horizontal, gap_vertical, cell_id, split_dir);
      if (result_opt.has_value()) {
        current_selection = result_opt->new_selection_index;
      }
    } else {
      // Subsequent cells: split current selection
      auto result_opt = split_leaf(pc.cluster, current_selection, gap_horizontal, gap_vertical,
                                   cell_id, split_dir);
      if (result_opt.has_value()) {
        // After split, selection moves to first child
        current_selection = result_opt->new_selection_index;
      }
    }
  }

  return current_selection;
}

// ============================================================================
// Initialization
// ============================================================================

System create_system(const std::vector<ClusterInitInfo>& infos, float gap_horizontal,
                     float gap_vertical) {
  System system;
  system.clusters.reserve(infos.size());

  for (size_t cluster_index = 0; cluster_index < infos.size(); ++cluster_index) {
    const auto& info = infos[cluster_index];

    PositionedCluster pc;
    pc.global_x = info.x;
    pc.global_y = info.y;
    pc.monitor_x = info.monitor_x;
    pc.monitor_y = info.monitor_y;
    pc.monitor_width = info.monitor_width;
    pc.monitor_height = info.monitor_height;

    // Initialize cluster dimensions
    pc.cluster.window_width = info.width;
    pc.cluster.window_height = info.height;

    int selection_index = -1;

    // Pre-create leaves if initial_cell_ids provided
    if (!info.initial_cell_ids.empty()) {
      selection_index = pre_create_leaves(pc, info.initial_cell_ids, gap_horizontal, gap_vertical,
                                          system.split_mode);
    }

    // If this is the first cluster with cells, make it the selected cluster
    if (!system.selection.has_value() && selection_index >= 0) {
      system.selection = CellIndicatorByIndex{static_cast<int>(cluster_index), selection_index};
    }

    system.clusters.push_back(std::move(pc));
  }

  return system;
}

} // namespace wintiler::ctrl
