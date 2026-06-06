#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <vector>

#include "engine.h"
#include "options.h"
#include "winapi.h"

namespace wintiler {

struct MonitorLayoutRuleUpdate {
  size_t monitor_index = 0;
  winapi::MonitorInfo monitor;
  LayoutRule rule;
};

struct SaveLayoutConfigUpdateResult {
  size_t saved_monitor_count = 0;
};

[[nodiscard]] tl::expected<LayoutRule, std::string>
build_layout_rule_from_cluster(const ctrl::Cluster& cluster);

[[nodiscard]] tl::expected<SaveLayoutConfigUpdateResult, std::string>
save_monitor_layout_rules_to_config(const std::filesystem::path& config_path,
                                    const std::vector<winapi::MonitorInfo>& current_monitors,
                                    const std::vector<MonitorLayoutRuleUpdate>& updates);

} // namespace wintiler
