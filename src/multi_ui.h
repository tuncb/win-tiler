#pragma once

#include "multi_cells.h"
#include "options.h"

#include <optional>
#include <vector>

namespace wintiler {

void runRaylibUIMultiCluster(const std::vector<multi_cell_logic::ClusterInitInfo>& infos,
                              std::optional<GapOptions> gapOptions = std::nullopt);

} // namespace wintiler
