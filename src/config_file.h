#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <toml++/toml.hpp>
#include <vector>

namespace wintiler {

[[nodiscard]] tl::expected<std::string, std::string>
read_config_text(const std::filesystem::path& path);
[[nodiscard]] tl::expected<void, std::string>
write_config_text_atomically(const std::filesystem::path& path, const std::string& text,
                             std::optional<std::string_view> expected = std::nullopt,
                             bool create_new = false);
[[nodiscard]] size_t config_source_offset(std::string_view text, toml::source_position position);

struct ConfigValueEdit {
  std::vector<std::string_view> key;
  std::string value; // A TOML scalar literal, supplied by the application.
};

// Replace only the requested values, retaining comments and unrelated configuration.
[[nodiscard]] tl::expected<std::string, std::string>
rewrite_config_values(std::string text, const std::vector<ConfigValueEdit>& edits);

} // namespace wintiler
