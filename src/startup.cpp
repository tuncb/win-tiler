#include "startup.h"

#include <Windows.h>

#include <cctype>
#include <utility>
#include <vector>

namespace wintiler {

namespace {

constexpr wchar_t kStartupRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupValueName[] = L"win-tiler";

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
    LocalFree(buffer);
  }

  std::string message = wide_to_utf8(message_wide);
  while (!message.empty() && std::isspace(static_cast<unsigned char>(message.back())) != 0) {
    message.pop_back();
  }
  return message;
}

std::wstring quote_windows_argument(const std::wstring& argument) {
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

std::wstring build_startup_command_line_wide(const std::filesystem::path& executable_path,
                                             std::optional<std::filesystem::path> config_path,
                                             std::optional<std::filesystem::path> log_file_path) {
  std::wstring command_line = quote_windows_argument(executable_path.wstring());

  if (log_file_path.has_value()) {
    command_line += L" --log-file ";
    command_line += quote_windows_argument(log_file_path->wstring());
  }

  if (config_path.has_value()) {
    command_line += L" --config ";
    command_line += quote_windows_argument(config_path->wstring());
  }

  command_line += L" loop";
  return command_line;
}

tl::expected<std::optional<std::wstring>, std::string> read_startup_value() {
  HKEY key = nullptr;
  LSTATUS open_status = RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRunKeyPath, 0,
                                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
  if (open_status == ERROR_FILE_NOT_FOUND) {
    return std::optional<std::wstring>{std::nullopt};
  }
  if (open_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to open startup registry key: " +
                          format_win32_error(static_cast<DWORD>(open_status)));
  }

  DWORD type = 0;
  DWORD byte_count = 0;
  LSTATUS query_status =
      RegQueryValueExW(key, kStartupValueName, nullptr, &type, nullptr, &byte_count);
  if (query_status == ERROR_FILE_NOT_FOUND) {
    RegCloseKey(key);
    return std::optional<std::wstring>{std::nullopt};
  }
  if (query_status != ERROR_SUCCESS) {
    RegCloseKey(key);
    return tl::unexpected("Failed to query startup registry value: " +
                          format_win32_error(static_cast<DWORD>(query_status)));
  }
  if (type != REG_SZ && type != REG_EXPAND_SZ) {
    RegCloseKey(key);
    return tl::unexpected("Startup registry value has an unsupported type");
  }

  std::vector<wchar_t> buffer(static_cast<size_t>(byte_count / sizeof(wchar_t)), L'\0');
  query_status = RegQueryValueExW(key, kStartupValueName, nullptr, &type,
                                  reinterpret_cast<LPBYTE>(buffer.data()), &byte_count);
  RegCloseKey(key);

  if (query_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to read startup registry value: " +
                          format_win32_error(static_cast<DWORD>(query_status)));
  }

  if (!buffer.empty() && buffer.back() == L'\0') {
    buffer.pop_back();
  }

  return std::optional<std::wstring>{std::wstring(buffer.begin(), buffer.end())};
}

} // namespace

std::string build_startup_command_line(const std::filesystem::path& executable_path,
                                       std::optional<std::filesystem::path> config_path,
                                       std::optional<std::filesystem::path> log_file_path) {
  return wide_to_utf8(build_startup_command_line_wide(executable_path, std::move(config_path),
                                                      std::move(log_file_path)));
}

tl::expected<void, std::string>
enable_startup_registration(const std::filesystem::path& executable_path,
                            std::optional<std::filesystem::path> config_path,
                            std::optional<std::filesystem::path> log_file_path) {
  std::wstring command_line = build_startup_command_line_wide(
      executable_path, std::move(config_path), std::move(log_file_path));

  HKEY key = nullptr;
  LSTATUS create_status =
      RegCreateKeyExW(HKEY_CURRENT_USER, kStartupRunKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
                      KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
  if (create_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to open startup registry key for writing: " +
                          format_win32_error(static_cast<DWORD>(create_status)));
  }

  DWORD byte_count = static_cast<DWORD>((command_line.size() + 1) * sizeof(wchar_t));
  LSTATUS set_status =
      RegSetValueExW(key, kStartupValueName, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(command_line.c_str()), byte_count);
  RegCloseKey(key);

  if (set_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to write startup registry value: " +
                          format_win32_error(static_cast<DWORD>(set_status)));
  }

  return {};
}

tl::expected<bool, std::string> disable_startup_registration() {
  HKEY key = nullptr;
  LSTATUS open_status = RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRunKeyPath, 0,
                                      KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
  if (open_status == ERROR_FILE_NOT_FOUND) {
    return false;
  }
  if (open_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to open startup registry key for deletion: " +
                          format_win32_error(static_cast<DWORD>(open_status)));
  }

  LSTATUS delete_status = RegDeleteValueW(key, kStartupValueName);
  RegCloseKey(key);

  if (delete_status == ERROR_FILE_NOT_FOUND) {
    return false;
  }
  if (delete_status != ERROR_SUCCESS) {
    return tl::unexpected("Failed to remove startup registry value: " +
                          format_win32_error(static_cast<DWORD>(delete_status)));
  }

  return true;
}

tl::expected<StartupRegistrationStatus, std::string> get_startup_registration_status() {
  auto read_result = read_startup_value();
  if (!read_result.has_value()) {
    return tl::unexpected(read_result.error());
  }

  StartupRegistrationStatus status;
  status.enabled = read_result->has_value();
  if (read_result->has_value()) {
    status.command_line = wide_to_utf8(**read_result);
  }

  return status;
}

} // namespace wintiler
