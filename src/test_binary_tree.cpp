#include <doctest/doctest.h>

#include "binary_tree.h"

using namespace wintiler;

// Simple test data type
struct TestData {
  int value = 0;
};

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
    CHECK_FALSE(tree.is_dead(idx));
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

  TEST_CASE("mark_dead and is_dead") {
    BinaryTree<TestData> tree;

    int idx = tree.add_node(TestData{1});

    CHECK_FALSE(tree.is_dead(idx));
    CHECK(tree.is_leaf(idx));

    tree.mark_dead(idx);

    CHECK(tree.is_dead(idx));
    CHECK_FALSE(tree.is_leaf(idx)); // Dead nodes are not leaves
  }

  TEST_CASE("compact removes dead nodes") {
    BinaryTree<TestData> tree;

    int idx0 = tree.add_node(TestData{0});
    int idx1 = tree.add_node(TestData{1});
    int idx2 = tree.add_node(TestData{2});

    CHECK(tree.size() == 3);

    tree.mark_dead(idx1);

    auto remap = tree.compact();

    CHECK(tree.size() == 2);
    CHECK(remap[static_cast<size_t>(idx0)] == 0);
    CHECK(remap[static_cast<size_t>(idx1)] == -1); // Deleted
    CHECK(remap[static_cast<size_t>(idx2)] == 1);

    // Verify data is preserved
    CHECK(tree[0].value == 0);
    CHECK(tree[1].value == 2);
  }

  TEST_CASE("compact remaps parent-child pointers correctly") {
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
    tree.mark_dead(node1);

    auto remap = tree.compact();

    CHECK(tree.size() == 4);

    // Verify remap
    CHECK(remap[static_cast<size_t>(root)] == 0);
    CHECK(remap[static_cast<size_t>(node1)] == -1);
    CHECK(remap[static_cast<size_t>(node2)] == 1);
    CHECK(remap[static_cast<size_t>(node3)] == 2);
    CHECK(remap[static_cast<size_t>(node4)] == 3);

    // Verify tree structure after compaction
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

  TEST_CASE("compact on empty tree") {
    BinaryTree<TestData> tree;

    auto remap = tree.compact();

    CHECK(remap.empty());
    CHECK(tree.empty());
  }

  TEST_CASE("compact with no dead nodes") {
    BinaryTree<TestData> tree;

    tree.add_node(TestData{0});
    tree.add_node(TestData{1});
    tree.add_node(TestData{2});

    auto remap = tree.compact();

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
}
