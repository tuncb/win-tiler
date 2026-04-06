#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "controller.h"
#include "options.h"
#include "winapi.h"

namespace wintiler {

struct MaximizeTrackingState {
  std::unordered_map<size_t, bool> maximize_state_by_leaf_id;
};

namespace loop_detail {

// Converts newly observed normal -> maximized transitions into zen mode.
// First observation of a window seeds tracking state and does not trigger zen.
[[nodiscard]] bool apply_zen_to_newly_maximized_windows(
    ctrl::System& system,
    const std::vector<std::vector<winapi::ManagedWindowInfo>>& windows_per_monitor,
    MaximizeTrackingState& tracking_state);

} // namespace loop_detail

void run_loop_mode(GlobalOptionsProvider& provider);

} // namespace wintiler
