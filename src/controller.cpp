#include "controller.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <set>

namespace wintiler::ctrl {

// ============================================================================
// Query Functions
// ============================================================================

bool is_leaf(const Cluster& cluster, int cell_index) {
  return cluster.tree.is_leaf(cell_index);
}

// ============================================================================
// Internal Helpers
// ============================================================================

// Determine split direction based on split mode and current selection
static SplitDir determine_split_dir(const Cluster& cluster, int selected_index, SplitMode mode) {
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
static std::optional<SplitResult> split_leaf(Cluster& cluster, int selected_index,
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

  // Return second child as new selection (the newly added window)
  return SplitResult{new_leaf_id, second_child_index};
}

// Pre-create leaves in a cluster from initial cell IDs
// Returns the selection index (or -1 if no cells created)
static int pre_create_leaves(Cluster& cluster, const std::vector<size_t>& cell_ids,
                             SplitMode mode) {
  int current_selection = -1;

  for (size_t cell_id : cell_ids) {
    // Determine split direction based on mode
    SplitDir split_dir = determine_split_dir(cluster, current_selection, mode);

    if (cluster.tree.empty()) {
      // First cell: create root leaf (pass -1 for empty cluster)
      auto result_opt = split_leaf(cluster, -1, cell_id, split_dir);
      if (result_opt.has_value()) {
        current_selection = result_opt->new_selection_index;
      }
    } else {
      // Subsequent cells: split current selection
      auto result_opt = split_leaf(cluster, current_selection, cell_id, split_dir);
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

System create_system(const std::vector<ClusterInitInfo>& infos) {
  System system;
  system.clusters.reserve(infos.size());

  for (size_t cluster_index = 0; cluster_index < infos.size(); ++cluster_index) {
    const auto& info = infos[cluster_index];

    Cluster cluster;
    cluster.global_x = info.x;
    cluster.global_y = info.y;
    cluster.monitor_x = info.monitor_x;
    cluster.monitor_y = info.monitor_y;
    cluster.monitor_width = info.monitor_width;
    cluster.monitor_height = info.monitor_height;

    // Initialize cluster dimensions
    cluster.window_width = info.width;
    cluster.window_height = info.height;

    int selection_index = -1;

    // Pre-create leaves if initial_cell_ids provided
    if (!info.initial_cell_ids.empty()) {
      selection_index = pre_create_leaves(cluster, info.initial_cell_ids, system.split_mode);
    }

    // If this is the first cluster with cells, make it the selected cluster
    if (!system.selection.has_value() && selection_index >= 0) {
      system.selection = CellIndicatorByIndex{static_cast<int>(cluster_index), selection_index};
    }

    system.clusters.push_back(std::move(cluster));
  }

  return system;
}

// ============================================================================
// Cell Operations
// ============================================================================

bool delete_leaf(Cluster& cluster, int cell_index) {
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

  auto& cluster1 = system.clusters[static_cast<size_t>(cluster_index1)];
  auto& cluster2 = system.clusters[static_cast<size_t>(cluster_index2)];

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

  auto& src_cluster = system.clusters[static_cast<size_t>(source_cluster_index)];
  auto& tgt_cluster = system.clusters[static_cast<size_t>(target_cluster_index)];

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
  auto result_opt =
      split_leaf(tgt_cluster, adjusted_target_index, source_leaf_id.value_or(0), split_dir);

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
  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return false;
  }
  cluster.zen_cell_index = cell_index;
  return true;
}

void clear_zen(System& system, int cluster_index) {
  assert(cluster_index >= 0 && static_cast<size_t>(cluster_index) < system.clusters.size());
  system.clusters[static_cast<size_t>(cluster_index)].zen_cell_index.reset();
}

bool is_cell_zen(const System& system, int cluster_index, int cell_index) {
  assert(cluster_index >= 0 && static_cast<size_t>(cluster_index) < system.clusters.size());
  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  return cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == cell_index;
}

bool toggle_selected_zen(System& system) {
  if (!system.selection.has_value()) {
    return false;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;

  assert(cluster_index >= 0 && static_cast<size_t>(cluster_index) < system.clusters.size());
  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];

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

// ============================================================================
// Query Functions (additional)
// ============================================================================

std::optional<int> find_cell_by_leaf_id(const Cluster& cluster, size_t leaf_id) {
  for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
    if (cluster.tree.is_leaf(i) && cluster.tree[i].leaf_id.has_value() &&
        *cluster.tree[i].leaf_id == leaf_id) {
      return i;
    }
  }
  return std::nullopt;
}

std::vector<size_t> get_cluster_leaf_ids(const Cluster& cluster) {
  std::vector<size_t> leaf_ids;
  for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
    if (cluster.tree.is_leaf(i) && cluster.tree[i].leaf_id.has_value()) {
      leaf_ids.push_back(*cluster.tree[i].leaf_id);
    }
  }
  return leaf_ids;
}

// Helper: recursively compute child rectangles
static void compute_children_rects(const Cluster& cluster, int node_index, std::vector<Rect>& rects,
                                   float gap_h, float gap_v) {
  auto first_opt = cluster.tree.get_first_child(node_index);
  auto second_opt = cluster.tree.get_second_child(node_index);

  if (!first_opt.has_value() || !second_opt.has_value()) {
    return;
  }

  const Rect& parent = rects[static_cast<size_t>(node_index)];
  const CellData& data = cluster.tree[node_index];

  if (data.split_dir == SplitDir::Vertical) {
    float available = parent.width - gap_h;
    float first_w = available > 0.0f ? available * data.split_ratio : 0.0f;
    float second_w = available > 0.0f ? available * (1.0f - data.split_ratio) : 0.0f;

    rects[static_cast<size_t>(*first_opt)] = {parent.x, parent.y, first_w, parent.height};
    rects[static_cast<size_t>(*second_opt)] = {parent.x + first_w + gap_h, parent.y, second_w,
                                               parent.height};
  } else {
    float available = parent.height - gap_v;
    float first_h = available > 0.0f ? available * data.split_ratio : 0.0f;
    float second_h = available > 0.0f ? available * (1.0f - data.split_ratio) : 0.0f;

    rects[static_cast<size_t>(*first_opt)] = {parent.x, parent.y, parent.width, first_h};
    rects[static_cast<size_t>(*second_opt)] = {parent.x, parent.y + first_h + gap_v, parent.width,
                                               second_h};
  }

  // Recurse into children
  compute_children_rects(cluster, *first_opt, rects, gap_h, gap_v);
  compute_children_rects(cluster, *second_opt, rects, gap_h, gap_v);
}

std::vector<Rect> compute_cluster_geometry(const Cluster& cluster, float gap_h, float gap_v,
                                           float zen_percentage) {
  // Allocate output vector, initialized to empty rects
  std::vector<Rect> rects(cluster.tree.size(), Rect{0.0f, 0.0f, 0.0f, 0.0f});

  if (cluster.tree.empty()) {
    return rects;
  }

  // Compute root rect with outer gaps (in global coordinates)
  float root_w = cluster.window_width - 2.0f * gap_h;
  float root_h = cluster.window_height - 2.0f * gap_v;
  rects[0] = Rect{cluster.global_x + gap_h, cluster.global_y + gap_v, root_w > 0.0f ? root_w : 0.0f,
                  root_h > 0.0f ? root_h : 0.0f};

  // Recursively compute child rects starting from root (index 0)
  compute_children_rects(cluster, 0, rects, gap_h, gap_v);

  // Handle zen mode: override zen cell with centered rect (in global coordinates)
  if (cluster.zen_cell_index.has_value()) {
    int zen_idx = *cluster.zen_cell_index;
    if (cluster.tree.is_valid_index(zen_idx) && cluster.tree.is_leaf(zen_idx)) {
      float zen_w = cluster.window_width * zen_percentage;
      float zen_h = cluster.window_height * zen_percentage;
      float offset_x = (cluster.window_width - zen_w) / 2.0f;
      float offset_y = (cluster.window_height - zen_h) / 2.0f;
      rects[static_cast<size_t>(zen_idx)] =
          Rect{cluster.global_x + offset_x, cluster.global_y + offset_y, zen_w, zen_h};
    }
  }

  // Clear internal node rects (only keep leaf rects)
  for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
    if (!cluster.tree.is_leaf(i)) {
      rects[static_cast<size_t>(i)] = Rect{0.0f, 0.0f, 0.0f, 0.0f};
    }
  }

  return rects;
}

// Find any leaf in the cluster
static std::optional<int> find_any_leaf(const Cluster& cluster) {
  for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
    if (cluster.tree.is_leaf(i)) {
      return i;
    }
  }
  return std::nullopt;
}

// ============================================================================
// Geometric Navigation Helpers
// ============================================================================

// Check if a rect is valid (non-empty)
static bool is_valid_rect(const Rect& r) {
  return r.width > 0.0f && r.height > 0.0f;
}

// Check if 'to' rect is in the given direction from 'from' rect
static bool is_in_direction(const Rect& from, const Rect& to, Direction dir) {
  switch (dir) {
  case Direction::Left:
    return to.x + to.width <= from.x;
  case Direction::Right:
    return to.x >= from.x + from.width;
  case Direction::Up:
    return to.y + to.height <= from.y;
  case Direction::Down:
    return to.y >= from.y + from.height;
  default:
    return false;
  }
}

// Calculate directional distance with overlap preference
// Returns a score where lower is better
static float directional_distance(const Rect& from, const Rect& to, Direction dir) {
  float dx_center = (to.x + to.width * 0.5f) - (from.x + from.width * 0.5f);
  float dy_center = (to.y + to.height * 0.5f) - (from.y + from.height * 0.5f);

  // Check for perpendicular overlap - cells that share vertical/horizontal space
  // are strongly preferred over cells that don't
  bool has_vertical_overlap = (to.y < from.y + from.height) && (to.y + to.height > from.y);
  bool has_horizontal_overlap = (to.x < from.x + from.width) && (to.x + to.width > from.x);

  switch (dir) {
  case Direction::Left:
  case Direction::Right: {
    float primary_dist = (dir == Direction::Left) ? -dx_center : dx_center;
    if (has_vertical_overlap) {
      return primary_dist; // Overlapping cells get pure horizontal distance
    }
    // Non-overlapping cells get a large penalty
    float gap =
        std::min(std::abs(to.y - (from.y + from.height)), std::abs(from.y - (to.y + to.height)));
    return primary_dist + 10000.0f + gap;
  }
  case Direction::Up:
  case Direction::Down: {
    float primary_dist = (dir == Direction::Up) ? -dy_center : dy_center;
    if (has_horizontal_overlap) {
      return primary_dist; // Overlapping cells get pure vertical distance
    }
    float gap =
        std::min(std::abs(to.x - (from.x + from.width)), std::abs(from.x - (to.x + to.width)));
    return primary_dist + 10000.0f + gap;
  }
  default:
    return std::numeric_limits<float>::max();
  }
}

// ============================================================================
// Selection Navigation
// ============================================================================

std::optional<CellIndicatorByIndex>
move_selection(System& system, Direction dir,
               const std::vector<std::vector<Rect>>& cell_geometries) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int current_cluster = system.selection->cluster_index;
  int current_cell = system.selection->cell_index;

  // Validate current selection and get its geometry
  if (current_cluster < 0 || static_cast<size_t>(current_cluster) >= cell_geometries.size()) {
    return std::nullopt;
  }
  if (current_cell < 0 || static_cast<size_t>(current_cell) >=
                              cell_geometries[static_cast<size_t>(current_cluster)].size()) {
    return std::nullopt;
  }

  const Rect& current_rect =
      cell_geometries[static_cast<size_t>(current_cluster)][static_cast<size_t>(current_cell)];
  if (!is_valid_rect(current_rect)) {
    return std::nullopt;
  }

  // Find best candidate using geometric navigation
  std::optional<CellIndicatorByIndex> best_candidate;
  float best_score = std::numeric_limits<float>::max();

  for (size_t ci = 0; ci < cell_geometries.size(); ++ci) {
    if (ci >= system.clusters.size()) {
      continue;
    }
    const auto& cluster = system.clusters[ci];
    const auto& cluster_rects = cell_geometries[ci];

    // ZEN MODE HANDLING: If cluster has zen, only consider the zen cell
    if (cluster.zen_cell_index.has_value()) {
      int zen_idx = *cluster.zen_cell_index;

      // Skip if this is the current cell
      if (static_cast<int>(ci) == current_cluster && zen_idx == current_cell) {
        continue;
      }

      // Check if zen cell has valid geometry
      if (zen_idx < 0 || static_cast<size_t>(zen_idx) >= cluster_rects.size()) {
        continue;
      }
      const Rect& zen_rect = cluster_rects[static_cast<size_t>(zen_idx)];
      if (!is_valid_rect(zen_rect)) {
        continue;
      }

      // Check if in direction and score
      if (!is_in_direction(current_rect, zen_rect, dir)) {
        continue;
      }

      float score = directional_distance(current_rect, zen_rect, dir);
      if (score < best_score) {
        best_score = score;
        best_candidate = CellIndicatorByIndex{static_cast<int>(ci), zen_idx};
      }
      continue; // Skip normal leaf iteration for this cluster
    }

    // No zen cell: search all leaves in this cluster
    for (size_t cell_idx = 0; cell_idx < cluster_rects.size(); ++cell_idx) {
      // Skip current cell
      if (static_cast<int>(ci) == current_cluster && static_cast<int>(cell_idx) == current_cell) {
        continue;
      }

      const Rect& candidate_rect = cluster_rects[cell_idx];
      if (!is_valid_rect(candidate_rect)) {
        continue;
      }

      if (!is_in_direction(current_rect, candidate_rect, dir)) {
        continue;
      }

      float score = directional_distance(current_rect, candidate_rect, dir);
      if (score < best_score) {
        best_score = score;
        best_candidate = CellIndicatorByIndex{static_cast<int>(ci), static_cast<int>(cell_idx)};
      }
    }
  }

  if (best_candidate.has_value()) {
    system.selection = best_candidate;

    // Clear zen if moving to a non-zen cell in a zen cluster
    auto& new_cluster = system.clusters[static_cast<size_t>(best_candidate->cluster_index)];
    if (new_cluster.zen_cell_index.has_value() &&
        *new_cluster.zen_cell_index != best_candidate->cell_index) {
      new_cluster.zen_cell_index.reset();
    }

    return best_candidate;
  }

  return std::nullopt;
}

// ============================================================================
// Split Operations
// ============================================================================

bool toggle_selected_split_dir(System& system) {
  if (!system.selection.has_value()) {
    return false;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;

  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return false;
  }

  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];

  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return false;
  }

  auto parent_opt = cluster.tree.get_parent(cell_index);
  if (!parent_opt.has_value()) {
    return false; // Root leaf has no parent
  }

  int parent_index = *parent_opt;

  // Verify both children are leaves (can only toggle when splitting two leaves)
  auto first_child = cluster.tree.get_first_child(parent_index);
  auto second_child = cluster.tree.get_second_child(parent_index);

  if (!first_child.has_value() || !second_child.has_value()) {
    return false;
  }

  if (!cluster.tree.is_leaf(*first_child) || !cluster.tree.is_leaf(*second_child)) {
    return false;
  }

  // Toggle the split direction
  CellData& parent_data = cluster.tree[parent_index];
  parent_data.split_dir =
      (parent_data.split_dir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;

  return true;
}

bool cycle_split_mode(System& system) {
  switch (system.split_mode) {
  case SplitMode::Zigzag:
    system.split_mode = SplitMode::Vertical;
    break;
  case SplitMode::Vertical:
    system.split_mode = SplitMode::Horizontal;
    break;
  case SplitMode::Horizontal:
    system.split_mode = SplitMode::Zigzag;
    break;
  }
  return true;
}

bool set_selected_split_ratio(System& system, float new_ratio) {
  if (!system.selection.has_value()) {
    return false;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;

  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return false;
  }

  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];

  if (!cluster.tree.is_valid_index(cell_index)) {
    return false;
  }

  // If selected cell is a leaf, get its parent
  int parent_index = cell_index;
  if (cluster.tree.is_leaf(cell_index)) {
    auto parent_opt = cluster.tree.get_parent(cell_index);
    if (!parent_opt.has_value()) {
      return false; // Root leaf has no parent to adjust
    }
    parent_index = *parent_opt;
  }

  // Clamp ratio to valid range
  float clamped_ratio = std::clamp(new_ratio, 0.1f, 0.9f);

  cluster.tree[parent_index].split_ratio = clamped_ratio;
  return true;
}

bool adjust_selected_split_ratio(System& system, float delta) {
  if (!system.selection.has_value()) {
    return false;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;

  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return false;
  }

  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];

  if (!cluster.tree.is_valid_index(cell_index)) {
    return false;
  }

  // If selected cell is a leaf, get its parent
  int parent_index = cell_index;
  if (cluster.tree.is_leaf(cell_index)) {
    auto parent_opt = cluster.tree.get_parent(cell_index);
    if (!parent_opt.has_value()) {
      return false; // Root leaf has no parent to adjust
    }
    parent_index = *parent_opt;
  }

  // If selected is second child, negate delta so positive = grow selected
  float adjusted_delta = delta;
  auto second_child = cluster.tree.get_second_child(parent_index);
  if (second_child.has_value() && *second_child == cell_index) {
    adjusted_delta = -delta;
  }

  float new_ratio = cluster.tree[parent_index].split_ratio + adjusted_delta;
  float clamped_ratio = std::clamp(new_ratio, 0.1f, 0.9f);

  cluster.tree[parent_index].split_ratio = clamped_ratio;
  return true;
}

// ============================================================================
// System State Updates
// ============================================================================

bool update(System& system, const std::vector<ClusterCellUpdateInfo>& cluster_updates,
            std::optional<int> redirect_cluster_index) {
  bool updated = false;

  // Make a mutable copy for redirection
  std::vector<ClusterCellUpdateInfo> redirected_updates = cluster_updates;

  // If redirect_cluster_index is provided, find new windows and redirect them
  if (redirect_cluster_index.has_value()) {
    int target_idx = *redirect_cluster_index;
    if (target_idx >= 0 && static_cast<size_t>(target_idx) < system.clusters.size()) {
      // Find windows that don't exist in any cluster yet
      std::vector<size_t> new_windows;
      for (const auto& upd : redirected_updates) {
        for (size_t leaf_id : upd.leaf_ids) {
          bool exists = false;
          for (const auto& cluster : system.clusters) {
            if (find_cell_by_leaf_id(cluster, leaf_id).has_value()) {
              exists = true;
              break;
            }
          }
          if (!exists) {
            new_windows.push_back(leaf_id);
          }
        }
      }

      // Remove new windows from all clusters and add to target
      if (!new_windows.empty()) {
        for (auto& upd : redirected_updates) {
          auto& ids = upd.leaf_ids;
          ids.erase(std::remove_if(ids.begin(), ids.end(),
                                   [&new_windows](size_t id) {
                                     return std::find(new_windows.begin(), new_windows.end(), id) !=
                                            new_windows.end();
                                   }),
                    ids.end());
        }

        // Add to target cluster
        if (static_cast<size_t>(target_idx) < redirected_updates.size()) {
          for (size_t id : new_windows) {
            redirected_updates[static_cast<size_t>(target_idx)].leaf_ids.push_back(id);
          }
        }
      }
    }
  }

  // Process each cluster update (vector index = cluster index)
  for (size_t cluster_idx = 0; cluster_idx < redirected_updates.size(); ++cluster_idx) {
    if (cluster_idx >= system.clusters.size()) {
      continue;
    }

    const auto& cluster_update = redirected_updates[cluster_idx];
    auto& cluster = system.clusters[cluster_idx];

    // Update fullscreen state
    cluster.has_fullscreen_cell = cluster_update.has_fullscreen_cell;

    // Get current leaf IDs
    std::vector<size_t> current_leaf_ids = get_cluster_leaf_ids(cluster);

    // Compute set differences
    std::vector<size_t> sorted_current = current_leaf_ids;
    std::vector<size_t> sorted_desired = cluster_update.leaf_ids;
    std::sort(sorted_current.begin(), sorted_current.end());
    std::sort(sorted_desired.begin(), sorted_desired.end());

    // to_delete = current - desired
    std::vector<size_t> to_delete;
    std::set_difference(sorted_current.begin(), sorted_current.end(), sorted_desired.begin(),
                        sorted_desired.end(), std::back_inserter(to_delete));

    // to_add = desired - current
    std::vector<size_t> to_add;
    std::set_difference(sorted_desired.begin(), sorted_desired.end(), sorted_current.begin(),
                        sorted_current.end(), std::back_inserter(to_add));

    // Handle deletions
    for (size_t leaf_id : to_delete) {
      auto cell_index_opt = find_cell_by_leaf_id(cluster, leaf_id);
      if (!cell_index_opt.has_value()) {
        continue;
      }

      int cell_idx = *cell_index_opt;

      // Check if this was the selected cell
      bool was_selected = system.selection.has_value() &&
                          system.selection->cluster_index == static_cast<int>(cluster_idx) &&
                          system.selection->cell_index == cell_idx;

      // Get parent before deletion (sibling will move here)
      auto parent_opt = cluster.tree.get_parent(cell_idx);

      if (delete_leaf(cluster, cell_idx)) {
        updated = true;

        // Update selection if deleted cell was selected
        if (was_selected) {
          if (parent_opt.has_value()) {
            // After deletion, sibling moved to parent's position
            system.selection = CellIndicatorByIndex{static_cast<int>(cluster_idx), *parent_opt};
          } else {
            // Root was deleted, find any remaining leaf
            auto new_leaf = find_any_leaf(cluster);
            if (new_leaf.has_value()) {
              system.selection = CellIndicatorByIndex{static_cast<int>(cluster_idx), *new_leaf};
            } else {
              system.selection.reset();
            }
          }
        }
      }
    }

    // Determine starting split index for additions (prefer selection)
    int split_from_index = -1;
    if (system.selection.has_value() &&
        system.selection->cluster_index == static_cast<int>(cluster_idx) &&
        cluster.tree.is_valid_index(system.selection->cell_index) &&
        cluster.tree.is_leaf(system.selection->cell_index)) {
      split_from_index = system.selection->cell_index;
    }

    // Handle additions
    for (size_t leaf_id : to_add) {
      int current_selection = -1;

      if (cluster.tree.empty()) {
        // Cluster is empty - will create root
        current_selection = -1;
      } else if (split_from_index >= 0 && cluster.tree.is_valid_index(split_from_index) &&
                 cluster.tree.is_leaf(split_from_index)) {
        current_selection = split_from_index;
      } else {
        // Fallback: find any leaf
        auto leaf_opt = find_any_leaf(cluster);
        if (leaf_opt.has_value()) {
          current_selection = *leaf_opt;
        }
      }

      // Determine split direction
      SplitDir split_dir = determine_split_dir(cluster, current_selection, system.split_mode);

      auto result_opt = split_leaf(cluster, current_selection, leaf_id, split_dir);
      if (result_opt.has_value()) {
        split_from_index = result_opt->new_selection_index;
        system.selection = CellIndicatorByIndex{static_cast<int>(cluster_idx), split_from_index};
        updated = true;
      }
    }

    // Reset zen if cells were added or removed
    if (!to_delete.empty() || !to_add.empty()) {
      cluster.zen_cell_index.reset();
    }
  }

  return updated;
}

// ============================================================================
// Validation Helpers
// ============================================================================

static bool validate_cluster(const Cluster& cluster) {
  // Validate tree structure
  for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
    // Check parent-child relationship consistency
    auto parent_opt = cluster.tree.get_parent(i);
    if (parent_opt.has_value()) {
      int parent_idx = *parent_opt;
      auto first_child = cluster.tree.get_first_child(parent_idx);
      auto second_child = cluster.tree.get_second_child(parent_idx);

      bool is_child_of_parent = (first_child.has_value() && *first_child == i) ||
                                (second_child.has_value() && *second_child == i);

      if (!is_child_of_parent) {
        spdlog::error("[validate_cluster] Node {} claims parent {} but is not a child of it", i,
                      parent_idx);
        return false;
      }
    }

    // Leaf nodes must have leaf_id
    if (cluster.tree.is_leaf(i)) {
      if (!cluster.tree[i].leaf_id.has_value()) {
        spdlog::error("[validate_cluster] Leaf node {} has no leaf_id", i);
        return false;
      }
    } else {
      // Internal nodes must not have leaf_id
      if (cluster.tree[i].leaf_id.has_value()) {
        spdlog::error("[validate_cluster] Internal node {} has leaf_id", i);
        return false;
      }
    }
  }

  // Check zen_cell_index points to valid leaf
  if (cluster.zen_cell_index.has_value()) {
    int zen_idx = *cluster.zen_cell_index;
    if (!cluster.tree.is_valid_index(zen_idx) || !cluster.tree.is_leaf(zen_idx)) {
      spdlog::error("[validate_cluster] zen_cell_index {} is invalid or not a leaf", zen_idx);
      return false;
    }
  }

  return true;
}

// ============================================================================
// Utilities
// ============================================================================

bool validate_system(const System& system) {
  bool ok = true;

  spdlog::debug("===== Validating System =====");
  spdlog::debug("Total clusters: {}", system.clusters.size());

  if (system.selection.has_value()) {
    spdlog::debug("selection: cluster={}, cell_index={}", system.selection->cluster_index,
                  system.selection->cell_index);
  } else {
    spdlog::debug("selection: null");
  }

  // Check that selection points to a valid cluster and cell
  if (system.selection.has_value()) {
    if (system.selection->cluster_index < 0 ||
        static_cast<size_t>(system.selection->cluster_index) >= system.clusters.size()) {
      spdlog::error("[validate] ERROR: selection points to non-existent cluster");
      ok = false;
    } else {
      const auto& sel_cluster =
          system.clusters[static_cast<size_t>(system.selection->cluster_index)];
      if (!sel_cluster.tree.is_valid_index(system.selection->cell_index) ||
          !sel_cluster.tree.is_leaf(system.selection->cell_index)) {
        spdlog::error("[validate] ERROR: selection points to non-leaf cell");
        ok = false;
      }
    }
  }

  // Validate each cluster
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& cluster = system.clusters[ci];
    spdlog::debug("--- Cluster {} at ({}, {}) ---", ci, cluster.global_x, cluster.global_y);
    if (!validate_cluster(cluster)) {
      ok = false;
    }
  }

  // Check for duplicate leaf_ids across all clusters
  std::vector<size_t> all_leaf_ids;
  for (const auto& cluster : system.clusters) {
    auto ids = get_cluster_leaf_ids(cluster);
    all_leaf_ids.insert(all_leaf_ids.end(), ids.begin(), ids.end());
  }
  std::sort(all_leaf_ids.begin(), all_leaf_ids.end());
  for (size_t i = 1; i < all_leaf_ids.size(); ++i) {
    if (all_leaf_ids[i] == all_leaf_ids[i - 1]) {
      spdlog::error("[validate] ERROR: duplicate leaf_id {} across clusters", all_leaf_ids[i]);
      ok = false;
    }
  }

  if (ok) {
    spdlog::debug("[validate] System OK");
  } else {
    spdlog::warn("[validate] System has anomalies");
  }

  spdlog::debug("===== End Validation =====");

  return ok;
}

static void debug_print_cluster(const Cluster& cluster) {
  spdlog::debug("  tree.size = {}", cluster.tree.size());
  spdlog::debug("  window_width = {}, window_height = {}", cluster.window_width,
                cluster.window_height);

  if (cluster.zen_cell_index.has_value()) {
    spdlog::debug("  zen_cell_index = {}", *cluster.zen_cell_index);
  } else {
    spdlog::debug("  zen_cell_index = null");
  }

  spdlog::debug("  has_fullscreen_cell = {}", cluster.has_fullscreen_cell);

  // Print tree structure
  for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
    const auto& node = cluster.tree.node(i);
    const auto& data = cluster.tree[i];

    std::string parent_str = node.parent.has_value() ? std::to_string(*node.parent) : "null";
    std::string first_str =
        node.first_child.has_value() ? std::to_string(*node.first_child) : "null";
    std::string second_str =
        node.second_child.has_value() ? std::to_string(*node.second_child) : "null";
    std::string leaf_str = data.leaf_id.has_value() ? std::to_string(*data.leaf_id) : "null";

    spdlog::debug("  [{}] parent={}, first={}, second={}, split_dir={}, ratio={:.2f}, leaf_id={}",
                  i, parent_str, first_str, second_str, magic_enum::enum_name(data.split_dir),
                  data.split_ratio, leaf_str);
  }
}

void debug_print_system(const System& system) {
  spdlog::debug("===== System =====");
  spdlog::debug("clusters.size = {}", system.clusters.size());
  spdlog::debug("split_mode = {}", magic_enum::enum_name(system.split_mode));

  if (system.selection.has_value()) {
    spdlog::debug("selection = cluster={}, cell_index={}", system.selection->cluster_index,
                  system.selection->cell_index);
  } else {
    spdlog::debug("selection = null");
  }

  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& cluster = system.clusters[ci];
    spdlog::debug("--- Cluster {} ---", ci);
    spdlog::debug("  global_x = {}, global_y = {}", cluster.global_x, cluster.global_y);
    debug_print_cluster(cluster);
  }

  spdlog::debug("===== End System =====");
}

// ============================================================================
// Additional Query Functions
// ============================================================================

bool has_leaf_id(const System& system, size_t leaf_id) {
  for (const auto& cluster : system.clusters) {
    if (find_cell_by_leaf_id(cluster, leaf_id).has_value()) {
      return true;
    }
  }
  return false;
}

Point get_rect_center(const Rect& rect) {
  return Point{static_cast<long>(rect.x + rect.width / 2.0f),
               static_cast<long>(rect.y + rect.height / 2.0f)};
}

// ============================================================================
// Drop Move Operation
// ============================================================================

// Find cell at global point using precomputed geometries
static std::optional<std::pair<int, int>>
find_cell_at_point(const System& system, const std::vector<std::vector<Rect>>& geometries,
                   float global_x, float global_y) {
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& cluster = system.clusters[ci];

    // If cluster has zen cell, only check zen cell's rect
    if (cluster.zen_cell_index.has_value()) {
      int zen_idx = *cluster.zen_cell_index;
      if (ci < geometries.size() && static_cast<size_t>(zen_idx) < geometries[ci].size()) {
        const auto& r = geometries[ci][static_cast<size_t>(zen_idx)];
        if (global_x >= r.x && global_x < r.x + r.width && global_y >= r.y &&
            global_y < r.y + r.height) {
          return std::make_pair(static_cast<int>(ci), zen_idx);
        }
      }
      continue;
    }

    // Check all leaves in this cluster
    if (ci >= geometries.size()) {
      continue;
    }
    for (int i = 0; i < static_cast<int>(geometries[ci].size()); ++i) {
      if (!cluster.tree.is_valid_index(i) || !cluster.tree.is_leaf(i)) {
        continue;
      }
      const auto& r = geometries[ci][static_cast<size_t>(i)];
      if (r.width <= 0.0f || r.height <= 0.0f) {
        continue;
      }
      if (global_x >= r.x && global_x < r.x + r.width && global_y >= r.y &&
          global_y < r.y + r.height) {
        return std::make_pair(static_cast<int>(ci), i);
      }
    }
  }
  return std::nullopt;
}

// Find cluster index containing a leaf_id
static std::optional<int> find_cluster_by_leaf_id(const System& system, size_t leaf_id) {
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    if (find_cell_by_leaf_id(system.clusters[ci], leaf_id).has_value()) {
      return static_cast<int>(ci);
    }
  }
  return std::nullopt;
}

std::optional<DropMoveResult> perform_drop_move(System& system, size_t source_leaf_id,
                                                float cursor_x, float cursor_y,
                                                const std::vector<std::vector<Rect>>& geometries,
                                                bool do_exchange) {
  // Check if source window is managed by the system
  if (!has_leaf_id(system, source_leaf_id)) {
    return std::nullopt;
  }

  // Find source cluster and cell
  auto source_cluster_opt = find_cluster_by_leaf_id(system, source_leaf_id);
  if (!source_cluster_opt.has_value()) {
    return std::nullopt;
  }
  int source_cluster_index = *source_cluster_opt;
  auto source_cell_opt = find_cell_by_leaf_id(
      system.clusters[static_cast<size_t>(source_cluster_index)], source_leaf_id);
  if (!source_cell_opt.has_value()) {
    return std::nullopt;
  }
  int source_cell_index = *source_cell_opt;

  // Find target cell at cursor position
  auto target_opt = find_cell_at_point(system, geometries, cursor_x, cursor_y);
  if (!target_opt.has_value()) {
    return std::nullopt;
  }
  auto [target_cluster_index, target_cell_index] = *target_opt;

  // Skip if target cluster has fullscreen app
  if (system.clusters[static_cast<size_t>(target_cluster_index)].has_fullscreen_cell) {
    return std::nullopt;
  }

  // Skip if dropping on same cell
  if (source_cluster_index == target_cluster_index && source_cell_index == target_cell_index) {
    return std::nullopt;
  }

  bool success = false;
  if (do_exchange) {
    success = swap_cells(system, source_cluster_index, source_cell_index, target_cluster_index,
                         target_cell_index);
  } else {
    success = move_cell(system, source_cluster_index, source_cell_index, target_cluster_index,
                        target_cell_index);
  }

  if (!success) {
    return std::nullopt;
  }

  // Find the result cell's center (the source leaf_id is now at a new location)
  auto new_cluster_opt = find_cluster_by_leaf_id(system, source_leaf_id);
  if (!new_cluster_opt.has_value()) {
    return std::nullopt;
  }
  int new_cluster = *new_cluster_opt;
  auto new_cell_opt =
      find_cell_by_leaf_id(system.clusters[static_cast<size_t>(new_cluster)], source_leaf_id);
  if (!new_cell_opt.has_value()) {
    return std::nullopt;
  }
  int new_cell = *new_cell_opt;

  // Get the cell's rect from the geometry (need to recompute since tree changed)
  // For now, use the target's rect as approximation
  if (static_cast<size_t>(new_cluster) < geometries.size() &&
      static_cast<size_t>(new_cell) < geometries[static_cast<size_t>(new_cluster)].size()) {
    const auto& rect = geometries[static_cast<size_t>(new_cluster)][static_cast<size_t>(new_cell)];
    return DropMoveResult{get_rect_center(rect), do_exchange};
  }

  // Fallback: return cursor position
  return DropMoveResult{Point{static_cast<long>(cursor_x), static_cast<long>(cursor_y)},
                        do_exchange};
}

// ============================================================================
// Resize-based Split Ratio Update
// ============================================================================

// Edge types for resize detection
enum class EdgeType { Left, Right, Top, Bottom };

// Calculate new ratio based on edge position change
static float calculate_new_ratio_from_edge(const Rect& parent_rect, EdgeType edge,
                                           const Rect& actual_rect, float gap_h, float gap_v) {
  if (edge == EdgeType::Left || edge == EdgeType::Right) {
    float available = parent_rect.width - gap_h;
    if (available <= 0.0f) {
      return 0.5f;
    }
    if (edge == EdgeType::Left) {
      float first_width = actual_rect.x - parent_rect.x;
      return first_width / available;
    } else {
      float actual_right = actual_rect.x + actual_rect.width;
      float parent_right = parent_rect.x + parent_rect.width;
      float second_width = parent_right - actual_right;
      return 1.0f - (second_width / available);
    }
  } else {
    float available = parent_rect.height - gap_v;
    if (available <= 0.0f) {
      return 0.5f;
    }
    if (edge == EdgeType::Top) {
      float first_height = actual_rect.y - parent_rect.y;
      return first_height / available;
    } else {
      float actual_bottom = actual_rect.y + actual_rect.height;
      float parent_bottom = parent_rect.y + parent_rect.height;
      float second_height = parent_bottom - actual_bottom;
      return 1.0f - (second_height / available);
    }
  }
}

// Update ratio for a single edge by finding the controlling ancestor
static bool update_ratio_for_edge(Cluster& cluster, const std::vector<Rect>& cluster_geometry,
                                  int start_cell_index, EdgeType edge, const Rect& actual_rect,
                                  float gap_h, float gap_v) {
  SplitDir required_dir = (edge == EdgeType::Left || edge == EdgeType::Right)
                              ? SplitDir::Vertical
                              : SplitDir::Horizontal;
  bool need_from_second = (edge == EdgeType::Left || edge == EdgeType::Top);

  int current_index = start_cell_index;
  while (true) {
    auto parent_opt = cluster.tree.get_parent(current_index);
    if (!parent_opt.has_value()) {
      return false;
    }
    int parent_index = *parent_opt;

    if (!cluster.tree.is_valid_index(parent_index)) {
      return false;
    }

    const CellData& parent_data = cluster.tree[parent_index];
    auto first_child = cluster.tree.get_first_child(parent_index);
    auto second_child = cluster.tree.get_second_child(parent_index);

    if (!first_child.has_value() || !second_child.has_value()) {
      return false;
    }

    bool is_second = *second_child == current_index;
    bool is_first = *first_child == current_index;

    if (parent_data.split_dir == required_dir) {
      if ((need_from_second && is_second) || (!need_from_second && is_first)) {
        // Found controlling ancestor
        if (static_cast<size_t>(parent_index) >= cluster_geometry.size()) {
          return false;
        }
        const Rect& parent_rect = cluster_geometry[static_cast<size_t>(parent_index)];
        float new_ratio =
            calculate_new_ratio_from_edge(parent_rect, edge, actual_rect, gap_h, gap_v);
        float clamped = std::clamp(new_ratio, 0.1f, 0.9f);
        cluster.tree[parent_index].split_ratio = clamped;
        return true;
      }
    }
    current_index = parent_index;
  }
}

bool update_split_ratio_from_resize(System& system, int cluster_index, size_t leaf_id,
                                    const Rect& actual_window_rect,
                                    const std::vector<Rect>& cluster_geometry) {
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return false;
  }

  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  auto cell_index_opt = find_cell_by_leaf_id(cluster, leaf_id);
  if (!cell_index_opt.has_value()) {
    return false;
  }
  int cell_index = *cell_index_opt;

  if (!cluster.tree.is_leaf(cell_index)) {
    return false;
  }

  auto parent_opt = cluster.tree.get_parent(cell_index);
  if (!parent_opt.has_value()) {
    return false; // Root has no parent
  }

  if (static_cast<size_t>(cell_index) >= cluster_geometry.size()) {
    return false;
  }
  const Rect& expected_rect = cluster_geometry[static_cast<size_t>(cell_index)];

  // Detect which edges changed
  constexpr float kEdgeTolerance = 2.0f;
  bool left_changed = std::abs(actual_window_rect.x - expected_rect.x) > kEdgeTolerance;
  bool right_changed = std::abs((actual_window_rect.x + actual_window_rect.width) -
                                (expected_rect.x + expected_rect.width)) > kEdgeTolerance;
  bool top_changed = std::abs(actual_window_rect.y - expected_rect.y) > kEdgeTolerance;
  bool bottom_changed = std::abs((actual_window_rect.y + actual_window_rect.height) -
                                 (expected_rect.y + expected_rect.height)) > kEdgeTolerance;

  if (!left_changed && !right_changed && !top_changed && !bottom_changed) {
    return false;
  }

  // Estimate gaps from geometry (root rect vs window dimensions)
  float gap_h = 10.0f; // Default fallback
  float gap_v = 10.0f;
  if (!cluster_geometry.empty()) {
    const Rect& root_rect = cluster_geometry[0];
    gap_h = root_rect.x - cluster.global_x;
    gap_v = root_rect.y - cluster.global_y;
  }

  bool any_updated = false;
  if (left_changed) {
    any_updated |= update_ratio_for_edge(cluster, cluster_geometry, cell_index, EdgeType::Left,
                                         actual_window_rect, gap_h, gap_v);
  }
  if (right_changed) {
    any_updated |= update_ratio_for_edge(cluster, cluster_geometry, cell_index, EdgeType::Right,
                                         actual_window_rect, gap_h, gap_v);
  }
  if (top_changed) {
    any_updated |= update_ratio_for_edge(cluster, cluster_geometry, cell_index, EdgeType::Top,
                                         actual_window_rect, gap_h, gap_v);
  }
  if (bottom_changed) {
    any_updated |= update_ratio_for_edge(cluster, cluster_geometry, cell_index, EdgeType::Bottom,
                                         actual_window_rect, gap_h, gap_v);
  }

  return any_updated;
}

} // namespace wintiler::ctrl
