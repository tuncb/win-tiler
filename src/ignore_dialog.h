#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include "ignore_config.h"
#include "winapi.h"

namespace wintiler {
struct IgnoreRuleEntry {
  IgnoreRule rule;
  bool built_in = false;
};
[[nodiscard]] std::vector<IgnoreRuleEntry>
make_ignore_rule_entries(const std::vector<IgnoreRule>& user_rules, const IgnoreOptions& options);
[[nodiscard]] bool rule_is_related_to_window(const IgnoreRule& rule,
                                             const winapi::WindowManagementSnapshot& window);

void show_ignore_dialog(HWND owner, std::optional<std::filesystem::path> config_path,
                        IgnoreOptions options, std::function<void(const IgnoreOptions&)> changed);
[[nodiscard]] bool handle_ignore_dialog_message(MSG& message);
void close_ignore_dialog();
} // namespace wintiler
