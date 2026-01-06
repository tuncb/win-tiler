#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace wintiler {

// ArenaResource: Owns memory blocks and provides raw allocation.
// Allocations grow by adding new blocks. Only bulk reset is supported.
class ArenaResource {
public:
  // Maximum alignment supported (64 bytes covers most SIMD requirements)
  static constexpr size_t kMaxAlignment = 64;

  explicit ArenaResource(size_t block_size = 64 * 1024) : default_block_size_(block_size) {
    add_block(default_block_size_);
  }

  ~ArenaResource() {
    for (auto& block : blocks_) {
      free_aligned(block.data);
    }
  }

  // Non-copyable, non-movable (allocators reference this)
  ArenaResource(const ArenaResource&) = delete;
  ArenaResource& operator=(const ArenaResource&) = delete;
  ArenaResource(ArenaResource&&) = delete;
  ArenaResource& operator=(ArenaResource&&) = delete;

  void* allocate(size_t bytes, size_t alignment) {
    if (bytes == 0) {
      return nullptr;
    }

    Block& block = blocks_.back();

    // Align current offset
    size_t aligned_offset = align_up(block.offset, alignment);

    if (aligned_offset + bytes > block.size) {
      // Current block exhausted, allocate new block
      add_block(std::max(bytes + alignment, default_block_size_));
      return allocate(bytes, alignment); // retry in new block
    }

    void* ptr = block.data + aligned_offset;
    block.offset = aligned_offset + bytes;
    return ptr;
  }

  // No-op for monotonic arena - memory freed only on reset() or destruction
  void deallocate([[maybe_unused]] void* p, [[maybe_unused]] size_t bytes) noexcept {
    // Intentionally empty
  }

  // Reset arena: keep first block, release others, reset offset to 0
  void reset() {
    if (blocks_.empty()) {
      return;
    }

    // Free all blocks except the first
    for (size_t i = 1; i < blocks_.size(); ++i) {
      free_aligned(blocks_[i].data);
    }
    blocks_.resize(1);

    blocks_[0].offset = 0;
  }

  [[nodiscard]] size_t bytes_allocated() const noexcept {
    size_t total = 0;
    for (const auto& block : blocks_) {
      total += block.offset;
    }
    return total;
  }

  [[nodiscard]] size_t blocks_count() const noexcept {
    return blocks_.size();
  }

  [[nodiscard]] size_t total_capacity() const noexcept {
    size_t total = 0;
    for (const auto& block : blocks_) {
      total += block.size;
    }
    return total;
  }

private:
  struct Block {
    std::byte* data;
    size_t size;
    size_t offset; // current allocation position
  };

  std::vector<Block> blocks_;
  size_t default_block_size_;

  void add_block(size_t min_size) {
    Block block;
    block.size = min_size;
    block.offset = 0;
    block.data = static_cast<std::byte*>(allocate_aligned(min_size, kMaxAlignment));
    blocks_.push_back(block);
  }

  static void* allocate_aligned(size_t size, size_t alignment) {
#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
  }

  static void free_aligned(void* ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
  }

  static size_t align_up(size_t value, size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
  }
};

// ArenaAllocator: STL-compatible allocator that uses ArenaResource.
// Designed for use with std::vector, BinaryTree, and other STL containers.
template <typename T>
class ArenaAllocator {
public:
  using value_type = T;

  explicit ArenaAllocator(ArenaResource& resource) noexcept : resource_(&resource) {
  }

  // Rebind constructor for allocator_traits
  template <typename U>
  ArenaAllocator(const ArenaAllocator<U>& other) noexcept : resource_(&other.resource()) {
  }

  T* allocate(size_t n) {
    return static_cast<T*>(resource_->allocate(n * sizeof(T), alignof(T)));
  }

  void deallocate(T* p, size_t n) noexcept {
    resource_->deallocate(p, n * sizeof(T));
  }

  ArenaResource& resource() const noexcept {
    return *resource_;
  }

  template <typename U>
  bool operator==(const ArenaAllocator<U>& other) const noexcept {
    return resource_ == &other.resource();
  }

  template <typename U>
  bool operator!=(const ArenaAllocator<U>& other) const noexcept {
    return !(*this == other);
  }

private:
  ArenaResource* resource_;
};

} // namespace wintiler
