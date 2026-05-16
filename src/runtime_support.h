#pragma once

#include <vector>

#include "engine.h"
#include "options.h"
#include "winapi.h"

namespace wintiler {

void extract_cluster_updates_from_input_into(const winapi::LoopInputState& input_state,
                                             std::vector<ctrl::ClusterCellUpdateInfo>& result);

void extract_managed_window_states_from_input_into(
    const winapi::LoopInputState& input_state,
    std::vector<std::vector<ManagedWindowState>>& result);

[[nodiscard]] ClusterTilingOptions
resolve_monitor_tiling_options(const GlobalOptions& options, const winapi::MonitorInfo& monitor,
                               size_t monitor_index);

void resolve_cluster_tiling_options_into(const std::vector<winapi::MonitorInfo>& monitors,
                                         const GlobalOptions& options,
                                         std::vector<ClusterTilingOptions>& cluster_options);

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
