#pragma once

#include <vector>

#include "engine.h"
#include "options.h"
#include "winapi.h"

namespace wintiler {

[[nodiscard]] std::vector<ctrl::ClusterCellUpdateInfo>
extract_cluster_updates_from_input(const winapi::LoopInputState& input_state);

[[nodiscard]] std::vector<std::vector<ManagedWindowState>>
extract_managed_window_states_from_input(const winapi::LoopInputState& input_state);

[[nodiscard]] std::vector<ctrl::ClusterInitInfo>
create_cluster_infos_from_monitors(const std::vector<winapi::MonitorInfo>& monitors,
                                   const GlobalOptions& options);

void initialize_engine_from_monitors(Engine& engine,
                                     const std::vector<winapi::MonitorInfo>& monitors,
                                     const GlobalOptions& options);

void apply_tile_positions(const ctrl::System& system,
                          const std::vector<std::vector<ctrl::Rect>>& geometries);

} // namespace wintiler
