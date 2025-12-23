#include <doctest/doctest.h>

#include "process.h"

using namespace wintiler::process_logic;
using namespace wintiler::cell_logic;

TEST_CASE("Process Logic") {
  AppState appState;
  size_t nextProcessId = 10;
  resetAppState(appState, 1920.0f, 1080.0f);

  SUBCASE("Initial State") {
    CHECK(appState.processToLeafIdMap.empty());
    CHECK(appState.leafIdToProcessMap.empty());
    CHECK(appState.CellCluster.cells.empty());
  }

  SUBCASE("Add New Process") {
    // First add creates the root
    addNewProcess(appState, nextProcessId);
    CHECK(appState.processToLeafIdMap.size() == 1);
    CHECK(appState.leafIdToProcessMap.size() == 1);
    CHECK(appState.processToLeafIdMap.contains(10));
    CHECK(nextProcessId == 11);

    // Second add splits the root
    addNewProcess(appState, nextProcessId);
    CHECK(appState.processToLeafIdMap.size() == 2);
    CHECK(appState.leafIdToProcessMap.size() == 2);
    CHECK(appState.processToLeafIdMap.contains(11));
    CHECK(nextProcessId == 12);
  }

  SUBCASE("Update Processes - Add") {
    std::vector<size_t> ids = {10, 11, 12};
    updateProcesses(appState, ids);

    CHECK(appState.processToLeafIdMap.size() == 3);
    CHECK(appState.processToLeafIdMap.contains(10));
    CHECK(appState.processToLeafIdMap.contains(11));
    CHECK(appState.processToLeafIdMap.contains(12));
  }

  SUBCASE("Update Processes - Remove") {
    std::vector<size_t> ids = {10, 11};
    updateProcesses(appState, ids);
    CHECK(appState.processToLeafIdMap.size() == 2);

    ids = {10};
    updateProcesses(appState, ids);
    CHECK(appState.processToLeafIdMap.size() == 1);
    CHECK(appState.processToLeafIdMap.contains(10));
    CHECK(!appState.processToLeafIdMap.contains(11));
  }

  SUBCASE("Update Processes - Mixed") {
    std::vector<size_t> ids = {10, 11};
    updateProcesses(appState, ids);
    CHECK(appState.processToLeafIdMap.size() == 2);

    // Remove 11, Add 12
    ids = {10, 12};
    updateProcesses(appState, ids);
    CHECK(appState.processToLeafIdMap.size() == 2);
    CHECK(appState.processToLeafIdMap.contains(10));
    CHECK(!appState.processToLeafIdMap.contains(11));
    CHECK(appState.processToLeafIdMap.contains(12));
  }

  SUBCASE("Delete Selected Process") {
    addNewProcess(appState, nextProcessId); // 10
    addNewProcess(appState, nextProcessId); // 11

    // Select process 11 (it should be selected by default after split)
    // But let's make sure we select the leaf corresponding to process 11
    size_t leafId11 = appState.processToLeafIdMap[11];

    // Find cell index for leafId11
    for (int i = 0; i < appState.CellCluster.cells.size(); ++i) {
      if (appState.CellCluster.cells[i].leafId == leafId11) {
        appState.CellCluster.selectedIndex = i;
        break;
      }
    }

    deleteSelectedCellsProcess(appState);

    CHECK(appState.processToLeafIdMap.size() == 1);
    CHECK(!appState.processToLeafIdMap.contains(11));
    CHECK(appState.processToLeafIdMap.contains(10));
  }
}
