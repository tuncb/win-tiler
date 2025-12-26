# Test Report: `src\test_cells.cpp`

## Overview
This file contains unit tests for the cell/cluster management system using the doctest framework. Tests are organized into 5 test suites covering basic cell operations, multi-cluster systems, navigation, system updates, and cell swap/move operations.

---

## TEST_SUITE: "cells - basic"

### `createInitialState creates empty cluster with correct dimensions`
**Lines 24-32** | Tests that `cells::createInitialState()` correctly initializes an empty cluster state with:
- Empty cells vector
- Correct window dimensions (800x600)
- Default vertical split direction
- Initial leaf ID counter of 1

### `isLeaf returns false for empty state`
**Lines 34-39** | Verifies that `cells::isLeaf()` returns false for:
- Index 0 (no cells exist)
- Invalid indices (-1, 100)

### `splitLeaf creates root cell when cluster is empty`
**Lines 41-61** | Tests creating the first cell in an empty cluster:
- Using -1 as selectedIndex triggers root creation
- Returns valid result with newLeafId=1 and newSelectionIndex=0
- Creates exactly one cell that is a leaf
- Root cell fills entire window (0,0,800,600)
- Root cell gets leafId=1

### `splitLeaf splits existing leaf`
**Lines 63-96** | Tests splitting an existing leaf cell:
- Creates 3 cells total (1 parent + 2 children)
- Parent loses leaf status (no leafId)
- Both children are leaves
- First child inherits parent's leafId (1)
- Second child gets new leafId (2)
- Selection points to first child

### `splitLeaf alternates split direction`
**Lines 98-111** | Verifies global split direction toggles:
- Initial: Vertical
- After creating root: Still Vertical (no actual split)
- After first split: Horizontal
- After second split: Vertical again

### `splitLeaf creates correct rects for vertical split`
**Lines 113-132** | Tests geometry of vertical splits:
- Children are positioned side by side
- Each child gets `(width - gap) / 2` width
- Second child offset by `expectedWidth + gap`
- Both children maintain full parent height

### `splitLeaf creates correct rects for horizontal split`
**Lines 134-157** | Tests geometry of horizontal splits:
- Children are stacked vertically
- Each child gets `(height - gap) / 2` height
- Second child offset by `expectedHeight + gap`
- Both children maintain parent width

### `deleteLeaf removes root cell`
**Lines 159-168** | Tests deleting the only cell:
- Returns nullopt (cluster becomes empty)
- Cells vector is emptied

### `deleteLeaf promotes sibling`
**Lines 170-182** | Tests sibling promotion after deletion:
- Deleting one of two siblings promotes the other
- Returns valid selection index
- Promoted cell is still a leaf

### `deleteLeaf returns nullopt for invalid index`
**Lines 184-189** | Verifies deletion fails gracefully for invalid index (-1)

### `toggleSplitDir toggles parent's split direction`
**Lines 191-208** | Tests toggling parent's split direction:
- Initial parent split: Vertical
- After toggle: Horizontal
- After second toggle: Vertical again
- Returns true on success

### `toggleSplitDir returns false for root leaf`
**Lines 210-216** | Verifies toggle fails for root leaf (no parent to toggle)

### `validateState returns true for valid empty state`
**Lines 218-221** | Tests state validation on empty cluster

### `validateState returns true for valid state with cells`
**Lines 223-230** | Tests state validation on cluster with multiple cells

---

## TEST_SUITE: "cells - multi-cluster"

### `createSystem creates empty system`
**Lines 240-246** | Tests empty system initialization:
- No clusters
- No selection
- globalNextLeafId starts at 1

### `createSystem creates system with single cluster`
**Lines 248-264** | Tests single cluster creation with correct:
- Cluster ID, position (globalX, globalY)
- Window dimensions

### `createSystem creates system with multiple clusters`
**Lines 266-292** | Tests multi-cluster system:
- Both clusters stored correctly
- Second cluster positioned at x=800

### `createSystem with initialCellIds pre-creates leaves`
**Lines 294-313** | Tests pre-populating clusters with cells:
- Creates 2 leaves from `{10, 20}` initialCellIds
- Selection auto-set to first cluster
- `countTotalLeaves` returns 2

### `addCluster adds cluster to existing system`
**Lines 315-332** | Tests dynamic cluster addition:
- Returns assigned cluster ID
- Cluster stored with correct position/dimensions

### `removeCluster removes existing cluster`
**Lines 334-346** | Tests cluster removal:
- Returns true on success
- Only remaining cluster stays

### `removeCluster returns false for non-existent cluster`
**Lines 348-353** | Verifies removal fails gracefully for unknown cluster ID

### `getCluster returns correct cluster`
**Lines 355-368** | Tests cluster lookup:
- Returns pointer to correct cluster
- Returns nullptr for unknown ID

### `localToGlobal converts coordinates correctly`
**Lines 370-382** | Tests coordinate conversion:
- Adds cluster's globalX/Y offset to local rect
- Preserves width/height

### `globalToLocal converts coordinates correctly`
**Lines 384-396** | Tests inverse coordinate conversion

### `getCellGlobalRect returns correct global rect`
**Lines 398-412** | Tests getting cell rect in global coordinates:
- Accounts for cluster offset (100, 50)

### `splitSelectedLeaf creates new leaf with global ID`
**Lines 414-422** | Tests splitting via system-level API:
- Returns new leaf ID
- Increases total leaf count

### `splitSelectedLeaf returns nullopt with no selection`
**Lines 424-429** | Verifies split fails with no selection

### `deleteSelectedLeaf removes leaf`
**Lines 431-441** | Tests deletion via system-level API:
- Reduces leaf count from 2 to 1

### `getSelectedCell returns current selection`
**Lines 443-452** | Tests selection query:
- Returns (clusterId, cellIndex) pair

### `getSelectedCell returns nullopt with no selection`
**Lines 454-459** | Verifies null return when no selection

### `getSelectedCellGlobalRect returns correct rect`
**Lines 461-472** | Tests getting selected cell's global rect

### `countTotalLeaves counts correctly across clusters`
**Lines 474-482** | Tests leaf counting across multiple clusters:
- Cluster 1: 2 leaves, Cluster 2: 3 leaves → 5 total

### `validateSystem returns true for valid system`
**Lines 484-489** | Tests system validation with cells

### `validateSystem returns true for empty system`
**Lines 491-494** | Tests system validation when empty

---

## TEST_SUITE: "cells - navigation"

### `moveSelection moves within single cluster horizontally`
**Lines 504-521** | Tests horizontal navigation within cluster:
- Move Right navigates to sibling
- Selection stays in same cluster but different cell

### `moveSelection returns false when no selection`
**Lines 523-528** | Verifies navigation fails with no selection

### `moveSelection returns false when no cell in direction`
**Lines 530-537** | Verifies navigation fails when edge reached (single cell)

### `moveSelection moves across clusters`
**Lines 539-559** | Tests cross-cluster navigation:
- Two side-by-side clusters
- Move Right → cluster 2
- Move Left → back to cluster 1

### `findNextLeafInDirection finds correct cell left`
**Lines 561-592** | Tests directional search for left neighbor:
- Identifies leftmost/rightmost leaves
- From right leaf, finds left leaf correctly

### `findNextLeafInDirection finds correct cell right`
**Lines 594-624** | Tests directional search for right neighbor

### `findNextLeafInDirection crosses clusters`
**Lines 626-648** | Tests cross-cluster directional search:
- From cluster 1's edge, finds cluster 2's leaf

### `findNextLeafInDirection returns nullopt when no cell in direction`
**Lines 650-657** | Verifies search returns null at edges

### `toggleSelectedSplitDir works`
**Lines 659-674** | Tests toggling via system-level selection

### `removeCluster updates selection to remaining cluster`
**Lines 676-691** | Tests selection recovery after cluster removal:
- Selection moves to remaining cluster when selected cluster removed

---

## TEST_SUITE: "cells - updateSystem"

### `getClusterLeafIds returns all leaf IDs`
**Lines 701-716** | Tests extracting all leaf IDs from cluster:
- Returns {10, 20, 30} correctly

### `getClusterLeafIds returns empty for empty cluster`
**Lines 718-723** | Tests empty cluster returns empty vector

### `findCellByLeafId finds existing leaf`
**Lines 725-738** | Tests cell lookup by leaf ID:
- Finds both cells with IDs 10 and 20
- Returns different indices

### `findCellByLeafId returns nullopt for non-existent leaf`
**Lines 740-749** | Verifies lookup fails for unknown leaf ID

### `updateSystem adds leaves to empty cluster`
**Lines 751-767** | Tests adding leaves via update:
- Adds 2 leaves ({100, 200})
- No errors, no deletions
- `addedLeafIds.size() == 2`

### `updateSystem adds leaves to existing cluster`
**Lines 769-785** | Tests adding to existing:
- Keep 10, add 20 and 30
- 2 additions, 0 deletions

### `updateSystem deletes leaves`
**Lines 787-803** | Tests leaf deletion via update:
- Keep only {10}, delete {20, 30}
- 2 deletions, 0 additions

### `updateSystem handles mixed add and delete`
**Lines 805-821** | Tests simultaneous add/delete:
- Update {10, 20} → {10, 30}
- Deletes 20, adds 30

### `updateSystem updates selection`
**Lines 823-844** | Tests selection change via update:
- Sets selection to leaf with ID 20
- Verifies `selectionUpdated` flag

### `updateSystem reports error for unknown cluster`
**Lines 846-859** | Tests error reporting:
- Update for cluster 999 → `ClusterNotFound` error

### `updateSystem reports error for invalid selection cluster`
**Lines 861-874** | Tests error for invalid selection cluster:
- Selection on cluster 999 → `SelectionInvalid` error

### `updateSystem reports error for invalid selection leaf`
**Lines 876-890** | Tests error for invalid selection leaf:
- Selection on leaf 999 → `SelectionInvalid` error with leafId

### `updateSystem handles multiple clusters`
**Lines 892-907** | Tests updating multiple clusters simultaneously:
- Adds 1 leaf to each of 2 clusters
- 2 total additions

### `updateSystem leaves unchanged cluster alone`
**Lines 909-934** | Tests partial updates:
- Only update cluster 2
- Cluster 1 retains original leaves

### `updateSystem can clear cluster to empty`
**Lines 936-949** | Tests clearing all leaves:
- Update to empty `{}` deletes all

---

## TEST_SUITE: "cells - swap and move"

### `swapCells swaps two cells in same cluster`
**Lines 959-997** | Tests in-cluster swap:
- Cells exchange rects
- LeafIds stay with their cells
- System remains valid

### `swapCells is no-op for same cell`
**Lines 999-1008** | Tests self-swap (no change)

### `swapCells swaps cells across clusters`
**Lines 1010-1035** | Tests cross-cluster swap:
- LeafIds are exchanged between clusters
- Cluster 1 cell now has leafId 20, cluster 2 has leafId 10

### `swapCells returns error for non-existent cluster`
**Lines 1037-1045** | Tests error for unknown cluster

### `swapCells returns error for non-existent leaf`
**Lines 1047-1055** | Tests error for unknown leaf ID

### `swapCells updates selection correctly in same cluster`
**Lines 1057-1077** | Tests selection tracking through swap:
- Selection follows the cell (leafId 10)

### `moveCell moves cell within same cluster`
**Lines 1079-1104** | Tests in-cluster move:
- Target cell (20) is split
- Source cell (10) moves into new position
- Total leaves stays at 2

### `moveCell is no-op for same cell`
**Lines 1106-1115** | Tests self-move (no change)

### `moveCell moves cell across clusters`
**Lines 1117-1148** | Tests cross-cluster move:
- Cell 10 moves from cluster 1 to cluster 2
- Cluster 1: 2 → 1 leaf
- Cluster 2: 1 → 2 leaves

### `moveCell preserves source leafId`
**Lines 1150-1164** | Tests leafId preservation:
- Moved cell keeps its original leafId (10)

### `moveCell returns error for non-existent source cluster`
**Lines 1166-1174** | Tests error handling

### `moveCell returns error for non-existent target cluster`
**Lines 1176-1184** | Tests error handling

### `moveCell returns error for non-existent source leaf`
**Lines 1186-1194** | Tests error handling

### `moveCell returns error for non-existent target leaf`
**Lines 1196-1204** | Tests error handling

### `moveCell updates selection when source was selected`
**Lines 1206-1230** | Tests selection follows moved cell:
- Selection updates to new cell index
- Selected cell still has leafId 10

### `moveCell handles source cluster becoming empty`
**Lines 1232-1249** | Tests cluster emptying after move:
- Moving only cell leaves source cluster empty
- Target cluster gains the leaf

---

## Summary Statistics

| Test Suite | Test Count |
|------------|------------|
| cells - basic | 14 |
| cells - multi-cluster | 20 |
| cells - navigation | 10 |
| cells - updateSystem | 14 |
| cells - swap and move | 14 |
| **Total** | **72** |
