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

[[nodiscard]] ClusterTilingOptions
resolve_monitor_tiling_options(const GlobalOptions& options, const winapi::MonitorInfo& monitor,
                               size_t monitor_index);

[[nodiscard]] std::vector<ClusterTilingOptions>
resolve_cluster_tiling_options(const std::vector<winapi::MonitorInfo>& monitors,
                               const GlobalOptions& options);

[[nodiscard]] std::vector<ctrl::ClusterInitInfo>
create_cluster_infos_from_monitors(const std::vector<winapi::MonitorInfo>& monitors,
                                   const GlobalOptions& options);

void initialize_engine_from_monitors(Engine& engine,
                                     const std::vector<winapi::MonitorInfo>& monitors,
                                     const GlobalOptions& options);

void apply_tile_positions(const ctrl::System& system,
                          const std::vector<std::vector<ctrl::Rect>>& geometries);

void apply_tile_positions_for_leaf_ids(const ctrl::System& system,
                                       const std::vector<std::vector<ctrl::Rect>>& geometries,
                                       const std::vector<size_t>& leaf_ids);

} // namespace wintiler
