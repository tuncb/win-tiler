#include "multi_cells.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iterator>
#include <limits>
#include <magic_enum/magic_enum.hpp>

namespace wintiler {

// ============================================================================
// Cell Logic Implementation
// ============================================================================
namespace cells {

static CellCluster create_initial_state(float width, float height) {
  CellCluster state{};

  state.cells.clear();

  state.window_width = width;
  state.window_height = height;

  return state;
}

bool is_leaf(const CellCluster& state, int cell_index) {
  if (cell_index < 0 || static_cast<std::size_t>(cell_index) >= state.cells.size()) {
    return false;
  }

  const Cell& cell = state.cells[static_cast<std::size_t>(cell_index)];
  if (cell.is_dead) {
    return false;
  }

  return !cell.first_child.has_value() && !cell.second_child.has_value();
}

int add_cell(CellCluster& state, const Cell& cell) {
  state.cells.push_back(cell);
  return static_cast<int>(state.cells.size() - 1);
}

static void recompute_children_rects(CellCluster& state, int node_index, float gap_horizontal,
                                     float gap_vertical) {
  Cell& node = state.cells[static_cast<std::size_t>(node_index)];

  if (node.is_dead) {
    return;
  }

  if (!node.first_child.has_value() || !node.second_child.has_value()) {
    return;
  }

  Rect parent_rect = node.rect;

  Rect first{};
  Rect second{};

  float ratio = node.split_ratio;

  if (node.split_dir == SplitDir::Vertical) {
    float available_width = parent_rect.width - gap_horizontal;
    float first_width = available_width > 0.0f ? available_width * ratio : 0.0f;
    float second_width = available_width > 0.0f ? available_width * (1.0f - ratio) : 0.0f;
    first = Rect{parent_rect.x, parent_rect.y, first_width, parent_rect.height};
    second = Rect{parent_rect.x + first_width + gap_horizontal, parent_rect.y, second_width,
                  parent_rect.height};
  } else {
    float available_height = parent_rect.height - gap_vertical;
    float first_height = available_height > 0.0f ? available_height * ratio : 0.0f;
    float second_height = available_height > 0.0f ? available_height * (1.0f - ratio) : 0.0f;
    first = Rect{parent_rect.x, parent_rect.y, parent_rect.width, first_height};
    second = Rect{parent_rect.x, parent_rect.y + first_height + gap_vertical, parent_rect.width,
                  second_height};
  }

  Cell& first_child = state.cells[static_cast<std::size_t>(*node.first_child)];
  Cell& second_child = state.cells[static_cast<std::size_t>(*node.second_child)];

  first_child.rect = first;
  second_child.rect = second;
}

static void recompute_subtree_rects(CellCluster& state, int node_index, float gap_horizontal,
                                    float gap_vertical) {
  if (node_index < 0 || static_cast<std::size_t>(node_index) >= state.cells.size()) {
    return;
  }

  Cell& node = state.cells[static_cast<std::size_t>(node_index)];

  if (node.is_dead) {
    return;
  }

  if (node.first_child.has_value() && node.second_child.has_value()) {
    recompute_children_rects(state, node_index, gap_horizontal, gap_vertical);
    recompute_subtree_rects(state, *node.first_child, gap_horizontal, gap_vertical);
    recompute_subtree_rects(state, *node.second_child, gap_horizontal, gap_vertical);
  }
}

static std::optional<int> delete_leaf(CellCluster& state, int selected_index, float gap_horizontal,
                                      float gap_vertical) {
  if (!is_leaf(state, selected_index)) {
    return std::nullopt;
  }
  if (state.cells.empty()) {
    return std::nullopt;
  }

  Cell& selected_cell = state.cells[static_cast<std::size_t>(selected_index)];
  if (selected_cell.is_dead) {
    return std::nullopt;
  }

  if (selected_index == 0) {
    state.cells.clear();
    return std::nullopt; // Cluster is now empty
  }

  if (!selected_cell.parent.has_value()) {
    return std::nullopt;
  }

  int parent_index = *selected_cell.parent;
  Cell& parent = state.cells[static_cast<std::size_t>(parent_index)];

  if (parent.is_dead) {
    return std::nullopt;
  }

  if (!parent.first_child.has_value() || !parent.second_child.has_value()) {
    return std::nullopt;
  }

  int first_idx = *parent.first_child;
  int second_idx = *parent.second_child;
  int sibling_index = (selected_index == first_idx) ? second_idx : first_idx;

  Cell& sibling = state.cells[static_cast<std::size_t>(sibling_index)];
  if (sibling.is_dead) {
    return std::nullopt;
  }

  Rect new_rect = parent.rect;

  Cell promoted = sibling;
  promoted.rect = new_rect;
  promoted.parent = parent.parent;

  if (promoted.first_child.has_value()) {
    Cell& c1 = state.cells[static_cast<std::size_t>(*promoted.first_child)];
    c1.parent = parent_index;
  }
  if (promoted.second_child.has_value()) {
    Cell& c2 = state.cells[static_cast<std::size_t>(*promoted.second_child)];
    c2.parent = parent_index;
  }

  state.cells[static_cast<std::size_t>(parent_index)] = promoted;

  recompute_subtree_rects(state, parent_index, gap_horizontal, gap_vertical);

  selected_cell.is_dead = true;
  sibling.is_dead = true;
  selected_cell.parent.reset();
  sibling.parent.reset();

  int current = parent_index;
  while (!is_leaf(state, current)) {
    Cell& n = state.cells[static_cast<std::size_t>(current)];
    if (n.first_child.has_value()) {
      current = *n.first_child;
    } else if (n.second_child.has_value()) {
      current = *n.second_child;
    } else {
      break;
    }
  }

  return current; // New selection index
}

static std::optional<SplitResult> split_leaf(CellCluster& state, int selected_index,
                                             float gap_horizontal, float gap_vertical,
                                             size_t new_leaf_id, SplitDir split_dir,
                                             float split_ratio = 0.5f) {
  // Special case: if cluster is empty and selected_index is -1, create root
  if (state.cells.empty() && selected_index == -1) {
    Cell root{};
    root.split_dir = split_dir;
    root.is_dead = false;
    root.parent = std::nullopt;
    root.first_child = std::nullopt;
    root.second_child = std::nullopt;
    root.leaf_id = new_leaf_id;

    float root_w = state.window_width;
    float root_h = state.window_height;
    float inset_w = root_w - 2.0f * gap_horizontal;
    float inset_h = root_h - 2.0f * gap_vertical;
    root.rect = Rect{gap_horizontal, gap_vertical, inset_w > 0.0f ? inset_w : 0.0f,
                     inset_h > 0.0f ? inset_h : 0.0f};

    int index = add_cell(state, root);

    return SplitResult{new_leaf_id, index};
  }

  if (!is_leaf(state, selected_index)) {
    return std::nullopt;
  }

  Cell& leaf = state.cells[static_cast<std::size_t>(selected_index)];
  if (leaf.is_dead) {
    return std::nullopt;
  }
  Rect r = leaf.rect;

  size_t parent_leaf_id = *leaf.leaf_id;

  Rect first_rect{};
  Rect second_rect{};

  if (split_dir == SplitDir::Vertical) {
    float available_width = r.width - gap_horizontal;
    float first_width = available_width > 0.0f ? available_width * split_ratio : 0.0f;
    float second_width = available_width > 0.0f ? available_width * (1.0f - split_ratio) : 0.0f;
    first_rect = Rect{r.x, r.y, first_width, r.height};
    second_rect = Rect{r.x + first_width + gap_horizontal, r.y, second_width, r.height};
  } else {
    float available_height = r.height - gap_vertical;
    float first_height = available_height > 0.0f ? available_height * split_ratio : 0.0f;
    float second_height = available_height > 0.0f ? available_height * (1.0f - split_ratio) : 0.0f;
    first_rect = Rect{r.x, r.y, r.width, first_height};
    second_rect = Rect{r.x, r.y + first_height + gap_vertical, r.width, second_height};
  }

  leaf.split_dir = split_dir;
  leaf.split_ratio = split_ratio;
  leaf.leaf_id = std::nullopt;

  Cell first_child{};
  first_child.split_dir = split_dir;
  first_child.is_dead = false;
  first_child.parent = selected_index;
  first_child.first_child = std::nullopt;
  first_child.second_child = std::nullopt;
  first_child.rect = first_rect;
  first_child.leaf_id = parent_leaf_id;

  Cell second_child{};
  second_child.split_dir = split_dir;
  second_child.is_dead = false;
  second_child.parent = selected_index;
  second_child.first_child = std::nullopt;
  second_child.second_child = std::nullopt;
  second_child.rect = second_rect;
  second_child.leaf_id = new_leaf_id;

  int first_index = add_cell(state, first_child);
  int second_index = add_cell(state, second_child);

  {
    Cell& parent = state.cells[static_cast<std::size_t>(selected_index)];
    parent.first_child = first_index;
    parent.second_child = second_index;
  }

  return SplitResult{new_leaf_id, first_index};
}

static bool toggle_split_dir(CellCluster& state, int selected_index, float gap_horizontal,
                             float gap_vertical) {
  if (!is_leaf(state, selected_index)) {
    return false;
  }

  Cell& leaf = state.cells[static_cast<std::size_t>(selected_index)];
  if (leaf.is_dead) {
    return false;
  }

  if (!leaf.parent.has_value()) {
    return false;
  }

  int parent_index = *leaf.parent;
  Cell& parent = state.cells[static_cast<std::size_t>(parent_index)];

  if (parent.is_dead) {
    return false;
  }

  if (!parent.first_child.has_value() || !parent.second_child.has_value()) {
    return false;
  }

  int first_idx = *parent.first_child;
  int second_idx = *parent.second_child;
  int sibling_index = (selected_index == first_idx) ? second_idx : first_idx;

  if (!is_leaf(state, sibling_index)) {
    return false;
  }

  if (state.cells[static_cast<std::size_t>(sibling_index)].is_dead) {
    return false;
  }

  parent.split_dir =
      (parent.split_dir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;

  recompute_subtree_rects(state, parent_index, gap_horizontal, gap_vertical);

  return true;
}

static void debug_print_state(const CellCluster& state) {
  spdlog::debug("===== CellCluster =====");
  spdlog::debug("cells.size = {}", state.cells.size());

  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    const Cell& c = state.cells[i];
    if (c.is_dead) {
      continue;
    }
    spdlog::debug("-- Cell {} --", i);
    spdlog::debug("  kind = {}", c.leaf_id.has_value() ? "Leaf" : "Split");
    spdlog::debug("  split_dir = {}",
                  c.split_dir == SplitDir::Vertical ? "Vertical" : "Horizontal");
    spdlog::debug("  parent = {}", c.parent.has_value() ? std::to_string(*c.parent) : "null");
    spdlog::debug("  first_child = {}",
                  c.first_child.has_value() ? std::to_string(*c.first_child) : "null");
    spdlog::debug("  second_child = {}",
                  c.second_child.has_value() ? std::to_string(*c.second_child) : "null");
    spdlog::debug("  rect = {{ x={}, y={}, w={}, h={} }}", c.rect.x, c.rect.y, c.rect.width,
                  c.rect.height);
  }

  spdlog::debug("===== End CellCluster =====");
}

static bool validate_state(const CellCluster& state) {
  bool ok = true;

  if (state.cells.empty()) {
    spdlog::debug("[validate] State OK (empty)");
    return ok;
  }

  if (state.cells[0].parent.has_value()) {
    spdlog::error("[validate] ERROR: root cell (index 0) has a parent");
    ok = false;
  }

  std::vector<int> parent_ref_count(state.cells.size(), 0);
  std::vector<int> child_ref_count(state.cells.size(), 0);

  for (int i = 0; i < static_cast<int>(state.cells.size()); ++i) {
    const Cell& c = state.cells[static_cast<std::size_t>(i)];

    if (c.is_dead) {
      continue;
    }

    if (c.parent.has_value()) {
      int p = *c.parent;
      if (p < 0 || static_cast<std::size_t>(p) >= state.cells.size()) {
        spdlog::error("[validate] ERROR: cell {} has out-of-range parent index {}", i, p);
        ok = false;
      } else {
        parent_ref_count[static_cast<std::size_t>(i)]++;
      }
    }

    if (c.leaf_id.has_value()) {
      if (c.first_child.has_value() || c.second_child.has_value()) {
        spdlog::error("[validate] ERROR: leaf cell {} has children", i);
        ok = false;
      }
    } else {
      if (!c.first_child.has_value() || !c.second_child.has_value()) {
        spdlog::error("[validate] ERROR: split cell {} is missing children", i);
        ok = false;
      }
    }

    auto check_child = [&](const std::optional<int>& child_opt, const char* label) {
      if (!child_opt.has_value()) {
        return;
      }

      int child = *child_opt;
      if (child < 0 || static_cast<std::size_t>(child) >= state.cells.size()) {
        spdlog::error("[validate] ERROR: cell {} has out-of-range {} index {}", i, label, child);
        ok = false;
        return;
      }

      const Cell& cc = state.cells[static_cast<std::size_t>(child)];
      if (cc.is_dead) {
        spdlog::warn("[validate] WARNING: cell {}'s {} ({}) is dead", i, label, child);
        ok = false;
      }
      if (!cc.parent.has_value() || *cc.parent != i) {
        spdlog::error("[validate] ERROR: cell {}'s {} ({}) does not point back to parent {}", i,
                      label, child, i);
        ok = false;
      }

      child_ref_count[static_cast<std::size_t>(child)]++;
    };

    check_child(c.first_child, "first_child");
    check_child(c.second_child, "second_child");
  }

  for (std::size_t i = 0; i < state.cells.size(); ++i) {
    if (parent_ref_count[i] > 1) {
      spdlog::warn("[validate] WARNING: cell {} has parent set more than once ({})", i,
                   parent_ref_count[i]);
      ok = false;
    }

    if (child_ref_count[i] > 2) {
      spdlog::warn("[validate] WARNING: cell {} is referenced as a child more than twice ({})", i,
                   child_ref_count[i]);
      ok = false;
    }
  }

  std::vector<size_t> leaf_ids;
  for (int i = 0; i < static_cast<int>(state.cells.size()); ++i) {
    const Cell& c = state.cells[static_cast<std::size_t>(i)];
    if (c.is_dead) {
      continue;
    }
    if (c.leaf_id.has_value()) {
      leaf_ids.push_back(*c.leaf_id);
    }
  }

  std::sort(leaf_ids.begin(), leaf_ids.end());
  for (std::size_t i = 1; i < leaf_ids.size(); ++i) {
    if (leaf_ids[i] == leaf_ids[i - 1]) {
      spdlog::error("[validate] ERROR: duplicate leaf_id {} found", leaf_ids[i]);
      ok = false;
    }
  }

  if (ok) {
    spdlog::debug("[validate] State OK ({} cells)", state.cells.size());
  } else {
    spdlog::warn("[validate] State has anomalies");
  }

  return ok;
}

// ============================================================================
// Multi-Cluster System Implementation
// ============================================================================

// ============================================================================
// Split Mode Helpers
// ============================================================================

// Determine the split direction based on mode and parent cell
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
    if (selected_index >= 0 && selected_index < static_cast<int>(cluster.cells.size())) {
      const Cell& selected = cluster.cells[static_cast<size_t>(selected_index)];
      if (selected.parent.has_value()) {
        const Cell& parent = cluster.cells[static_cast<size_t>(*selected.parent)];
        return (parent.split_dir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;
      }
    }
    // Fall back to Vertical for root-level splits
    return SplitDir::Vertical;
  }
  }
}

// ============================================================================
// Helper: Pre-create leaves in a cluster from initialCellIds
// Returns the selection index (or -1 if no cells created)
// ============================================================================

static int pre_create_leaves(PositionedCluster& pc, const std::vector<size_t>& cell_ids,
                             float gap_horizontal, float gap_vertical, SplitMode mode) {
  int current_selection = -1;

  for (size_t i = 0; i < cell_ids.size(); ++i) {
    size_t cell_id = cell_ids[i];

    // Determine split direction based on mode
    SplitDir split_dir = determine_split_dir(pc.cluster, current_selection, mode);

    if (pc.cluster.cells.empty()) {
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
  system.gap_horizontal = gap_horizontal;
  system.gap_vertical = gap_vertical;
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
    pc.cluster = create_initial_state(info.width, info.height);

    int selection_index = -1;
    // Pre-create leaves if initial_cell_ids provided
    if (!info.initial_cell_ids.empty()) {
      selection_index = pre_create_leaves(pc, info.initial_cell_ids, system.gap_horizontal,
                                          system.gap_vertical, system.split_mode);
    }

    // If this is the first cluster with cells, make it the selected cluster
    if (!system.selection.has_value() && selection_index >= 0) {
      system.selection = CellIndicatorByIndex{cluster_index, selection_index};
    }

    system.clusters.push_back(std::move(pc));
  }

  return system;
}

// ============================================================================
// Coordinate Conversion
// ============================================================================

static Rect local_to_global(const PositionedCluster& pc, const Rect& local_rect) {
  return Rect{local_rect.x + pc.global_x, local_rect.y + pc.global_y, local_rect.width,
              local_rect.height};
}

Rect get_cell_global_rect(const PositionedCluster& pc, int cell_index) {
  if (cell_index < 0 || static_cast<size_t>(cell_index) >= pc.cluster.cells.size()) {
    return Rect{0.0f, 0.0f, 0.0f, 0.0f};
  }

  const Cell& cell = pc.cluster.cells[static_cast<size_t>(cell_index)];
  return local_to_global(pc, cell.rect);
}

// ============================================================================
// Cross-Cluster Navigation Helpers
// ============================================================================

static bool is_in_direction_global(const Rect& from, const Rect& to, Direction dir) {
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

static float directional_distance_global(const Rect& from, const Rect& to, Direction dir) {
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

static bool is_cluster_in_direction(const PositionedCluster& pc, const Rect& from_global_rect,
                                    Direction dir) {
  Rect cluster_bounds{pc.global_x, pc.global_y, pc.cluster.window_width, pc.cluster.window_height};
  return is_in_direction_global(from_global_rect, cluster_bounds, dir);
}

// ============================================================================
// Cross-Cluster Navigation
// ============================================================================

static std::optional<std::pair<size_t, int>>
find_next_leaf_in_direction(const System& system, size_t current_cluster_index,
                            int current_cell_index, Direction dir) {
  assert(current_cluster_index < system.clusters.size());
  const PositionedCluster& current_pc = system.clusters[current_cluster_index];

  if (!is_leaf(current_pc.cluster, current_cell_index)) {
    return std::nullopt;
  }

  Rect current_global_rect = get_cell_global_rect(current_pc, current_cell_index);

  std::optional<std::pair<size_t, int>> best_candidate;
  float best_score = std::numeric_limits<float>::max();

  // Search all clusters
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& pc = system.clusters[ci];
    // Quick reject: skip clusters not in the desired direction
    // (except for current cluster, always search it for intra-cluster navigation)
    if (ci != current_cluster_index && !is_cluster_in_direction(pc, current_global_rect, dir)) {
      continue;
    }

    // If this cluster has a zen cell, only consider the zen cell
    if (pc.cluster.zen_cell_index.has_value()) {
      int zen_idx = *pc.cluster.zen_cell_index;

      // Skip if this is the current cell
      if (ci == current_cluster_index && zen_idx == current_cell_index) {
        continue;
      }

      Rect candidate_global_rect = get_cell_global_rect(pc, zen_idx);

      if (!is_in_direction_global(current_global_rect, candidate_global_rect, dir)) {
        continue;
      }

      float score = directional_distance_global(current_global_rect, candidate_global_rect, dir);
      if (score < best_score) {
        best_score = score;
        best_candidate = std::make_pair(ci, zen_idx);
      }
      continue; // Skip normal leaf iteration for this cluster
    }

    // No zen cell: search all leaves in this cluster
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (!is_leaf(pc.cluster, i)) {
        continue;
      }

      // Skip current cell
      if (ci == current_cluster_index && i == current_cell_index) {
        continue;
      }

      Rect candidate_global_rect = get_cell_global_rect(pc, i);

      if (!is_in_direction_global(current_global_rect, candidate_global_rect, dir)) {
        continue;
      }

      float score = directional_distance_global(current_global_rect, candidate_global_rect, dir);
      if (score < best_score) {
        best_score = score;
        best_candidate = std::make_pair(ci, i);
      }
    }
  }

  return best_candidate;
}

bool System::move_selection(Direction dir) {
  if (!selection.has_value()) {
    return false;
  }

  auto next_opt =
      find_next_leaf_in_direction(*this, selection->cluster_index, selection->cell_index, dir);
  if (!next_opt.has_value()) {
    return false;
  }

  auto [next_cluster_index, next_cell_index] = *next_opt;
  selection = CellIndicatorByIndex{next_cluster_index, next_cell_index};

  // Clear zen if moving to non-zen cell in a cluster that has zen
  assert(next_cluster_index < clusters.size());
  PositionedCluster& pc = clusters[next_cluster_index];
  if (pc.cluster.zen_cell_index.has_value() && *pc.cluster.zen_cell_index != next_cell_index) {
    pc.cluster.zen_cell_index.reset();
  }

  return true;
}

// ============================================================================
// Operations
// ============================================================================

std::optional<std::pair<size_t, int>> get_selected_cell(const System& system) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  return std::make_pair(system.selection->cluster_index, system.selection->cell_index);
}

std::optional<Rect> get_selected_cell_global_rect(const System& system) {
  auto selected_opt = get_selected_cell(system);
  if (!selected_opt.has_value()) {
    return std::nullopt;
  }

  auto [cluster_index, cell_index] = *selected_opt;
  assert(cluster_index < system.clusters.size());
  const PositionedCluster& pc = system.clusters[cluster_index];

  return get_cell_global_rect(pc, cell_index);
}

std::optional<int> get_cluster_zen_cell(const CellCluster& cluster) {
  return cluster.zen_cell_index;
}

Rect get_cell_display_rect(const PositionedCluster& pc, int cell_index, bool is_zen,
                           float zen_percentage) {
  if (is_zen) {
    // Return centered rect at zen_percentage of cluster size
    float zen_w = pc.cluster.window_width * zen_percentage;
    float zen_h = pc.cluster.window_height * zen_percentage;
    float offset_x = (pc.cluster.window_width - zen_w) / 2.0f;
    float offset_y = (pc.cluster.window_height - zen_h) / 2.0f;
    return Rect{pc.global_x + offset_x, pc.global_y + offset_y, zen_w, zen_h};
  }
  // Normal: return cell's tree position
  return get_cell_global_rect(pc, cell_index);
}

std::optional<Rect> get_cluster_zen_display_rect(const System& system, size_t cluster_index,
                                                 float zen_percentage) {
  assert(cluster_index < system.clusters.size());
  const PositionedCluster& pc = system.clusters[cluster_index];
  if (!pc.cluster.zen_cell_index.has_value()) {
    return std::nullopt;
  }
  return get_cell_display_rect(pc, *pc.cluster.zen_cell_index, true, zen_percentage);
}

bool System::toggle_selected_split_dir() {
  if (!selection.has_value()) {
    return false;
  }

  assert(selection->cluster_index < clusters.size());
  PositionedCluster& pc = clusters[selection->cluster_index];

  return toggle_split_dir(pc.cluster, selection->cell_index, gap_horizontal, gap_vertical);
}

bool System::cycle_split_mode() {
  switch (split_mode) {
  case SplitMode::Zigzag:
    split_mode = SplitMode::Vertical;
    break;
  case SplitMode::Vertical:
    split_mode = SplitMode::Horizontal;
    break;
  case SplitMode::Horizontal:
    split_mode = SplitMode::Zigzag;
    break;
  }
  return true;
}

bool set_split_ratio(CellCluster& state, int cell_index, float new_ratio, float gap_horizontal,
                     float gap_vertical) {
  if (cell_index < 0 || static_cast<size_t>(cell_index) >= state.cells.size()) {
    return false;
  }

  Cell& cell = state.cells[static_cast<size_t>(cell_index)];
  if (cell.is_dead) {
    return false;
  }

  // Can only set ratio on non-leaf cells (cells with children)
  if (!cell.first_child.has_value() || !cell.second_child.has_value()) {
    return false;
  }

  // Clamp ratio to valid range (0.1 to 0.9 to ensure both children have reasonable space)
  constexpr float kMinSplitRatio = 0.1f;
  constexpr float kMaxSplitRatio = 0.9f;
  float clamped_ratio = std::max(kMinSplitRatio, std::min(kMaxSplitRatio, new_ratio));

  cell.split_ratio = clamped_ratio;
  recompute_subtree_rects(state, cell_index, gap_horizontal, gap_vertical);
  return true;
}

bool System::set_selected_split_ratio(float new_ratio) {
  if (!selection.has_value()) {
    return false;
  }

  assert(selection->cluster_index < clusters.size());
  PositionedCluster& pc = clusters[selection->cluster_index];

  int selected_index = selection->cell_index;

  // If the selected cell is a leaf, get its parent
  if (is_leaf(pc.cluster, selected_index)) {
    Cell& leaf = pc.cluster.cells[static_cast<size_t>(selected_index)];
    if (!leaf.parent.has_value()) {
      return false; // Root leaf has no parent to adjust
    }
    selected_index = *leaf.parent;
  }

  return set_split_ratio(pc.cluster, selected_index, new_ratio, gap_horizontal, gap_vertical);
}

bool System::adjust_selected_split_ratio(float delta) {
  if (!selection.has_value()) {
    return false;
  }

  assert(selection->cluster_index < clusters.size());
  PositionedCluster& pc = clusters[selection->cluster_index];

  int selected_index = selection->cell_index;
  int parent_index = selected_index;

  // If the selected cell is a leaf, get its parent
  if (is_leaf(pc.cluster, parent_index)) {
    Cell& leaf = pc.cluster.cells[static_cast<size_t>(parent_index)];
    if (!leaf.parent.has_value()) {
      return false; // Root leaf has no parent to adjust
    }
    parent_index = *leaf.parent;
  }

  Cell& parent = pc.cluster.cells[static_cast<size_t>(parent_index)];
  if (parent.is_dead || !parent.first_child.has_value() || !parent.second_child.has_value()) {
    return false;
  }

  // Determine if selected cell is the second child - if so, negate delta
  // so that "increase" always makes the selected cell larger
  float adjusted_delta = delta;
  if (parent.second_child.has_value() && *parent.second_child == selected_index) {
    adjusted_delta = -delta;
  }

  float new_ratio = parent.split_ratio + adjusted_delta;
  return set_split_ratio(pc.cluster, parent_index, new_ratio, gap_horizontal, gap_vertical);
}

bool System::exchange_selected_with_sibling() {
  if (!selection.has_value()) {
    return false;
  }

  assert(selection->cluster_index < clusters.size());
  PositionedCluster& pc = clusters[selection->cluster_index];

  int selected_index = selection->cell_index;
  if (!is_leaf(pc.cluster, selected_index)) {
    return false;
  }

  Cell& leaf = pc.cluster.cells[static_cast<size_t>(selected_index)];
  if (!leaf.parent.has_value()) {
    return false; // Root has no sibling
  }

  int parent_index = *leaf.parent;
  Cell& parent = pc.cluster.cells[static_cast<size_t>(parent_index)];

  if (parent.is_dead || !parent.first_child.has_value() || !parent.second_child.has_value()) {
    return false;
  }

  // Swap first_child and second_child
  std::swap(parent.first_child, parent.second_child);

  // Recompute rects
  recompute_subtree_rects(pc.cluster, parent_index, gap_horizontal, gap_vertical);

  return true;
}

bool System::set_zen(size_t cluster_index, size_t leaf_id) {
  assert(cluster_index < clusters.size());
  PositionedCluster& pc = clusters[cluster_index];
  auto cell_index_opt = find_cell_by_leaf_id(pc.cluster, leaf_id);
  if (!cell_index_opt.has_value()) {
    return false;
  }
  if (!is_leaf(pc.cluster, *cell_index_opt)) {
    return false;
  }
  pc.cluster.zen_cell_index = *cell_index_opt;
  return true;
}

void System::clear_zen(size_t cluster_index) {
  assert(cluster_index < clusters.size());
  PositionedCluster& pc = clusters[cluster_index];
  pc.cluster.zen_cell_index.reset();
}

bool System::is_cell_zen(size_t cluster_index, int cell_index) const {
  assert(cluster_index < clusters.size());
  const PositionedCluster& pc = clusters[cluster_index];
  return pc.cluster.zen_cell_index.has_value() && *pc.cluster.zen_cell_index == cell_index;
}

bool System::toggle_selected_zen() {
  if (!selection.has_value()) {
    return false;
  }

  assert(selection->cluster_index < clusters.size());
  PositionedCluster& pc = clusters[selection->cluster_index];

  if (!is_leaf(pc.cluster, selection->cell_index)) {
    return false;
  }

  // Toggle: if already zen, clear; otherwise set
  if (pc.cluster.zen_cell_index.has_value() &&
      *pc.cluster.zen_cell_index == selection->cell_index) {
    pc.cluster.zen_cell_index.reset();
  } else {
    pc.cluster.zen_cell_index = selection->cell_index;
  }

  return true;
}

tl::expected<void, std::string> System::swap_cells(size_t cluster_index1, size_t leaf_id1,
                                                   size_t cluster_index2, size_t leaf_id2) {
  // Validate cluster indices
  if (cluster_index1 >= clusters.size()) {
    return tl::unexpected("Cluster 1 not found");
  }
  if (cluster_index2 >= clusters.size()) {
    return tl::unexpected("Cluster 2 not found");
  }
  PositionedCluster& pc1 = clusters[cluster_index1];
  PositionedCluster& pc2 = clusters[cluster_index2];

  // Find cells by leaf_id
  auto idx1_opt = find_cell_by_leaf_id(pc1.cluster, leaf_id1);
  auto idx2_opt = find_cell_by_leaf_id(pc2.cluster, leaf_id2);

  if (!idx1_opt.has_value()) {
    return tl::unexpected("Leaf 1 not found");
  }
  if (!idx2_opt.has_value()) {
    return tl::unexpected("Leaf 2 not found");
  }

  int idx1 = *idx1_opt;
  int idx2 = *idx2_opt;

  // Check if same cell (no-op)
  if (cluster_index1 == cluster_index2 && leaf_id1 == leaf_id2) {
    return {};
  }

  // Validate both are leaves
  if (!is_leaf(pc1.cluster, idx1)) {
    return tl::unexpected("Cell 1 is not a leaf");
  }
  if (!is_leaf(pc2.cluster, idx2)) {
    return tl::unexpected("Cell 2 is not a leaf");
  }

  if (cluster_index1 == cluster_index2) {
    // Same-cluster swap: swap tree positions
    CellCluster& cluster = pc1.cluster;
    Cell& cell1 = cluster.cells[static_cast<size_t>(idx1)];
    Cell& cell2 = cluster.cells[static_cast<size_t>(idx2)];

    // Store original parent info
    auto parent1 = cell1.parent;
    auto parent2 = cell2.parent;

    // Swap parent pointers
    cell1.parent = parent2;
    cell2.parent = parent1;

    // Update parent's child pointers
    if (parent1.has_value()) {
      Cell& p1 = cluster.cells[static_cast<size_t>(*parent1)];
      if (p1.first_child.has_value() && *p1.first_child == idx1) {
        p1.first_child = idx2;
      } else if (p1.second_child.has_value() && *p1.second_child == idx1) {
        p1.second_child = idx2;
      }
    }
    if (parent2.has_value()) {
      Cell& p2 = cluster.cells[static_cast<size_t>(*parent2)];
      if (p2.first_child.has_value() && *p2.first_child == idx2) {
        p2.first_child = idx1;
      } else if (p2.second_child.has_value() && *p2.second_child == idx2) {
        p2.second_child = idx1;
      }
    }

    // Swap rects
    std::swap(cell1.rect, cell2.rect);

    // Note: Selection stays at the same cell index because the cells
    // still have the same leaf_ids - only their tree positions changed.
  } else {
    // Cross-cluster swap: exchange leaf_ids
    Cell& cell1 = pc1.cluster.cells[static_cast<size_t>(idx1)];
    Cell& cell2 = pc2.cluster.cells[static_cast<size_t>(idx2)];

    // Handle zen state for cross-cluster swap
    bool is_zen1 = pc1.cluster.zen_cell_index.has_value() && *pc1.cluster.zen_cell_index == idx1;
    bool is_zen2 = pc2.cluster.zen_cell_index.has_value() && *pc2.cluster.zen_cell_index == idx2;

    // If both are zen, they remain zen (exchange zen windows)
    // If only one is zen, clear it (the zen window left the cluster)
    if (is_zen1 && !is_zen2) {
      pc1.cluster.zen_cell_index.reset();
    }
    if (is_zen2 && !is_zen1) {
      pc2.cluster.zen_cell_index.reset();
    }

    std::swap(cell1.leaf_id, cell2.leaf_id);

    // Note: Selection doesn't need updating for cross-cluster swap
    // because the selection tracks cell index, not leaf_id
  }

  return {};
}

tl::expected<MoveSuccess, std::string> System::move_cell(size_t source_cluster_index,
                                                         size_t source_leaf_id,
                                                         size_t target_cluster_index,
                                                         size_t target_leaf_id) {
  // Validate cluster indices
  if (source_cluster_index >= clusters.size()) {
    return tl::unexpected("Source cluster not found");
  }
  if (target_cluster_index >= clusters.size()) {
    return tl::unexpected("Target cluster not found");
  }
  PositionedCluster& src_pc = clusters[source_cluster_index];
  PositionedCluster& tgt_pc = clusters[target_cluster_index];

  // Find cells by leaf_id
  auto src_idx_opt = find_cell_by_leaf_id(src_pc.cluster, source_leaf_id);
  auto tgt_idx_opt = find_cell_by_leaf_id(tgt_pc.cluster, target_leaf_id);

  if (!src_idx_opt.has_value()) {
    return tl::unexpected("Source leaf not found");
  }
  if (!tgt_idx_opt.has_value()) {
    return tl::unexpected("Target leaf not found");
  }

  // Check if same cell (no-op)
  if (source_cluster_index == target_cluster_index && source_leaf_id == target_leaf_id) {
    return MoveSuccess{*src_idx_opt, source_cluster_index};
  }

  // Validate both are leaves
  if (!is_leaf(src_pc.cluster, *src_idx_opt)) {
    return tl::unexpected("Source cell is not a leaf");
  }
  if (!is_leaf(tgt_pc.cluster, *tgt_idx_opt)) {
    return tl::unexpected("Target cell is not a leaf");
  }

  // Remember if source or target was selected
  bool source_was_selected = selection.has_value() &&
                             selection->cluster_index == source_cluster_index &&
                             selection->cell_index == *src_idx_opt;

  bool target_was_selected = selection.has_value() &&
                             selection->cluster_index == target_cluster_index &&
                             selection->cell_index == *tgt_idx_opt;

  // Store source's leaf_id
  size_t saved_leaf_id = source_leaf_id;

  // Clear zen on source cluster if moving the zen cell
  if (src_pc.cluster.zen_cell_index.has_value() && *src_pc.cluster.zen_cell_index == *src_idx_opt) {
    src_pc.cluster.zen_cell_index.reset();
  }

  // Delete source
  auto delete_result = delete_leaf(src_pc.cluster, *src_idx_opt, gap_horizontal, gap_vertical);

  // Re-find target by leaf_id (index may have changed if same cluster)
  // Note: tgt_pc is a reference, so if source == target cluster, it's already updated
  tgt_idx_opt = find_cell_by_leaf_id(clusters[target_cluster_index].cluster, target_leaf_id);
  if (!tgt_idx_opt.has_value()) {
    return tl::unexpected("Target lost after delete");
  }

  // Determine split direction based on mode and split target
  SplitDir split_dir =
      determine_split_dir(clusters[target_cluster_index].cluster, *tgt_idx_opt, split_mode);
  auto split_result = split_leaf(clusters[target_cluster_index].cluster, *tgt_idx_opt,
                                 gap_horizontal, gap_vertical, saved_leaf_id, split_dir);

  if (!split_result.has_value()) {
    return tl::unexpected("Split failed");
  }

  // Find the new cell (second child)
  int first_child_idx = split_result->new_selection_index;
  Cell& first_child =
      clusters[target_cluster_index].cluster.cells[static_cast<size_t>(first_child_idx)];

  if (!first_child.parent.has_value()) {
    return tl::unexpected("Could not find parent after split");
  }

  int parent_idx = *first_child.parent;
  Cell& parent = clusters[target_cluster_index].cluster.cells[static_cast<size_t>(parent_idx)];

  if (!parent.second_child.has_value()) {
    return tl::unexpected("Could not find new cell after split");
  }

  int new_cell_idx = *parent.second_child;

  // Update selection if source or target was selected
  if (source_was_selected) {
    // Source was selected - follow it to its new position
    selection = CellIndicatorByIndex{target_cluster_index, new_cell_idx};

    // Clear zen if selecting non-zen cell in a cluster with zen
    if (clusters[target_cluster_index].cluster.zen_cell_index.has_value() &&
        *clusters[target_cluster_index].cluster.zen_cell_index != new_cell_idx) {
      clusters[target_cluster_index].cluster.zen_cell_index.reset();
    }
  } else if (target_was_selected) {
    // Target was selected - it's now a parent, so select its first child
    // (which keeps the target's original leaf_id)
    selection = CellIndicatorByIndex{target_cluster_index, first_child_idx};

    // Clear zen if selecting non-zen cell in a cluster with zen
    if (clusters[target_cluster_index].cluster.zen_cell_index.has_value() &&
        *clusters[target_cluster_index].cluster.zen_cell_index != first_child_idx) {
      clusters[target_cluster_index].cluster.zen_cell_index.reset();
    }
  }

  return MoveSuccess{new_cell_idx, target_cluster_index};
}

// ============================================================================
// Gap/Rect Recalculation
// ============================================================================

void System::update_gaps(float horizontal, float vertical) {
  gap_horizontal = horizontal;
  gap_vertical = vertical;
  recompute_rects();
}

void System::recompute_rects() {
  for (auto& pc : clusters) {
    auto& cluster = pc.cluster;
    if (cluster.cells.empty()) {
      continue;
    }

    // Recompute root rect using cluster dimensions and current gaps
    float root_w = cluster.window_width;
    float root_h = cluster.window_height;
    float inset_w = root_w - 2.0f * gap_horizontal;
    float inset_h = root_h - 2.0f * gap_vertical;
    cluster.cells[0].rect = Rect{gap_horizontal, gap_vertical, inset_w > 0.0f ? inset_w : 0.0f,
                                 inset_h > 0.0f ? inset_h : 0.0f};

    // Recompute all children rects
    recompute_subtree_rects(cluster, 0, gap_horizontal, gap_vertical);
  }
}

// ============================================================================
// Utilities
// ============================================================================

bool validate_system(const System& system) {
  bool ok = true;

  spdlog::debug("===== Validating MultiClusterSystem =====");
  spdlog::debug("Total clusters: {}", system.clusters.size());
  if (system.selection.has_value()) {
    spdlog::debug("selection: cluster={}, cell_index={}", system.selection->cluster_index,
                  system.selection->cell_index);
  } else {
    spdlog::debug("selection: null");
  }

  // Check that selection points to a valid cluster and cell
  if (system.selection.has_value()) {
    if (system.selection->cluster_index >= system.clusters.size()) {
      spdlog::error("[validate] ERROR: selection points to non-existent cluster");
      ok = false;
    } else if (!is_leaf(system.clusters[system.selection->cluster_index].cluster,
                        system.selection->cell_index)) {
      spdlog::error("[validate] ERROR: selection points to non-leaf cell");
      ok = false;
    }
  }

  // Validate each cluster
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& pc = system.clusters[ci];
    spdlog::debug("--- Cluster {} at ({}, {}) ---", ci, pc.global_x, pc.global_y);
    if (!validate_state(pc.cluster)) {
      ok = false;
    }
  }

  // Check for duplicate leaf_ids across all clusters
  std::vector<size_t> all_leaf_ids;
  for (const auto& pc : system.clusters) {
    for (const auto& cell : pc.cluster.cells) {
      if (!cell.is_dead && cell.leaf_id.has_value()) {
        all_leaf_ids.push_back(*cell.leaf_id);
      }
    }
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

void debug_print_system(const System& system) {
  spdlog::debug("===== MultiClusterSystem =====");
  spdlog::debug("clusters.size = {}", system.clusters.size());
  spdlog::debug("split_mode = {}", magic_enum::enum_name(system.split_mode));

  if (system.selection.has_value()) {
    spdlog::debug("selection = cluster={}, cell_index={}", system.selection->cluster_index,
                  system.selection->cell_index);
  } else {
    spdlog::debug("selection = null");
  }

  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& pc = system.clusters[ci];
    spdlog::debug("--- Cluster {} ---", ci);
    spdlog::debug("  global_x = {}, global_y = {}", pc.global_x, pc.global_y);
    debug_print_state(pc.cluster);
  }

  spdlog::debug("===== End MultiClusterSystem =====");
}

size_t count_total_leaves(const System& system) {
  size_t count = 0;
  for (const auto& pc : system.clusters) {
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (is_leaf(pc.cluster, i)) {
        ++count;
      }
    }
  }
  return count;
}

bool has_leaf_id(const System& system, size_t leaf_id) {
  for (const auto& pc : system.clusters) {
    if (find_cell_by_leaf_id(pc.cluster, leaf_id).has_value()) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// Hit Testing
// ============================================================================

std::optional<std::pair<size_t, int>> find_cell_at_point(const System& system, float global_x,
                                                         float global_y, float zen_percentage) {
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& pc = system.clusters[ci];

    // If cluster has zen cell, only check that cell with its zen display rect
    if (pc.cluster.zen_cell_index.has_value()) {
      int zen_idx = *pc.cluster.zen_cell_index;
      Rect zen_rect = get_cell_display_rect(pc, zen_idx, true, zen_percentage);
      if (global_x >= zen_rect.x && global_x < zen_rect.x + zen_rect.width &&
          global_y >= zen_rect.y && global_y < zen_rect.y + zen_rect.height) {
        return std::make_pair(ci, zen_idx);
      }
      continue; // Skip normal leaf iteration for zen clusters
    }

    // Normal hit testing for non-zen clusters
    for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
      if (!is_leaf(pc.cluster, i)) {
        continue;
      }
      Rect global_rect = get_cell_global_rect(pc, i);
      if (global_x >= global_rect.x && global_x < global_rect.x + global_rect.width &&
          global_y >= global_rect.y && global_y < global_rect.y + global_rect.height) {
        return std::make_pair(ci, i);
      }
    }
  }
  return std::nullopt;
}

// ============================================================================
// System Update
// ============================================================================

std::vector<size_t> get_cluster_leaf_ids(const CellCluster& cluster) {
  std::vector<size_t> leaf_ids;
  for (int i = 0; i < static_cast<int>(cluster.cells.size()); ++i) {
    const auto& cell = cluster.cells[static_cast<size_t>(i)];
    if (!cell.is_dead && cell.leaf_id.has_value()) {
      leaf_ids.push_back(*cell.leaf_id);
    }
  }
  return leaf_ids;
}

std::optional<int> find_cell_by_leaf_id(const CellCluster& cluster, size_t leaf_id) {
  for (int i = 0; i < static_cast<int>(cluster.cells.size()); ++i) {
    const auto& cell = cluster.cells[static_cast<size_t>(i)];
    if (!cell.is_dead && cell.leaf_id.has_value() && *cell.leaf_id == leaf_id) {
      return i;
    }
  }
  return std::nullopt;
}

static bool is_cluster_empty(const CellCluster& cluster) {
  if (cluster.cells.empty()) {
    return true;
  }
  for (const auto& cell : cluster.cells) {
    if (!cell.is_dead) {
      return false;
    }
  }
  return true;
}

static bool is_point_in_cluster(const PositionedCluster& pc, float x, float y) {
  return x >= pc.monitor_x && x < pc.monitor_x + pc.monitor_width && y >= pc.monitor_y &&
         y < pc.monitor_y + pc.monitor_height;
}

// Helper: Find windows in cell_ids that don't exist in any cluster
static std::vector<size_t> find_unmanaged_windows(const std::vector<ClusterCellIds>& cell_ids,
                                                  const std::vector<PositionedCluster>& clusters) {
  std::vector<size_t> new_windows;
  for (const auto& upd : cell_ids) {
    for (size_t leaf_id : upd.leaf_ids) {
      bool exists = false;
      for (const auto& pc : clusters) {
        if (find_cell_by_leaf_id(pc.cluster, leaf_id).has_value()) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        new_windows.push_back(leaf_id);
      }
    }
  }
  return new_windows;
}

// Helper: Move windows from their detected clusters to target cluster
static void redirect_windows_to_cluster(std::vector<ClusterCellIds>& cell_ids,
                                        const std::vector<size_t>& windows_to_redirect,
                                        size_t target_cluster_index) {
  if (windows_to_redirect.empty()) {
    return;
  }

  // Remove from all clusters
  for (auto& upd : cell_ids) {
    auto& ids = upd.leaf_ids;
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [&windows_to_redirect](size_t id) {
                               return std::find(windows_to_redirect.begin(),
                                                windows_to_redirect.end(),
                                                id) != windows_to_redirect.end();
                             }),
              ids.end());
  }

  // Add to target cluster
  for (auto& upd : cell_ids) {
    if (upd.cluster_index == target_cluster_index) {
      for (size_t id : windows_to_redirect) {
        upd.leaf_ids.push_back(id);
      }
      break;
    }
  }
}

UpdateResult System::update(const std::vector<ClusterCellIds>& cluster_cell_ids,
                            std::optional<std::pair<size_t, size_t>> new_selection,
                            std::pair<float, float> pointer_coords) {
  UpdateResult result;
  result.selection_updated = false;

  // Make a mutable copy for redirection
  std::vector<ClusterCellIds> redirected_cell_ids = cluster_cell_ids;

  // Find empty cluster under pointer for redirection
  std::optional<size_t> pointer_cluster_index;
  for (size_t ci = 0; ci < clusters.size(); ++ci) {
    const auto& pc = clusters[ci];
    if (is_point_in_cluster(pc, pointer_coords.first, pointer_coords.second) &&
        is_cluster_empty(pc.cluster)) {
      pointer_cluster_index = ci;
      break;
    }
  }

  // Determine target cluster for new window redirection
  std::optional<size_t> redirect_target;

  // Priority 1: Empty cluster under pointer
  if (pointer_cluster_index.has_value()) {
    redirect_target = *pointer_cluster_index;
  }
  // Priority 2: Selected cluster (if in updates)
  else if (selection.has_value()) {
    size_t selected_cluster_index = selection->cluster_index;
    bool selected_cluster_in_updates = false;
    for (const auto& upd : redirected_cell_ids) {
      if (upd.cluster_index == selected_cluster_index) {
        selected_cluster_in_updates = true;
        break;
      }
    }
    if (selected_cluster_in_updates) {
      redirect_target = selected_cluster_index;
    }
  }

  // Redirect new windows to target cluster
  if (redirect_target.has_value()) {
    auto new_windows = find_unmanaged_windows(redirected_cell_ids, clusters);
    redirect_windows_to_cluster(redirected_cell_ids, new_windows, *redirect_target);
  }

  // Process each cluster update
  for (const auto& cluster_update : redirected_cell_ids) {
    // Bounds check for external input
    if (cluster_update.cluster_index >= clusters.size()) {
      result.errors.push_back({
          UpdateError::Type::ClusterNotFound, cluster_update.cluster_index,
          0 // no specific leaf ID
      });
      continue;
    }

    PositionedCluster& pc = clusters[cluster_update.cluster_index];

    // Get current leaf IDs
    std::vector<size_t> current_leaf_ids = get_cluster_leaf_ids(pc.cluster);

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
      auto cell_index_opt = find_cell_by_leaf_id(pc.cluster, leaf_id);
      if (!cell_index_opt.has_value()) {
        result.errors.push_back(
            {UpdateError::Type::LeafNotFound, cluster_update.cluster_index, leaf_id});
        continue;
      }

      auto new_selection_opt =
          delete_leaf(pc.cluster, *cell_index_opt, gap_horizontal, gap_vertical);
      result.deleted_leaf_ids.push_back(leaf_id);

      // If deletion succeeded and returned a new selection, update it if this was selected
      if (selection.has_value() && selection->cluster_index == cluster_update.cluster_index &&
          selection->cell_index == *cell_index_opt) {
        if (new_selection_opt.has_value()) {
          selection->cell_index = *new_selection_opt;
        } else {
          selection.reset();
        }
      }
    }

    // Determine starting split index for this cluster (prefer selection)
    int split_from_index = -1;
    if (selection.has_value() && selection->cluster_index == cluster_update.cluster_index &&
        is_leaf(pc.cluster, selection->cell_index)) {
      split_from_index = selection->cell_index;
    }

    // Handle additions
    for (size_t leaf_id : to_add) {
      // Find an existing leaf to split, or create root if empty
      int current_selection = -1;

      if (pc.cluster.cells.empty()) {
        // Cluster is empty - will create root with split_leaf(-1)
        current_selection = -1;
      } else if (split_from_index >= 0 && is_leaf(pc.cluster, split_from_index)) {
        // Use tracked split point (follows selection)
        current_selection = split_from_index;
      } else {
        // Fallback: find the first available leaf
        for (int i = 0; i < static_cast<int>(pc.cluster.cells.size()); ++i) {
          if (is_leaf(pc.cluster, i)) {
            current_selection = i;
            break;
          }
        }
      }

      // Determine split direction based on mode
      SplitDir split_dir = determine_split_dir(pc.cluster, current_selection, split_mode);
      auto result_opt = split_leaf(pc.cluster, current_selection, gap_horizontal, gap_vertical,
                                   leaf_id, split_dir);

      if (result_opt.has_value()) {
        // Update split_from_index to follow the first child for subsequent additions
        split_from_index = result_opt->new_selection_index;
        result.added_leaf_ids.push_back(leaf_id);
      }
    }

    // Reset zen if cells were added or removed from this cluster
    if (!to_delete.empty() || !to_add.empty()) {
      pc.cluster.zen_cell_index.reset();
    }
  }

  // Update selection
  if (new_selection.has_value()) {
    auto [cluster_index, leaf_id] = *new_selection;

    if (cluster_index >= clusters.size()) {
      result.errors.push_back({UpdateError::Type::SelectionInvalid, cluster_index, leaf_id});
    } else {
      PositionedCluster& sel_pc = clusters[cluster_index];
      auto cell_index_opt = find_cell_by_leaf_id(sel_pc.cluster, leaf_id);
      if (!cell_index_opt.has_value()) {
        result.errors.push_back({UpdateError::Type::SelectionInvalid, cluster_index, leaf_id});
      } else {
        selection = CellIndicatorByIndex{cluster_index, *cell_index_opt};
        result.selection_updated = true;

        // Clear zen if selecting non-zen cell in a cluster with zen
        if (sel_pc.cluster.zen_cell_index.has_value() &&
            *sel_pc.cluster.zen_cell_index != *cell_index_opt) {
          sel_pc.cluster.zen_cell_index.reset();
        }
      }
    }
  }

  return result;
}

} // namespace cells
} // namespace wintiler
