#pragma once

#include <vector>

#include "multi_cells.h"
#include "options.h"

namespace wintiler {

void runRaylibUIMultiCluster(const std::vector<cells::ClusterInitInfo>& infos,
                             GlobalOptionsProvider& optionsProvider);

} // namespace wintiler
