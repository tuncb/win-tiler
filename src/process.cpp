#include <cells.h>
#include <process.h>

namespace wintiler {
namespace process_logic {

void addNewProcess(AppState& appState, size_t& nextProcessId) {
  auto newLeafIdOpt = cell_logic::splitSelectedLeaf(appState.CellCluster);
  if (!newLeafIdOpt.has_value()) {
    return;
  }

  size_t processId = nextProcessId++;
  size_t leafId = *newLeafIdOpt;
  appState.processToLeafIdMap[processId] = leafId;
  appState.leafIdToProcessMap[leafId] = processId;
}

void deleteSelectedCellsProcess(AppState& appState) {
  auto selectedCell = appState.CellCluster.selectedIndex;
  if (!selectedCell.has_value()) {
    return;
  }

  // Check if the selected cell has a leafId and if it is in the map
  const auto& cell = appState.CellCluster.cells[static_cast<std::size_t>(*selectedCell)];
  if (!cell.leafId.has_value()) {
    return;
  }

  auto processIt = appState.leafIdToProcessMap.find(cell.leafId.value());
  if (processIt == appState.leafIdToProcessMap.end()) {
    return;
  }

  size_t selectedProcessId = processIt->second;

  if (!cell_logic::deleteSelectedLeaf(appState.CellCluster)) {
    return;
  }

  auto it = appState.processToLeafIdMap.find(selectedProcessId);
  if (it != appState.processToLeafIdMap.end()) {
    size_t leafId = it->second;
    appState.processToLeafIdMap.erase(it);
    appState.leafIdToProcessMap.erase(leafId);
  }
}

void resetAppState(AppState& appState, float width, float height) {
  appState.CellCluster = cell_logic::createInitialState(width, height);
  // nextProcessId is handled by the caller
  appState.processToLeafIdMap.clear();
  appState.leafIdToProcessMap.clear();
}

} // namespace process_logic
} // namespace wintiler
