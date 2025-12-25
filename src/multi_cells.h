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

// Result of splitting a leaf cell.
struct SplitResult {
  size_t newLeafId;
  int newSelectionIndex;
};

// Create initial CellCluster with given width/height (no cells yet).
CellCluster createInitialState(float width, float height);

// Returns true if the cell at cellIndex exists and has no children.
[[nodiscard]] bool isLeaf(const CellCluster& state, int cellIndex);

// Delete the given leaf cell. Returns new selection index (or nullopt if cluster empty).
std::optional<int> deleteLeaf(CellCluster& state, int selectedIndex, float gapHorizontal, float gapVertical);

// Split the given leaf cell. Returns SplitResult with new leaf ID and new selection index.
std::optional<SplitResult> splitLeaf(CellCluster& state, int selectedIndex, float gapHorizontal, float gapVertical);

// Toggle the splitDir of the given cell's parent.
bool toggleSplitDir(CellCluster& state, int selectedIndex, float gapHorizontal, float gapVertical);

// Debug: print the entire CellCluster to stdout.
void debugPrintState(const CellCluster& state);

// Validate internal invariants. Returns true if valid.
bool validateState(const CellCluster& state);

// ============================================================================
// Multi-Cluster System
// ============================================================================

using ClusterId = size_t;

struct PositionedCluster {
  ClusterId id;
  CellCluster cluster;
  float globalX; // Global position of cluster's top-left
  float globalY;
};

// System-wide selection tracking.
struct Selection {
  ClusterId clusterId;
  int cellIndex;  // always a leaf index
};

struct System {
  std::vector<PositionedCluster> clusters;
  std::optional<Selection> selection;  // System-wide selection
  size_t globalNextLeafId = 1;         // Shared across all clusters
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
// Update System Types
// ============================================================================

struct ClusterCellIds {
  ClusterId clusterId;
  std::vector<size_t> leafIds;  // Desired leaf IDs for this cluster
};

struct UpdateError {
  enum class Type {
    ClusterNotFound,
    LeafNotFound,
    SelectionInvalid,
  };
  Type type;
  ClusterId clusterId;
  size_t leafId;  // relevant leaf ID (if applicable)
};

struct UpdateResult {
  std::vector<size_t> deletedLeafIds;
  std::vector<size_t> addedLeafIds;
  std::vector<UpdateError> errors;
  bool selectionUpdated;
};

// ============================================================================
// Swap and Move Operation Types
// ============================================================================

struct SwapResult {
  bool success;
  std::string errorMessage;  // Empty if success
};

struct MoveResult {
  bool success;
  int newCellIndex;          // Index of source cell in its new position
  ClusterId newClusterId;    // Cluster where source ended up
  std::string errorMessage;  // Empty if success
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
Rect localToGlobal(const PositionedCluster& pc, const Rect& localRect);

// Convert a global rect to local coordinates.
Rect globalToLocal(const PositionedCluster& pc, const Rect& globalRect);

// Get the global rect of a cell in a positioned cluster.
Rect getCellGlobalRect(const PositionedCluster& pc, int cellIndex);

// ============================================================================
// Cross-Cluster Navigation
// ============================================================================

// Find the next leaf in the given direction, searching across all clusters.
[[nodiscard]] std::optional<std::pair<ClusterId, int>>
findNextLeafInDirection(const System& system, ClusterId currentClusterId, int currentCellIndex,
                        Direction dir);

// Move selection across the multi-cluster system.
bool moveSelection(System& system, Direction dir);

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
[[nodiscard]] std::optional<Rect> getSelectedCellGlobalRect(const System& system);

// Toggle the split direction of the selected cell's parent.
bool toggleSelectedSplitDir(System& system);

// Toggle the globalSplitDir of the cluster containing the selected cell.
bool toggleClusterGlobalSplitDir(System& system);

// Swap two leaf cells' positions (potentially in different clusters).
// Each cell keeps its identity (leafId) but they exchange visual positions.
// For same-cluster: actual tree position swap.
// For cross-cluster: leafIds are exchanged between the two positions.
// If both arguments refer to the same cell, this is a no-op and returns success.
SwapResult swapCells(System& system,
                     ClusterId clusterId1, size_t leafId1,
                     ClusterId clusterId2, size_t leafId2);

// Move source cell to target cell's location.
// - Deletes source from its current position
// - Splits target to create a new slot
// - Source's content (leafId) appears in the new split slot
// If source and target are the same cell, this is a no-op and returns success.
MoveResult moveCell(System& system,
                    ClusterId sourceClusterId, size_t sourceLeafId,
                    ClusterId targetClusterId, size_t targetLeafId);

// ============================================================================
// Utilities
// ============================================================================

// Validate the entire multi-cluster system.
bool validateSystem(const System& system);

// Debug: print the entire multi-cluster system to stdout.
void debugPrintSystem(const System& system);

// Count total leaves across all clusters.
[[nodiscard]] size_t countTotalLeaves(const System& system);

// ============================================================================
// Hit Testing
// ============================================================================

// Find the cluster and cell at a global point.
// Returns nullopt if no cell contains the point.
[[nodiscard]] std::optional<std::pair<ClusterId, int>>
findCellAtPoint(const System& system, float globalX, float globalY);

// ============================================================================
// System Update
// ============================================================================

// Get all leaf IDs from a cluster.
[[nodiscard]] std::vector<size_t> getClusterLeafIds(const CellCluster& cluster);

// Find cell index by leaf ID. Returns nullopt if not found.
[[nodiscard]] std::optional<int> findCellByLeafId(const CellCluster& cluster, size_t leafId);

// Update the system to match the desired state.
// - Deletes leaves that are not in the desired state
// - Adds leaves that are in the desired state but not currently present
// - Updates the selection if provided
UpdateResult updateSystem(
    System& system,
    const std::vector<ClusterCellIds>& clusterCellIds,
    std::optional<std::pair<ClusterId, size_t>> newSelection);

} // namespace cells
} // namespace wintiler
