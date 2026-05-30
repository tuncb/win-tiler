#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <tl/expected.hpp>

namespace wintiler {

enum class InstallerDialogResult {
  Closed,
  Installed,
  UninstallStarted,
};

[[nodiscard]] std::wstring quote_windows_argument_wide(const std::wstring& argument);

[[nodiscard]] std::wstring
build_uninstall_command_line_wide(const std::filesystem::path& executable_path, bool quiet);

[[nodiscard]] std::wstring
build_finish_uninstall_command_line_wide(const std::filesystem::path& helper_path,
                                         unsigned long pid,
                                         const std::filesystem::path& install_dir,
                                         std::optional<unsigned long> running_pid = std::nullopt);

[[nodiscard]] std::wstring format_install_date_for_registry(unsigned short year,
                                                            unsigned short month,
                                                            unsigned short day);

[[nodiscard]] tl::expected<std::filesystem::path, std::string> get_default_install_directory();

[[nodiscard]] std::filesystem::path
get_installed_executable_path(const std::filesystem::path& install_dir);

[[nodiscard]] std::filesystem::path
get_installed_config_path(const std::filesystem::path& install_dir);

[[nodiscard]] bool is_installation_present(const std::filesystem::path& install_dir);

[[nodiscard]] bool startup_command_targets_executable(const std::string& command_line,
                                                      const std::filesystem::path& executable_path);

[[nodiscard]] tl::expected<void, std::string>
ensure_installed_config_file(const std::filesystem::path& install_dir);

[[nodiscard]] tl::expected<void, std::string>
apply_startup_option_for_installation(const std::filesystem::path& install_dir, bool auto_start);

[[nodiscard]] tl::expected<void, std::string>
install_current_executable(const std::filesystem::path& current_executable, bool auto_start);

[[nodiscard]] tl::expected<void, std::string>
start_uninstall_helper(const std::filesystem::path& current_executable,
                       const std::filesystem::path& install_dir);

[[nodiscard]] tl::expected<void, std::string>
finish_uninstall(unsigned long original_pid, const std::filesystem::path& install_dir,
                 const std::filesystem::path& helper_executable,
                 std::optional<unsigned long> running_pid);

[[nodiscard]] tl::expected<InstallerDialogResult, std::string>
show_installer_dialog(void* owner_window, const std::filesystem::path& current_executable);

} // namespace wintiler
