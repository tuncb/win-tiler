#pragma once

#include <vector>

#include "multi_cells.h"
#include "options.h"

namespace wintiler {

void run_raylib_ui_multi_cluster(const std::vector<cells::ClusterInitInfo>& infos,
                                 GlobalOptionsProvider& options_provider);

} // namespace wintiler
