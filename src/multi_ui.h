#pragma once

#include <vector>

#include "controller.h"
#include "options.h"

namespace wintiler {

void run_raylib_ui_multi_cluster(const std::vector<ctrl::ClusterInitInfo>& infos,
                                 GlobalOptionsProvider& options_provider);

} // namespace wintiler
