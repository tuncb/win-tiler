#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace wintiler {

template <typename T, typename Allocator = std::allocator<T>>
class BinaryTree {
public:
  struct Node {
    T data;
    std::optional<int> parent;
    std::optional<int> first_child;
    std::optional<int> second_child;
  };

  using allocator_type = Allocator;
  using NodeAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<Node>;

  // Constructors

  BinaryTree() = default;

  explicit BinaryTree(const Allocator& alloc) : nodes_(NodeAllocator(alloc)) {
  }

  // Core Operations

  int add_node(T data, std::optional<int> parent_index = std::nullopt) {
    Node node;
    node.data = std::move(data);
    node.parent = parent_index;
    node.first_child = std::nullopt;
    node.second_child = std::nullopt;

    nodes_.push_back(std::move(node));
    return static_cast<int>(nodes_.size() - 1);
  }

  [[nodiscard]] bool is_leaf(int index) const {
    if (!is_valid_index(index)) {
      return false;
    }
    const Node& node = nodes_[static_cast<size_t>(index)];
    return !node.first_child.has_value() && !node.second_child.has_value();
  }

  [[nodiscard]] bool is_valid_index(int index) const {
    return index >= 0 && static_cast<size_t>(index) < nodes_.size();
  }

  // Traversal

  [[nodiscard]] std::optional<int> get_parent(int index) const {
    if (!is_valid_index(index)) {
      return std::nullopt;
    }
    return nodes_[static_cast<size_t>(index)].parent;
  }

  [[nodiscard]] std::optional<int> get_first_child(int index) const {
    if (!is_valid_index(index)) {
      return std::nullopt;
    }
    return nodes_[static_cast<size_t>(index)].first_child;
  }

  [[nodiscard]] std::optional<int> get_second_child(int index) const {
    if (!is_valid_index(index)) {
      return std::nullopt;
    }
    return nodes_[static_cast<size_t>(index)].second_child;
  }

  [[nodiscard]] std::optional<int> get_sibling(int index) const {
    if (!is_valid_index(index)) {
      return std::nullopt;
    }

    const Node& node = nodes_[static_cast<size_t>(index)];
    if (!node.parent.has_value()) {
      return std::nullopt; // Root has no sibling
    }

    int parent_idx = *node.parent;
    if (!is_valid_index(parent_idx)) {
      return std::nullopt;
    }

    const Node& parent = nodes_[static_cast<size_t>(parent_idx)];

    if (parent.first_child.has_value() && *parent.first_child == index) {
      return parent.second_child;
    }
    if (parent.second_child.has_value() && *parent.second_child == index) {
      return parent.first_child;
    }

    return std::nullopt;
  }

  // Structure Modification

  void set_children(int parent_index, int first_child, int second_child) {
    if (!is_valid_index(parent_index)) {
      return;
    }

    Node& parent = nodes_[static_cast<size_t>(parent_index)];
    parent.first_child = first_child;
    parent.second_child = second_child;

    // Update children's parent pointers
    if (is_valid_index(first_child)) {
      nodes_[static_cast<size_t>(first_child)].parent = parent_index;
    }
    if (is_valid_index(second_child)) {
      nodes_[static_cast<size_t>(second_child)].parent = parent_index;
    }
  }

  void swap_children(int parent_index) {
    if (!is_valid_index(parent_index)) {
      return;
    }

    Node& parent = nodes_[static_cast<size_t>(parent_index)];
    std::swap(parent.first_child, parent.second_child);
  }

  void reparent(int child_index, std::optional<int> new_parent) {
    if (!is_valid_index(child_index)) {
      return;
    }

    nodes_[static_cast<size_t>(child_index)].parent = new_parent;
  }

  // Removal
  // Removes nodes at specified indices and remaps all indices.
  // Returns: vector where remap[old_index] = new_index, or -1 if removed.
  [[nodiscard]] std::vector<int> remove(const std::vector<int>& indices_to_remove) {
    if (nodes_.empty()) {
      return {};
    }

    // Build set for O(1) lookup
    std::set<int> to_remove(indices_to_remove.begin(), indices_to_remove.end());

    // Build remap: old_index -> new_index
    std::vector<int> remap(nodes_.size(), -1);
    int new_index = 0;

    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (to_remove.find(static_cast<int>(i)) == to_remove.end()) {
        remap[i] = new_index++;
      }
    }

    // Create new nodes vector with remapped indices
    std::vector<Node, NodeAllocator> new_nodes(nodes_.get_allocator());
    new_nodes.reserve(static_cast<size_t>(new_index));

    for (size_t i = 0; i < nodes_.size(); ++i) {
      if (to_remove.find(static_cast<int>(i)) != to_remove.end()) {
        continue;
      }

      Node node = std::move(nodes_[i]);

      // Remap parent pointer
      if (node.parent.has_value()) {
        int old_parent = *node.parent;
        if (old_parent >= 0 && static_cast<size_t>(old_parent) < remap.size()) {
          int new_parent = remap[static_cast<size_t>(old_parent)];
          node.parent = (new_parent >= 0) ? std::optional<int>(new_parent) : std::nullopt;
        } else {
          node.parent = std::nullopt;
        }
      }

      // Remap first_child pointer
      if (node.first_child.has_value()) {
        int old_child = *node.first_child;
        if (old_child >= 0 && static_cast<size_t>(old_child) < remap.size()) {
          int new_child = remap[static_cast<size_t>(old_child)];
          node.first_child = (new_child >= 0) ? std::optional<int>(new_child) : std::nullopt;
        } else {
          node.first_child = std::nullopt;
        }
      }

      // Remap second_child pointer
      if (node.second_child.has_value()) {
        int old_child = *node.second_child;
        if (old_child >= 0 && static_cast<size_t>(old_child) < remap.size()) {
          int new_child = remap[static_cast<size_t>(old_child)];
          node.second_child = (new_child >= 0) ? std::optional<int>(new_child) : std::nullopt;
        } else {
          node.second_child = std::nullopt;
        }
      }

      new_nodes.push_back(std::move(node));
    }

    nodes_ = std::move(new_nodes);
    return remap;
  }

  // Accessors

  T& operator[](int index) {
    return nodes_[static_cast<size_t>(index)].data;
  }

  const T& operator[](int index) const {
    return nodes_[static_cast<size_t>(index)].data;
  }

  Node& node(int index) {
    return nodes_[static_cast<size_t>(index)];
  }

  const Node& node(int index) const {
    return nodes_[static_cast<size_t>(index)];
  }

  [[nodiscard]] size_t size() const {
    return nodes_.size();
  }

  [[nodiscard]] bool empty() const {
    return nodes_.empty();
  }

  // Clear all nodes
  void clear() {
    nodes_.clear();
  }

  // Capacity management

  void reserve(size_t capacity) {
    nodes_.reserve(capacity);
  }

  [[nodiscard]] size_t capacity() const {
    return nodes_.capacity();
  }

  // Allocator access

  [[nodiscard]] allocator_type get_allocator() const {
    return allocator_type(nodes_.get_allocator());
  }

private:
  std::vector<Node, NodeAllocator> nodes_;
};

} // namespace wintiler
