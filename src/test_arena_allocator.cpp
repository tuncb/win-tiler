#include <doctest/doctest.h>

#include <vector>

#include "arena_allocator.h"
#include "binary_tree.h"

using namespace wintiler;

TEST_SUITE("ArenaAllocator") {
  TEST_CASE("ArenaResource basic allocation") {
    ArenaResource arena(1024);

    void* p1 = arena.allocate(100, alignof(int));
    CHECK(p1 != nullptr);
    CHECK(arena.bytes_allocated() >= 100);
    CHECK(arena.blocks_count() == 1);

    void* p2 = arena.allocate(200, alignof(int));
    CHECK(p2 != nullptr);
    CHECK(p1 != p2);
    CHECK(arena.bytes_allocated() >= 300);
  }

  TEST_CASE("ArenaResource zero allocation returns nullptr") {
    ArenaResource arena(1024);

    void* p = arena.allocate(0, alignof(int));
    CHECK(p == nullptr);
  }

  TEST_CASE("ArenaResource grows when block exhausted") {
    ArenaResource arena(256); // Small block size

    // Allocate more than one block can hold
    void* p1 = arena.allocate(200, alignof(int));
    CHECK(p1 != nullptr);
    CHECK(arena.blocks_count() == 1);

    void* p2 = arena.allocate(200, alignof(int));
    CHECK(p2 != nullptr);
    CHECK(arena.blocks_count() == 2); // Should have grown
  }

  TEST_CASE("ArenaResource handles large allocation") {
    ArenaResource arena(256); // Small default block

    // Allocate larger than default block size
    void* p = arena.allocate(1024, alignof(int));
    CHECK(p != nullptr);
    CHECK(arena.blocks_count() == 2); // Original + large block
  }

  TEST_CASE("ArenaResource alignment is respected") {
    ArenaResource arena(1024);

    // Allocate 1 byte to misalign
    arena.allocate(1, 1);

    // Allocate with 16-byte alignment
    void* p = arena.allocate(32, 16);
    CHECK(p != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(p) % 16 == 0);

    // Allocate with 64-byte alignment
    void* p2 = arena.allocate(64, 64);
    CHECK(p2 != nullptr);
    CHECK(reinterpret_cast<uintptr_t>(p2) % 64 == 0);
  }

  TEST_CASE("ArenaResource reset clears allocations") {
    ArenaResource arena(1024);

    arena.allocate(100, alignof(int));
    arena.allocate(200, alignof(int));
    CHECK(arena.bytes_allocated() >= 300);

    arena.reset();

    CHECK(arena.bytes_allocated() == 0);
    CHECK(arena.blocks_count() == 1); // Keeps first block
  }

  TEST_CASE("ArenaResource reset releases extra blocks") {
    ArenaResource arena(256);

    // Force multiple blocks
    arena.allocate(200, alignof(int));
    arena.allocate(200, alignof(int));
    arena.allocate(200, alignof(int));
    CHECK(arena.blocks_count() >= 2);

    arena.reset();

    CHECK(arena.blocks_count() == 1);
    CHECK(arena.bytes_allocated() == 0);
  }

  TEST_CASE("ArenaResource deallocate is no-op") {
    ArenaResource arena(1024);

    void* p = arena.allocate(100, alignof(int));
    size_t allocated_before = arena.bytes_allocated();

    arena.deallocate(p, 100);

    // Deallocate should not change anything
    CHECK(arena.bytes_allocated() == allocated_before);
  }

  TEST_CASE("ArenaAllocator with std::vector<int>") {
    ArenaResource arena(4096);
    ArenaAllocator<int> alloc(arena);
    std::vector<int, ArenaAllocator<int>> vec(alloc);

    vec.reserve(100);
    for (int i = 0; i < 100; ++i) {
      vec.push_back(i);
    }

    CHECK(vec.size() == 100);
    CHECK(vec[0] == 0);
    CHECK(vec[99] == 99);
    CHECK(arena.bytes_allocated() >= 100 * sizeof(int));
  }

  TEST_CASE("ArenaAllocator with std::vector grows arena") {
    ArenaResource arena(256); // Small block
    ArenaAllocator<int> alloc(arena);
    std::vector<int, ArenaAllocator<int>> vec(alloc);

    // Push enough to trigger multiple reallocations
    for (int i = 0; i < 1000; ++i) {
      vec.push_back(i);
    }

    CHECK(vec.size() == 1000);
    CHECK(arena.blocks_count() >= 1);

    // Verify data integrity
    for (int i = 0; i < 1000; ++i) {
      CHECK(vec[static_cast<size_t>(i)] == i);
    }
  }

  TEST_CASE("ArenaAllocator with struct alignment") {
    struct alignas(32) AlignedStruct {
      double data[4];
    };

    ArenaResource arena(4096);
    ArenaAllocator<AlignedStruct> alloc(arena);
    std::vector<AlignedStruct, ArenaAllocator<AlignedStruct>> vec(alloc);

    vec.push_back(AlignedStruct{{1.0, 2.0, 3.0, 4.0}});
    vec.push_back(AlignedStruct{{5.0, 6.0, 7.0, 8.0}});

    CHECK(vec.size() == 2);
    CHECK(reinterpret_cast<uintptr_t>(vec.data()) % 32 == 0);
    CHECK(vec[0].data[0] == 1.0);
    CHECK(vec[1].data[0] == 5.0);
  }

  TEST_CASE("ArenaAllocator equality") {
    ArenaResource arena1(1024);
    ArenaResource arena2(1024);

    ArenaAllocator<int> alloc1a(arena1);
    ArenaAllocator<int> alloc1b(arena1);
    ArenaAllocator<int> alloc2(arena2);

    CHECK(alloc1a == alloc1b); // Same resource
    CHECK(alloc1a != alloc2);  // Different resource
  }

  TEST_CASE("ArenaAllocator rebind works") {
    ArenaResource arena(1024);
    ArenaAllocator<int> int_alloc(arena);

    // Rebind to double
    ArenaAllocator<double> double_alloc(int_alloc);

    double* p = double_alloc.allocate(10);
    CHECK(p != nullptr);

    // Both should reference the same arena
    CHECK(&int_alloc.resource() == &double_alloc.resource());
  }

  TEST_CASE("ArenaAllocator with BinaryTree") {
    struct TestData {
      int value = 0;
    };

    ArenaResource arena(4096);
    ArenaAllocator<TestData> alloc(arena);
    BinaryTree<TestData, ArenaAllocator<TestData>> tree(alloc);

    tree.reserve(50);

    int root = tree.add_node(TestData{0});
    int child1 = tree.add_node(TestData{1});
    int child2 = tree.add_node(TestData{2});

    tree.set_children(root, child1, child2);

    CHECK(tree.size() == 3);
    CHECK(tree[root].value == 0);
    CHECK(tree[child1].value == 1);
    CHECK(tree[child2].value == 2);
    CHECK(tree.get_first_child(root) == child1);
    CHECK(tree.get_second_child(root) == child2);
  }

  TEST_CASE("ArenaAllocator BinaryTree remove works") {
    struct TestData {
      int value = 0;
    };

    ArenaResource arena(4096);
    ArenaAllocator<TestData> alloc(arena);
    BinaryTree<TestData, ArenaAllocator<TestData>> tree(alloc);

    tree.add_node(TestData{0});
    tree.add_node(TestData{1});
    tree.add_node(TestData{2});

    CHECK(tree.size() == 3);

    auto remap = tree.remove({1});

    CHECK(tree.size() == 2);
    CHECK(remap[0] == 0);
    CHECK(remap[1] == -1);
    CHECK(remap[2] == 1);
  }

  TEST_CASE("ArenaResource total_capacity") {
    ArenaResource arena(1024);

    CHECK(arena.total_capacity() == 1024);

    // Force new block
    arena.allocate(2000, alignof(int));

    CHECK(arena.total_capacity() >= 1024 + 2000);
    CHECK(arena.blocks_count() == 2);
  }

  TEST_CASE("Arena reset allows reuse") {
    ArenaResource arena(1024);
    ArenaAllocator<int> alloc(arena);
    std::vector<int, ArenaAllocator<int>> vec(alloc);

    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(3);

    // Clear vector (doesn't free arena memory)
    vec.clear();
    vec.shrink_to_fit();

    // Reset arena
    arena.reset();

    // Create new vector using same arena
    std::vector<int, ArenaAllocator<int>> vec2(alloc);
    vec2.push_back(10);
    vec2.push_back(20);

    CHECK(vec2.size() == 2);
    CHECK(vec2[0] == 10);
    CHECK(vec2[1] == 20);
  }
}
