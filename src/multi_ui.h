#pragma once

#include <optional>
#include <vector>

#include "multi_cells.h"
#include "options.h"

namespace wintiler {

void runRaylibUIMultiCluster(const std::vector<cells::ClusterInitInfo>& infos,
                             std::optional<GapOptions> gapOptions = std::nullopt);

} // namespace wintiler
