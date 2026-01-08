#include "controller.h"

#include <cassert>

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

// ============================================================================
// Cell Operations
// ============================================================================

bool delete_leaf(CellCluster& cluster, int cell_index) {
  // Must be a valid leaf
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return false;
  }

  auto parent_opt = cluster.tree.get_parent(cell_index);

  // If no parent, this is the root - can only delete if it's the only node
  if (!parent_opt.has_value()) {
    if (cluster.tree.size() == 1) {
      cluster.tree.clear();
      return true;
    }
    return false; // Root with children can't be deleted
  }

  int parent_index = *parent_opt;

  // Get sibling
  auto sibling_opt = cluster.tree.get_sibling(cell_index);
  if (!sibling_opt.has_value()) {
    return false; // Should not happen in a valid binary tree
  }
  int sibling_index = *sibling_opt;

  // Get grandparent (parent's parent)
  auto grandparent_opt = cluster.tree.get_parent(parent_index);

  // Copy sibling's data to parent (sibling takes parent's place)
  CellData sibling_data = cluster.tree[sibling_index];
  cluster.tree[parent_index] = sibling_data;

  // Copy sibling's children to parent
  auto sibling_first = cluster.tree.get_first_child(sibling_index);
  auto sibling_second = cluster.tree.get_second_child(sibling_index);

  if (sibling_first.has_value() && sibling_second.has_value()) {
    // Sibling is not a leaf - copy its children to parent
    cluster.tree.set_children(parent_index, *sibling_first, *sibling_second);
  } else {
    // Sibling is a leaf - parent becomes a leaf (clear children)
    cluster.tree.node(parent_index).first_child = std::nullopt;
    cluster.tree.node(parent_index).second_child = std::nullopt;
  }

  // Remove the deleted cell and sibling (parent took sibling's place)
  std::vector<int> indices_to_remove = {cell_index, sibling_index};

  // Clear zen if deleted cell was the zen cell
  if (cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == cell_index) {
    cluster.zen_cell_index = std::nullopt;
  }

  [[maybe_unused]] auto remap = cluster.tree.remove(indices_to_remove);

  // Update zen_cell_index if it was affected by removal
  if (cluster.zen_cell_index.has_value()) {
    int old_zen = *cluster.zen_cell_index;
    if (old_zen >= 0 && static_cast<size_t>(old_zen) < remap.size()) {
      int new_zen = remap[static_cast<size_t>(old_zen)];
      if (new_zen >= 0) {
        cluster.zen_cell_index = new_zen;
      } else {
        cluster.zen_cell_index = std::nullopt;
      }
    }
  }

  return true;
}

bool swap_cells(System& system, int cluster_index1, int cell_index1, int cluster_index2,
                int cell_index2) {
  // Validate cluster indices
  if (cluster_index1 < 0 || static_cast<size_t>(cluster_index1) >= system.clusters.size() ||
      cluster_index2 < 0 || static_cast<size_t>(cluster_index2) >= system.clusters.size()) {
    return false;
  }

  auto& cluster1 = system.clusters[static_cast<size_t>(cluster_index1)].cluster;
  auto& cluster2 = system.clusters[static_cast<size_t>(cluster_index2)].cluster;

  // Validate cell indices are valid leaves
  if (!cluster1.tree.is_valid_index(cell_index1) || !cluster1.tree.is_leaf(cell_index1) ||
      !cluster2.tree.is_valid_index(cell_index2) || !cluster2.tree.is_leaf(cell_index2)) {
    return false;
  }

  // Same cluster swap
  if (cluster_index1 == cluster_index2) {
    // Same cell - nothing to do
    if (cell_index1 == cell_index2) {
      return true;
    }

    auto parent1_opt = cluster1.tree.get_parent(cell_index1);
    auto parent2_opt = cluster1.tree.get_parent(cell_index2);

    // Check if siblings (same parent)
    if (parent1_opt.has_value() && parent2_opt.has_value() && *parent1_opt == *parent2_opt) {
      // Siblings: just swap children of parent
      cluster1.tree.swap_children(*parent1_opt);

      // Update selection if needed (swap cell indices)
      if (system.selection.has_value()) {
        auto& sel = *system.selection;
        if (sel.cluster_index == cluster_index1) {
          if (sel.cell_index == cell_index1) {
            sel.cell_index = cell_index2;
          } else if (sel.cell_index == cell_index2) {
            sel.cell_index = cell_index1;
          }
        }
      }
      return true;
    }

    // Non-siblings in same cluster: swap leaf_ids
    std::swap(cluster1.tree[cell_index1].leaf_id, cluster1.tree[cell_index2].leaf_id);

    // No selection update needed - cell indices stay the same, just contents swapped
    return true;
  }

  // Cross-cluster swap: just swap leaf_ids
  std::swap(cluster1.tree[cell_index1].leaf_id, cluster2.tree[cell_index2].leaf_id);

  // Handle zen mode
  bool cell1_is_zen =
      cluster1.zen_cell_index.has_value() && *cluster1.zen_cell_index == cell_index1;
  bool cell2_is_zen =
      cluster2.zen_cell_index.has_value() && *cluster2.zen_cell_index == cell_index2;

  if (cell1_is_zen && !cell2_is_zen) {
    // Cell1 was zen, cell2 wasn't - clear zen on cluster1 (window leaving)
    cluster1.zen_cell_index = std::nullopt;
  } else if (!cell1_is_zen && cell2_is_zen) {
    // Cell2 was zen, cell1 wasn't - clear zen on cluster2 (window leaving)
    cluster2.zen_cell_index = std::nullopt;
  }
  // If both zen or neither zen, no zen changes needed

  // Update selection if needed (swap cluster+cell indices)
  if (system.selection.has_value()) {
    auto& sel = *system.selection;
    if (sel.cluster_index == cluster_index1 && sel.cell_index == cell_index1) {
      sel.cluster_index = cluster_index2;
      sel.cell_index = cell_index2;
    } else if (sel.cluster_index == cluster_index2 && sel.cell_index == cell_index2) {
      sel.cluster_index = cluster_index1;
      sel.cell_index = cell_index1;
    }
  }

  return true;
}

bool move_cell(System& system, int source_cluster_index, int source_cell_index,
               int target_cluster_index, int target_cell_index) {
  // Validate cluster indices
  if (source_cluster_index < 0 ||
      static_cast<size_t>(source_cluster_index) >= system.clusters.size() ||
      target_cluster_index < 0 ||
      static_cast<size_t>(target_cluster_index) >= system.clusters.size()) {
    return false;
  }

  auto& src_cluster = system.clusters[static_cast<size_t>(source_cluster_index)].cluster;
  auto& tgt_cluster = system.clusters[static_cast<size_t>(target_cluster_index)].cluster;

  // Validate cell indices are valid leaves
  if (!src_cluster.tree.is_valid_index(source_cell_index) ||
      !src_cluster.tree.is_leaf(source_cell_index) ||
      !tgt_cluster.tree.is_valid_index(target_cell_index) ||
      !tgt_cluster.tree.is_leaf(target_cell_index)) {
    return false;
  }

  // Same cell - nothing to do
  if (source_cluster_index == target_cluster_index && source_cell_index == target_cell_index) {
    return true;
  }

  // Track selection state
  bool source_was_selected = system.selection.has_value() &&
                             system.selection->cluster_index == source_cluster_index &&
                             system.selection->cell_index == source_cell_index;
  bool target_was_selected = system.selection.has_value() &&
                             system.selection->cluster_index == target_cluster_index &&
                             system.selection->cell_index == target_cell_index;

  // Handle zen mode - clear if source is zen
  if (src_cluster.zen_cell_index.has_value() && *src_cluster.zen_cell_index == source_cell_index) {
    src_cluster.zen_cell_index = std::nullopt;
  }

  // Same cluster, check for sibling shortcut
  if (source_cluster_index == target_cluster_index) {
    auto src_parent_opt = src_cluster.tree.get_parent(source_cell_index);
    auto tgt_parent_opt = src_cluster.tree.get_parent(target_cell_index);

    if (src_parent_opt.has_value() && tgt_parent_opt.has_value() &&
        *src_parent_opt == *tgt_parent_opt) {
      // Siblings: just swap children
      src_cluster.tree.swap_children(*src_parent_opt);

      // Update selection
      if (source_was_selected) {
        system.selection->cell_index = target_cell_index;
      } else if (target_was_selected) {
        system.selection->cell_index = source_cell_index;
      }
      return true;
    }
  }

  // Full move path: save source leaf_id, delete source, split target
  std::optional<size_t> source_leaf_id = src_cluster.tree[source_cell_index].leaf_id;

  // Delete source cell
  // First, we need to handle the deletion manually to track index remapping

  auto src_parent_opt = src_cluster.tree.get_parent(source_cell_index);

  // If source is root and only node, can't move
  if (!src_parent_opt.has_value() && src_cluster.tree.size() == 1) {
    // Source is the only cell - can't move it (would leave cluster empty)
    return false;
  }

  // For same-cluster move, we need to track how deletion affects target index
  int adjusted_target_index = target_cell_index;

  if (src_parent_opt.has_value()) {
    int src_parent = *src_parent_opt;
    auto sibling_opt = src_cluster.tree.get_sibling(source_cell_index);
    if (!sibling_opt.has_value()) {
      return false;
    }
    int sibling_index = *sibling_opt;

    // Copy sibling's data to parent
    CellData sibling_data = src_cluster.tree[sibling_index];
    src_cluster.tree[src_parent] = sibling_data;

    // Copy sibling's children to parent
    auto sibling_first = src_cluster.tree.get_first_child(sibling_index);
    auto sibling_second = src_cluster.tree.get_second_child(sibling_index);

    if (sibling_first.has_value() && sibling_second.has_value()) {
      src_cluster.tree.set_children(src_parent, *sibling_first, *sibling_second);
    } else {
      src_cluster.tree.node(src_parent).first_child = std::nullopt;
      src_cluster.tree.node(src_parent).second_child = std::nullopt;
    }

    // Remove source and sibling
    std::vector<int> indices_to_remove = {source_cell_index, sibling_index};
    auto remap = src_cluster.tree.remove(indices_to_remove);

    // Update target index if same cluster
    if (source_cluster_index == target_cluster_index) {
      if (target_cell_index >= 0 && static_cast<size_t>(target_cell_index) < remap.size()) {
        adjusted_target_index = remap[static_cast<size_t>(target_cell_index)];
        if (adjusted_target_index < 0) {
          // Target was removed (shouldn't happen, but handle it)
          return false;
        }
      }
    }

    // Update zen index if needed
    if (src_cluster.zen_cell_index.has_value()) {
      int old_zen = *src_cluster.zen_cell_index;
      if (old_zen >= 0 && static_cast<size_t>(old_zen) < remap.size()) {
        int new_zen = remap[static_cast<size_t>(old_zen)];
        if (new_zen >= 0) {
          src_cluster.zen_cell_index = new_zen;
        } else {
          src_cluster.zen_cell_index = std::nullopt;
        }
      }
    }
  }

  // Now split target to insert source
  // Determine split direction
  SplitDir split_dir = determine_split_dir(tgt_cluster, adjusted_target_index, system.split_mode);

  // Call split_leaf to split target and add source
  // Get target's current leaf_id
  // The split_leaf function will create two children:
  // - first_child gets target's original leaf_id
  // - second_child gets source's leaf_id
  auto result_opt = split_leaf(tgt_cluster, adjusted_target_index, 0.0f, 0.0f,
                               source_leaf_id.value_or(0), split_dir);

  if (!result_opt.has_value()) {
    return false;
  }

  // The new cell is the second child of the split
  int new_selection_index = result_opt->new_selection_index;

  // Get the second child (where source ended up)
  auto second_child_opt = tgt_cluster.tree.get_second_child(adjusted_target_index);
  int new_cell_index = second_child_opt.value_or(new_selection_index);

  // Update selection
  if (source_was_selected) {
    system.selection = CellIndicatorByIndex{target_cluster_index, new_cell_index};
  } else if (target_was_selected) {
    // Target becomes first child
    system.selection = CellIndicatorByIndex{target_cluster_index, new_selection_index};
  }

  return true;
}

// ============================================================================
// Zen Mode
// ============================================================================

bool set_zen(System& system, int cluster_index, int cell_index) {
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return false;
  }
  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)].cluster;
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return false;
  }
  cluster.zen_cell_index = cell_index;
  return true;
}

void clear_zen(System& system, int cluster_index) {
  assert(cluster_index >= 0 && static_cast<size_t>(cluster_index) < system.clusters.size());
  system.clusters[static_cast<size_t>(cluster_index)].cluster.zen_cell_index.reset();
}

bool is_cell_zen(const System& system, int cluster_index, int cell_index) {
  assert(cluster_index >= 0 && static_cast<size_t>(cluster_index) < system.clusters.size());
  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)].cluster;
  return cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == cell_index;
}

bool toggle_selected_zen(System& system) {
  if (!system.selection.has_value()) {
    return false;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;

  assert(cluster_index >= 0 && static_cast<size_t>(cluster_index) < system.clusters.size());
  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)].cluster;

  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return false;
  }

  // Toggle: if already zen, clear; otherwise set
  if (cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == cell_index) {
    cluster.zen_cell_index.reset();
  } else {
    cluster.zen_cell_index = cell_index;
  }

  return true;
}

} // namespace wintiler::ctrl
