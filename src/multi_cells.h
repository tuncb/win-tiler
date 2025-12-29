#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wintiler {

// ============================================================================
// Basic Cell Types
// ============================================================================
namespace cells {

enum class SplitDir {
  Vertical,
  Horizontal,
};

struct Rect {
  float x;
  float y;
  float width;
  float height;
};

struct Cell {
  SplitDir split_dir;
  float split_ratio = 0.5f; // Ratio for first child (0.0-1.0), default 50/50

  bool is_dead = false;

  std::optional<int> parent;       // empty for root
  std::optional<int> first_child;  // empty if none (leaf)
  std::optional<int> second_child; // empty if none (leaf)

  Rect rect; // logical rectangle in window coordinates

  std::optional<size_t> leaf_id; // unique ID for leaf cells only
};

struct CellCluster {
  std::vector<Cell> cells;

  // Global split direction that alternates on each cell creation.
  SplitDir global_split_dir;

  // Logical window size used to derive the initial root cell rect
  // when the first cell is created lazily on a split.
  float window_width = 0.0f;
  float window_height = 0.0f;
};

enum class Direction {
  Left,
  Right,
  Up,
  Down,
};

// Result of splitting a leaf cell.
struct SplitResult {
  size_t new_leaf_id;
  int new_selection_index;
};

// Returns true if the cell at cell_index exists and has no children.
[[nodiscard]] bool is_leaf(const CellCluster& state, int cell_index);

// ============================================================================
// Multi-Cluster System
// ============================================================================

using ClusterId = size_t;

struct PositionedCluster {
  ClusterId id;
  CellCluster cluster;
  float global_x; // Global position of cluster's top-left
  float global_y;
};

// Points to a specific cell by cluster ID and cell index.
struct CellIndicatorByIndex {
  ClusterId cluster_id;
  int cell_index; // always a leaf index
};

// Default gap values for cell spacing
constexpr float kDefaultCellGapHorizontal = 10.0f;
constexpr float kDefaultCellGapVertical = 10.0f;

// ============================================================================
// Forward declarations for System member function return types
// ============================================================================

struct ClusterCellIds {
  ClusterId cluster_id;
  std::vector<size_t> leaf_ids; // Desired leaf IDs for this cluster
};

struct UpdateError {
  enum class Type {
    ClusterNotFound,
    LeafNotFound,
    SelectionInvalid,
  };
  Type type;
  ClusterId cluster_id;
  size_t leaf_id; // relevant leaf ID (if applicable)
};

struct UpdateResult {
  std::vector<size_t> deleted_leaf_ids;
  std::vector<size_t> added_leaf_ids;
  std::vector<UpdateError> errors;
  bool selection_updated;
};

struct SwapResult {
  bool success;
  std::string error_message; // Empty if success
};

struct MoveResult {
  bool success;
  int new_cell_index;        // Index of source cell in its new position
  ClusterId new_cluster_id;  // Cluster where source ended up
  std::string error_message; // Empty if success
};

// ============================================================================
// System
// ============================================================================

struct System {
  std::vector<PositionedCluster> clusters;
  std::optional<CellIndicatorByIndex> selection; // System-wide selection
  std::optional<CellIndicatorByIndex>
      signified; // System-wide signified cell (for full-cluster display)
  float gap_horizontal = kDefaultCellGapHorizontal;
  float gap_vertical = kDefaultCellGapVertical;

  // Mutating member functions
  PositionedCluster* get_cluster(ClusterId id);
  [[nodiscard]] const PositionedCluster* get_cluster(ClusterId id) const;
  [[nodiscard]] bool move_selection(Direction dir);
  [[nodiscard]] bool toggle_selected_split_dir();
  [[nodiscard]] bool toggle_cluster_global_split_dir();
  [[nodiscard]] bool set_selected_split_ratio(float new_ratio);
  [[nodiscard]] bool adjust_selected_split_ratio(float delta);
  [[nodiscard]] bool exchange_selected_with_sibling();
  SwapResult swap_cells(ClusterId cluster_id1, size_t leaf_id1, ClusterId cluster_id2,
                        size_t leaf_id2);
  MoveResult move_cell(ClusterId source_cluster_id, size_t source_leaf_id,
                       ClusterId target_cluster_id, size_t target_leaf_id);
  void update_gaps(float horizontal, float vertical);
  void recompute_rects();
  UpdateResult update(const std::vector<ClusterCellIds>& cluster_cell_ids,
                      std::optional<std::pair<ClusterId, size_t>> new_selection);

  // Signified cell operations
  [[nodiscard]] bool set_signified(ClusterId cluster_id, size_t leaf_id);
  void clear_signified();
  [[nodiscard]] bool is_cell_signified(ClusterId cluster_id, int cell_index) const;
};

struct ClusterInitInfo {
  ClusterId id;
  float x;
  float y;
  float width;
  float height;
  std::vector<size_t> initial_cell_ids; // Optional pre-assigned leaf IDs
};

// ============================================================================
// Initialization
// ============================================================================

// Create a multi-cluster system from cluster initialization info.
// Optional gap values default to kDefaultCellGapHorizontal/kDefaultCellGapVertical.
System create_system(const std::vector<ClusterInitInfo>& infos,
                     float gap_horizontal = kDefaultCellGapHorizontal,
                     float gap_vertical = kDefaultCellGapVertical);

// ============================================================================
// Coordinate Conversion
// ============================================================================

// Get the global rect of a cell in a positioned cluster.
Rect get_cell_global_rect(const PositionedCluster& pc, int cell_index);

// ============================================================================
// Operations
// ============================================================================

// Get the currently selected cell across the entire system.
[[nodiscard]] std::optional<std::pair<ClusterId, int>> get_selected_cell(const System& system);

// Get the global rect of the currently selected cell.
[[nodiscard]] std::optional<Rect> get_selected_cell_global_rect(const System& system);

// Get the currently signified cell across the entire system.
[[nodiscard]] std::optional<std::pair<ClusterId, int>> get_signified_cell(const System& system);

// Get the global rect of a cell, considering signified state.
// If is_signified is true, returns cluster bounds (with gaps from edges).
// Otherwise returns the cell's normal tree position.
[[nodiscard]] Rect get_cell_display_rect(const PositionedCluster& pc, int cell_index,
                                         bool is_signified, float gap_horizontal,
                                         float gap_vertical);

// Convenience: Get display rect for signified cell (full cluster bounds with gaps).
[[nodiscard]] std::optional<Rect> get_signified_cell_display_rect(const System& system);

// Set the split ratio of a parent cell and recompute all descendant rectangles.
// Returns false if the cell is not a valid non-leaf cell.
bool set_split_ratio(CellCluster& state, int cell_index, float new_ratio, float gap_horizontal,
                     float gap_vertical);

// ============================================================================
// Utilities
// ============================================================================

// Validate the entire multi-cluster system.
bool validate_system(const System& system);

// Debug: print the entire multi-cluster system to stdout.
void debug_print_system(const System& system);

// Count total leaves across all clusters.
[[nodiscard]] size_t count_total_leaves(const System& system);

// Check if a leaf_id exists in the system (in any non-dead cell).
[[nodiscard]] bool has_leaf_id(const System& system, size_t leaf_id);

// ============================================================================
// Hit Testing
// ============================================================================

// Find the cluster and cell at a global point.
// Returns nullopt if no cell contains the point.
[[nodiscard]] std::optional<std::pair<ClusterId, int>>
find_cell_at_point(const System& system, float global_x, float global_y);

// ============================================================================
// Leaf Utilities
// ============================================================================

// Get all leaf IDs from a cluster.
[[nodiscard]] std::vector<size_t> get_cluster_leaf_ids(const CellCluster& cluster);

// Find cell index by leaf ID. Returns nullopt if not found.
[[nodiscard]] std::optional<int> find_cell_by_leaf_id(const CellCluster& cluster, size_t leaf_id);

} // namespace cells
} // namespace wintiler
