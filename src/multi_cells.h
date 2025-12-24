#pragma once

#include <optional>
#include <utility>
#include <vector>

namespace wintiler {

// ============================================================================
// Basic Cell Types (formerly in cells.h)
// ============================================================================
namespace cell_logic {

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
  SplitDir splitDir;

  bool isDead = false;

  std::optional<int> parent;      // empty for root
  std::optional<int> firstChild;  // empty if none (leaf)
  std::optional<int> secondChild; // empty if none (leaf)

  Rect rect; // logical rectangle in window coordinates

  std::optional<size_t> leafId; // unique ID for leaf cells only
};

struct CellCluster {
  std::vector<Cell> cells;
  std::optional<int> selectedIndex; // always holds a Leaf index when set

  // Global split direction that alternates on each cell creation.
  SplitDir globalSplitDir;

  // Logical window size used to derive the initial root cell rect
  // when the first cell is created lazily on a split.
  float windowWidth = 0.0f;
  float windowHeight = 0.0f;

  size_t nextLeafId = 1; // Counter for unique leaf IDs
};

enum class Direction {
  Left,
  Right,
  Up,
  Down,
};

// Create initial CellCluster with given width/height (no cells yet).
CellCluster createInitialState(float width, float height);

// Returns true if the cell at cellIndex exists and has no children.
[[nodiscard]] bool isLeaf(const CellCluster& state, int cellIndex);

// Delete the currently selected leaf cell. Returns true if deletion occurred.
bool deleteSelectedLeaf(CellCluster& state, float gapHorizontal, float gapVertical);

// Split the currently selected leaf cell. Returns id of the new leaf.
std::optional<size_t> splitSelectedLeaf(CellCluster& state, float gapHorizontal, float gapVertical);

// Toggle the splitDir of the selected cell's parent.
bool toggleSelectedSplitDir(CellCluster& state, float gapHorizontal, float gapVertical);

// Debug: print the entire CellCluster to stdout.
void debugPrintState(const CellCluster& state);

// Validate internal invariants. Returns true if valid.
bool validateState(const CellCluster& state);

} // namespace cell_logic

// ============================================================================
// Multi-Cluster System
// ============================================================================
namespace multi_cell_logic {

using ClusterId = size_t;

struct PositionedCluster {
  ClusterId id;
  cell_logic::CellCluster cluster;
  float globalX; // Global position of cluster's top-left
  float globalY;
};

struct System {
  std::vector<PositionedCluster> clusters;
  std::optional<ClusterId> selectedClusterId; // Which cluster has selection
  size_t globalNextLeafId = 1;                // Shared across all clusters
  float gapHorizontal = 10.0f;
  float gapVertical = 10.0f;
};

struct ClusterInitInfo {
  ClusterId id;
  float x;
  float y;
  float width;
  float height;
  std::vector<size_t> initialCellIds; // Optional pre-assigned leaf IDs
};

// ============================================================================
// Initialization
// ============================================================================

// Create a multi-cluster system from cluster initialization info.
System createSystem(const std::vector<ClusterInitInfo>& infos);

// Add a new cluster to an existing system.
ClusterId addCluster(System& system, const ClusterInitInfo& info);

// Remove a cluster from the system. Returns true if removed.
bool removeCluster(System& system, ClusterId id);

// Get a pointer to a cluster by ID. Returns nullptr if not found.
PositionedCluster* getCluster(System& system, ClusterId id);
const PositionedCluster* getCluster(const System& system, ClusterId id);

// ============================================================================
// Coordinate Conversion
// ============================================================================

// Convert a local rect to global coordinates.
cell_logic::Rect localToGlobal(const PositionedCluster& pc, const cell_logic::Rect& localRect);

// Convert a global rect to local coordinates.
cell_logic::Rect globalToLocal(const PositionedCluster& pc, const cell_logic::Rect& globalRect);

// Get the global rect of a cell in a positioned cluster.
cell_logic::Rect getCellGlobalRect(const PositionedCluster& pc, int cellIndex);

// ============================================================================
// Cross-Cluster Navigation
// ============================================================================

// Find the next leaf in the given direction, searching across all clusters.
[[nodiscard]] std::optional<std::pair<ClusterId, int>>
findNextLeafInDirection(const System& system, ClusterId currentClusterId, int currentCellIndex,
                        cell_logic::Direction dir);

// Move selection across the multi-cluster system.
bool moveSelection(System& system, cell_logic::Direction dir);

// ============================================================================
// Operations
// ============================================================================

// Split the selected leaf in the currently selected cluster.
std::optional<size_t> splitSelectedLeaf(System& system);

// Delete the selected leaf in the currently selected cluster.
bool deleteSelectedLeaf(System& system);

// Get the currently selected cell across the entire system.
[[nodiscard]] std::optional<std::pair<ClusterId, int>> getSelectedCell(const System& system);

// Get the global rect of the currently selected cell.
[[nodiscard]] std::optional<cell_logic::Rect> getSelectedCellGlobalRect(const System& system);

// Toggle the split direction of the selected cell's parent.
bool toggleSelectedSplitDir(System& system);

// ============================================================================
// Utilities
// ============================================================================

// Validate the entire multi-cluster system.
bool validateSystem(const System& system);

// Debug: print the entire multi-cluster system to stdout.
void debugPrintSystem(const System& system);

// Count total leaves across all clusters.
[[nodiscard]] size_t countTotalLeaves(const System& system);

} // namespace multi_cell_logic
} // namespace wintiler
