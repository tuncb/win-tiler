#pragma once

#include <optional>
#include <string>
#include <tl/expected.hpp>
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

  std::optional<int> parent;       // empty for root
  std::optional<int> first_child;  // empty if none (leaf)
  std::optional<int> second_child; // empty if none (leaf)

  Rect rect; // logical rectangle in window coordinates

  std::optional<size_t> leaf_id; // unique ID for leaf cells only
  bool is_dead = false;          // true if cell is logically deleted but not yet compacted
};

struct CellCluster {
  std::vector<Cell> cells;

  // Logical window size used to derive the initial root cell rect
  // when the first cell is created lazily on a split.
  float window_width = 0.0f;
  float window_height = 0.0f;

  // Zen cell index for this cluster (full-cluster display mode)
  std::optional<int> zen_cell_index;

  // True if any window in this cluster is fullscreen
  bool has_fullscreen_cell = false;
};

enum class Direction {
  Left,
  Right,
  Up,
  Down,
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
// Forward declarations for System function return types
// ============================================================================

struct ClusterCellUpdateInfo {
  size_t cluster_index;
  std::vector<size_t> leaf_ids;     // Desired leaf IDs for this cluster
  bool has_fullscreen_cell = false; // True if any window in this cluster is fullscreen
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

struct UpdateResult {
  std::vector<size_t> deleted_leaf_ids;
  std::vector<size_t> added_leaf_ids;
  std::vector<UpdateError> errors;
  bool selection_updated;

  // All window position updates to apply
  std::vector<TileUpdate> tile_updates;

  // Selection/foreground update info (selection already mutated inside update())
  SelectionUpdateResult selection_update;

  // Cursor position for newly added windows (if any)
  std::optional<Point> new_window_cursor_pos;
};

struct MoveSuccess {
  int new_cell_index;       // Index of source cell in its new position
  size_t new_cluster_index; // Cluster where source ended up
  Point center;             // Cursor position for mouse movement
};

// Result of a successful selection move operation
struct MoveSelectionResult {
  size_t leaf_id; // Window handle for setting foreground
  Point center;   // Cursor position for mouse movement
};

// Result of a drop move operation (drag-and-drop window move)
struct DropMoveResult {
  Point cursor_pos;  // Where to move cursor after operation
  bool was_exchange; // Whether exchange (vs move) was performed
};

// ============================================================================
// System
// ============================================================================

struct System {
  std::vector<PositionedCluster> clusters;
  std::optional<CellIndicatorByIndex> selection; // System-wide selection
  SplitMode split_mode = SplitMode::Zigzag;      // How splits determine direction
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
System create_system(const std::vector<ClusterInitInfo>& infos, float gap_horizontal,
                     float gap_vertical);

// ============================================================================
// Coordinate Conversion
// ============================================================================

// Get the global rect of a cell in a positioned cluster.
Rect get_cell_global_rect(const PositionedCluster& pc, int cell_index);

// ============================================================================
// Query Operations
// ============================================================================

// Get the global rect of a cell, considering zen state.
// If is_zen is true, returns centered rect at zen_percentage of cluster size.
// Otherwise returns the cell's normal tree position.
[[nodiscard]] Rect get_cell_display_rect(const PositionedCluster& pc, int cell_index, bool is_zen,
                                         float zen_percentage);

// ============================================================================
// Selection Navigation
// ============================================================================

// Move selection to adjacent cell in given direction
[[nodiscard]] std::optional<MoveSelectionResult> move_selection(System& system, Direction dir);

// ============================================================================
// Split Operations
// ============================================================================

// Set the split ratio of a parent cell and recompute all descendant rectangles.
// Returns false if the cell is not a valid non-leaf cell.
bool set_split_ratio(CellCluster& state, int cell_index, float new_ratio, float gap_horizontal,
                     float gap_vertical);

// Toggle split direction of selected cell's parent
[[nodiscard]] bool toggle_selected_split_dir(System& system, float gap_horizontal,
                                             float gap_vertical);

// Cycle through split modes (Zigzag -> Vertical -> Horizontal -> Zigzag)
[[nodiscard]] bool cycle_split_mode(System& system);

// Set split ratio of selected cell's parent
[[nodiscard]] std::optional<Point>
set_selected_split_ratio(System& system, float new_ratio, float gap_horizontal, float gap_vertical);

// Adjust split ratio of selected cell's parent by delta
[[nodiscard]] std::optional<Point>
adjust_selected_split_ratio(System& system, float delta, float gap_horizontal, float gap_vertical);

// Update split ratio based on window resize
[[nodiscard]] bool update_split_ratio_from_resize(System& system, size_t cluster_index,
                                                  size_t leaf_id, const Rect& actual_window_rect,
                                                  float gap_horizontal, float gap_vertical);

// ============================================================================
// Cell Movement & Exchange
// ============================================================================

// Get the leaf_id of the selected cell's sibling (for use with swap_cells)
[[nodiscard]] std::optional<size_t> get_selected_sibling_leaf_id(const System& system);

// Swap two cells (exchange leaf IDs)
std::optional<Point> swap_cells(System& system, size_t cluster_index1, size_t leaf_id1,
                                size_t cluster_index2, size_t leaf_id2, float gap_horizontal,
                                float gap_vertical);

// Move a cell from source to target (delete + split)
std::optional<MoveSuccess> move_cell(System& system, size_t source_cluster_index,
                                     size_t source_leaf_id, size_t target_cluster_index,
                                     size_t target_leaf_id, float gap_horizontal,
                                     float gap_vertical);

// Perform drop move (drag-and-drop operation)
std::optional<DropMoveResult> perform_drop_move(System& system, size_t source_leaf_id,
                                                float cursor_x, float cursor_y,
                                                float zen_percentage, bool do_exchange,
                                                float gap_horizontal, float gap_vertical);

// ============================================================================
// Zen Mode
// ============================================================================

// Set zen mode for a cell
[[nodiscard]] bool set_zen(System& system, size_t cluster_index, size_t leaf_id);

// Clear zen mode for a cluster
void clear_zen(System& system, size_t cluster_index);

// Check if a cell is in zen mode
[[nodiscard]] bool is_cell_zen(const System& system, size_t cluster_index, int cell_index);

// Toggle zen mode for selected cell
[[nodiscard]] bool toggle_selected_zen(System& system);

// ============================================================================
// System State Updates
// ============================================================================

// Recompute all cell rectangles
void recompute_rects(System& system, float gap_horizontal, float gap_vertical);

// Update system state with new window configuration
UpdateResult update(System& system, const std::vector<ClusterCellUpdateInfo>& cluster_cell_ids,
                    std::optional<std::pair<size_t, size_t>> new_selection,
                    std::pair<float, float> pointer_coords, float zen_percentage,
                    size_t foreground_leaf_id, float gap_horizontal, float gap_vertical);

// ============================================================================
// Utilities
// ============================================================================

// Validate the entire multi-cluster system.
bool validate_system(const System& system);

// Debug: print the entire multi-cluster system to stdout.
void debug_print_system(const System& system);

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

} // namespace cells
} // namespace wintiler
