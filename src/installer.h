#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace wintiler {

enum class InstallerDialogResult {
  Closed,
  Installed,
  UninstallStarted,
  UpdateStarted,
};

struct VersionNumber {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

struct ReleaseAsset {
  std::string name;
  std::string browser_download_url;
  std::string digest;
};

struct LatestReleaseInfo {
  std::string tag_name;
  std::string html_url;
  VersionNumber version;
  std::vector<ReleaseAsset> assets;
};

[[nodiscard]] std::wstring quote_windows_argument_wide(const std::wstring& argument);

[[nodiscard]] std::wstring
build_uninstall_command_line_wide(const std::filesystem::path& executable_path, bool quiet);

[[nodiscard]] std::wstring
build_finish_uninstall_command_line_wide(const std::filesystem::path& helper_path,
                                         unsigned long pid,
                                         const std::filesystem::path& install_dir,
                                         std::optional<unsigned long> running_pid = std::nullopt);

[[nodiscard]] std::wstring build_finish_update_command_line_wide(
    const std::filesystem::path& helper_path, unsigned long pid,
    const std::filesystem::path& install_dir, const std::filesystem::path& downloaded_executable,
    const std::string& expected_sha256, std::optional<unsigned long> running_pid = std::nullopt,
    bool restart = false);

[[nodiscard]] std::wstring
format_install_date_for_registry(unsigned short year, unsigned short month, unsigned short day);

[[nodiscard]] std::optional<VersionNumber> parse_version_tag(const std::string& tag);

[[nodiscard]] bool is_version_newer(const VersionNumber& candidate, const VersionNumber& current);

[[nodiscard]] std::optional<std::string> extract_sha256_from_text(const std::string& text);

[[nodiscard]] tl::expected<LatestReleaseInfo, std::string>
parse_latest_release_response(const std::string& response);

[[nodiscard]] bool can_update_installed_instance(const std::filesystem::path& current_executable,
                                                 const std::filesystem::path& install_dir);

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

[[nodiscard]] tl::expected<InstallerDialogResult, std::string>
check_for_updates_from_installed_instance(void* owner_window,
                                          const std::filesystem::path& current_executable);

[[nodiscard]] tl::expected<void, std::string>
finish_update(unsigned long original_pid, const std::filesystem::path& install_dir,
              const std::filesystem::path& downloaded_executable,
              const std::string& expected_sha256, const std::filesystem::path& helper_executable,
              std::optional<unsigned long> running_pid, bool restart);

[[nodiscard]] tl::expected<void, std::string>
finish_uninstall(unsigned long original_pid, const std::filesystem::path& install_dir,
                 const std::filesystem::path& helper_executable,
                 std::optional<unsigned long> running_pid);

[[nodiscard]] tl::expected<InstallerDialogResult, std::string>
show_installer_dialog(void* owner_window, const std::filesystem::path& current_executable);

} // namespace wintiler
