#pragma once

#include "options.h"

namespace wintiler {

struct LoopRunOptions {
  bool perf_stats = false;
};

void run_loop_mode(GlobalOptionsProvider& provider, const LoopRunOptions& run_options = {});

} // namespace wintiler
