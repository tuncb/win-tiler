#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "cells.h"

namespace wintiler {
namespace process_logic {

struct AppState {
  cell_logic::CellCluster CellCluster;
  std::unordered_map<size_t, size_t> processToLeafIdMap;
  std::unordered_map<size_t, size_t> leafIdToProcessMap;
};

void addNewProcess(AppState& appState, size_t& nextProcessId);
void updateProcesses(AppState& appState, const std::vector<size_t>& currentProcessIds);
void deleteSelectedCellsProcess(AppState& appState);
void resetAppState(AppState& appState, cell_logic::Rect windowRect);

} // namespace process_logic
} // namespace wintiler
