#include "engine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <magic_enum/magic_enum.hpp>
#include <set>

#include "spdlog/spdlog.h"

namespace wintiler::ctrl {
namespace {

bool find_cell_rect_for_split(const Cluster& cluster, int node_index, int target_index,
                              const Rect& rect, Rect& result) {
  if (!cluster.tree.is_valid_index(node_index)) {
    return false;
  }

  if (node_index == target_index) {
    result = rect;
    return true;
  }

  auto first_opt = cluster.tree.get_first_child(node_index);
  auto second_opt = cluster.tree.get_second_child(node_index);
  if (!first_opt.has_value() || !second_opt.has_value()) {
    return false;
  }

  const CellData& data = cluster.tree[node_index];
  Rect first_rect = rect;
  Rect second_rect = rect;
  if (data.split_dir == SplitDir::Vertical) {
    float first_w = rect.width * data.split_ratio;
    float second_w = rect.width * (1.0f - data.split_ratio);
    first_rect.width = first_w;
    second_rect.x = rect.x + first_w;
    second_rect.width = second_w;
  } else {
    float first_h = rect.height * data.split_ratio;
    float second_h = rect.height * (1.0f - data.split_ratio);
    first_rect.height = first_h;
    second_rect.y = rect.y + first_h;
    second_rect.height = second_h;
  }

  return find_cell_rect_for_split(cluster, *first_opt, target_index, first_rect, result) ||
         find_cell_rect_for_split(cluster, *second_opt, target_index, second_rect, result);
}

std::optional<Rect> get_cell_rect_for_split(const Cluster& cluster, int selected_index) {
  if (!cluster.tree.is_valid_index(selected_index)) {
    return std::nullopt;
  }

  Rect result;
  Rect root{0.0f, 0.0f, cluster.window_width, cluster.window_height};
  if (!find_cell_rect_for_split(cluster, 0, selected_index, root, result)) {
    return std::nullopt;
  }
  return result;
}

SplitDir determine_dwindle_split_dir(const Cluster& cluster, int selected_index) {
  if (cluster.tree.empty() || selected_index < 0) {
    return cluster.window_width >= cluster.window_height ? SplitDir::Vertical
                                                         : SplitDir::Horizontal;
  }

  auto rect = get_cell_rect_for_split(cluster, selected_index);
  if (!rect.has_value()) {
    return cluster.window_width >= cluster.window_height ? SplitDir::Vertical
                                                         : SplitDir::Horizontal;
  }

  return rect->width >= rect->height ? SplitDir::Vertical : SplitDir::Horizontal;
}

SplitDir determine_split_dir(const Cluster& cluster, int selected_index, SplitMode mode) {
  switch (mode) {
  case SplitMode::Vertical:
    return SplitDir::Vertical;
  case SplitMode::Horizontal:
    return SplitDir::Horizontal;
  case SplitMode::Dwindle:
    return determine_dwindle_split_dir(cluster, selected_index);
  }
  return determine_dwindle_split_dir(cluster, selected_index);
}

struct SplitResult {
  int new_selection_index;
};

std::optional<SplitResult> split_leaf(Cluster& cluster, int selected_index, size_t new_leaf_id,
                                      SplitDir split_dir, float split_ratio = 0.5f) {
  if (cluster.tree.empty() && selected_index == -1) {
    CellData root_data;
    root_data.split_dir = split_dir;
    root_data.leaf_id = new_leaf_id;

    int index = cluster.tree.add_node(root_data);
    return SplitResult{index};
  }

  if (!cluster.tree.is_leaf(selected_index)) {
    return std::nullopt;
  }

  CellData& leaf_data = cluster.tree[selected_index];
  size_t parent_leaf_id = *leaf_data.leaf_id;

  leaf_data.split_dir = split_dir;
  leaf_data.split_ratio = split_ratio;
  leaf_data.leaf_id = std::nullopt;

  CellData first_child_data;
  first_child_data.split_dir = split_dir;
  first_child_data.leaf_id = parent_leaf_id;

  CellData second_child_data;
  second_child_data.split_dir = split_dir;
  second_child_data.leaf_id = new_leaf_id;

  int first_child_index = cluster.tree.add_node(first_child_data, selected_index);
  int second_child_index = cluster.tree.add_node(second_child_data, selected_index);
  cluster.tree.set_children(selected_index, first_child_index, second_child_index);

  return SplitResult{second_child_index};
}

SplitDir to_engine_split_dir(LayoutSplitDir split_dir) {
  switch (split_dir) {
  case LayoutSplitDir::Vertical:
    return SplitDir::Vertical;
  case LayoutSplitDir::Horizontal:
    return SplitDir::Horizontal;
  }
  return SplitDir::Vertical;
}

struct LayoutBuildState {
  size_t next_leaf_id_index = 0;
  int last_leaf_index = -1;
};

std::optional<int> build_layout_leaf(Cluster& cluster, const std::vector<size_t>& leaf_ids,
                                     std::optional<int> parent_index, SplitDir split_dir,
                                     LayoutBuildState& state) {
  if (state.next_leaf_id_index >= leaf_ids.size()) {
    return std::nullopt;
  }

  CellData leaf_data;
  leaf_data.split_dir = split_dir;
  leaf_data.leaf_id = leaf_ids[state.next_leaf_id_index++];

  int leaf_index = cluster.tree.add_node(leaf_data, parent_index);
  state.last_leaf_index = leaf_index;
  return leaf_index;
}

std::optional<int> build_layout_tree(Cluster& cluster, const LayoutTreeNode& layout_node,
                                     const std::vector<size_t>& leaf_ids,
                                     std::optional<int> parent_index, LayoutBuildState& state) {
  SplitDir split_dir = to_engine_split_dir(layout_node.split_dir);

  CellData node_data;
  node_data.split_dir = split_dir;
  node_data.split_ratio = layout_node.split_ratio;

  int node_index = cluster.tree.add_node(node_data, parent_index);

  std::optional<int> first_child =
      layout_node.first
          ? build_layout_tree(cluster, *layout_node.first, leaf_ids, node_index, state)
          : build_layout_leaf(cluster, leaf_ids, node_index, split_dir, state);
  if (!first_child.has_value()) {
    return std::nullopt;
  }

  std::optional<int> second_child =
      layout_node.second
          ? build_layout_tree(cluster, *layout_node.second, leaf_ids, node_index, state)
          : build_layout_leaf(cluster, leaf_ids, node_index, split_dir, state);
  if (!second_child.has_value()) {
    return std::nullopt;
  }

  cluster.tree.set_children(node_index, *first_child, *second_child);
  return node_index;
}

std::optional<int> rebuild_cluster_from_layout_rule(Cluster& cluster,
                                                    const std::vector<size_t>& leaf_ids,
                                                    const LayoutRule& layout_rule) {
  if (count_layout_windows(layout_rule.tree) != leaf_ids.size()) {
    return std::nullopt;
  }

  cluster.tree.clear();
  LayoutBuildState state;
  auto root = build_layout_tree(cluster, layout_rule.tree, leaf_ids, std::nullopt, state);
  if (!root.has_value() || *root != 0 || state.next_leaf_id_index != leaf_ids.size()) {
    cluster.tree.clear();
    return std::nullopt;
  }

  cluster.zen_cell_index.reset();
  return state.last_leaf_index;
}

void collect_leaf_ids_in_layout_order(const Cluster& cluster, int cell_index,
                                      std::vector<size_t>& leaf_ids) {
  if (!cluster.tree.is_valid_index(cell_index)) {
    return;
  }

  if (cluster.tree.is_leaf(cell_index)) {
    if (cluster.tree[cell_index].leaf_id.has_value()) {
      leaf_ids.push_back(*cluster.tree[cell_index].leaf_id);
    }
    return;
  }

  auto first_child = cluster.tree.get_first_child(cell_index);
  if (first_child.has_value()) {
    collect_leaf_ids_in_layout_order(cluster, *first_child, leaf_ids);
  }

  auto second_child = cluster.tree.get_second_child(cell_index);
  if (second_child.has_value()) {
    collect_leaf_ids_in_layout_order(cluster, *second_child, leaf_ids);
  }
}

int pre_create_leaves(Cluster& cluster, const std::vector<size_t>& cell_ids, SplitMode mode) {
  int current_selection = -1;

  for (size_t cell_id : cell_ids) {
    SplitDir split_dir = determine_split_dir(cluster, current_selection, mode);

    if (cluster.tree.empty()) {
      auto result_opt = split_leaf(cluster, -1, cell_id, split_dir);
      if (result_opt.has_value()) {
        current_selection = result_opt->new_selection_index;
      }
    } else {
      auto result_opt = split_leaf(cluster, current_selection, cell_id, split_dir);
      if (result_opt.has_value()) {
        current_selection = result_opt->new_selection_index;
      }
    }
  }

  return current_selection;
}

System create_system(const std::vector<ClusterInitInfo>& infos, SplitMode split_mode) {
  System system;
  system.split_mode = split_mode;
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
    cluster.window_width = info.width;
    cluster.window_height = info.height;

    int selection_index = -1;
    if (!info.initial_cell_ids.empty()) {
      if (info.initial_layout_rule.has_value()) {
        auto layout_selection = rebuild_cluster_from_layout_rule(cluster, info.initial_cell_ids,
                                                                 *info.initial_layout_rule);
        if (layout_selection.has_value()) {
          selection_index = *layout_selection;
        } else {
          selection_index = pre_create_leaves(cluster, info.initial_cell_ids, system.split_mode);
        }
      } else {
        selection_index = pre_create_leaves(cluster, info.initial_cell_ids, system.split_mode);
      }
    }

    if (!system.selection.has_value() && selection_index >= 0) {
      system.selection = CellIndicatorByIndex{static_cast<int>(cluster_index), selection_index};
    }

    system.clusters.push_back(std::move(cluster));
  }

  return system;
}

bool delete_leaf(Cluster& cluster, int cell_index) {
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return false;
  }

  auto parent_opt = cluster.tree.get_parent(cell_index);
  if (!parent_opt.has_value()) {
    if (cluster.tree.size() == 1) {
      cluster.tree.clear();
      return true;
    }
    return false;
  }

  int parent_index = *parent_opt;
  auto sibling_opt = cluster.tree.get_sibling(cell_index);
  if (!sibling_opt.has_value()) {
    return false;
  }
  int sibling_index = *sibling_opt;

  CellData sibling_data = cluster.tree[sibling_index];
  cluster.tree[parent_index] = sibling_data;

  auto sibling_first = cluster.tree.get_first_child(sibling_index);
  auto sibling_second = cluster.tree.get_second_child(sibling_index);

  if (sibling_first.has_value() && sibling_second.has_value()) {
    cluster.tree.set_children(parent_index, *sibling_first, *sibling_second);
  } else {
    cluster.tree.node(parent_index).first_child = std::nullopt;
    cluster.tree.node(parent_index).second_child = std::nullopt;
  }

  std::vector<int> indices_to_remove = {cell_index, sibling_index};

  if (cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == cell_index) {
    cluster.zen_cell_index = std::nullopt;
  }

  auto remap = cluster.tree.remove(indices_to_remove);
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
  if (cluster_index1 < 0 || static_cast<size_t>(cluster_index1) >= system.clusters.size() ||
      cluster_index2 < 0 || static_cast<size_t>(cluster_index2) >= system.clusters.size()) {
    return false;
  }

  auto& cluster1 = system.clusters[static_cast<size_t>(cluster_index1)];
  auto& cluster2 = system.clusters[static_cast<size_t>(cluster_index2)];

  if (!cluster1.tree.is_valid_index(cell_index1) || !cluster1.tree.is_leaf(cell_index1) ||
      !cluster2.tree.is_valid_index(cell_index2) || !cluster2.tree.is_leaf(cell_index2)) {
    return false;
  }

  if (cluster_index1 == cluster_index2) {
    if (cell_index1 == cell_index2) {
      return true;
    }

    bool swapped_cell_is_zen =
        cluster1.zen_cell_index.has_value() &&
        (*cluster1.zen_cell_index == cell_index1 || *cluster1.zen_cell_index == cell_index2);
    std::swap(cluster1.tree[cell_index1].leaf_id, cluster1.tree[cell_index2].leaf_id);
    if (swapped_cell_is_zen) {
      cluster1.zen_cell_index = std::nullopt;
    }
    return true;
  }

  std::swap(cluster1.tree[cell_index1].leaf_id, cluster2.tree[cell_index2].leaf_id);

  bool cell1_is_zen =
      cluster1.zen_cell_index.has_value() && *cluster1.zen_cell_index == cell_index1;
  bool cell2_is_zen =
      cluster2.zen_cell_index.has_value() && *cluster2.zen_cell_index == cell_index2;

  if (cell1_is_zen && !cell2_is_zen) {
    cluster1.zen_cell_index = std::nullopt;
  } else if (!cell1_is_zen && cell2_is_zen) {
    cluster2.zen_cell_index = std::nullopt;
  }

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

bool subtree_contains_cell(const Cluster& cluster, int root_index, int target_index) {
  if (!cluster.tree.is_valid_index(root_index) || !cluster.tree.is_valid_index(target_index)) {
    return false;
  }
  if (root_index == target_index) {
    return true;
  }

  auto first_child = cluster.tree.get_first_child(root_index);
  if (first_child.has_value() && subtree_contains_cell(cluster, *first_child, target_index)) {
    return true;
  }

  auto second_child = cluster.tree.get_second_child(root_index);
  return second_child.has_value() && subtree_contains_cell(cluster, *second_child, target_index);
}

bool exchange_siblings(System& system, int cluster_index, int cell_index) {
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return false;
  }

  auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  if (!cluster.tree.is_valid_index(cell_index)) {
    return false;
  }

  auto parent_opt = cluster.tree.get_parent(cell_index);
  if (!parent_opt.has_value()) {
    return false;
  }

  int parent_index = *parent_opt;
  auto sibling_opt = cluster.tree.get_sibling(cell_index);
  if (!sibling_opt.has_value()) {
    return false;
  }

  if (cluster.zen_cell_index.has_value()) {
    int zen_cell_index = *cluster.zen_cell_index;
    if (subtree_contains_cell(cluster, cell_index, zen_cell_index) ||
        subtree_contains_cell(cluster, *sibling_opt, zen_cell_index)) {
      cluster.zen_cell_index = std::nullopt;
    }
  }

  cluster.tree.swap_children(parent_index);
  return true;
}

bool move_cell(System& system, int source_cluster_index, int source_cell_index,
               int target_cluster_index, int target_cell_index) {
  if (source_cluster_index < 0 ||
      static_cast<size_t>(source_cluster_index) >= system.clusters.size() ||
      target_cluster_index < 0 ||
      static_cast<size_t>(target_cluster_index) >= system.clusters.size()) {
    return false;
  }

  auto& src_cluster = system.clusters[static_cast<size_t>(source_cluster_index)];
  auto& tgt_cluster = system.clusters[static_cast<size_t>(target_cluster_index)];

  if (!src_cluster.tree.is_valid_index(source_cell_index) ||
      !src_cluster.tree.is_leaf(source_cell_index) ||
      !tgt_cluster.tree.is_valid_index(target_cell_index) ||
      !tgt_cluster.tree.is_leaf(target_cell_index)) {
    return false;
  }

  if (source_cluster_index == target_cluster_index && source_cell_index == target_cell_index) {
    return true;
  }

  bool source_was_selected = system.selection.has_value() &&
                             system.selection->cluster_index == source_cluster_index &&
                             system.selection->cell_index == source_cell_index;
  bool target_was_selected = system.selection.has_value() &&
                             system.selection->cluster_index == target_cluster_index &&
                             system.selection->cell_index == target_cell_index;

  if (src_cluster.zen_cell_index.has_value() && *src_cluster.zen_cell_index == source_cell_index) {
    src_cluster.zen_cell_index = std::nullopt;
  }

  if (source_cluster_index == target_cluster_index) {
    auto src_parent_opt = src_cluster.tree.get_parent(source_cell_index);
    auto tgt_parent_opt = src_cluster.tree.get_parent(target_cell_index);

    if (src_parent_opt.has_value() && tgt_parent_opt.has_value() &&
        *src_parent_opt == *tgt_parent_opt) {
      src_cluster.tree.swap_children(*src_parent_opt);
      if (source_was_selected) {
        system.selection->cell_index = target_cell_index;
      } else if (target_was_selected) {
        system.selection->cell_index = source_cell_index;
      }
      return true;
    }
  }

  std::optional<size_t> source_leaf_id = src_cluster.tree[source_cell_index].leaf_id;
  auto src_parent_opt = src_cluster.tree.get_parent(source_cell_index);
  if (!src_parent_opt.has_value() && src_cluster.tree.size() == 1) {
    if (source_cluster_index == target_cluster_index) {
      return false;
    }
    src_cluster.tree.clear();
    src_cluster.zen_cell_index = std::nullopt;
  }

  int adjusted_target_index = target_cell_index;

  if (src_parent_opt.has_value()) {
    int src_parent = *src_parent_opt;
    auto sibling_opt = src_cluster.tree.get_sibling(source_cell_index);
    if (!sibling_opt.has_value()) {
      return false;
    }
    int sibling_index = *sibling_opt;

    CellData sibling_data = src_cluster.tree[sibling_index];
    src_cluster.tree[src_parent] = sibling_data;

    auto sibling_first = src_cluster.tree.get_first_child(sibling_index);
    auto sibling_second = src_cluster.tree.get_second_child(sibling_index);

    if (sibling_first.has_value() && sibling_second.has_value()) {
      src_cluster.tree.set_children(src_parent, *sibling_first, *sibling_second);
    } else {
      src_cluster.tree.node(src_parent).first_child = std::nullopt;
      src_cluster.tree.node(src_parent).second_child = std::nullopt;
    }

    std::vector<int> indices_to_remove = {source_cell_index, sibling_index};
    auto remap = src_cluster.tree.remove(indices_to_remove);

    if (source_cluster_index == target_cluster_index) {
      if (target_cell_index >= 0 && static_cast<size_t>(target_cell_index) < remap.size()) {
        adjusted_target_index = remap[static_cast<size_t>(target_cell_index)];
        if (adjusted_target_index < 0) {
          return false;
        }
      }
    }

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

  SplitDir split_dir = determine_split_dir(tgt_cluster, adjusted_target_index, system.split_mode);
  auto result_opt =
      split_leaf(tgt_cluster, adjusted_target_index, source_leaf_id.value_or(0), split_dir);

  if (!result_opt.has_value()) {
    return false;
  }

  int new_selection_index = result_opt->new_selection_index;
  auto second_child_opt = tgt_cluster.tree.get_second_child(adjusted_target_index);
  int new_cell_index = second_child_opt.value_or(new_selection_index);

  if (tgt_cluster.zen_cell_index.has_value() &&
      *tgt_cluster.zen_cell_index == adjusted_target_index) {
    tgt_cluster.zen_cell_index = std::nullopt;
  }

  if (source_was_selected) {
    system.selection = CellIndicatorByIndex{target_cluster_index, new_cell_index};
  } else if (target_was_selected) {
    system.selection = CellIndicatorByIndex{target_cluster_index, new_selection_index};
  }

  return true;
}

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

  if (cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == cell_index) {
    cluster.zen_cell_index.reset();
  } else {
    cluster.zen_cell_index = cell_index;
  }

  return true;
}

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
  leaf_ids.reserve(cluster.tree.size());
  if (!cluster.tree.empty()) {
    collect_leaf_ids_in_layout_order(cluster, 0, leaf_ids);
  }
  return leaf_ids;
}

std::vector<size_t> build_layout_rule_leaf_order(const Cluster& cluster,
                                                 const std::vector<size_t>& desired_leaf_ids) {
  std::vector<size_t> ordered_leaf_ids;
  ordered_leaf_ids.reserve(desired_leaf_ids.size());

  for (size_t leaf_id : get_cluster_leaf_ids(cluster)) {
    bool still_exists = std::find(desired_leaf_ids.begin(), desired_leaf_ids.end(), leaf_id) !=
                        desired_leaf_ids.end();
    bool already_added = std::find(ordered_leaf_ids.begin(), ordered_leaf_ids.end(), leaf_id) !=
                         ordered_leaf_ids.end();
    if (still_exists && !already_added) {
      ordered_leaf_ids.push_back(leaf_id);
    }
  }

  for (size_t leaf_id : desired_leaf_ids) {
    bool already_added = std::find(ordered_leaf_ids.begin(), ordered_leaf_ids.end(), leaf_id) !=
                         ordered_leaf_ids.end();
    if (!already_added) {
      ordered_leaf_ids.push_back(leaf_id);
    }
  }

  return ordered_leaf_ids;
}

void compute_children_rects(const Cluster& cluster, int node_index, std::vector<Rect>& rects,
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

  compute_children_rects(cluster, *first_opt, rects, gap_h, gap_v);
  compute_children_rects(cluster, *second_opt, rects, gap_h, gap_v);
}

std::vector<Rect> compute_cluster_geometry(const Cluster& cluster, float gap_h, float gap_v,
                                           float zen_percentage) {
  std::vector<Rect> rects(cluster.tree.size(), Rect{0.0f, 0.0f, 0.0f, 0.0f});
  if (cluster.tree.empty()) {
    return rects;
  }

  float root_w = cluster.window_width - 2.0f * gap_h;
  float root_h = cluster.window_height - 2.0f * gap_v;
  rects[0] = Rect{cluster.global_x + gap_h, cluster.global_y + gap_v, root_w > 0.0f ? root_w : 0.0f,
                  root_h > 0.0f ? root_h : 0.0f};

  compute_children_rects(cluster, 0, rects, gap_h, gap_v);

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

  return rects;
}

std::optional<int> find_any_leaf(const Cluster& cluster) {
  for (int i = 0; i < static_cast<int>(cluster.tree.size()); ++i) {
    if (cluster.tree.is_leaf(i)) {
      return i;
    }
  }
  return std::nullopt;
}

bool is_in_direction(const Rect& from, const Rect& to, Direction dir) {
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

float directional_distance(const Rect& from, const Rect& to, Direction dir) {
  float dx_center = (to.x + to.width * 0.5f) - (from.x + from.width * 0.5f);
  float dy_center = (to.y + to.height * 0.5f) - (from.y + from.height * 0.5f);

  bool has_vertical_overlap = (to.y < from.y + from.height) && (to.y + to.height > from.y);
  bool has_horizontal_overlap = (to.x < from.x + from.width) && (to.x + to.width > from.x);

  switch (dir) {
  case Direction::Left:
  case Direction::Right: {
    float primary_dist = (dir == Direction::Left) ? -dx_center : dx_center;
    if (has_vertical_overlap) {
      return primary_dist;
    }
    float gap =
        std::min(std::abs(to.y - (from.y + from.height)), std::abs(from.y - (to.y + to.height)));
    return primary_dist + 10000.0f + gap;
  }
  case Direction::Up:
  case Direction::Down: {
    float primary_dist = (dir == Direction::Up) ? -dy_center : dy_center;
    if (has_horizontal_overlap) {
      return primary_dist;
    }
    float gap =
        std::min(std::abs(to.x - (from.x + from.width)), std::abs(from.x - (to.x + to.width)));
    return primary_dist + 10000.0f + gap;
  }
  default:
    return std::numeric_limits<float>::max();
  }
}

std::optional<CellIndicatorByIndex>
move_selection(System& system, Direction dir,
               const std::vector<std::vector<Rect>>& cell_geometries) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int current_cluster = system.selection->cluster_index;
  int current_cell = system.selection->cell_index;
  if (current_cluster < 0 || static_cast<size_t>(current_cluster) >= cell_geometries.size()) {
    return std::nullopt;
  }
  if (current_cell < 0 || static_cast<size_t>(current_cell) >=
                              cell_geometries[static_cast<size_t>(current_cluster)].size()) {
    return std::nullopt;
  }
  if (!system.clusters[static_cast<size_t>(current_cluster)].tree.is_leaf(current_cell)) {
    return std::nullopt;
  }

  const Rect& current_rect =
      cell_geometries[static_cast<size_t>(current_cluster)][static_cast<size_t>(current_cell)];

  std::optional<CellIndicatorByIndex> best_candidate;
  float best_score = std::numeric_limits<float>::max();

  for (size_t ci = 0; ci < cell_geometries.size(); ++ci) {
    if (ci >= system.clusters.size()) {
      continue;
    }
    const auto& cluster = system.clusters[ci];
    const auto& cluster_rects = cell_geometries[ci];

    if (cluster.zen_cell_index.has_value()) {
      int zen_idx = *cluster.zen_cell_index;
      if (static_cast<int>(ci) == current_cluster && zen_idx == current_cell) {
        continue;
      }
      if (zen_idx < 0 || static_cast<size_t>(zen_idx) >= cluster_rects.size()) {
        continue;
      }

      const Rect& zen_rect = cluster_rects[static_cast<size_t>(zen_idx)];
      if (!is_in_direction(current_rect, zen_rect, dir)) {
        continue;
      }

      float score = directional_distance(current_rect, zen_rect, dir);
      if (score < best_score) {
        best_score = score;
        best_candidate = CellIndicatorByIndex{static_cast<int>(ci), zen_idx};
      }
      continue;
    }

    for (size_t cell_idx = 0; cell_idx < cluster_rects.size(); ++cell_idx) {
      if (!cluster.tree.is_leaf(static_cast<int>(cell_idx))) {
        continue;
      }
      if (static_cast<int>(ci) == current_cluster && static_cast<int>(cell_idx) == current_cell) {
        continue;
      }

      const Rect& candidate_rect = cluster_rects[cell_idx];
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

  if (!best_candidate.has_value()) {
    return std::nullopt;
  }

  system.selection = best_candidate;

  auto& new_cluster = system.clusters[static_cast<size_t>(best_candidate->cluster_index)];
  if (new_cluster.zen_cell_index.has_value() &&
      *new_cluster.zen_cell_index != best_candidate->cell_index) {
    new_cluster.zen_cell_index.reset();
  }

  return best_candidate;
}

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
    return false;
  }

  int parent_index = *parent_opt;
  auto first_child = cluster.tree.get_first_child(parent_index);
  auto second_child = cluster.tree.get_second_child(parent_index);
  if (!first_child.has_value() || !second_child.has_value()) {
    return false;
  }

  CellData& parent_data = cluster.tree[parent_index];
  parent_data.split_dir =
      (parent_data.split_dir == SplitDir::Vertical) ? SplitDir::Horizontal : SplitDir::Vertical;
  return true;
}

bool cycle_split_mode(System& system) {
  switch (system.split_mode) {
  case SplitMode::Dwindle:
    system.split_mode = SplitMode::Vertical;
    break;
  case SplitMode::Vertical:
    system.split_mode = SplitMode::Horizontal;
    break;
  case SplitMode::Horizontal:
    system.split_mode = SplitMode::Dwindle;
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

  int parent_index = cell_index;
  if (cluster.tree.is_leaf(cell_index)) {
    auto parent_opt = cluster.tree.get_parent(cell_index);
    if (!parent_opt.has_value()) {
      return false;
    }
    parent_index = *parent_opt;
  }

  cluster.tree[parent_index].split_ratio = std::clamp(new_ratio, 0.1f, 0.9f);
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

  int parent_index = cell_index;
  if (cluster.tree.is_leaf(cell_index)) {
    auto parent_opt = cluster.tree.get_parent(cell_index);
    if (!parent_opt.has_value()) {
      return false;
    }
    parent_index = *parent_opt;
  }

  float adjusted_delta = delta;
  auto second_child = cluster.tree.get_second_child(parent_index);
  if (second_child.has_value() && *second_child == cell_index) {
    adjusted_delta = -delta;
  }

  float new_ratio = cluster.tree[parent_index].split_ratio + adjusted_delta;
  cluster.tree[parent_index].split_ratio = std::clamp(new_ratio, 0.1f, 0.9f);
  return true;
}

std::optional<size_t> get_selected_leaf_id_for_cluster(const System& system, size_t cluster_idx) {
  if (!system.selection.has_value() ||
      system.selection->cluster_index != static_cast<int>(cluster_idx)) {
    return std::nullopt;
  }

  const auto& cluster = system.clusters[cluster_idx];
  int cell_index = system.selection->cell_index;
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return std::nullopt;
  }

  return cluster.tree[cell_index].leaf_id;
}

void select_after_layout_rebuild(System& system, size_t cluster_idx,
                                 std::optional<size_t> preferred_leaf_id, int fallback_leaf_index) {
  if (preferred_leaf_id.has_value()) {
    auto preferred_cell = find_cell_by_leaf_id(system.clusters[cluster_idx], *preferred_leaf_id);
    if (preferred_cell.has_value()) {
      system.selection =
          CellIndicatorByIndex{static_cast<int>(cluster_idx), static_cast<int>(*preferred_cell)};
      return;
    }
  }

  if (fallback_leaf_index >= 0) {
    system.selection = CellIndicatorByIndex{static_cast<int>(cluster_idx), fallback_leaf_index};
  } else {
    system.selection.reset();
  }
}

bool apply_layout_templates(System& system, const LayoutOptions& layout_options) {
  bool updated = false;

  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    auto& cluster = system.clusters[cluster_idx];
    std::vector<size_t> leaf_ids = get_cluster_leaf_ids(cluster);
    auto layout_rule = find_layout_rule_for_window_count(layout_options, leaf_ids.size());
    if (!layout_rule.has_value()) {
      continue;
    }

    bool selection_was_in_cluster =
        system.selection.has_value() &&
        system.selection->cluster_index == static_cast<int>(cluster_idx);
    auto preferred_leaf_id = get_selected_leaf_id_for_cluster(system, cluster_idx);
    auto fallback_leaf_index = rebuild_cluster_from_layout_rule(cluster, leaf_ids, *layout_rule);
    if (!fallback_leaf_index.has_value()) {
      continue;
    }

    if (selection_was_in_cluster || !system.selection.has_value()) {
      select_after_layout_rebuild(system, cluster_idx, preferred_leaf_id, *fallback_leaf_index);
    }
    updated = true;
  }

  return updated;
}

bool apply_layout_templates(System& system,
                            const std::vector<ClusterTilingOptions>& cluster_options) {
  bool updated = false;

  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    if (cluster_idx >= cluster_options.size()) {
      continue;
    }

    auto& cluster = system.clusters[cluster_idx];
    const auto& layout_options = cluster_options[cluster_idx].layoutOptions;
    std::vector<size_t> leaf_ids = get_cluster_leaf_ids(cluster);
    auto layout_rule = find_layout_rule_for_window_count(layout_options, leaf_ids.size());
    if (!layout_rule.has_value()) {
      continue;
    }

    bool selection_was_in_cluster =
        system.selection.has_value() &&
        system.selection->cluster_index == static_cast<int>(cluster_idx);
    auto preferred_leaf_id = get_selected_leaf_id_for_cluster(system, cluster_idx);
    auto fallback_leaf_index = rebuild_cluster_from_layout_rule(cluster, leaf_ids, *layout_rule);
    if (!fallback_leaf_index.has_value()) {
      continue;
    }

    if (selection_was_in_cluster || !system.selection.has_value()) {
      select_after_layout_rebuild(system, cluster_idx, preferred_leaf_id, *fallback_leaf_index);
    }
    updated = true;
  }

  return updated;
}

bool update_impl(System& system, const std::vector<ClusterCellUpdateInfo>& cluster_updates,
                 std::optional<int> redirect_cluster_index, const LayoutOptions* layout_options,
                 const std::vector<ClusterTilingOptions>* cluster_options) {
  bool updated = false;
  std::vector<ClusterCellUpdateInfo> redirected_updates = cluster_updates;

  if (redirect_cluster_index.has_value()) {
    int target_idx = *redirect_cluster_index;
    if (target_idx >= 0 && static_cast<size_t>(target_idx) < system.clusters.size()) {
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

        if (static_cast<size_t>(target_idx) < redirected_updates.size()) {
          for (size_t id : new_windows) {
            redirected_updates[static_cast<size_t>(target_idx)].leaf_ids.push_back(id);
          }
        }
      }
    }
  }

  for (size_t cluster_idx = 0; cluster_idx < redirected_updates.size(); ++cluster_idx) {
    if (cluster_idx >= system.clusters.size()) {
      continue;
    }

    const auto& cluster_update = redirected_updates[cluster_idx];
    auto& cluster = system.clusters[cluster_idx];
    cluster.has_fullscreen_cell = cluster_update.has_fullscreen_cell;

    std::vector<size_t> current_leaf_ids = get_cluster_leaf_ids(cluster);
    std::vector<size_t> sorted_current = current_leaf_ids;
    std::vector<size_t> sorted_desired = cluster_update.leaf_ids;
    std::sort(sorted_current.begin(), sorted_current.end());
    std::sort(sorted_desired.begin(), sorted_desired.end());

    std::vector<size_t> to_delete;
    std::set_difference(sorted_current.begin(), sorted_current.end(), sorted_desired.begin(),
                        sorted_desired.end(), std::back_inserter(to_delete));

    std::vector<size_t> to_add;
    std::set_difference(sorted_desired.begin(), sorted_desired.end(), sorted_current.begin(),
                        sorted_current.end(), std::back_inserter(to_add));

    const LayoutOptions* cluster_layout_options = layout_options;
    if (cluster_options != nullptr && cluster_idx < cluster_options->size()) {
      cluster_layout_options = &(*cluster_options)[cluster_idx].layoutOptions;
    }

    if ((!to_delete.empty() || !to_add.empty()) && cluster_layout_options != nullptr) {
      auto layout_rule = find_layout_rule_for_window_count(*cluster_layout_options,
                                                           cluster_update.leaf_ids.size());
      if (layout_rule.has_value()) {
        bool selection_was_in_cluster =
            system.selection.has_value() &&
            system.selection->cluster_index == static_cast<int>(cluster_idx);
        auto preferred_leaf_id = get_selected_leaf_id_for_cluster(system, cluster_idx);
        auto ordered_leaf_ids = build_layout_rule_leaf_order(cluster, cluster_update.leaf_ids);
        auto fallback_leaf_index =
            rebuild_cluster_from_layout_rule(cluster, ordered_leaf_ids, *layout_rule);
        if (fallback_leaf_index.has_value()) {
          if (selection_was_in_cluster || !system.selection.has_value()) {
            select_after_layout_rebuild(system, cluster_idx, preferred_leaf_id,
                                        *fallback_leaf_index);
          }
          updated = true;
          continue;
        }
      }
    }

    for (size_t leaf_id : to_delete) {
      auto cell_index_opt = find_cell_by_leaf_id(cluster, leaf_id);
      if (!cell_index_opt.has_value()) {
        continue;
      }

      int cell_idx = *cell_index_opt;
      bool was_selected = system.selection.has_value() &&
                          system.selection->cluster_index == static_cast<int>(cluster_idx) &&
                          system.selection->cell_index == cell_idx;
      auto parent_opt = cluster.tree.get_parent(cell_idx);

      if (delete_leaf(cluster, cell_idx)) {
        updated = true;

        if (was_selected) {
          if (parent_opt.has_value()) {
            system.selection = CellIndicatorByIndex{static_cast<int>(cluster_idx), *parent_opt};
          } else {
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

    int split_from_index = -1;
    if (system.selection.has_value() &&
        system.selection->cluster_index == static_cast<int>(cluster_idx) &&
        cluster.tree.is_valid_index(system.selection->cell_index) &&
        cluster.tree.is_leaf(system.selection->cell_index)) {
      split_from_index = system.selection->cell_index;
    }

    for (size_t leaf_id : to_add) {
      int current_selection = -1;

      if (cluster.tree.empty()) {
        current_selection = -1;
      } else if (split_from_index >= 0 && cluster.tree.is_valid_index(split_from_index) &&
                 cluster.tree.is_leaf(split_from_index)) {
        current_selection = split_from_index;
      } else {
        auto leaf_opt = find_any_leaf(cluster);
        if (leaf_opt.has_value()) {
          current_selection = *leaf_opt;
        }
      }

      SplitDir split_dir = determine_split_dir(cluster, current_selection, system.split_mode);
      auto result_opt = split_leaf(cluster, current_selection, leaf_id, split_dir);
      if (result_opt.has_value()) {
        split_from_index = result_opt->new_selection_index;
        system.selection = CellIndicatorByIndex{static_cast<int>(cluster_idx), split_from_index};
        updated = true;
      }
    }

    if (!to_delete.empty() || !to_add.empty()) {
      cluster.zen_cell_index.reset();
    }
  }

  return updated;
}

bool update(System& system, const std::vector<ClusterCellUpdateInfo>& cluster_updates,
            std::optional<int> redirect_cluster_index, const LayoutOptions* layout_options) {
  return update_impl(system, cluster_updates, redirect_cluster_index, layout_options, nullptr);
}

bool update(System& system, const std::vector<ClusterCellUpdateInfo>& cluster_updates,
            std::optional<int> redirect_cluster_index,
            const std::vector<ClusterTilingOptions>& cluster_options) {
  return update_impl(system, cluster_updates, redirect_cluster_index, nullptr, &cluster_options);
}

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

std::optional<std::pair<int, int>>
find_cell_at_point(const System& system, const std::vector<std::vector<Rect>>& geometries,
                   float global_x, float global_y) {
  for (size_t ci = 0; ci < system.clusters.size(); ++ci) {
    const auto& cluster = system.clusters[ci];

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

    if (ci >= geometries.size()) {
      continue;
    }
    for (int i = 0; i < static_cast<int>(geometries[ci].size()); ++i) {
      if (!cluster.tree.is_valid_index(i) || !cluster.tree.is_leaf(i)) {
        continue;
      }
      const auto& r = geometries[ci][static_cast<size_t>(i)];
      if (global_x >= r.x && global_x < r.x + r.width && global_y >= r.y &&
          global_y < r.y + r.height) {
        return std::make_pair(static_cast<int>(ci), i);
      }
    }
  }
  return std::nullopt;
}

std::optional<int> find_cluster_by_leaf_id(const System& system, size_t leaf_id) {
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
  if (!has_leaf_id(system, source_leaf_id)) {
    return std::nullopt;
  }

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

  auto target_opt = find_cell_at_point(system, geometries, cursor_x, cursor_y);
  if (!target_opt.has_value()) {
    return std::nullopt;
  }
  auto [target_cluster_index, target_cell_index] = *target_opt;

  if (system.clusters[static_cast<size_t>(target_cluster_index)].has_fullscreen_cell) {
    return std::nullopt;
  }
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

  if (static_cast<size_t>(new_cluster) < geometries.size() &&
      static_cast<size_t>(new_cell) < geometries[static_cast<size_t>(new_cluster)].size()) {
    const auto& rect = geometries[static_cast<size_t>(new_cluster)][static_cast<size_t>(new_cell)];
    return DropMoveResult{get_rect_center(rect), do_exchange};
  }

  return DropMoveResult{Point{static_cast<long>(cursor_x), static_cast<long>(cursor_y)},
                        do_exchange};
}

enum class EdgeType { Left, Right, Top, Bottom };

bool is_point_inside_monitor(const Cluster& cluster, float x, float y) {
  return x >= cluster.monitor_x && x < cluster.monitor_x + cluster.monitor_width &&
         y >= cluster.monitor_y && y < cluster.monitor_y + cluster.monitor_height;
}

float calculate_new_ratio_from_edge(const Rect& parent_rect, EdgeType edge, const Rect& actual_rect,
                                    float gap_h, float gap_v) {
  if (edge == EdgeType::Left || edge == EdgeType::Right) {
    float available = parent_rect.width - gap_h;
    if (available <= 0.0f) {
      return 0.5f;
    }
    if (edge == EdgeType::Left) {
      float first_width = actual_rect.x - parent_rect.x;
      return first_width / available;
    }

    float actual_right = actual_rect.x + actual_rect.width;
    float parent_right = parent_rect.x + parent_rect.width;
    float second_width = parent_right - actual_right;
    return 1.0f - (second_width / available);
  }

  float available = parent_rect.height - gap_v;
  if (available <= 0.0f) {
    return 0.5f;
  }
  if (edge == EdgeType::Top) {
    float first_height = actual_rect.y - parent_rect.y;
    return first_height / available;
  }

  float actual_bottom = actual_rect.y + actual_rect.height;
  float parent_bottom = parent_rect.y + parent_rect.height;
  float second_height = parent_bottom - actual_bottom;
  return 1.0f - (second_height / available);
}

bool update_ratio_for_edge(Cluster& cluster, const std::vector<Rect>& cluster_geometry,
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
        if (static_cast<size_t>(parent_index) >= cluster_geometry.size()) {
          return false;
        }

        const Rect& parent_rect = cluster_geometry[static_cast<size_t>(parent_index)];
        float new_ratio =
            calculate_new_ratio_from_edge(parent_rect, edge, actual_rect, gap_h, gap_v);
        cluster.tree[parent_index].split_ratio = std::clamp(new_ratio, 0.1f, 0.9f);
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
    return false;
  }

  if (static_cast<size_t>(cell_index) >= cluster_geometry.size()) {
    return false;
  }
  const Rect& expected_rect = cluster_geometry[static_cast<size_t>(cell_index)];

  float actual_center_x = actual_window_rect.x + actual_window_rect.width / 2.0f;
  float actual_center_y = actual_window_rect.y + actual_window_rect.height / 2.0f;
  if (!is_point_inside_monitor(cluster, actual_center_x, actual_center_y)) {
    return false;
  }

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

  float gap_h = 10.0f;
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

} // namespace
} // namespace wintiler::ctrl

namespace wintiler {

namespace {

ClusterTilingOptions make_legacy_cluster_options(float gap_h, float gap_v, float zen_pct,
                                                 const LayoutOptions* layout_options) {
  ClusterTilingOptions options;
  options.gapOptions.horizontal = gap_h;
  options.gapOptions.vertical = gap_v;
  options.zen_percentage = zen_pct;
  if (layout_options != nullptr) {
    options.layoutOptions = *layout_options;
  }
  return options;
}

std::vector<ClusterTilingOptions> make_legacy_cluster_options(size_t cluster_count, float gap_h,
                                                              float gap_v, float zen_pct,
                                                              const LayoutOptions* layout_options) {
  std::vector<ClusterTilingOptions> options;
  options.reserve(cluster_count);
  for (size_t i = 0; i < cluster_count; ++i) {
    options.push_back(make_legacy_cluster_options(gap_h, gap_v, zen_pct, layout_options));
  }
  return options;
}

const ClusterTilingOptions&
cluster_options_or_default(const std::vector<ClusterTilingOptions>& cluster_options,
                           size_t cluster_index) {
  static const ClusterTilingOptions default_options{};
  if (cluster_index < cluster_options.size()) {
    return cluster_options[cluster_index];
  }
  return default_options;
}

// Find the cluster and cell index at a global point using precomputed geometries
std::optional<std::pair<size_t, int>>
find_cell_at_global_point(const ctrl::System& system,
                          const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                          float global_x, float global_y) {
  for (size_t cluster_idx = 0; cluster_idx < system.clusters.size(); ++cluster_idx) {
    if (cluster_idx >= global_geometries.size()) {
      continue;
    }
    const auto& cluster = system.clusters[cluster_idx];
    const auto& rects = global_geometries[cluster_idx];

    if (cluster.zen_cell_index.has_value()) {
      int zen_idx = *cluster.zen_cell_index;
      if (!cluster.tree.is_valid_index(zen_idx) || !cluster.tree.is_leaf(zen_idx) ||
          static_cast<size_t>(zen_idx) >= rects.size()) {
        continue;
      }

      const auto& zen_rect = rects[static_cast<size_t>(zen_idx)];
      if (global_x >= zen_rect.x && global_x < zen_rect.x + zen_rect.width &&
          global_y >= zen_rect.y && global_y < zen_rect.y + zen_rect.height) {
        return std::make_pair(cluster_idx, zen_idx);
      }
      continue;
    }

    for (int i = 0; i < static_cast<int>(rects.size()); ++i) {
      // Skip non-leaf cells
      if (!cluster.tree.is_leaf(i)) {
        continue;
      }

      const auto& r = rects[static_cast<size_t>(i)];
      if (global_x >= r.x && global_x < r.x + r.width && global_y >= r.y &&
          global_y < r.y + r.height) {
        return std::make_pair(cluster_idx, i);
      }
    }
  }
  return std::nullopt;
}

// Find which cluster contains a global point using full monitor bounds, including taskbar area.
std::optional<size_t> find_cluster_at_global_point(const ctrl::System& system, float global_x,
                                                   float global_y) {
  for (size_t i = 0; i < system.clusters.size(); ++i) {
    const auto& cluster = system.clusters[i];
    if (global_x >= cluster.monitor_x && global_x < cluster.monitor_x + cluster.monitor_width &&
        global_y >= cluster.monitor_y && global_y < cluster.monitor_y + cluster.monitor_height) {
      return i;
    }
  }
  return std::nullopt;
}

bool selections_equal(const std::optional<ctrl::CellIndicatorByIndex>& lhs,
                      const std::optional<ctrl::CellIndicatorByIndex>& rhs) {
  if (!lhs.has_value() && !rhs.has_value()) {
    return true;
  }
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  return lhs->cluster_index == rhs->cluster_index && lhs->cell_index == rhs->cell_index;
}

std::optional<size_t> get_selected_leaf_id(const ctrl::System& system) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return std::nullopt;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return std::nullopt;
  }

  return cluster.tree[cell_index].leaf_id;
}

std::optional<ctrl::CellIndicatorByIndex> find_cell_by_leaf_id(const ctrl::System& system,
                                                               size_t leaf_id) {
  for (size_t cluster_index = 0; cluster_index < system.clusters.size(); ++cluster_index) {
    auto cell_index = ctrl::find_cell_by_leaf_id(system.clusters[cluster_index], leaf_id);
    if (cell_index.has_value()) {
      return ctrl::CellIndicatorByIndex{static_cast<int>(cluster_index), *cell_index};
    }
  }
  return std::nullopt;
}

struct AutoZenResult {
  bool layout_changed = false;
  bool apply_tiles = false;
  bool initial_tile_pass_completed = false;
  std::optional<size_t> focus_leaf_id;
};

struct DragResult {
  bool handled = false;
  bool selection_changed = false;
  bool layout_changed = false;
  bool apply_tiles = false;
  bool clear_drag_ended = false;
  std::optional<size_t> cursor_leaf_id;
};

bool store_selected_cell(const ctrl::System& system, std::optional<StoredCell>& stored_cell) {
  if (!system.selection.has_value()) {
    return false;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return false;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];
  if (!cluster.tree.is_valid_index(cell_index) || !cluster.tree.is_leaf(cell_index)) {
    return false;
  }

  const auto& cell_data = cluster.tree[cell_index];
  if (cell_data.leaf_id.has_value()) {
    stored_cell = StoredCell{static_cast<size_t>(cluster_index), *cell_data.leaf_id};
    return true;
  }

  return false;
}

std::optional<ctrl::Point>
get_selected_center(const ctrl::System& system,
                    const std::vector<std::vector<ctrl::Rect>>& geometries) {
  if (!system.selection.has_value()) {
    return std::nullopt;
  }

  int cluster_index = system.selection->cluster_index;
  int cell_index = system.selection->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= geometries.size()) {
    return std::nullopt;
  }
  if (cell_index < 0 ||
      static_cast<size_t>(cell_index) >= geometries[static_cast<size_t>(cluster_index)].size()) {
    return std::nullopt;
  }

  const auto& rect =
      geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(cell_index)];
  return ctrl::get_rect_center(rect);
}

std::optional<ctrl::Point> get_leaf_center(const ctrl::System& system, size_t leaf_id,
                                           const std::vector<std::vector<ctrl::Rect>>& geometries) {
  auto cell = find_cell_by_leaf_id(system, leaf_id);
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

  return ctrl::get_rect_center(
      geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(cell_index)]);
}

void ensure_maximized_tracking_size(std::vector<std::optional<size_t>>& previous_maximized_leaf_ids,
                                    size_t cluster_count) {
  previous_maximized_leaf_ids.resize(cluster_count);
}

AutoZenResult
update_zen_for_maximized_windows(ctrl::System& system,
                                 std::vector<std::optional<size_t>>& previous_maximized_leaf_ids,
                                 const std::vector<std::vector<ManagedWindowState>>& windows,
                                 bool has_completed_initial_tile_pass) {
  AutoZenResult result;
  result.initial_tile_pass_completed = true;
  ensure_maximized_tracking_size(previous_maximized_leaf_ids, system.clusters.size());

  std::vector<std::optional<size_t>> current_maximized_leaf_ids(system.clusters.size());
  size_t cluster_count = std::min(system.clusters.size(), windows.size());
  for (size_t cluster_index = 0; cluster_index < cluster_count; ++cluster_index) {
    const auto& cluster = system.clusters[cluster_index];
    if (cluster.has_fullscreen_cell) {
      continue;
    }

    for (const auto& window : windows[cluster_index]) {
      if (window.leaf_id == 0) {
        continue;
      }
      if (!window.is_maximized || window.is_fullscreen) {
        continue;
      }

      current_maximized_leaf_ids[cluster_index] = window.leaf_id;
      break;
    }
  }

  if (!has_completed_initial_tile_pass) {
    previous_maximized_leaf_ids = std::move(current_maximized_leaf_ids);
    return result;
  }

  for (size_t cluster_index = 0; cluster_index < system.clusters.size(); ++cluster_index) {
    auto& cluster = system.clusters[cluster_index];
    std::optional<size_t> current_leaf_id = current_maximized_leaf_ids[cluster_index];
    if (!current_leaf_id.has_value()) {
      previous_maximized_leaf_ids[cluster_index].reset();
      continue;
    }

    if (previous_maximized_leaf_ids[cluster_index].has_value() &&
        *previous_maximized_leaf_ids[cluster_index] == *current_leaf_id) {
      continue;
    }

    auto target_cell_index = ctrl::find_cell_by_leaf_id(cluster, *current_leaf_id);
    if (!target_cell_index.has_value()) {
      previous_maximized_leaf_ids[cluster_index] = current_leaf_id;
      continue;
    }

    bool zen_changed = false;
    if (cluster.zen_cell_index.has_value() && *cluster.zen_cell_index == *target_cell_index) {
      ctrl::clear_zen(system, static_cast<int>(cluster_index));
      zen_changed = true;
    } else {
      bool set_zen_result =
          ctrl::set_zen(system, static_cast<int>(cluster_index), *target_cell_index);
      if (!set_zen_result) {
        spdlog::error("Failed to set zen for maximized window {}", *current_leaf_id);
        previous_maximized_leaf_ids[cluster_index] = current_leaf_id;
        continue;
      }
      zen_changed = true;
    }

    if (zen_changed) {
      result.layout_changed = true;
      result.focus_leaf_id = current_leaf_id;
    }

    previous_maximized_leaf_ids[cluster_index] = current_leaf_id;
  }

  result.apply_tiles = result.layout_changed;
  return result;
}

DragResult process_completed_drag(ctrl::System& system, const CompletedDragRequest& request,
                                  const std::vector<std::vector<ctrl::Rect>>& geometries) {
  DragResult result;
  result.clear_drag_ended = true;

  auto previous_selection = system.selection;
  if (!ctrl::has_leaf_id(system, request.leaf_id)) {
    return result;
  }

  auto cell = find_cell_by_leaf_id(system, request.leaf_id);
  if (!cell.has_value()) {
    return result;
  }

  int cluster_index = cell->cluster_index;
  int cell_index = cell->cell_index;
  if (cluster_index < 0 || static_cast<size_t>(cluster_index) >= system.clusters.size()) {
    return result;
  }

  const auto& cluster = system.clusters[static_cast<size_t>(cluster_index)];

  bool can_try_resize =
      request.actual_window_rect.has_value() && !cluster.has_fullscreen_cell &&
      !cluster.zen_cell_index.has_value() &&
      static_cast<size_t>(cluster_index) < geometries.size() &&
      static_cast<size_t>(cell_index) < geometries[static_cast<size_t>(cluster_index)].size();

  if (can_try_resize) {
    const auto& expected_rect =
        geometries[static_cast<size_t>(cluster_index)][static_cast<size_t>(cell_index)];
    const auto& actual_rect = *request.actual_window_rect;
    bool size_changed = (std::abs(actual_rect.width - expected_rect.width) > 2.0f ||
                         std::abs(actual_rect.height - expected_rect.height) > 2.0f);
    if (size_changed) {
      result.handled =
          ctrl::update_split_ratio_from_resize(system, cluster_index, request.leaf_id, actual_rect,
                                               geometries[static_cast<size_t>(cluster_index)]);
      if (result.handled) {
        result.layout_changed = true;
        result.apply_tiles = true;
        result.selection_changed = !selections_equal(previous_selection, system.selection);
        return result;
      }
    }
  }

  if (!request.cursor_pos.has_value()) {
    result.apply_tiles = true;
    return result;
  }

  auto drop_result = ctrl::perform_drop_move(
      system, request.leaf_id, static_cast<float>(request.cursor_pos->x),
      static_cast<float>(request.cursor_pos->y), geometries, request.do_exchange);
  if (!drop_result.has_value()) {
    result.selection_changed = !selections_equal(previous_selection, system.selection);
    result.apply_tiles = true;
    return result;
  }

  result.handled = true;
  result.selection_changed = !selections_equal(previous_selection, system.selection);
  result.layout_changed = true;
  result.apply_tiles = true;
  result.cursor_leaf_id = request.leaf_id;
  return result;
}

bool rect_differs_from_target(const ctrl::Rect& actual_rect, const ctrl::Rect& target_rect) {
  constexpr float kPlacementTolerance = 2.0f;

  return std::abs(actual_rect.x - target_rect.x) > kPlacementTolerance ||
         std::abs(actual_rect.y - target_rect.y) > kPlacementTolerance ||
         std::abs(actual_rect.width - target_rect.width) > kPlacementTolerance ||
         std::abs(actual_rect.height - target_rect.height) > kPlacementTolerance;
}

std::vector<size_t>
find_placement_correction_leaf_ids(const ctrl::System& system,
                                   const std::vector<std::vector<ManagedWindowState>>& windows,
                                   const std::vector<std::vector<ctrl::Rect>>& geometries) {
  std::vector<size_t> leaf_ids;
  size_t cluster_count = std::min(system.clusters.size(), windows.size());

  for (size_t cluster_index = 0; cluster_index < cluster_count; ++cluster_index) {
    const auto& cluster = system.clusters[cluster_index];
    if (cluster.has_fullscreen_cell) {
      continue;
    }

    if (cluster_index >= geometries.size()) {
      continue;
    }

    for (const auto& window : windows[cluster_index]) {
      if (window.leaf_id == 0 || window.is_fullscreen || window.is_maximized ||
          window.is_minimized || !window.actual_rect.has_value()) {
        continue;
      }

      auto cell_index = ctrl::find_cell_by_leaf_id(cluster, window.leaf_id);
      if (!cell_index.has_value()) {
        continue;
      }

      if (*cell_index < 0 || static_cast<size_t>(*cell_index) >= geometries[cluster_index].size()) {
        continue;
      }

      const auto& target_rect = geometries[cluster_index][static_cast<size_t>(*cell_index)];
      if (rect_differs_from_target(*window.actual_rect, target_rect)) {
        leaf_ids.push_back(window.leaf_id);
      }
    }
  }

  return leaf_ids;
}

} // namespace

void Engine::init(const std::vector<ctrl::ClusterInitInfo>& infos, ctrl::SplitMode split_mode) {
  system = ctrl::create_system(infos, split_mode);
  previous_maximized_leaf_ids.assign(system.clusters.size(), std::nullopt);
}

std::vector<std::vector<ctrl::Rect>> Engine::compute_geometries(float gap_h, float gap_v,
                                                                float zen_pct) const {
  auto cluster_options =
      make_legacy_cluster_options(system.clusters.size(), gap_h, gap_v, zen_pct, nullptr);
  return compute_geometries(cluster_options);
}

std::vector<std::vector<ctrl::Rect>>
Engine::compute_geometries(const std::vector<ClusterTilingOptions>& cluster_options) const {
  std::vector<std::vector<ctrl::Rect>> geometries;
  geometries.reserve(system.clusters.size());
  for (size_t cluster_index = 0; cluster_index < system.clusters.size(); ++cluster_index) {
    const auto& cluster = system.clusters[cluster_index];
    const auto& options = cluster_options_or_default(cluster_options, cluster_index);
    geometries.push_back(ctrl::compute_cluster_geometry(cluster, options.gapOptions.horizontal,
                                                        options.gapOptions.vertical,
                                                        options.zen_percentage));
  }
  return geometries;
}

HoverInfo
Engine::get_hover_info(float global_x, float global_y,
                       const std::vector<std::vector<ctrl::Rect>>& global_geometries) const {
  HoverInfo info;
  info.cluster_index = find_cluster_at_global_point(system, global_x, global_y);

  auto cell_at_mouse = find_cell_at_global_point(system, global_geometries, global_x, global_y);
  if (cell_at_mouse.has_value()) {
    auto [cluster_index, cell_index] = *cell_at_mouse;
    info.cell = ctrl::CellIndicatorByIndex{static_cast<int>(cluster_index), cell_index};
  }
  return info;
}

UpdateResult Engine::update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                            std::optional<int> redirect_cluster_index,
                            const LayoutOptions* layout_options, bool reapply_layout_templates) {
  UpdateResult result;
  auto previous_selection = system.selection;
  std::vector<bool> previous_fullscreen_state;
  previous_fullscreen_state.reserve(system.clusters.size());
  for (const auto& cluster : system.clusters) {
    previous_fullscreen_state.push_back(cluster.has_fullscreen_cell);
  }

  result.topology_changed =
      ctrl::update(system, cluster_updates, redirect_cluster_index, layout_options);

  bool layout_template_changed = false;
  if (reapply_layout_templates && layout_options != nullptr) {
    layout_template_changed = ctrl::apply_layout_templates(system, *layout_options);
  }

  result.selection_changed = !selections_equal(previous_selection, system.selection);

  bool fullscreen_state_changed = false;
  for (size_t i = 0; i < system.clusters.size() && i < previous_fullscreen_state.size(); ++i) {
    if (previous_fullscreen_state[i] != system.clusters[i].has_fullscreen_cell) {
      fullscreen_state_changed = true;
      break;
    }
  }

  result.layout_changed =
      result.topology_changed || fullscreen_state_changed || layout_template_changed;
  result.apply_tiles = result.layout_changed;
  return result;
}

UpdateResult Engine::update(const std::vector<ctrl::ClusterCellUpdateInfo>& cluster_updates,
                            std::optional<int> redirect_cluster_index,
                            const std::vector<ClusterTilingOptions>& cluster_options,
                            bool reapply_layout_templates) {
  UpdateResult result;
  auto previous_selection = system.selection;
  std::vector<bool> previous_fullscreen_state;
  previous_fullscreen_state.reserve(system.clusters.size());
  for (const auto& cluster : system.clusters) {
    previous_fullscreen_state.push_back(cluster.has_fullscreen_cell);
  }

  result.topology_changed =
      ctrl::update(system, cluster_updates, redirect_cluster_index, cluster_options);

  bool layout_template_changed = false;
  if (reapply_layout_templates) {
    layout_template_changed = ctrl::apply_layout_templates(system, cluster_options);
  }

  result.selection_changed = !selections_equal(previous_selection, system.selection);

  bool fullscreen_state_changed = false;
  for (size_t i = 0; i < system.clusters.size() && i < previous_fullscreen_state.size(); ++i) {
    if (previous_fullscreen_state[i] != system.clusters[i].has_fullscreen_cell) {
      fullscreen_state_changed = true;
      break;
    }
  }

  result.layout_changed =
      result.topology_changed || fullscreen_state_changed || layout_template_changed;
  result.apply_tiles = result.layout_changed;
  return result;
}

HoverSelectionResult
Engine::update_selection_from_hover(float global_x, float global_y,
                                    const std::vector<std::vector<ctrl::Rect>>& global_geometries) {
  HoverSelectionResult result;
  auto previous_selection = system.selection;

  auto hover_info = get_hover_info(global_x, global_y, global_geometries);
  if (hover_info.cell.has_value()) {
    system.selection = *hover_info.cell;
  }

  result.selection_changed = !selections_equal(previous_selection, system.selection);
  return result;
}

EngineFrameOutput Engine::process_frame(const EngineFrameInput& input) {
  EngineFrameOutput output;
  output.has_completed_initial_tile_pass = input.has_completed_initial_tile_pass;
  std::vector<ClusterTilingOptions> cluster_options = input.cluster_options;
  if (cluster_options.empty()) {
    cluster_options = make_legacy_cluster_options(system.clusters.size(), input.gap_h, input.gap_v,
                                                  input.zen_pct, input.layout_options);
  }

  if (!input.has_completed_initial_tile_pass) {
    output.apply_tiles = true;
    output.has_completed_initial_tile_pass = true;
  }

  std::vector<std::vector<ctrl::Rect>> geometries;
  bool has_geometries = false;
  bool geometries_dirty = false;
  bool skip_cluster_update = false;
  std::optional<size_t> drag_cursor_leaf_id;

  auto ensure_geometries = [&]() -> const std::vector<std::vector<ctrl::Rect>>& {
    if (!has_geometries || geometries_dirty) {
      geometries = compute_geometries(cluster_options);
      has_geometries = true;
      geometries_dirty = false;
    }
    return geometries;
  };

  auto mark_geometries_dirty = [&]() { geometries_dirty = true; };

  if (input.completed_drag.has_value()) {
    DragResult drag_result =
        process_completed_drag(system, *input.completed_drag, ensure_geometries());
    output.clear_drag_ended = drag_result.clear_drag_ended;
    output.selection_changed = output.selection_changed || drag_result.selection_changed;
    output.layout_changed = output.layout_changed || drag_result.layout_changed;
    output.apply_tiles = output.apply_tiles || drag_result.apply_tiles;
    if (drag_result.cursor_leaf_id.has_value()) {
      drag_cursor_leaf_id = drag_result.cursor_leaf_id;
    }
    if (drag_result.layout_changed) {
      mark_geometries_dirty();
    }
    // A handled drag already decided the intended layout for this frame. The current
    // OS window snapshot can still reflect the pre-retile monitor membership, especially
    // for cross-monitor exchanges, so defer topology sync until the next frame.
    if (drag_result.handled) {
      skip_cluster_update = true;
    }
  }

  if (input.hotkey_action.has_value()) {
    ActionResult action_result =
        process_action(*input.hotkey_action, ensure_geometries(), cluster_options);
    output.control = action_result.control;
    output.selection_changed = output.selection_changed || action_result.selection_changed;
    output.layout_changed = output.layout_changed || action_result.layout_changed;
    output.apply_tiles = output.apply_tiles || action_result.apply_tiles;
    output.dump_window_management =
        output.dump_window_management || action_result.dump_window_management;
    output.restart_system = output.restart_system || action_result.restart_system;
    output.toggle_floating = output.toggle_floating || action_result.toggle_floating;
    output.toggle_verbose_logging =
        output.toggle_verbose_logging || action_result.toggle_verbose_logging;
    if (action_result.focus_leaf_id.has_value()) {
      output.focus_leaf_id = action_result.focus_leaf_id;
    }
    if (action_result.floating_leaf_id.has_value()) {
      output.floating_leaf_id = action_result.floating_leaf_id;
    }
    if (action_result.cursor_pos.has_value()) {
      output.cursor_pos = action_result.cursor_pos;
    }
    if (action_result.toast_message.has_value()) {
      output.toast_message = action_result.toast_message;
    }
    if (action_result.layout_changed) {
      mark_geometries_dirty();
    }
    if (output.control != LoopControl::Continue) {
      output.geometries = ensure_geometries();
      return output;
    }
  }

  if (!skip_cluster_update) {
    std::optional<int> redirect_cluster_index;
    if (input.cursor_pos.has_value()) {
      auto hover_cluster_index = find_cluster_at_global_point(
          system, static_cast<float>(input.cursor_pos->x), static_cast<float>(input.cursor_pos->y));
      if (hover_cluster_index.has_value()) {
        redirect_cluster_index = static_cast<int>(*hover_cluster_index);
      }
    }
    if (!redirect_cluster_index.has_value() && system.selection.has_value()) {
      redirect_cluster_index = system.selection->cluster_index;
    }

    if (!input.hotkey_action.has_value() && input.cursor_pos.has_value() &&
        !output.selection_changed && !output.cursor_pos.has_value()) {
      HoverSelectionResult hover_result =
          update_selection_from_hover(static_cast<float>(input.cursor_pos->x),
                                      static_cast<float>(input.cursor_pos->y), ensure_geometries());
      output.selection_changed = output.selection_changed || hover_result.selection_changed;
    }

    UpdateResult update_result = update(input.cluster_updates, redirect_cluster_index,
                                        cluster_options, input.reapply_layout_templates);
    output.topology_changed = output.topology_changed || update_result.topology_changed;
    output.selection_changed = output.selection_changed || update_result.selection_changed;
    output.layout_changed = output.layout_changed || update_result.layout_changed;
    output.apply_tiles = output.apply_tiles || update_result.apply_tiles;
    if (update_result.layout_changed) {
      mark_geometries_dirty();
    }
    if (update_result.topology_changed) {
      if (update_result.cursor_pos.has_value()) {
        output.cursor_pos = update_result.cursor_pos;
      } else if (auto center = get_selected_center(system, ensure_geometries())) {
        output.cursor_pos = center;
      }
    }
  }

  if (input.auto_zen_on_maximize) {
    AutoZenResult zen_result =
        update_zen_for_maximized_windows(system, previous_maximized_leaf_ids, input.managed_windows,
                                         input.has_completed_initial_tile_pass);
    output.has_completed_initial_tile_pass = zen_result.initial_tile_pass_completed;
    output.layout_changed = output.layout_changed || zen_result.layout_changed;
    output.apply_tiles = output.apply_tiles || zen_result.apply_tiles;
    if (!output.focus_leaf_id.has_value() && zen_result.focus_leaf_id.has_value()) {
      output.focus_leaf_id = zen_result.focus_leaf_id;
    }
    if (zen_result.layout_changed) {
      mark_geometries_dirty();
    }
  }

  if (input.update_hover_selection && input.cursor_pos.has_value() && !output.selection_changed &&
      !output.cursor_pos.has_value()) {
    HoverSelectionResult hover_result =
        update_selection_from_hover(static_cast<float>(input.cursor_pos->x),
                                    static_cast<float>(input.cursor_pos->y), ensure_geometries());
    output.selection_changed = output.selection_changed || hover_result.selection_changed;
  }

  if (!output.cursor_pos.has_value() && drag_cursor_leaf_id.has_value()) {
    output.cursor_pos = get_leaf_center(system, *drag_cursor_leaf_id, ensure_geometries());
  }

  output.geometries = ensure_geometries();
  if (!output.apply_tiles) {
    output.placement_correction_leaf_ids =
        find_placement_correction_leaf_ids(system, input.managed_windows, output.geometries);
  }
  return output;
}

void Engine::clear_stored_cell() {
  stored_cell.reset();
}

std::optional<ctrl::CellIndicatorByIndex> Engine::find_leaf(size_t leaf_id) const {
  return find_cell_by_leaf_id(system, leaf_id);
}

std::optional<size_t> Engine::selected_leaf_id() const {
  return get_selected_leaf_id(system);
}

bool Engine::select_leaf(size_t leaf_id) {
  auto cell = find_cell_by_leaf_id(system, leaf_id);
  if (!cell.has_value()) {
    return false;
  }

  system.selection = *cell;
  return true;
}

bool Engine::swap_leaves(size_t first_leaf_id, size_t second_leaf_id) {
  auto first_cell = find_cell_by_leaf_id(system, first_leaf_id);
  if (!first_cell.has_value()) {
    return false;
  }

  auto second_cell = find_cell_by_leaf_id(system, second_leaf_id);
  if (!second_cell.has_value()) {
    return false;
  }

  return ctrl::swap_cells(system, first_cell->cluster_index, first_cell->cell_index,
                          second_cell->cluster_index, second_cell->cell_index);
}

bool Engine::move_leaf_to_cell(size_t source_leaf_id, int target_cluster_index,
                               int target_cell_index) {
  auto source_cell = find_cell_by_leaf_id(system, source_leaf_id);
  if (!source_cell.has_value()) {
    return false;
  }

  return ctrl::move_cell(system, source_cell->cluster_index, source_cell->cell_index,
                         target_cluster_index, target_cell_index);
}

ActionResult Engine::process_action(HotkeyAction action,
                                    const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                                    float gap_h, float gap_v, float zen_pct) {
  auto cluster_options =
      make_legacy_cluster_options(system.clusters.size(), gap_h, gap_v, zen_pct, nullptr);
  return process_action(action, global_geometries, cluster_options);
}

ActionResult Engine::process_action(HotkeyAction action,
                                    const std::vector<std::vector<ctrl::Rect>>& global_geometries,
                                    const std::vector<ClusterTilingOptions>& cluster_options) {
  ActionResult result;

  auto compute_updated_cluster_geometry = [&](int cluster_index) {
    const auto& options =
        cluster_options_or_default(cluster_options, static_cast<size_t>(cluster_index));
    return ctrl::compute_cluster_geometry(system.clusters[static_cast<size_t>(cluster_index)],
                                          options.gapOptions.horizontal,
                                          options.gapOptions.vertical, options.zen_percentage);
  };

  switch (action) {
  case HotkeyAction::NavigateLeft:
    spdlog::info("NavigateLeft: moving selection to the left");
    if (ctrl::move_selection(system, ctrl::Direction::Left, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::NavigateDown:
    spdlog::info("NavigateDown: moving selection downward");
    if (ctrl::move_selection(system, ctrl::Direction::Down, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::NavigateUp:
    spdlog::info("NavigateUp: moving selection upward");
    if (ctrl::move_selection(system, ctrl::Direction::Up, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::NavigateRight:
    spdlog::info("NavigateRight: moving selection to the right");
    if (ctrl::move_selection(system, ctrl::Direction::Right, global_geometries)) {
      result.success = true;
      result.selection_changed = true;
      result.cursor_pos = get_selected_center(system, global_geometries);
    }
    break;

  case HotkeyAction::ToggleSplit:
    spdlog::info("ToggleSplit: toggling split direction of selected cell");
    result.success = ctrl::toggle_selected_split_dir(system);
    result.layout_changed = result.success;
    result.apply_tiles = result.success;
    if (!result.success) {
      spdlog::trace("Failed to toggle split direction");
    }
    break;

  case HotkeyAction::StoreCell:
    spdlog::info("StoreCell: storing current cell for swap/move operation");
    result.success = store_selected_cell(system, stored_cell);
    break;

  case HotkeyAction::ClearStored:
    spdlog::info("ClearStored: clearing stored cell reference");
    clear_stored_cell();
    result.success = true;
    break;

  case HotkeyAction::Exchange:
    spdlog::info("Exchange: swapping stored cell with selected cell");
    if (stored_cell.has_value() && system.selection.has_value()) {
      auto previous_selection = system.selection;
      // Find stored cell index from leaf_id
      auto stored_cell_idx = ctrl::find_cell_by_leaf_id(system.clusters[stored_cell->cluster_index],
                                                        stored_cell->leaf_id);
      if (stored_cell_idx.has_value()) {
        if (ctrl::swap_cells(system, system.selection->cluster_index, system.selection->cell_index,
                             static_cast<int>(stored_cell->cluster_index), *stored_cell_idx)) {
          clear_stored_cell();
          result.success = true;
          result.selection_changed = !selections_equal(previous_selection, system.selection);
          result.layout_changed = true;
          result.apply_tiles = true;
        }
      }
    }
    break;

  case HotkeyAction::Move:
    spdlog::info("Move: moving stored cell to selected cell's position");
    if (stored_cell.has_value() && system.selection.has_value()) {
      auto previous_selection = system.selection;
      // Find stored cell index from leaf_id
      auto stored_cell_idx = ctrl::find_cell_by_leaf_id(system.clusters[stored_cell->cluster_index],
                                                        stored_cell->leaf_id);
      if (stored_cell_idx.has_value()) {
        if (ctrl::move_cell(system, static_cast<int>(stored_cell->cluster_index), *stored_cell_idx,
                            system.selection->cluster_index, system.selection->cell_index)) {
          clear_stored_cell();
          result.success = true;
          result.selection_changed = !selections_equal(previous_selection, system.selection);
          result.layout_changed = true;
          result.apply_tiles = true;
        }
      }
    }
    break;

  case HotkeyAction::SplitIncrease:
    spdlog::info("SplitIncrease: increasing split ratio by 5%%");
    if (ctrl::adjust_selected_split_ratio(system, 0.05f)) {
      result.success = true;
      result.layout_changed = true;
      result.selection_changed = true;
      result.apply_tiles = true;
      // Recompute geometry for the affected cluster to get updated center
      if (system.selection.has_value()) {
        int ci = system.selection->cluster_index;
        if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
          auto updated_geom = compute_updated_cluster_geometry(ci);
          int cell_idx = system.selection->cell_index;
          if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
            result.cursor_pos = ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
          }
        }
      }
    }
    break;

  case HotkeyAction::SplitDecrease:
    spdlog::info("SplitDecrease: decreasing split ratio by 5%%");
    if (ctrl::adjust_selected_split_ratio(system, -0.05f)) {
      result.success = true;
      result.layout_changed = true;
      result.selection_changed = true;
      result.apply_tiles = true;
      // Recompute geometry for the affected cluster to get updated center
      if (system.selection.has_value()) {
        int ci = system.selection->cluster_index;
        if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
          auto updated_geom = compute_updated_cluster_geometry(ci);
          int cell_idx = system.selection->cell_index;
          if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
            result.cursor_pos = ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
          }
        }
      }
    }
    break;

  case HotkeyAction::ExchangeSiblings:
    spdlog::info("ExchangeSiblings: exchanging selected cell with its sibling");
    if (system.selection.has_value()) {
      if (ctrl::exchange_siblings(system, system.selection->cluster_index,
                                  system.selection->cell_index)) {
        result.success = true;
        result.layout_changed = true;
        result.selection_changed = true;
        result.apply_tiles = true;
        // Recompute geometry to get updated center
        int ci = system.selection->cluster_index;
        if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
          auto updated_geom = compute_updated_cluster_geometry(ci);
          int cell_idx = system.selection->cell_index;
          if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
            result.cursor_pos = ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
          }
        }
      }
    }
    break;

  case HotkeyAction::ToggleZen:
    spdlog::info("ToggleZen: toggling zen mode for selected cell");
    result.success = ctrl::toggle_selected_zen(system);
    result.layout_changed = result.success;
    result.apply_tiles = result.success;
    if (result.success) {
      result.focus_leaf_id = get_selected_leaf_id(system);
    }
    if (!result.success) {
      spdlog::error("ToggleZen: failed to toggle zen mode");
    }
    break;

  case HotkeyAction::CycleSplitMode:
    result.success = ctrl::cycle_split_mode(system);
    if (!result.success) {
      spdlog::error("CycleSplitMode: failed to cycle split mode");
    } else {
      const auto mode_name = magic_enum::enum_name(system.split_mode);
      result.toast_message = std::string("Split mode: ").append(mode_name.data(), mode_name.size());
      spdlog::info("CycleSplitMode: switched to {}", mode_name);
    }
    break;

  case HotkeyAction::ResetSplitRatio:
    spdlog::info("ResetSplitRatio: resetting split ratio of parent to 50%%");
    if (ctrl::set_selected_split_ratio(system, 0.5f)) {
      result.success = true;
      result.layout_changed = true;
      result.selection_changed = true;
      result.apply_tiles = true;
      // Recompute geometry to get updated center
      if (system.selection.has_value()) {
        int ci = system.selection->cluster_index;
        if (ci >= 0 && static_cast<size_t>(ci) < system.clusters.size()) {
          auto updated_geom = compute_updated_cluster_geometry(ci);
          int cell_idx = system.selection->cell_index;
          if (cell_idx >= 0 && static_cast<size_t>(cell_idx) < updated_geom.size()) {
            result.cursor_pos = ctrl::get_rect_center(updated_geom[static_cast<size_t>(cell_idx)]);
          }
        }
      }
    }
    break;

  case HotkeyAction::Exit:
    spdlog::info("Exit: exit requested");
    result.success = true;
    result.control = LoopControl::Exit;
    break;

  case HotkeyAction::TogglePause:
    result.success = true;
    result.control = LoopControl::EnterManualPause;
    break;

  case HotkeyAction::DumpWindowManagement:
    spdlog::info("DumpWindowManagement: dumping current window management state");
    result.success = true;
    result.dump_window_management = true;
    break;

  case HotkeyAction::RestartSystem:
    spdlog::info("RestartSystem: full system restart requested");
    result.success = true;
    result.restart_system = true;
    result.toast_message = "System restarted";
    break;

  case HotkeyAction::ToggleFloating:
    spdlog::info("ToggleFloating: toggling selected window floating state");
    result.success = true;
    result.toggle_floating = true;
    result.floating_leaf_id = get_selected_leaf_id(system);
    break;

  case HotkeyAction::ToggleVerboseLogging:
    result.success = true;
    result.toggle_verbose_logging = true;
    break;
  }

  if (result.success && result.selection_changed) {
    result.focus_leaf_id = get_selected_leaf_id(system);
  }

  return result;
}

} // namespace wintiler
