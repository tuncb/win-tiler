#pragma once

#include <cstddef>

namespace wintiler {

// Type for stored cell used in swap/move operations
struct StoredCell {
  size_t cluster_index;
  size_t leaf_id;
};

} // namespace wintiler
