#include "installer.h"

#include <bcrypt.h>
#include <commctrl.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "options.h"
#include "startup.h"
#include "version.h"
#include "winapi.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Bcrypt.lib")

namespace wintiler {

namespace {

constexpr wchar_t kUninstallKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\win-tiler";
constexpr wchar_t kAppName[] = L"win-tiler";
constexpr wchar_t kPublisher[] = L"tuncb";
constexpr int kInstallButtonId = 1001;
constexpr int kUninstallButtonId = 1002;
constexpr int kApplyButtonId = 1003;
constexpr wchar_t kGitHubLatestReleaseUrl[] =
    L"https://api.github.com/repos/tuncb/win-tiler/releases/latest";
constexpr size_t kMaxReleaseResponseBytes = 2 * 1024 * 1024;
constexpr size_t kMaxSha256ResponseBytes = 4096;
constexpr size_t kMaxExecutableDownloadBytes = 50 * 1024 * 1024;
constexpr DWORD kNetworkResolveTimeoutMs = 5000;
constexpr DWORD kNetworkConnectTimeoutMs = 5000;
constexpr DWORD kNetworkSendTimeoutMs = 10000;
constexpr DWORD kNetworkReceiveTimeoutMs = 10000;
constexpr DWORD kUpdateProcessWaitTimeoutMs = 60000;

struct InstallerDialogCallbackState {
  bool installed = false;
};

tl::expected<void, std::string> launch_process(const std::wstring& command_line);
void show_result_message(HWND owner, const wchar_t* title, const std::wstring& message,
                         UINT icon_flags);

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

struct WinHttpHandle {
  HINTERNET handle = nullptr;

  WinHttpHandle() = default;
  WinHttpHandle(const WinHttpHandle&) = delete;
  WinHttpHandle& operator=(const WinHttpHandle&) = delete;

  WinHttpHandle(WinHttpHandle&& other) noexcept : handle(other.handle) {
    other.handle = nullptr;
  }

  WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
    if (this != &other) {
      if (handle != nullptr && WinHttpCloseHandle(handle) == 0) {
        spdlog::error("Failed to close WinHTTP handle: {}", format_win32_error(GetLastError()));
      }
      handle = other.handle;
      other.handle = nullptr;
    }
    return *this;
  }

  ~WinHttpHandle() {
    if (handle != nullptr && WinHttpCloseHandle(handle) == 0) {
      spdlog::error("Failed to close WinHTTP handle: {}", format_win32_error(GetLastError()));
    }
  }
};

std::string to_lower_ascii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_hex_sha256(std::string_view text) {
  if (text.size() != 64) {
    return false;
  }
  return std::all_of(text.begin(), text.end(),
                     [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)) != 0; });
}

std::wstring widen_url_ascii(std::string_view text) {
  return std::wstring(text.begin(), text.end());
}

std::optional<std::wstring> parse_json_string_at(const std::string& text, size_t quote_pos,
                                                 size_t end_pos) {
  if (quote_pos >= end_pos || text[quote_pos] != '"') {
    return std::nullopt;
  }

  std::wstring result;
  for (size_t i = quote_pos + 1; i < end_pos; ++i) {
    char ch = text[i];
    if (ch == '"') {
      return result;
    }

    if (ch != '\\') {
      result.push_back(static_cast<unsigned char>(ch));
      continue;
    }

    if (i + 1 >= end_pos) {
      return std::nullopt;
    }
    char escaped = text[++i];
    switch (escaped) {
    case '"':
    case '\\':
    case '/':
      result.push_back(static_cast<unsigned char>(escaped));
      break;
    case 'b':
      result.push_back(L'\b');
      break;
    case 'f':
      result.push_back(L'\f');
      break;
    case 'n':
      result.push_back(L'\n');
      break;
    case 'r':
      result.push_back(L'\r');
      break;
    case 't':
      result.push_back(L'\t');
      break;
    default:
      return std::nullopt;
    }
  }

  return std::nullopt;
}

std::optional<std::string> find_json_string_property(const std::string& text, std::string_view key,
                                                     size_t start_pos, size_t end_pos) {
  std::string quoted_key = "\"" + std::string(key) + "\"";
  size_t key_pos = text.find(quoted_key, start_pos);
  while (key_pos != std::string::npos && key_pos < end_pos) {
    size_t colon_pos = text.find(':', key_pos + quoted_key.size());
    if (colon_pos == std::string::npos || colon_pos >= end_pos) {
      return std::nullopt;
    }

    size_t value_pos = colon_pos + 1;
    while (value_pos < end_pos && std::isspace(static_cast<unsigned char>(text[value_pos])) != 0) {
      ++value_pos;
    }
    auto value = parse_json_string_at(text, value_pos, end_pos);
    if (value.has_value()) {
      return wide_to_utf8(*value);
    }

    key_pos = text.find(quoted_key, key_pos + quoted_key.size());
  }

  return std::nullopt;
}

std::optional<bool> find_json_bool_property(const std::string& text, std::string_view key,
                                            size_t start_pos, size_t end_pos) {
  std::string quoted_key = "\"" + std::string(key) + "\"";
  size_t key_pos = text.find(quoted_key, start_pos);
  if (key_pos == std::string::npos || key_pos >= end_pos) {
    return std::nullopt;
  }

  size_t colon_pos = text.find(':', key_pos + quoted_key.size());
  if (colon_pos == std::string::npos || colon_pos >= end_pos) {
    return std::nullopt;
  }

  size_t value_pos = colon_pos + 1;
  while (value_pos < end_pos && std::isspace(static_cast<unsigned char>(text[value_pos])) != 0) {
    ++value_pos;
  }

  if (text.compare(value_pos, 4, "true") == 0) {
    return true;
  }
  if (text.compare(value_pos, 5, "false") == 0) {
    return false;
  }
  return std::nullopt;
}

std::optional<size_t> find_matching_json_array_end(const std::string& text, size_t open_pos) {
  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  for (size_t i = open_pos; i < text.size(); ++i) {
    char ch = text[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }

    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '[') {
      ++depth;
      continue;
    }
    if (ch == ']') {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }

  return std::nullopt;
}

std::vector<std::pair<size_t, size_t>>
find_json_objects_in_array(const std::string& text, size_t array_start, size_t array_end) {
  std::vector<std::pair<size_t, size_t>> objects;
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  std::optional<size_t> object_start;

  for (size_t i = array_start + 1; i < array_end; ++i) {
    char ch = text[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
      continue;
    }

    if (ch == '"') {
      in_string = true;
      continue;
    }
    if (ch == '{') {
      if (depth == 0) {
        object_start = i;
      }
      ++depth;
      continue;
    }
    if (ch == '}') {
      --depth;
      if (depth == 0 && object_start.has_value()) {
        objects.emplace_back(*object_start, i + 1);
        object_start.reset();
      }
    }
  }

  return objects;
}

tl::expected<std::filesystem::path, std::string>
create_temp_update_directory(const std::string& prefix) {
  std::error_code ec;
  auto temp_dir = std::filesystem::temp_directory_path(ec);
  if (ec) {
    return tl::unexpected("Failed to resolve temp directory: " + ec.message());
  }

  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  auto update_dir =
      temp_dir / (prefix + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(now));
  std::filesystem::create_directories(update_dir, ec);
  if (ec) {
    return tl::unexpected("Failed to create update directory: " + ec.message());
  }

  return update_dir;
}

tl::expected<std::filesystem::path, std::string>
create_temp_update_helper(const std::filesystem::path& current_executable,
                          const std::filesystem::path& helper_dir) {
  auto helper_path = helper_dir / "update-helper.exe";
  std::error_code ec;
  std::filesystem::copy_file(current_executable, helper_path,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    return tl::unexpected("Failed to copy update helper: " + ec.message());
  }
  return helper_path;
}

tl::expected<std::vector<unsigned char>, std::string>
read_file_sha256_bytes(const std::filesystem::path& path) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  if (status < 0) {
    return tl::unexpected("Failed to open SHA-256 provider");
  }

  auto close_algorithm = [&algorithm]() {
    if (algorithm != nullptr) {
      NTSTATUS close_status = BCryptCloseAlgorithmProvider(algorithm, 0);
      if (close_status < 0) {
        spdlog::error("Failed to close SHA-256 provider");
      }
      algorithm = nullptr;
    }
  };

  DWORD object_length = 0;
  DWORD bytes_written = 0;
  status =
      BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length),
                        sizeof(object_length), &bytes_written, 0);
  if (status < 0) {
    close_algorithm();
    return tl::unexpected("Failed to query SHA-256 object length");
  }

  DWORD hash_length = 0;
  status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_length),
                             sizeof(hash_length), &bytes_written, 0);
  if (status < 0) {
    close_algorithm();
    return tl::unexpected("Failed to query SHA-256 hash length");
  }

  std::vector<unsigned char> hash_object(object_length);
  BCRYPT_HASH_HANDLE hash = nullptr;
  status = BCryptCreateHash(algorithm, &hash, hash_object.data(), object_length, nullptr, 0, 0);
  if (status < 0) {
    close_algorithm();
    return tl::unexpected("Failed to create SHA-256 hash");
  }

  auto destroy_hash = [&hash]() {
    if (hash != nullptr) {
      NTSTATUS destroy_status = BCryptDestroyHash(hash);
      if (destroy_status < 0) {
        spdlog::error("Failed to destroy SHA-256 hash");
      }
      hash = nullptr;
    }
  };

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    destroy_hash();
    close_algorithm();
    return tl::unexpected("Failed to open file for SHA-256: " + path.string());
  }

  std::array<char, 64 * 1024> buffer = {};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::streamsize read_count = file.gcount();
    if (read_count <= 0) {
      break;
    }
    status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                            static_cast<ULONG>(read_count), 0);
    if (status < 0) {
      destroy_hash();
      close_algorithm();
      return tl::unexpected("Failed to hash file data");
    }
  }

  if (file.bad()) {
    destroy_hash();
    close_algorithm();
    return tl::unexpected("Failed while reading file for SHA-256: " + path.string());
  }

  std::vector<unsigned char> hash_bytes(hash_length);
  status = BCryptFinishHash(hash, hash_bytes.data(), hash_length, 0);
  destroy_hash();
  close_algorithm();
  if (status < 0) {
    return tl::unexpected("Failed to finish SHA-256 hash");
  }

  return hash_bytes;
}

std::string hex_from_bytes(const std::vector<unsigned char>& bytes) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (unsigned char byte : bytes) {
    stream << std::setw(2) << static_cast<int>(byte);
  }
  return stream.str();
}

tl::expected<std::string, std::string> hash_file_sha256(const std::filesystem::path& path) {
  auto bytes = read_file_sha256_bytes(path);
  if (!bytes.has_value()) {
    return tl::unexpected(bytes.error());
  }
  return hex_from_bytes(*bytes);
}

tl::expected<void, std::string> configure_winhttp_timeouts(HINTERNET session) {
  BOOL result = WinHttpSetTimeouts(session, static_cast<int>(kNetworkResolveTimeoutMs),
                                   static_cast<int>(kNetworkConnectTimeoutMs),
                                   static_cast<int>(kNetworkSendTimeoutMs),
                                   static_cast<int>(kNetworkReceiveTimeoutMs));
  if (result == 0) {
    return tl::unexpected("Failed to set network timeouts: " + format_win32_error(GetLastError()));
  }
  return {};
}

tl::expected<void, std::string> query_http_success(HINTERNET request) {
  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  BOOL queried = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     nullptr, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
  if (queried == 0) {
    return tl::unexpected("Failed to read HTTP status: " + format_win32_error(GetLastError()));
  }

  if (status_code < 200 || status_code >= 300) {
    return tl::unexpected("GitHub request failed with HTTP status " + std::to_string(status_code));
  }

  return {};
}

tl::expected<WinHttpHandle, std::string>
open_url_request(const std::wstring& url, WinHttpHandle& session, WinHttpHandle& connection) {
  URL_COMPONENTS components = {};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  if (WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) == 0) {
    return tl::unexpected("Failed to parse URL: " + format_win32_error(GetLastError()));
  }

  if (components.nScheme != INTERNET_SCHEME_HTTPS) {
    return tl::unexpected("Refusing to download update over a non-HTTPS URL");
  }

  std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
  path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

  connection.handle = WinHttpConnect(session.handle, host.c_str(), components.nPort, 0);
  if (connection.handle == nullptr) {
    return tl::unexpected("Failed to connect to update host: " +
                          format_win32_error(GetLastError()));
  }

  WinHttpHandle request;
  request.handle =
      WinHttpOpenRequest(connection.handle, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (request.handle == nullptr) {
    return tl::unexpected("Failed to open update request: " + format_win32_error(GetLastError()));
  }

  DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  if (WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                       sizeof(redirect_policy)) == 0) {
    return tl::unexpected("Failed to configure update redirects: " +
                          format_win32_error(GetLastError()));
  }

  return std::move(request);
}

tl::expected<std::string, std::string> http_get_string(const std::wstring& url, size_t max_bytes) {
  WinHttpHandle session;
  session.handle = WinHttpOpen(L"win-tiler updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session.handle == nullptr) {
    return tl::unexpected("Failed to open network session: " + format_win32_error(GetLastError()));
  }

  auto timeout_result = configure_winhttp_timeouts(session.handle);
  if (!timeout_result.has_value()) {
    return tl::unexpected(timeout_result.error());
  }

  WinHttpHandle connection;
  auto request = open_url_request(url, session, connection);
  if (!request.has_value()) {
    return tl::unexpected(request.error());
  }

  std::wstring headers = L"Accept: application/vnd.github+json\r\n"
                         L"X-GitHub-Api-Version: 2022-11-28\r\n";
  BOOL sent =
      WinHttpSendRequest(request->handle, headers.c_str(), static_cast<DWORD>(headers.size()),
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (sent == 0) {
    return tl::unexpected("Failed to send update request: " + format_win32_error(GetLastError()));
  }

  if (WinHttpReceiveResponse(request->handle, nullptr) == 0) {
    return tl::unexpected("Failed to receive update response: " +
                          format_win32_error(GetLastError()));
  }

  auto status_result = query_http_success(request->handle);
  if (!status_result.has_value()) {
    return tl::unexpected(status_result.error());
  }

  std::string response;
  std::array<char, 8192> buffer = {};
  while (true) {
    DWORD read_count = 0;
    BOOL read = WinHttpReadData(request->handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                &read_count);
    if (read == 0) {
      return tl::unexpected("Failed to read update response: " +
                            format_win32_error(GetLastError()));
    }
    if (read_count == 0) {
      break;
    }
    if (response.size() + read_count > max_bytes) {
      return tl::unexpected("Update response exceeded the maximum allowed size");
    }
    response.append(buffer.data(), read_count);
  }

  return response;
}

tl::expected<void, std::string> http_download_file(const std::string& url,
                                                   const std::filesystem::path& destination,
                                                   size_t max_bytes) {
  WinHttpHandle session;
  session.handle = WinHttpOpen(L"win-tiler updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session.handle == nullptr) {
    return tl::unexpected("Failed to open network session: " + format_win32_error(GetLastError()));
  }

  auto timeout_result = configure_winhttp_timeouts(session.handle);
  if (!timeout_result.has_value()) {
    return tl::unexpected(timeout_result.error());
  }

  WinHttpHandle connection;
  auto request = open_url_request(widen_url_ascii(url), session, connection);
  if (!request.has_value()) {
    return tl::unexpected(request.error());
  }

  std::wstring headers = L"Accept: application/octet-stream\r\n";
  BOOL sent =
      WinHttpSendRequest(request->handle, headers.c_str(), static_cast<DWORD>(headers.size()),
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (sent == 0) {
    return tl::unexpected("Failed to send download request: " + format_win32_error(GetLastError()));
  }

  if (WinHttpReceiveResponse(request->handle, nullptr) == 0) {
    return tl::unexpected("Failed to receive download response: " +
                          format_win32_error(GetLastError()));
  }

  auto status_result = query_http_success(request->handle);
  if (!status_result.has_value()) {
    return tl::unexpected(status_result.error());
  }

  std::ofstream output(destination, std::ios::binary | std::ios::trunc);
  if (!output) {
    return tl::unexpected("Failed to open download target: " + destination.string());
  }

  size_t total_bytes = 0;
  std::array<char, 64 * 1024> buffer = {};
  while (true) {
    DWORD read_count = 0;
    BOOL read = WinHttpReadData(request->handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                &read_count);
    if (read == 0) {
      return tl::unexpected("Failed to read download response: " +
                            format_win32_error(GetLastError()));
    }
    if (read_count == 0) {
      break;
    }
    total_bytes += read_count;
    if (total_bytes > max_bytes) {
      return tl::unexpected("Downloaded update exceeded the maximum allowed size");
    }
    output.write(buffer.data(), read_count);
    if (!output) {
      return tl::unexpected("Failed while writing download target: " + destination.string());
    }
  }

  return {};
}

VersionNumber current_version_number() {
  return VersionNumber{VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH};
}

std::optional<ReleaseAsset> find_release_asset(const LatestReleaseInfo& release,
                                               const std::string& suffix) {
  std::string expected_name = "win-tiler-" + release.tag_name + suffix;
  auto found = std::find_if(release.assets.begin(), release.assets.end(),
                            [&](const ReleaseAsset& asset) { return asset.name == expected_name; });
  if (found == release.assets.end()) {
    return std::nullopt;
  }
  return *found;
}

tl::expected<void, std::string>
verify_downloaded_executable(const std::filesystem::path& downloaded_executable,
                             const std::string& expected_sha256) {
  if (!is_hex_sha256(expected_sha256)) {
    return tl::unexpected("Release SHA-256 file did not contain a valid hash");
  }

  auto actual_sha256 = hash_file_sha256(downloaded_executable);
  if (!actual_sha256.has_value()) {
    return tl::unexpected(actual_sha256.error());
  }
  if (to_lower_ascii(*actual_sha256) != to_lower_ascii(expected_sha256)) {
    return tl::unexpected("Downloaded update SHA-256 did not match the release hash");
  }

  return {};
}

tl::expected<void, std::string>
replace_installed_executable(const std::filesystem::path& installed_executable,
                             const std::filesystem::path& downloaded_executable) {
  std::filesystem::path backup = installed_executable;
  backup += ".update-backup";

  std::error_code ec;
  std::filesystem::remove(backup, ec);
  ec.clear();

  std::filesystem::rename(installed_executable, backup, ec);
  if (ec) {
    return tl::unexpected("Failed to move current executable out of the way: " + ec.message());
  }

  std::filesystem::copy_file(downloaded_executable, installed_executable,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    std::error_code restore_ec;
    std::filesystem::rename(backup, installed_executable, restore_ec);
    if (restore_ec) {
      spdlog::error("Failed to restore executable backup after update failure: {}",
                    restore_ec.message());
    }
    return tl::unexpected("Failed to copy updated executable into install directory: " +
                          ec.message());
  }

  std::filesystem::remove(backup, ec);
  if (ec) {
    spdlog::error("Failed to remove update backup: {}", ec.message());
  }
  return {};
}

tl::expected<void, std::string> launch_updated_app(const std::filesystem::path& executable) {
  std::wstring command_line = quote_windows_argument_wide(executable.wstring()) + L" loop";
  return launch_process(command_line);
}

tl::expected<void, std::string>
start_update_helper(const std::filesystem::path& current_executable,
                    const std::filesystem::path& install_dir,
                    const std::filesystem::path& downloaded_executable,
                    const std::string& expected_sha256, const std::filesystem::path& helper_dir,
                    std::optional<unsigned long> running_pid, bool restart) {
  auto helper_path = create_temp_update_helper(current_executable, helper_dir);
  if (!helper_path.has_value()) {
    return tl::unexpected(helper_path.error());
  }

  std::wstring command_line = build_finish_update_command_line_wide(
      *helper_path, GetCurrentProcessId(), install_dir, downloaded_executable, expected_sha256,
      running_pid, restart);
  return launch_process(command_line);
}

tl::expected<InstallerDialogResult, std::string>
check_download_and_start_update(HWND owner, const std::filesystem::path& current_executable,
                                const std::filesystem::path& install_dir) {
  auto response = http_get_string(kGitHubLatestReleaseUrl, kMaxReleaseResponseBytes);
  if (!response.has_value()) {
    return tl::unexpected(response.error());
  }

  auto release = parse_latest_release_response(*response);
  if (!release.has_value()) {
    return tl::unexpected(release.error());
  }

  if (!is_version_newer(release->version, current_version_number())) {
    show_result_message(owner, L"win-tiler updater", L"win-tiler is already up to date.",
                        MB_ICONINFORMATION);
    return InstallerDialogResult::Closed;
  }

  auto exe_asset = find_release_asset(*release, ".exe");
  if (!exe_asset.has_value()) {
    return tl::unexpected("Latest GitHub release does not include the expected executable asset");
  }
  auto sha_asset = find_release_asset(*release, ".exe.sha256");
  if (!sha_asset.has_value()) {
    return tl::unexpected("Latest GitHub release does not include the expected SHA-256 asset");
  }

  std::wstring prompt = L"win-tiler ";
  prompt += widen_ascii(release->tag_name);
  prompt += L" is available.\n\nUpdate now?";
  int update_choice = MessageBoxW(owner, prompt.c_str(), L"win-tiler updater",
                                  MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
  if (update_choice == 0) {
    return tl::unexpected("Failed to show update confirmation");
  }
  if (update_choice != IDYES) {
    return InstallerDialogResult::Closed;
  }

  auto update_dir = create_temp_update_directory("win-tiler-update");
  if (!update_dir.has_value()) {
    return tl::unexpected(update_dir.error());
  }

  auto downloaded_executable = *update_dir / "win-tiler-update.exe";
  auto downloaded_sha = *update_dir / "win-tiler-update.exe.sha256";
  auto exe_download = http_download_file(exe_asset->browser_download_url, downloaded_executable,
                                         kMaxExecutableDownloadBytes);
  if (!exe_download.has_value()) {
    return tl::unexpected(exe_download.error());
  }

  auto sha_download =
      http_download_file(sha_asset->browser_download_url, downloaded_sha, kMaxSha256ResponseBytes);
  if (!sha_download.has_value()) {
    return tl::unexpected(sha_download.error());
  }

  std::ifstream sha_file(downloaded_sha, std::ios::binary);
  if (!sha_file) {
    return tl::unexpected("Failed to open downloaded SHA-256 file");
  }
  std::string sha_text((std::istreambuf_iterator<char>(sha_file)),
                       std::istreambuf_iterator<char>());
  auto expected_sha256 = extract_sha256_from_text(sha_text);
  if (!expected_sha256.has_value()) {
    return tl::unexpected("Downloaded SHA-256 file did not contain a hash");
  }

  auto verify_result = verify_downloaded_executable(downloaded_executable, *expected_sha256);
  if (!verify_result.has_value()) {
    return tl::unexpected(verify_result.error());
  }

  show_result_message(owner, L"win-tiler updater",
                      L"Update downloaded. Press OK to close win-tiler and finish updating.",
                      MB_ICONINFORMATION);

  auto running_pid = winapi::find_notification_area_process_id();
  bool restart = running_pid.has_value();
  auto helper_result = start_update_helper(current_executable, install_dir, downloaded_executable,
                                           *expected_sha256, *update_dir, running_pid, restart);
  if (!helper_result.has_value()) {
    return tl::unexpected(helper_result.error());
  }

  return InstallerDialogResult::UpdateStarted;
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
  record(set_registry_string(
      key, L"InstallDate",
      format_install_date_for_registry(local_time.wYear, local_time.wMonth, local_time.wDay)));
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
  LSTATUS open_status =
      RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                    0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
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
    return tl::unexpected("Failed to launch helper process: " + format_win32_error(GetLastError()));
  }

  if (CloseHandle(process_info.hThread) == 0) {
    spdlog::error("Failed to close helper thread handle: {}", format_win32_error(GetLastError()));
  }
  if (CloseHandle(process_info.hProcess) == 0) {
    spdlog::error("Failed to close helper process handle: {}", format_win32_error(GetLastError()));
  }

  return {};
}

tl::expected<void, std::string> wait_for_process_exit(unsigned long pid, DWORD timeout_ms,
                                                      const std::string& label) {
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
  if (process == nullptr) {
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER) {
      return {};
    }
    return tl::unexpected("Failed to open " + label + " process: " + format_win32_error(error));
  }

  DWORD wait_result = WaitForSingleObject(process, timeout_ms);
  if (CloseHandle(process) == 0) {
    spdlog::error("Failed to close {} process handle: {}", label,
                  format_win32_error(GetLastError()));
  }

  if (wait_result == WAIT_OBJECT_0) {
    return {};
  }
  if (wait_result == WAIT_TIMEOUT) {
    return tl::unexpected("Timed out waiting for " + label + " process to exit");
  }
  return tl::unexpected("Failed while waiting for " + label +
                        " process to exit: " + format_win32_error(GetLastError()));
}

tl::expected<void, std::string> wait_for_original_process(unsigned long original_pid) {
  return wait_for_process_exit(original_pid, INFINITE, "original win-tiler");
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

std::wstring build_finish_update_command_line_wide(
    const std::filesystem::path& helper_path, unsigned long pid,
    const std::filesystem::path& install_dir, const std::filesystem::path& downloaded_executable,
    const std::string& expected_sha256, std::optional<unsigned long> running_pid, bool restart) {
  std::wstring command = quote_windows_argument_wide(helper_path.wstring());
  command += L" --finish-update --pid ";
  command += std::to_wstring(pid);
  command += L" --dir ";
  command += quote_windows_argument_wide(install_dir.wstring());
  command += L" --downloaded-exe ";
  command += quote_windows_argument_wide(downloaded_executable.wstring());
  command += L" --expected-sha256 ";
  command += quote_windows_argument_wide(widen_ascii(expected_sha256));
  if (running_pid.has_value()) {
    command += L" --running-pid ";
    command += std::to_wstring(*running_pid);
  }
  if (restart) {
    command += L" --restart";
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

std::optional<VersionNumber> parse_version_tag(const std::string& tag) {
  std::string value = tag;
  if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
    value.erase(value.begin());
  }

  VersionNumber version;
  std::array<int*, 3> fields = {&version.major, &version.minor, &version.patch};
  size_t start = 0;
  for (size_t index = 0; index < fields.size(); ++index) {
    size_t end = value.find('.', start);
    if (index == fields.size() - 1) {
      end = value.size();
    } else if (end == std::string::npos) {
      return std::nullopt;
    }

    if (end <= start) {
      return std::nullopt;
    }
    std::string segment = value.substr(start, end - start);
    if (!std::all_of(segment.begin(), segment.end(),
                     [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; })) {
      return std::nullopt;
    }

    try {
      *fields[index] = std::stoi(segment);
    } catch (const std::exception&) {
      return std::nullopt;
    }

    start = end + 1;
  }

  return version;
}

bool is_version_newer(const VersionNumber& candidate, const VersionNumber& current) {
  if (candidate.major != current.major) {
    return candidate.major > current.major;
  }
  if (candidate.minor != current.minor) {
    return candidate.minor > current.minor;
  }
  return candidate.patch > current.patch;
}

std::optional<std::string> extract_sha256_from_text(const std::string& text) {
  for (size_t i = 0; i + 64 <= text.size(); ++i) {
    std::string candidate = text.substr(i, 64);
    if (is_hex_sha256(candidate)) {
      return to_lower_ascii(candidate);
    }
  }
  return std::nullopt;
}

tl::expected<LatestReleaseInfo, std::string>
parse_latest_release_response(const std::string& response) {
  auto draft = find_json_bool_property(response, "draft", 0, response.size());
  if (draft.value_or(false)) {
    return tl::unexpected("Latest GitHub release is still a draft");
  }

  auto prerelease = find_json_bool_property(response, "prerelease", 0, response.size());
  if (prerelease.value_or(false)) {
    return tl::unexpected("Latest GitHub release is a prerelease");
  }

  auto tag_name = find_json_string_property(response, "tag_name", 0, response.size());
  if (!tag_name.has_value()) {
    return tl::unexpected("Latest GitHub release did not include a tag name");
  }

  auto version = parse_version_tag(*tag_name);
  if (!version.has_value()) {
    return tl::unexpected("Latest GitHub release tag is not a supported version: " + *tag_name);
  }

  LatestReleaseInfo release;
  release.tag_name = *tag_name;
  release.version = *version;
  if (auto html_url = find_json_string_property(response, "html_url", 0, response.size())) {
    release.html_url = *html_url;
  }

  size_t assets_key = response.find("\"assets\"");
  if (assets_key == std::string::npos) {
    return tl::unexpected("Latest GitHub release did not include assets");
  }
  size_t assets_start = response.find('[', assets_key);
  if (assets_start == std::string::npos) {
    return tl::unexpected("Latest GitHub release assets were malformed");
  }
  auto assets_end = find_matching_json_array_end(response, assets_start);
  if (!assets_end.has_value()) {
    return tl::unexpected("Latest GitHub release assets were malformed");
  }

  for (auto [object_start, object_end] :
       find_json_objects_in_array(response, assets_start, *assets_end)) {
    ReleaseAsset asset;
    if (auto name = find_json_string_property(response, "name", object_start, object_end)) {
      asset.name = *name;
    }
    if (auto download_url =
            find_json_string_property(response, "browser_download_url", object_start, object_end)) {
      asset.browser_download_url = *download_url;
    }
    if (auto digest = find_json_string_property(response, "digest", object_start, object_end)) {
      asset.digest = *digest;
    }
    if (!asset.name.empty() && !asset.browser_download_url.empty()) {
      release.assets.push_back(std::move(asset));
    }
  }

  return release;
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

bool can_update_installed_instance(const std::filesystem::path& current_executable,
                                   const std::filesystem::path& install_dir) {
  if (!is_installation_present(install_dir)) {
    return false;
  }

  std::error_code ec;
  bool same_file = std::filesystem::equivalent(current_executable,
                                               get_installed_executable_path(install_dir), ec);
  return !ec && same_file;
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

tl::expected<InstallerDialogResult, std::string> check_for_updates_from_installed_instance(
    void* owner_window, const std::filesystem::path& current_executable) {
  auto install_dir = get_default_install_directory();
  if (!install_dir.has_value()) {
    return tl::unexpected(install_dir.error());
  }

  HWND owner = static_cast<HWND>(owner_window);
  if (!can_update_installed_instance(current_executable, *install_dir)) {
    show_result_message(owner, L"win-tiler updater",
                        L"Updates can only be checked from the installed win-tiler executable.",
                        MB_ICONERROR);
    return InstallerDialogResult::Closed;
  }

  return check_download_and_start_update(owner, current_executable, *install_dir);
}

tl::expected<void, std::string>
finish_update(unsigned long original_pid, const std::filesystem::path& install_dir,
              const std::filesystem::path& downloaded_executable,
              const std::string& expected_sha256, const std::filesystem::path& helper_executable,
              std::optional<unsigned long> running_pid, bool restart) {
  auto default_install_dir = get_default_install_directory();
  if (!default_install_dir.has_value()) {
    return tl::unexpected(default_install_dir.error());
  }
  if (normalize_for_compare(install_dir) != normalize_for_compare(*default_install_dir)) {
    return tl::unexpected("Refusing to update an unexpected directory");
  }

  auto verify_result = verify_downloaded_executable(downloaded_executable, expected_sha256);
  if (!verify_result.has_value()) {
    return tl::unexpected(verify_result.error());
  }

  auto wait_result =
      wait_for_process_exit(original_pid, kUpdateProcessWaitTimeoutMs, "original win-tiler");
  if (!wait_result.has_value()) {
    return tl::unexpected(wait_result.error());
  }

  if (running_pid.has_value()) {
    if (!winapi::request_notification_area_exit_for_running_instance()) {
      spdlog::debug("No running notification area instance accepted the update exit request");
    }

    if (*running_pid != original_pid) {
      auto running_wait_result =
          wait_for_process_exit(*running_pid, kUpdateProcessWaitTimeoutMs, "running win-tiler");
      if (!running_wait_result.has_value()) {
        return tl::unexpected(running_wait_result.error());
      }
    }
  }

  auto installed_executable = get_installed_executable_path(install_dir);
  auto replace_result = replace_installed_executable(installed_executable, downloaded_executable);
  if (!replace_result.has_value()) {
    return tl::unexpected(replace_result.error());
  }

  auto registry_result = register_uninstall_entry(install_dir, installed_executable);
  if (!registry_result.has_value()) {
    return tl::unexpected(registry_result.error());
  }

  std::error_code ec;
  std::filesystem::remove(downloaded_executable, ec);
  if (ec) {
    spdlog::error("Failed to remove downloaded update executable: {}", ec.message());
  }
  ec.clear();
  std::filesystem::path downloaded_sha = downloaded_executable;
  downloaded_sha += ".sha256";
  std::filesystem::remove(downloaded_sha, ec);
  if (ec) {
    spdlog::error("Failed to remove downloaded update SHA-256 file: {}", ec.message());
  }

  if (restart) {
    auto restart_result = launch_updated_app(installed_executable);
    if (!restart_result.has_value()) {
      spdlog::error("{}", restart_result.error());
    }
  }

  std::filesystem::path helper_dir = helper_executable.parent_path();
  BOOL helper_delete_scheduled =
      MoveFileExW(helper_executable.wstring().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
  if (helper_delete_scheduled == 0) {
    spdlog::error("Failed to schedule update helper deletion, error={}", GetLastError());
  }
  if (!helper_dir.empty()) {
    BOOL dir_delete_scheduled =
        MoveFileExW(helper_dir.wstring().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    if (dir_delete_scheduled == 0) {
      spdlog::debug("Failed to schedule update helper directory deletion, error={}",
                    GetLastError());
    }
  }

  return {};
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
  bool can_update = can_update_installed_instance(current_executable, *install_dir);
  if (can_update) {
    content += L"\n\nCurrent version: ";
    content += widen_ascii(get_version_string());
  }
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
    auto install_result =
        install_current_executable(current_executable, auto_start_checked == TRUE);
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
    auto apply_result =
        apply_startup_option_for_installation(*install_dir, auto_start_checked == TRUE);
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
