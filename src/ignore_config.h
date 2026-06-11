#pragma once

#include <filesystem>
#include <string>
#include <tl/expected.hpp>

namespace wintiler {

struct IgnoreProcessTitlePairUpdateResult {
  bool changed = false;
  size_t process_title_pair_count = 0;
};

[[nodiscard]] tl::expected<IgnoreProcessTitlePairUpdateResult, std::string>
add_ignore_process_title_pair_to_config(const std::filesystem::path& config_path,
                                        const std::string& process,
                                        const std::string& title);

[[nodiscard]] tl::expected<IgnoreProcessTitlePairUpdateResult, std::string>
remove_ignore_process_title_pair_from_config(const std::filesystem::path& config_path,
                                             const std::string& process,
                                             const std::string& title);

} // namespace wintiler
