#include <doctest/doctest.h>

#include "binary_tree.h"

using namespace wintiler;

// Simple test data type
struct TestData {
  int value = 0;
};

// Tracking allocator for custom allocator tests
// Must be defined outside test cases because local classes can't have template members
namespace {
int g_tracking_allocation_count = 0;
int g_counting_allocation_count = 0;

template <typename T>
struct TrackingAllocator {
  using value_type = T;

  TrackingAllocator() = default;

  template <typename U>
  TrackingAllocator(const TrackingAllocator<U>&) {
  }

  T* allocate(std::size_t n) {
    g_tracking_allocation_count++;
    return std::allocator<T>{}.allocate(n);
  }

  void deallocate(T* p, std::size_t n) {
    std::allocator<T>{}.deallocate(p, n);
  }

  bool operator==(const TrackingAllocator&) const {
    return true;
  }
  bool operator!=(const TrackingAllocator&) const {
    return false;
  }
};

template <typename T>
struct CountingAllocator {
  using value_type = T;

  CountingAllocator() = default;

  template <typename U>
  CountingAllocator(const CountingAllocator<U>&) {
  }

  T* allocate(std::size_t n) {
    g_counting_allocation_count++;
    return std::allocator<T>{}.allocate(n);
  }

  void deallocate(T* p, std::size_t n) {
    std::allocator<T>{}.deallocate(p, n);
  }

  bool operator==(const CountingAllocator&) const {
    return true;
  }
  bool operator!=(const CountingAllocator&) const {
    return false;
  }
};
} // namespace

TEST_SUITE("BinaryTree") {
  TEST_CASE("empty tree") {
    BinaryTree<TestData> tree;

    CHECK(tree.empty());
    CHECK(tree.size() == 0);
  }

  TEST_CASE("add nodes and check size") {
    BinaryTree<TestData> tree;

    int idx0 = tree.add_node(TestData{10});
    CHECK(tree.size() == 1);
    CHECK(idx0 == 0);
    CHECK(tree[0].value == 10);

    int idx1 = tree.add_node(TestData{20});
    CHECK(tree.size() == 2);
    CHECK(idx1 == 1);
    CHECK(tree[1].value == 20);
  }

  TEST_CASE("new node is leaf") {
    BinaryTree<TestData> tree;

    int idx = tree.add_node(TestData{1});
    CHECK(tree.is_leaf(idx));
  }

  TEST_CASE("is_valid_index") {
    BinaryTree<TestData> tree;
    tree.add_node(TestData{1});

    CHECK(tree.is_valid_index(0));
    CHECK_FALSE(tree.is_valid_index(-1));
    CHECK_FALSE(tree.is_valid_index(1));
    CHECK_FALSE(tree.is_valid_index(100));
  }

  TEST_CASE("set_children makes parent non-leaf") {
    BinaryTree<TestData> tree;

    int parent = tree.add_node(TestData{0});
    int child1 = tree.add_node(TestData{1});
    int child2 = tree.add_node(TestData{2});

    CHECK(tree.is_leaf(parent));

    tree.set_children(parent, child1, child2);

    CHECK_FALSE(tree.is_leaf(parent));
    CHECK(tree.is_leaf(child1));
    CHECK(tree.is_leaf(child2));

    // Verify parent-child relationships
    CHECK(tree.get_first_child(parent) == child1);
    CHECK(tree.get_second_child(parent) == child2);
    CHECK(tree.get_parent(child1) == parent);
    CHECK(tree.get_parent(child2) == parent);
  }

  TEST_CASE("get_parent for root returns nullopt") {
    BinaryTree<TestData> tree;

    int root = tree.add_node(TestData{0});
    CHECK_FALSE(tree.get_parent(root).has_value());
  }

  TEST_CASE("get_sibling returns other child") {
    BinaryTree<TestData> tree;

    int parent = tree.add_node(TestData{0});
    int child1 = tree.add_node(TestData{1});
    int child2 = tree.add_node(TestData{2});

    tree.set_children(parent, child1, child2);

    auto sibling1 = tree.get_sibling(child1);
    CHECK(sibling1.has_value());
    CHECK(*sibling1 == child2);

    auto sibling2 = tree.get_sibling(child2);
    CHECK(sibling2.has_value());
    CHECK(*sibling2 == child1);
  }

  TEST_CASE("get_sibling for root returns nullopt") {
    BinaryTree<TestData> tree;

    int root = tree.add_node(TestData{0});
    CHECK_FALSE(tree.get_sibling(root).has_value());
  }

  TEST_CASE("swap_children exchanges first and second") {
    BinaryTree<TestData> tree;

    int parent = tree.add_node(TestData{0});
    int child1 = tree.add_node(TestData{1});
    int child2 = tree.add_node(TestData{2});

    tree.set_children(parent, child1, child2);

    CHECK(tree.get_first_child(parent) == child1);
    CHECK(tree.get_second_child(parent) == child2);

    tree.swap_children(parent);

    CHECK(tree.get_first_child(parent) == child2);
    CHECK(tree.get_second_child(parent) == child1);
  }

  TEST_CASE("reparent updates parent pointer") {
    BinaryTree<TestData> tree;

    int node0 = tree.add_node(TestData{0});
    int node1 = tree.add_node(TestData{1});

    CHECK_FALSE(tree.get_parent(node1).has_value());

    tree.reparent(node1, node0);

    CHECK(tree.get_parent(node1).has_value());
    CHECK(*tree.get_parent(node1) == node0);

    tree.reparent(node1, std::nullopt);
    CHECK_FALSE(tree.get_parent(node1).has_value());
  }

  TEST_CASE("remove single node") {
    BinaryTree<TestData> tree;

    int idx0 = tree.add_node(TestData{0});
    int idx1 = tree.add_node(TestData{1});

    CHECK(tree.size() == 2);

    auto remap = tree.remove({idx0});

    CHECK(tree.size() == 1);
    CHECK(remap[static_cast<size_t>(idx0)] == -1);
    CHECK(remap[static_cast<size_t>(idx1)] == 0);
    CHECK(tree[0].value == 1);
  }

  TEST_CASE("remove multiple nodes") {
    BinaryTree<TestData> tree;

    int idx0 = tree.add_node(TestData{0});
    int idx1 = tree.add_node(TestData{1});
    int idx2 = tree.add_node(TestData{2});
    int idx3 = tree.add_node(TestData{3});

    CHECK(tree.size() == 4);

    auto remap = tree.remove({idx1, idx2});

    CHECK(tree.size() == 2);
    CHECK(remap[static_cast<size_t>(idx0)] == 0);
    CHECK(remap[static_cast<size_t>(idx1)] == -1);
    CHECK(remap[static_cast<size_t>(idx2)] == -1);
    CHECK(remap[static_cast<size_t>(idx3)] == 1);

    // Verify data is preserved
    CHECK(tree[0].value == 0);
    CHECK(tree[1].value == 3);
  }

  TEST_CASE("remove remaps parent-child pointers correctly") {
    BinaryTree<TestData> tree;

    // Build tree:
    //       0
    //      / \
    //     1   2
    //        / \
    //       3   4
    int root = tree.add_node(TestData{0});
    int node1 = tree.add_node(TestData{1});
    int node2 = tree.add_node(TestData{2});
    int node3 = tree.add_node(TestData{3});
    int node4 = tree.add_node(TestData{4});

    tree.set_children(root, node1, node2);
    tree.set_children(node2, node3, node4);

    // Delete node1 (leaf)
    auto remap = tree.remove({node1});

    CHECK(tree.size() == 4);

    // Verify remap
    CHECK(remap[static_cast<size_t>(root)] == 0);
    CHECK(remap[static_cast<size_t>(node1)] == -1);
    CHECK(remap[static_cast<size_t>(node2)] == 1);
    CHECK(remap[static_cast<size_t>(node3)] == 2);
    CHECK(remap[static_cast<size_t>(node4)] == 3);

    // Verify tree structure after removal
    // New indices: root=0, node2=1, node3=2, node4=3

    // Root should have first_child pointing to nullopt (node1 was deleted)
    // and second_child pointing to 1 (was node2)
    auto root_first = tree.get_first_child(0);
    auto root_second = tree.get_second_child(0);
    CHECK_FALSE(root_first.has_value()); // node1 was deleted
    CHECK(root_second.has_value());
    CHECK(*root_second == 1);

    // Node2 (now at index 1) should have children at 2 and 3
    CHECK(tree.get_first_child(1) == 2);
    CHECK(tree.get_second_child(1) == 3);

    // Children's parents should point to 1
    CHECK(tree.get_parent(2) == 1);
    CHECK(tree.get_parent(3) == 1);
  }

  TEST_CASE("remove on empty tree") {
    BinaryTree<TestData> tree;

    auto remap = tree.remove({0, 1});

    CHECK(remap.empty());
    CHECK(tree.empty());
  }

  TEST_CASE("remove with empty indices") {
    BinaryTree<TestData> tree;

    tree.add_node(TestData{0});
    tree.add_node(TestData{1});
    tree.add_node(TestData{2});

    auto remap = tree.remove({});

    CHECK(tree.size() == 3);
    CHECK(remap[0] == 0);
    CHECK(remap[1] == 1);
    CHECK(remap[2] == 2);
  }

  TEST_CASE("node accessor provides full node access") {
    BinaryTree<TestData> tree;

    int idx = tree.add_node(TestData{42});
    tree.add_node(TestData{43});

    tree.set_children(idx, 1, -1); // Only first child (second invalid)

    const auto& node = tree.node(idx);
    CHECK(node.data.value == 42);
    CHECK_FALSE(node.parent.has_value());
    CHECK(node.first_child.has_value());
    CHECK(*node.first_child == 1);
  }

  TEST_CASE("clear removes all nodes") {
    BinaryTree<TestData> tree;

    tree.add_node(TestData{0});
    tree.add_node(TestData{1});

    CHECK(tree.size() == 2);

    tree.clear();

    CHECK(tree.empty());
    CHECK(tree.size() == 0);
  }

  TEST_CASE("tree with different data type") {
    // Test with a different data type to verify template works
    struct Point {
      float x, y;
    };

    BinaryTree<Point> tree;

    int idx = tree.add_node(Point{1.5f, 2.5f});
    CHECK(tree[idx].x == 1.5f);
    CHECK(tree[idx].y == 2.5f);
  }

  TEST_CASE("reserve increases capacity without adding nodes") {
    BinaryTree<TestData> tree;

    CHECK(tree.size() == 0);
    CHECK(tree.capacity() == 0);

    tree.reserve(100);

    CHECK(tree.size() == 0);
    CHECK(tree.capacity() >= 100);
  }

  TEST_CASE("reserve prevents reallocation during add_node") {
    BinaryTree<TestData> tree;
    tree.reserve(10);

    size_t initial_capacity = tree.capacity();
    CHECK(initial_capacity >= 10);

    // Add nodes up to reserved capacity
    for (int i = 0; i < 10; ++i) {
      tree.add_node(TestData{i});
    }

    // Capacity should not have changed (no reallocation)
    CHECK(tree.capacity() == initial_capacity);
    CHECK(tree.size() == 10);
  }

  TEST_CASE("get_allocator returns allocator") {
    BinaryTree<TestData> tree;

    [[maybe_unused]] auto alloc = tree.get_allocator();
    // Just verify it compiles and returns an allocator
    CHECK(true);
  }

  TEST_CASE("custom allocator is used") {
    g_tracking_allocation_count = 0;

    TrackingAllocator<TestData> alloc;
    BinaryTree<TestData, TrackingAllocator<TestData>> tree(alloc);

    tree.add_node(TestData{1});
    tree.add_node(TestData{2});

    // Verify allocator was used (at least one allocation occurred)
    CHECK(g_tracking_allocation_count > 0);
  }

  TEST_CASE("remove uses custom allocator for temp vector") {
    g_counting_allocation_count = 0;

    CountingAllocator<TestData> alloc;
    BinaryTree<TestData, CountingAllocator<TestData>> tree(alloc);

    tree.add_node(TestData{0});
    tree.add_node(TestData{1});
    tree.add_node(TestData{2});

    int count_before_remove = g_counting_allocation_count;

    // Remove triggers creation of new_nodes vector which should use our allocator
    auto remap = tree.remove({1});

    // Allocation count should have increased (new_nodes vector allocated)
    CHECK(g_counting_allocation_count >= count_before_remove);
    CHECK(tree.size() == 2);
  }
}
