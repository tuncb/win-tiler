#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <vector>

namespace wintiler {

enum class IgnoreRuleKind { Process, Title, ProcessTitlePair, ChildProcess };

struct IgnoreRule {
  IgnoreRuleKind kind = IgnoreRuleKind::ProcessTitlePair;
  std::string process;
  std::string title;
};

// Actual changes, not a config snapshot. Swap added/removed to undo without
// overwriting unrelated edits made in an external editor.
struct IgnoreRuleEdit {
  std::vector<IgnoreRule> removed;
  std::vector<IgnoreRule> added;
  [[nodiscard]] bool changed() const {
    return !removed.empty() || !added.empty();
  }
};

[[nodiscard]] bool ignore_rules_equal(const IgnoreRule& left, const IgnoreRule& right);
[[nodiscard]] bool ignore_rule_has_values(const IgnoreRule& rule);
[[nodiscard]] bool ignore_rule_matches(const IgnoreRule& rule, std::string_view process,
                                       std::string_view title, bool child_or_owned);
[[nodiscard]] const char* ignore_rule_kind_name(IgnoreRuleKind kind);
[[nodiscard]] tl::expected<std::vector<IgnoreRule>, std::string>
read_user_ignore_rules(const std::filesystem::path& config_path);
// Removal must still exist; missing or externally changed rules fail without writing.
// Duplicate additions are merged, and both sides of a replacement are written once.
// A first addition may create a missing config; it never overwrites a concurrently created file.
[[nodiscard]] tl::expected<IgnoreRuleEdit, std::string>
edit_ignore_rules(const std::filesystem::path& config_path, const std::vector<IgnoreRule>& remove,
                  const std::vector<IgnoreRule>& add, bool create_if_missing = false);

// Creates an empty valid configuration without overwriting an existing file.
[[nodiscard]] tl::expected<void, std::string>
create_ignore_config(const std::filesystem::path& config_path);

struct IgnoreProcessTitlePairUpdateResult {
  bool changed = false;
  size_t process_title_pair_count = 0;
};

struct IgnoreStringListUpdateResult {
  bool changed = false;
  size_t value_count = 0;
};

[[nodiscard]] tl::expected<IgnoreStringListUpdateResult, std::string>
add_ignore_process_to_config(const std::filesystem::path& config_path, const std::string& process);

[[nodiscard]] tl::expected<IgnoreStringListUpdateResult, std::string>
remove_ignore_process_from_config(const std::filesystem::path& config_path,
                                  const std::string& process);

[[nodiscard]] tl::expected<IgnoreStringListUpdateResult, std::string>
add_ignore_window_title_to_config(const std::filesystem::path& config_path,
                                  const std::string& title);

[[nodiscard]] tl::expected<IgnoreStringListUpdateResult, std::string>
remove_ignore_window_title_from_config(const std::filesystem::path& config_path,
                                       const std::string& title);

[[nodiscard]] tl::expected<IgnoreStringListUpdateResult, std::string>
add_ignore_child_process_to_config(const std::filesystem::path& config_path,
                                   const std::string& process);

[[nodiscard]] tl::expected<IgnoreStringListUpdateResult, std::string>
remove_ignore_child_process_from_config(const std::filesystem::path& config_path,
                                        const std::string& process);

[[nodiscard]] tl::expected<IgnoreProcessTitlePairUpdateResult, std::string>
add_ignore_process_title_pair_to_config(const std::filesystem::path& config_path,
                                        const std::string& process, const std::string& title);

[[nodiscard]] tl::expected<IgnoreProcessTitlePairUpdateResult, std::string>
remove_ignore_process_title_pair_from_config(const std::filesystem::path& config_path,
                                             const std::string& process, const std::string& title);

} // namespace wintiler
