#include "process.h"

#include "cells.h"

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

void updateProcesses(AppState& appState, const std::vector<size_t>& currentProcessIds) {
  // 1. Identify processes to remove
  std::vector<size_t> processesToRemove;
  for (const auto& pair : appState.processToLeafIdMap) {
    bool found = false;
    for (size_t pid : currentProcessIds) {
      if (pair.first == pid) {
        found = true;
        break;
      }
    }
    if (!found) {
      processesToRemove.push_back(pair.first);
    }
  }

  // Remove processes
  for (size_t processId : processesToRemove) {
    auto it = appState.processToLeafIdMap.find(processId);
    if (it == appState.processToLeafIdMap.end()) {
      continue;
    }
    size_t leafId = it->second;

    // Find cell with this leafId
    int cellIndex = -1;
    for (size_t i = 0; i < appState.CellCluster.cells.size(); ++i) {
      const auto& cell = appState.CellCluster.cells[i];
      if (!cell.isDead && cell.leafId.has_value() && cell.leafId.value() == leafId) {
        cellIndex = static_cast<int>(i);
        break;
      }
    }

    if (cellIndex != -1) {
      appState.CellCluster.selectedIndex = cellIndex;
      if (cell_logic::deleteSelectedLeaf(appState.CellCluster)) {
        appState.processToLeafIdMap.erase(processId);
        appState.leafIdToProcessMap.erase(leafId);
      }
    } else {
      // Cell not found (maybe already deleted or inconsistent state), just clean up maps
      appState.processToLeafIdMap.erase(processId);
      appState.leafIdToProcessMap.erase(leafId);
    }
  }

  // 2. Identify processes to add
  for (size_t processId : currentProcessIds) {
    if (appState.processToLeafIdMap.find(processId) == appState.processToLeafIdMap.end()) {
      // Add new process
      auto newLeafIdOpt = cell_logic::splitSelectedLeaf(appState.CellCluster);
      if (newLeafIdOpt.has_value()) {
        size_t leafId = *newLeafIdOpt;
        appState.processToLeafIdMap[processId] = leafId;
        appState.leafIdToProcessMap[leafId] = processId;
      }
    }
  }
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

void resetAppState(AppState& appState, cell_logic::Rect windowRect) {
  appState.CellCluster = cell_logic::createInitialState(windowRect);
  // nextProcessId is handled by the caller
  appState.processToLeafIdMap.clear();
  appState.leafIdToProcessMap.clear();
}

} // namespace process_logic
} // namespace wintiler
