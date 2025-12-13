#pragma once

#include <cells.h>

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace wintiler {
namespace process_logic {

struct AppState {
  cell_logic::CellCluster CellCluster;
  std::unordered_map<size_t, size_t> processToLeafIdMap;
  std::unordered_map<size_t, size_t> leafIdToProcessMap;
};

void addNewProcess(AppState& appState, size_t& nextProcessId);
void deleteSelectedCellsProcess(AppState& appState);
void resetAppState(AppState& appState, float width, float height);

} // namespace process_logic
} // namespace wintiler
