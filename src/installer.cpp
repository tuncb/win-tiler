#include "installer.h"

#include <commctrl.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <optional>
#include <sstream>
#include <vector>

#include "options.h"
#include "startup.h"
#include "version.h"
#include "winapi.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")

namespace wintiler {

namespace {

constexpr wchar_t kUninstallKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\win-tiler";
constexpr wchar_t kAppName[] = L"win-tiler";
constexpr wchar_t kPublisher[] = L"tuncb";
constexpr int kInstallButtonId = 1001;
constexpr int kUninstallButtonId = 1002;
constexpr int kApplyButtonId = 1003;

struct InstallerDialogCallbackState {
  bool installed = false;
};

std::string wide_to_utf8(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }

  int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr,
                                 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }

  std::string result(static_cast<size_t>(size), '\0');
  int converted = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                      result.data(), size, nullptr, nullptr);
  if (converted <= 0) {
    return {};
  }

  return result;
}

std::wstring widen_ascii(const std::string& text) {
  return std::wstring(text.begin(), text.end());
}

std::string format_win32_error(DWORD error) {
  LPWSTR buffer = nullptr;
  DWORD size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                  FORMAT_MESSAGE_IGNORE_INSERTS,
                              nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                              reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

  std::wstring message_wide;
  if (size > 0 && buffer != nullptr) {
    message_wide.assign(buffer, size);
  } else {
    message_wide = L"error code " + std::to_wstring(error);
  }

  if (buffer != nullptr) {
    HLOCAL free_result = LocalFree(buffer);
    if (free_result != nullptr) {
      spdlog::error("Failed to release Win32 error message buffer");
    }
  }

  std::string message = wide_to_utf8(message_wide);
  while (!message.empty() && std::isspace(static_cast<unsigned char>(message.back())) != 0) {
    message.pop_back();
  }
  return message;
}

tl::expected<void, std::string> set_registry_string(HKEY key, const wchar_t* name,
                                                    const std::wstring& value) {
  DWORD byte_count = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  LSTATUS status = RegSetValueExW(key, name, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(value.c_str()), byte_count);
  if (status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to write uninstall registry value: " +
                          format_win32_error(static_cast<DWORD>(status)));
  }
  return {};
}

tl::expected<void, std::string> set_registry_dword(HKEY key, const wchar_t* name, DWORD value) {
  LSTATUS status =
      RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
  if (status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to write uninstall registry value: " +
                          format_win32_error(static_cast<DWORD>(status)));
  }
  return {};
}

tl::expected<void, std::string> register_uninstall_entry(const std::filesystem::path& install_dir,
                                                         const std::filesystem::path& executable) {
  HKEY key = nullptr;
  LSTATUS create_status =
      RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                      KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
  if (create_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to open uninstall registry key: " +
                          format_win32_error(static_cast<DWORD>(create_status)));
  }

  std::string version = get_version_string();
  std::wstring version_wide(version.begin(), version.end());

  SYSTEMTIME local_time = {};
  GetLocalTime(&local_time);

  std::error_code ec;
  auto file_size = std::filesystem::file_size(executable, ec);
  DWORD estimated_size_kb = 0;
  if (!ec) {
    uintmax_t kb = std::max<uintmax_t>(1, (file_size + 1023) / 1024);
    estimated_size_kb =
        static_cast<DWORD>(std::min<uintmax_t>(kb, std::numeric_limits<DWORD>::max()));
  }

  std::wstring uninstall_command = build_uninstall_command_line_wide(executable, false);
  std::wstring quiet_uninstall_command = build_uninstall_command_line_wide(executable, true);

  std::optional<std::string> error;
  auto record = [&](tl::expected<void, std::string> result) {
    if (!result.has_value() && !error.has_value()) {
      error = result.error();
    }
  };

  record(set_registry_string(key, L"DisplayName", kAppName));
  record(set_registry_string(key, L"DisplayVersion", version_wide));
  record(set_registry_string(key, L"Publisher", kPublisher));
  record(set_registry_string(key, L"DisplayIcon", executable.wstring()));
  record(set_registry_string(key, L"InstallLocation", install_dir.wstring()));
  record(set_registry_string(key, L"UninstallString", uninstall_command));
  record(set_registry_string(key, L"QuietUninstallString", quiet_uninstall_command));
  record(set_registry_string(key, L"InstallDate",
                             format_install_date_for_registry(local_time.wYear, local_time.wMonth,
                                                              local_time.wDay)));
  record(set_registry_dword(key, L"NoModify", 1));
  record(set_registry_dword(key, L"NoRepair", 1));
  record(set_registry_dword(key, L"EstimatedSize", estimated_size_kb));

  LSTATUS close_status = RegCloseKey(key);
  if (close_status != ERROR_SUCCESS) {
    spdlog::error("Failed to close uninstall registry key: {}",
                  format_win32_error(static_cast<DWORD>(close_status)));
  }

  if (error.has_value()) {
    return tl::unexpected(*error);
  }
  return {};
}

tl::expected<bool, std::string> unregister_uninstall_entry() {
  HKEY key = nullptr;
  LSTATUS open_status = RegOpenKeyExW(HKEY_CURRENT_USER,
                                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                                      KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
  if (open_status == ERROR_FILE_NOT_FOUND) {
    return false;
  }
  if (open_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to open uninstall registry root: " +
                          format_win32_error(static_cast<DWORD>(open_status)));
  }

  LSTATUS delete_status = RegDeleteKeyW(key, L"win-tiler");
  LSTATUS close_status = RegCloseKey(key);
  if (close_status != ERROR_SUCCESS) {
    spdlog::error("Failed to close uninstall registry root: {}",
                  format_win32_error(static_cast<DWORD>(close_status)));
  }

  if (delete_status == ERROR_FILE_NOT_FOUND) {
    return false;
  }
  if (delete_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to remove uninstall registry key: " +
                          format_win32_error(static_cast<DWORD>(delete_status)));
  }

  return true;
}

tl::expected<std::filesystem::path, std::string>
create_temp_uninstall_helper(const std::filesystem::path& current_executable) {
  std::error_code ec;
  auto temp_dir = std::filesystem::temp_directory_path(ec);
  if (ec) {
    return tl::unexpected("Failed to resolve temp directory: " + ec.message());
  }

  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  auto helper_dir = temp_dir / ("win-tiler-uninstall-" + std::to_string(GetCurrentProcessId()) +
                               "-" + std::to_string(now));
  std::filesystem::create_directories(helper_dir, ec);
  if (ec) {
    return tl::unexpected("Failed to create uninstall helper directory: " + ec.message());
  }

  auto helper_path = helper_dir / "uninstall-helper.exe";
  std::filesystem::copy_file(current_executable, helper_path,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return tl::unexpected("Failed to copy uninstall helper: " + ec.message());
  }

  return helper_path;
}

tl::expected<void, std::string> launch_process(const std::wstring& command_line) {
  std::vector<wchar_t> mutable_command_line(command_line.begin(), command_line.end());
  mutable_command_line.push_back(L'\0');

  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info = {};

  BOOL created = CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr, FALSE, 0,
                                nullptr, nullptr, &startup_info, &process_info);
  if (created == 0) {
    return tl::unexpected("Failed to launch uninstall helper: " + format_win32_error(GetLastError()));
  }

  if (CloseHandle(process_info.hThread) == 0) {
    spdlog::error("Failed to close uninstall helper thread handle: {}",
                  format_win32_error(GetLastError()));
  }
  if (CloseHandle(process_info.hProcess) == 0) {
    spdlog::error("Failed to close uninstall helper process handle: {}",
                  format_win32_error(GetLastError()));
  }

  return {};
}

tl::expected<void, std::string> wait_for_original_process(unsigned long original_pid) {
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(original_pid));
  if (process == nullptr) {
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER) {
      return {};
    }
    return tl::unexpected("Failed to open original process: " + format_win32_error(error));
  }

  DWORD wait_result = WaitForSingleObject(process, INFINITE);
  if (CloseHandle(process) == 0) {
    spdlog::error("Failed to close original process handle: {}", format_win32_error(GetLastError()));
  }

  if (wait_result == WAIT_OBJECT_0) {
    return {};
  }
  return tl::unexpected("Failed while waiting for win-tiler to exit: " +
                        format_win32_error(GetLastError()));
}

void show_result_message(HWND owner, const wchar_t* title, const std::wstring& message,
                         UINT icon_flags) {
  int result = MessageBoxW(owner, message.c_str(), title, MB_OK | icon_flags);
  if (result == 0) {
    spdlog::error("Failed to show installer result message, error={}", GetLastError());
  }
}

std::filesystem::path normalize_for_compare(const std::filesystem::path& path) {
  std::error_code ec;
  auto absolute_path = std::filesystem::absolute(path, ec);
  if (ec) {
    return path.lexically_normal();
  }
  return absolute_path.lexically_normal();
}

HRESULT CALLBACK installer_dialog_callback(HWND hwnd, UINT notification, WPARAM, LPARAM,
                                           LONG_PTR callback_data) {
  if (notification != TDN_CREATED) {
    return S_OK;
  }

  const auto* state = reinterpret_cast<const InstallerDialogCallbackState*>(callback_data);
  if (state == nullptr) {
    return E_INVALIDARG;
  }

  SendMessageW(hwnd, TDM_ENABLE_BUTTON, kInstallButtonId, state->installed ? FALSE : TRUE);
  SendMessageW(hwnd, TDM_ENABLE_BUTTON, kUninstallButtonId, state->installed ? TRUE : FALSE);
  SendMessageW(hwnd, TDM_ENABLE_BUTTON, kApplyButtonId, state->installed ? TRUE : FALSE);

  return S_OK;
}

} // namespace

std::wstring quote_windows_argument_wide(const std::wstring& argument) {
  if (argument.empty()) {
    return L"\"\"";
  }

  bool needs_quotes = false;
  for (wchar_t ch : argument) {
    if (ch == L' ' || ch == L'\t' || ch == L'"') {
      needs_quotes = true;
      break;
    }
  }

  if (!needs_quotes) {
    return argument;
  }

  std::wstring result = L"\"";
  size_t backslash_count = 0;
  for (wchar_t ch : argument) {
    if (ch == L'\\') {
      ++backslash_count;
      continue;
    }

    if (ch == L'"') {
      result.append(backslash_count * 2 + 1, L'\\');
      result += ch;
      backslash_count = 0;
      continue;
    }

    result.append(backslash_count, L'\\');
    backslash_count = 0;
    result += ch;
  }

  result.append(backslash_count * 2, L'\\');
  result += L'"';
  return result;
}

std::wstring build_uninstall_command_line_wide(const std::filesystem::path& executable_path,
                                               bool quiet) {
  std::wstring command = quote_windows_argument_wide(executable_path.wstring());
  command += L" --uninstall";
  if (quiet) {
    command += L" --quiet";
  }
  return command;
}

std::wstring build_finish_uninstall_command_line_wide(const std::filesystem::path& helper_path,
                                                      unsigned long pid,
                                                      const std::filesystem::path& install_dir,
                                                      std::optional<unsigned long> running_pid) {
  std::wstring command = quote_windows_argument_wide(helper_path.wstring());
  command += L" --finish-uninstall --pid ";
  command += std::to_wstring(pid);
  command += L" --dir ";
  command += quote_windows_argument_wide(install_dir.wstring());
  if (running_pid.has_value()) {
    command += L" --running-pid ";
    command += std::to_wstring(*running_pid);
  }
  return command;
}

std::wstring format_install_date_for_registry(unsigned short year, unsigned short month,
                                              unsigned short day) {
  wchar_t buffer[9] = {};
  int count = swprintf_s(buffer, L"%04hu%02hu%02hu", year, month, day);
  if (count <= 0) {
    return {};
  }
  return buffer;
}

tl::expected<std::filesystem::path, std::string> get_default_install_directory() {
  PWSTR local_app_data = nullptr;
  HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data);
  if (FAILED(result)) {
    return tl::unexpected("Failed to resolve LocalAppData folder");
  }

  std::filesystem::path install_dir = std::filesystem::path(local_app_data) / "win-tiler";
  CoTaskMemFree(local_app_data);
  return install_dir;
}

std::filesystem::path get_installed_executable_path(const std::filesystem::path& install_dir) {
  return install_dir / "win-tiler.exe";
}

std::filesystem::path get_installed_config_path(const std::filesystem::path& install_dir) {
  return install_dir / "win-tiler.toml";
}

bool is_installation_present(const std::filesystem::path& install_dir) {
  std::error_code ec;
  return std::filesystem::is_regular_file(get_installed_executable_path(install_dir), ec) && !ec;
}

bool startup_command_targets_executable(const std::string& command_line,
                                        const std::filesystem::path& executable_path) {
  return command_line == build_startup_command_line(executable_path, std::nullopt, std::nullopt);
}

tl::expected<void, std::string>
ensure_installed_config_file(const std::filesystem::path& install_dir) {
  auto config_path = get_installed_config_path(install_dir);

  std::error_code ec;
  if (std::filesystem::exists(config_path, ec)) {
    return {};
  }
  if (ec) {
    return tl::unexpected("Failed to check installed config file: " + ec.message());
  }

  return write_options_toml(get_default_global_options(), config_path);
}

tl::expected<void, std::string>
apply_startup_option_for_installation(const std::filesystem::path& install_dir, bool auto_start) {
  auto installed_executable = get_installed_executable_path(install_dir);
  if (auto_start) {
    return enable_startup_registration(installed_executable, std::nullopt, std::nullopt);
  }

  auto startup_result = disable_startup_registration();
  if (!startup_result.has_value()) {
    return tl::unexpected(startup_result.error());
  }
  return {};
}

tl::expected<void, std::string>
install_current_executable(const std::filesystem::path& current_executable, bool auto_start) {
  auto install_dir = get_default_install_directory();
  if (!install_dir.has_value()) {
    return tl::unexpected(install_dir.error());
  }

  std::error_code ec;
  std::filesystem::create_directories(*install_dir, ec);
  if (ec) {
    return tl::unexpected("Failed to create install directory: " + ec.message());
  }

  auto installed_executable = get_installed_executable_path(*install_dir);
  bool same_file = false;
  if (std::filesystem::exists(installed_executable, ec) && !ec) {
    same_file = std::filesystem::equivalent(current_executable, installed_executable, ec);
    if (ec) {
      same_file = false;
      ec.clear();
    }
  }

  if (!same_file) {
    std::filesystem::copy_file(current_executable, installed_executable,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      return tl::unexpected("Failed to copy win-tiler.exe to install directory: " + ec.message());
    }
  }

  auto config_result = ensure_installed_config_file(*install_dir);
  if (!config_result.has_value()) {
    return tl::unexpected(config_result.error());
  }

  auto startup_result = apply_startup_option_for_installation(*install_dir, auto_start);
  if (!startup_result.has_value()) {
    return tl::unexpected(startup_result.error());
  }

  auto registry_result = register_uninstall_entry(*install_dir, installed_executable);
  if (!registry_result.has_value()) {
    return tl::unexpected(registry_result.error());
  }

  return {};
}

tl::expected<void, std::string>
start_uninstall_helper(const std::filesystem::path& current_executable,
                       const std::filesystem::path& install_dir) {
  auto helper_path = create_temp_uninstall_helper(current_executable);
  if (!helper_path.has_value()) {
    return tl::unexpected(helper_path.error());
  }

  auto running_pid = winapi::find_notification_area_process_id();
  std::wstring command_line = build_finish_uninstall_command_line_wide(
      *helper_path, GetCurrentProcessId(), install_dir, running_pid);
  return launch_process(command_line);
}

tl::expected<void, std::string> finish_uninstall(unsigned long original_pid,
                                                const std::filesystem::path& install_dir,
                                                const std::filesystem::path& helper_executable,
                                                std::optional<unsigned long> running_pid) {
  auto default_install_dir = get_default_install_directory();
  if (!default_install_dir.has_value()) {
    return tl::unexpected(default_install_dir.error());
  }
  if (normalize_for_compare(install_dir) != normalize_for_compare(*default_install_dir)) {
    return tl::unexpected("Refusing to uninstall from an unexpected directory");
  }

  auto wait_result = wait_for_original_process(original_pid);
  if (!wait_result.has_value()) {
    return tl::unexpected(wait_result.error());
  }

  if (running_pid.has_value()) {
    if (!winapi::request_notification_area_exit_for_running_instance()) {
      spdlog::debug("No running notification area instance accepted the uninstall exit request");
    }

    if (*running_pid != original_pid) {
      auto running_wait_result = wait_for_original_process(*running_pid);
      if (!running_wait_result.has_value()) {
        return tl::unexpected(running_wait_result.error());
      }
    }
  }

  auto startup_result = disable_startup_registration();
  if (!startup_result.has_value()) {
    return tl::unexpected(startup_result.error());
  }

  auto unregister_result = unregister_uninstall_entry();
  if (!unregister_result.has_value()) {
    return tl::unexpected(unregister_result.error());
  }

  std::error_code ec;
  std::filesystem::remove_all(install_dir, ec);
  if (ec) {
    return tl::unexpected("Failed to remove install directory: " + ec.message());
  }

  std::filesystem::path helper_dir = helper_executable.parent_path();
  BOOL delete_scheduled =
      MoveFileExW(helper_executable.wstring().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
  if (delete_scheduled == 0) {
    spdlog::error("Failed to schedule uninstall helper deletion, error={}", GetLastError());
  }
  if (!helper_dir.empty()) {
    BOOL dir_delete_scheduled =
        MoveFileExW(helper_dir.wstring().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    if (dir_delete_scheduled == 0) {
      spdlog::debug("Failed to schedule uninstall helper directory deletion, error={}",
                    GetLastError());
    }
  }

  return {};
}

tl::expected<InstallerDialogResult, std::string>
show_installer_dialog(void* owner_window, const std::filesystem::path& current_executable) {
  auto install_dir = get_default_install_directory();
  if (!install_dir.has_value()) {
    return tl::unexpected(install_dir.error());
  }

  std::wstring content = L"Install folder:\n";
  content += install_dir->wstring();
  bool installed = is_installation_present(*install_dir);
  auto installed_executable = get_installed_executable_path(*install_dir);
  auto startup_status = get_startup_registration_status();
  BOOL auto_start_checked = FALSE;
  if (installed && startup_status.has_value() && startup_status->enabled &&
      startup_status->command_line.has_value() &&
      startup_command_targets_executable(*startup_status->command_line, installed_executable)) {
    auto_start_checked = TRUE;
  }
  InstallerDialogCallbackState callback_state{installed};

  TASKDIALOG_BUTTON buttons[] = {
      {kInstallButtonId, L"Install"},
      {kUninstallButtonId, L"Uninstall"},
      {kApplyButtonId, L"Apply"},
  };

  TASKDIALOGCONFIG config = {};
  config.cbSize = sizeof(config);
  config.hwndParent = static_cast<HWND>(owner_window);
  config.dwFlags = TDF_SIZE_TO_CONTENT | TDF_ALLOW_DIALOG_CANCELLATION;
  if (auto_start_checked == TRUE) {
    config.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;
  }
  config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
  config.pszWindowTitle = L"win-tiler installer";
  config.pszMainInstruction = L"Install win-tiler for this user";
  config.pszContent = content.c_str();
  config.pszVerificationText = L"Start win-tiler when Windows starts";
  config.cButtons = 3;
  config.pButtons = buttons;
  config.nDefaultButton = installed ? kUninstallButtonId : kInstallButtonId;
  config.pfCallback = installer_dialog_callback;
  config.lpCallbackData = reinterpret_cast<LONG_PTR>(&callback_state);

  int pressed_button = 0;
  HRESULT result = TaskDialogIndirect(&config, &pressed_button, nullptr, &auto_start_checked);
  if (FAILED(result)) {
    return tl::unexpected("Failed to show installer dialog");
  }

  HWND owner = static_cast<HWND>(owner_window);
  if (pressed_button == kInstallButtonId) {
    auto install_result = install_current_executable(current_executable, auto_start_checked == TRUE);
    if (!install_result.has_value()) {
      show_result_message(owner, L"win-tiler installer", widen_ascii(install_result.error()),
                          MB_ICONERROR);
      return tl::unexpected(install_result.error());
    }

    show_result_message(owner, L"win-tiler installer", L"win-tiler was installed.",
                        MB_ICONINFORMATION);
    return InstallerDialogResult::Installed;
  }

  if (pressed_button == kUninstallButtonId) {
    auto uninstall_result = start_uninstall_helper(current_executable, *install_dir);
    if (!uninstall_result.has_value()) {
      show_result_message(owner, L"win-tiler installer", widen_ascii(uninstall_result.error()),
                          MB_ICONERROR);
      return tl::unexpected(uninstall_result.error());
    }

    return InstallerDialogResult::UninstallStarted;
  }

  if (pressed_button == kApplyButtonId) {
    auto apply_result = apply_startup_option_for_installation(*install_dir, auto_start_checked == TRUE);
    if (!apply_result.has_value()) {
      show_result_message(owner, L"win-tiler installer", widen_ascii(apply_result.error()),
                          MB_ICONERROR);
      return tl::unexpected(apply_result.error());
    }

    show_result_message(owner, L"win-tiler installer", L"Startup option was updated.",
                        MB_ICONINFORMATION);
    return InstallerDialogResult::Closed;
  }

  return InstallerDialogResult::Closed;
}

} // namespace wintiler
