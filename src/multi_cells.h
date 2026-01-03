#pragma once

#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <unordered_set>
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

enum class SplitMode {
  Zigzag,     // Opposite of parent's split direction (Vertical for root)
  Vertical,   // Always vertical
  Horizontal, // Always horizontal
};

struct Rect {
  float x;
  float y;
  float width;
  float height;
};

// Point coordinates (integer) for cursor positioning
struct Point {
  long x;
  long y;
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

  // Logical window size used to derive the initial root cell rect
  // when the first cell is created lazily on a split.
  float window_width = 0.0f;
  float window_height = 0.0f;

  // Zen cell index for this cluster (full-cluster display mode)
  std::optional<int> zen_cell_index;
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

struct PositionedCluster {
  CellCluster cluster;
  float global_x; // Workspace position (for tiling)
  float global_y;
  // Full monitor bounds (for pointer detection)
  float monitor_x;
  float monitor_y;
  float monitor_width;
  float monitor_height;
};

// Points to a specific cell by cluster index and cell index.
struct CellIndicatorByIndex {
  size_t cluster_index;
  int cell_index; // always a leaf index
};

// Default gap values for cell spacing
constexpr float kDefaultCellGapHorizontal = 10.0f;
constexpr float kDefaultCellGapVertical = 10.0f;

// ============================================================================
// Forward declarations for System member function return types
// ============================================================================

struct ClusterCellIds {
  size_t cluster_index;
  std::vector<size_t> leaf_ids; // Desired leaf IDs for this cluster
};

struct UpdateError {
  enum class Type {
    ClusterNotFound,
    LeafNotFound,
    SelectionInvalid,
  };
  Type type;
  size_t cluster_index;
  size_t leaf_id; // relevant leaf ID (if applicable)
};

struct UpdateResult {
  std::vector<size_t> deleted_leaf_ids;
  std::vector<size_t> added_leaf_ids;
  std::vector<UpdateError> errors;
  bool selection_updated;
};

struct MoveSuccess {
  int new_cell_index;       // Index of source cell in its new position
  size_t new_cluster_index; // Cluster where source ended up
};

// Window tile position update (for pure layout calculation)
struct TileUpdate {
  size_t leaf_id;
  int x, y, width, height;
};

// Result of selection update computation
struct SelectionUpdateResult {
  bool needs_update;
  std::optional<CellIndicatorByIndex> new_selection;
  std::optional<size_t> window_to_foreground; // leaf_id
};

// ============================================================================
// System
// ============================================================================

struct System {
  std::vector<PositionedCluster> clusters;
  std::optional<CellIndicatorByIndex> selection; // System-wide selection
  float gap_horizontal = kDefaultCellGapHorizontal;
  float gap_vertical = kDefaultCellGapVertical;
  SplitMode split_mode = SplitMode::Zigzag; // How splits determine direction

  // Mutating member functions
  [[nodiscard]] bool move_selection(Direction dir);
  [[nodiscard]] bool toggle_selected_split_dir();
  [[nodiscard]] bool cycle_split_mode();
  [[nodiscard]] bool set_selected_split_ratio(float new_ratio);
  [[nodiscard]] bool adjust_selected_split_ratio(float delta);
  [[nodiscard]] bool exchange_selected_with_sibling();
  tl::expected<void, std::string> swap_cells(size_t cluster_index1, size_t leaf_id1,
                                             size_t cluster_index2, size_t leaf_id2);
  tl::expected<MoveSuccess, std::string> move_cell(size_t source_cluster_index,
                                                   size_t source_leaf_id,
                                                   size_t target_cluster_index,
                                                   size_t target_leaf_id);
  void update_gaps(float horizontal, float vertical);
  void recompute_rects();
  UpdateResult update(const std::vector<ClusterCellIds>& cluster_cell_ids,
                      std::optional<std::pair<size_t, size_t>> new_selection,
                      std::pair<float, float> pointer_coords);

  // Zen cell operations (per-cluster)
  [[nodiscard]] bool set_zen(size_t cluster_index, size_t leaf_id);
  void clear_zen(size_t cluster_index);
  [[nodiscard]] bool is_cell_zen(size_t cluster_index, int cell_index) const;
  [[nodiscard]] bool toggle_selected_zen();

  // Update split ratio based on window resize
  // Returns true if ratio was updated, false otherwise
  [[nodiscard]] bool update_split_ratio_from_resize(size_t cluster_index, size_t leaf_id,
                                                    const Rect& actual_window_rect);
};

struct ClusterInitInfo {
  float x;      // workspace x (for tiling)
  float y;      // workspace y
  float width;  // workspace width
  float height; // workspace height
  // Full monitor bounds (for pointer detection)
  float monitor_x;
  float monitor_y;
  float monitor_width;
  float monitor_height;
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
[[nodiscard]] std::optional<std::pair<size_t, int>> get_selected_cell(const System& system);

// Get the global rect of the currently selected cell.
[[nodiscard]] std::optional<Rect> get_selected_cell_global_rect(const System& system);

// Get the zen cell for a specific cluster (returns cell index).
[[nodiscard]] std::optional<int> get_cluster_zen_cell(const CellCluster& cluster);

// Get the global rect of a cell, considering zen state.
// If is_zen is true, returns centered rect at zen_percentage of cluster size.
// Otherwise returns the cell's normal tree position.
[[nodiscard]] Rect get_cell_display_rect(const PositionedCluster& pc, int cell_index, bool is_zen,
                                         float zen_percentage);

// Get display rect for zen cell of a specific cluster (centered at zen_percentage).
[[nodiscard]] std::optional<Rect>
get_cluster_zen_display_rect(const System& system, size_t cluster_index, float zen_percentage);

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
// When a cluster has a zen cell active, only that cell is considered for hit testing
// using its zen display rect (centered at zen_percentage of cluster size).
[[nodiscard]] std::optional<std::pair<size_t, int>>
find_cell_at_point(const System& system, float global_x, float global_y, float zen_percentage);

// ============================================================================
// Leaf Utilities
// ============================================================================

// Get all leaf IDs from a cluster.
[[nodiscard]] std::vector<size_t> get_cluster_leaf_ids(const CellCluster& cluster);

// Find cell index by leaf ID. Returns nullopt if not found.
[[nodiscard]] std::optional<int> find_cell_by_leaf_id(const CellCluster& cluster, size_t leaf_id);

// ============================================================================
// Pure Logic Utilities (no side effects, suitable for use without Windows API)
// ============================================================================

// Get the center point of the currently selected cell.
// Returns nullopt if no selection exists.
[[nodiscard]] std::optional<Point> get_selected_cell_center(const System& system);

// Find which cluster contains a cell with the given leaf_id.
// Returns the cluster index or nullopt if not found.
[[nodiscard]] std::optional<size_t> find_cluster_by_leaf_id(const System& system, size_t leaf_id);

// Find a cell by leaf_id and return its center point.
// Returns nullopt if not found.
[[nodiscard]] std::optional<Point> find_cell_center_by_leaf_id(const System& system,
                                                               size_t leaf_id);

// Calculate tile positions for all cells without applying them.
// Skips clusters in skip_clusters set (e.g., for fullscreen).
// Returns a list of updates for the caller to apply.
[[nodiscard]] std::vector<TileUpdate>
calculate_tile_layout(const System& system, float zen_percentage,
                      const std::unordered_set<size_t>& skip_clusters);

// Compute whether selection should change based on cursor position.
// Returns the new selection and window to foreground (if any).
// Does not mutate the system - caller applies the changes.
[[nodiscard]] SelectionUpdateResult
compute_selection_update(const System& system, float cursor_x, float cursor_y, float zen_percentage,
                         const std::unordered_set<size_t>& fullscreen_clusters,
                         size_t foreground_window_leaf_id);

} // namespace cells
} // namespace wintiler
