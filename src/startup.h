#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <tl/expected.hpp>

namespace wintiler {

struct StartupRegistrationStatus {
  bool enabled = false;
  std::optional<std::string> command_line;
};

std::string build_startup_command_line(const std::filesystem::path& executable_path,
                                       std::optional<std::filesystem::path> config_path,
                                       std::optional<std::filesystem::path> log_file_path);

tl::expected<void, std::string>
enable_startup_registration(const std::filesystem::path& executable_path,
                            std::optional<std::filesystem::path> config_path,
                            std::optional<std::filesystem::path> log_file_path);

tl::expected<bool, std::string> disable_startup_registration();

tl::expected<StartupRegistrationStatus, std::string> get_startup_registration_status();

} // namespace wintiler
