#pragma once

#include "cells.h"

#include <optional>
#include <utility>
#include <vector>

namespace wintiler {
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
  float defaultGapHorizontal = 10.0f;
  float defaultGapVertical = 10.0f;
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
// Each cluster will have leaves pre-created for each entry in initialCellIds.
System createSystem(const std::vector<ClusterInitInfo>& infos);

// Add a new cluster to an existing system.
ClusterId addCluster(System& system, const ClusterInitInfo& info);

// Remove a cluster from the system. Returns true if removed.
// If the removed cluster had the selection, moves selection to another cluster.
bool removeCluster(System& system, ClusterId id);

// Get a pointer to a cluster by ID. Returns nullptr if not found.
PositionedCluster* getCluster(System& system, ClusterId id);
const PositionedCluster* getCluster(const System& system, ClusterId id);

// ============================================================================
// Coordinate Conversion
// ============================================================================

// Convert a local rect (within cluster coordinates) to global coordinates.
cell_logic::Rect localToGlobal(const PositionedCluster& pc, const cell_logic::Rect& localRect);

// Convert a global rect to local coordinates for a specific cluster.
cell_logic::Rect globalToLocal(const PositionedCluster& pc, const cell_logic::Rect& globalRect);

// Get the global rect of a cell in a positioned cluster.
cell_logic::Rect getCellGlobalRect(const PositionedCluster& pc, int cellIndex);

// ============================================================================
// Cross-Cluster Navigation
// ============================================================================

// Find the next leaf in the given direction, searching across all clusters.
// Returns pair<ClusterId, cellIndex> or nullopt if no neighbor found.
[[nodiscard]] std::optional<std::pair<ClusterId, int>>
findNextLeafInDirection(const System& system, ClusterId currentClusterId, int currentCellIndex,
                        cell_logic::Direction dir);

// Move selection across the multi-cluster system.
// Returns true if selection changed (possibly to a different cluster).
bool moveSelection(System& system, cell_logic::Direction dir);

// ============================================================================
// Operations
// ============================================================================

// Split the selected leaf in the currently selected cluster.
// Uses the globalNextLeafId from the system for new leaves.
// Returns the leafId of the newly created leaf, or nullopt on failure.
std::optional<size_t> splitSelectedLeaf(System& system);

// Delete the selected leaf in the currently selected cluster.
// If the cluster becomes empty, moves selection to another cluster.
// Returns true if deletion occurred.
bool deleteSelectedLeaf(System& system);

// Get the currently selected cell across the entire system.
// Returns pair<ClusterId, cellIndex> or nullopt if no selection.
[[nodiscard]] std::optional<std::pair<ClusterId, int>> getSelectedCell(const System& system);

// Get the global rect of the currently selected cell.
[[nodiscard]] std::optional<cell_logic::Rect> getSelectedCellGlobalRect(const System& system);

// Toggle the split direction of the selected cell's parent.
// Returns true if toggled.
bool toggleSelectedSplitDir(System& system);

// ============================================================================
// Utilities
// ============================================================================

// Validate the entire multi-cluster system. Returns true if valid.
bool validateSystem(const System& system);

// Debug: print the entire multi-cluster system to stdout.
void debugPrintSystem(const System& system);

// Count total leaves across all clusters.
[[nodiscard]] size_t countTotalLeaves(const System& system);

} // namespace multi_cell_logic
} // namespace wintiler
