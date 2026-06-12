#include "installer.h"

#include <bcrypt.h>
#include <commctrl.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
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
#include "resource.h"
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
constexpr int kCloseButtonId = 1004;
constexpr int kStartMenuShortcutCheckboxId = 2001;
constexpr int kAutoStartCheckboxId = 2002;
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

struct InstallerDialogState {
  bool installed = false;
  InstallerOptions options;
  InstallerOptions original_options;
  std::wstring content;
  int pressed_button = 0;
  bool done = false;
  HFONT font = nullptr;
  HWND start_menu_checkbox = nullptr;
  HWND auto_start_checkbox = nullptr;
  HWND apply_button = nullptr;
};

struct InstallerDialogLayout {
  int client_width = 532;
  int client_height = 216;
  int content_x = 16;
  int content_y = 18;
  int content_width = 500;
  int content_height = 42;
  int options_group_x = 16;
  int options_group_y = 66;
  int options_group_width = 500;
  int options_group_height = 82;
  int start_menu_checkbox_x = 28;
  int start_menu_checkbox_y = 90;
  int checkbox_width = 360;
  int checkbox_height = 24;
  int auto_start_checkbox_y = 116;
  int install_button_x = 190;
  int uninstall_button_x = 272;
  int apply_button_x = 354;
  int close_button_x = 436;
  int button_y = 174;
  int button_width = 76;
  int button_height = 26;
};

template <typename T>
struct ComObject {
  T* ptr = nullptr;

  ComObject() = default;
  ComObject(const ComObject&) = delete;
  ComObject& operator=(const ComObject&) = delete;

  ~ComObject() {
    reset();
  }

  void reset() {
    if (ptr != nullptr) {
      ptr->Release();
      ptr = nullptr;
    }
  }

  T** put() {
    return &ptr;
  }

  T* operator->() const {
    return ptr;
  }
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

int scale_for_window(HWND hwnd, int value) {
  UINT dpi = hwnd == nullptr ? GetDpiForSystem() : GetDpiForWindow(hwnd);
  if (dpi == 0) {
    dpi = 96;
  }
  return MulDiv(value, static_cast<int>(dpi), 96);
}

HWND create_installer_child(HWND parent, const wchar_t* class_name, const wchar_t* text,
                            DWORD style, int id, int x, int y, int width, int height,
                            HFONT font) {
  HWND control =
      CreateWindowExW(0, class_name, text, WS_CHILD | WS_VISIBLE | style, scale_for_window(parent, x),
                      scale_for_window(parent, y), scale_for_window(parent, width),
                      scale_for_window(parent, height), parent,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                      nullptr);
  if (control == nullptr) {
    spdlog::error("Failed to create installer dialog control, error={}", GetLastError());
    return nullptr;
  }
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return control;
}

bool installer_options_equal(const InstallerOptions& left, const InstallerOptions& right) {
  return left.auto_start == right.auto_start &&
         left.start_menu_shortcut == right.start_menu_shortcut;
}

InstallerDialogLayout calculate_installer_dialog_layout(std::wstring_view content) {
  constexpr int kContentLineHeight = 16;
  constexpr int kContentOptionsGap = 6;
  constexpr int kBaseOptionsGroupY = 66;
  constexpr int kBaseStartMenuCheckboxY = 90;
  constexpr int kBaseAutoStartCheckboxY = 116;
  constexpr int kBaseButtonY = 174;
  constexpr int kBaseClientHeight = 216;

  InstallerDialogLayout layout;
  int line_count = 1;
  for (wchar_t character : content) {
    if (character == L'\n') {
      ++line_count;
    }
  }

  layout.content_height = std::max(layout.content_height, line_count * kContentLineHeight);
  layout.options_group_y = layout.content_y + layout.content_height + kContentOptionsGap;

  int y_offset = layout.options_group_y - kBaseOptionsGroupY;
  layout.start_menu_checkbox_y = kBaseStartMenuCheckboxY + y_offset;
  layout.auto_start_checkbox_y = kBaseAutoStartCheckboxY + y_offset;
  layout.button_y = kBaseButtonY + y_offset;
  layout.client_height = kBaseClientHeight + y_offset;
  return layout;
}

void read_installer_dialog_options(InstallerDialogState& state) {
  state.options.start_menu_shortcut =
      SendMessageW(state.start_menu_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
  state.options.auto_start =
      SendMessageW(state.auto_start_checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void update_installer_apply_button_state(InstallerDialogState& state) {
  if (state.apply_button == nullptr) {
    return;
  }

  BOOL enabled =
      should_enable_installer_apply_button(state.installed, state.options, state.original_options)
          ? TRUE
          : FALSE;
  EnableWindow(state.apply_button, enabled);
}

bool create_installer_dialog_controls(HWND hwnd, InstallerDialogState& state) {
  state.font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  InstallerDialogLayout layout = calculate_installer_dialog_layout(state.content);

  HWND content = create_installer_child(hwnd, L"STATIC", state.content.c_str(),
                                        SS_LEFT | SS_NOPREFIX, 0, layout.content_x,
                                        layout.content_y, layout.content_width,
                                        layout.content_height, state.font);
  HWND options_group =
      create_installer_child(hwnd, L"BUTTON", L"Options", BS_GROUPBOX, 0,
                             layout.options_group_x, layout.options_group_y,
                             layout.options_group_width, layout.options_group_height, state.font);
  state.start_menu_checkbox = create_installer_child(
      hwnd, L"BUTTON", L"Add win-tiler to the Start Menu",
      BS_AUTOCHECKBOX | WS_TABSTOP, kStartMenuShortcutCheckboxId,
      layout.start_menu_checkbox_x, layout.start_menu_checkbox_y, layout.checkbox_width,
      layout.checkbox_height, state.font);
  state.auto_start_checkbox = create_installer_child(
      hwnd, L"BUTTON", L"Start win-tiler when Windows starts",
      BS_AUTOCHECKBOX | WS_TABSTOP, kAutoStartCheckboxId, layout.start_menu_checkbox_x,
      layout.auto_start_checkbox_y, layout.checkbox_width, layout.checkbox_height, state.font);

  HWND install_button = create_installer_child(
      hwnd, L"BUTTON", L"Install",
      BS_DEFPUSHBUTTON | WS_TABSTOP | (state.installed ? WS_DISABLED : 0), kInstallButtonId,
      layout.install_button_x, layout.button_y, layout.button_width, layout.button_height,
      state.font);
  HWND uninstall_button = create_installer_child(
      hwnd, L"BUTTON", L"Uninstall",
      BS_PUSHBUTTON | WS_TABSTOP | (state.installed ? 0 : WS_DISABLED), kUninstallButtonId,
      layout.uninstall_button_x, layout.button_y, layout.button_width, layout.button_height,
      state.font);
  state.apply_button =
      create_installer_child(hwnd, L"BUTTON", L"Apply", BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED,
                             kApplyButtonId, layout.apply_button_x, layout.button_y,
                             layout.button_width, layout.button_height, state.font);
  HWND close_button = create_installer_child(hwnd, L"BUTTON", L"Close",
                                             BS_PUSHBUTTON | WS_TABSTOP, kCloseButtonId,
                                             layout.close_button_x, layout.button_y,
                                             layout.button_width, layout.button_height, state.font);

  if (content == nullptr || options_group == nullptr || state.start_menu_checkbox == nullptr ||
      state.auto_start_checkbox == nullptr || install_button == nullptr ||
      uninstall_button == nullptr || state.apply_button == nullptr || close_button == nullptr) {
    return false;
  }

  SendMessageW(state.start_menu_checkbox, BM_SETCHECK,
               state.options.start_menu_shortcut ? BST_CHECKED : BST_UNCHECKED, 0);
  SendMessageW(state.auto_start_checkbox, BM_SETCHECK,
               state.options.auto_start ? BST_CHECKED : BST_UNCHECKED, 0);
  update_installer_apply_button_state(state);
  return true;
}

void finish_installer_dialog(HWND hwnd, InstallerDialogState& state, int pressed_button) {
  read_installer_dialog_options(state);
  state.pressed_button = pressed_button;
  state.done = true;
  DestroyWindow(hwnd);
}

LRESULT CALLBACK installer_dialog_window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                              LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    return TRUE;
  }

  auto* state = reinterpret_cast<InstallerDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
  case WM_CREATE:
    if (state == nullptr || !create_installer_dialog_controls(hwnd, *state)) {
      return -1;
    }
    return 0;
  case WM_COMMAND: {
    if (state == nullptr) {
      return 0;
    }
    int id = LOWORD(wparam);
    int notification = HIWORD(wparam);
    if ((id == kStartMenuShortcutCheckboxId || id == kAutoStartCheckboxId) &&
        notification == BN_CLICKED) {
      read_installer_dialog_options(*state);
      update_installer_apply_button_state(*state);
      return 0;
    }
    if (id == kInstallButtonId || id == kUninstallButtonId || id == kApplyButtonId ||
        id == kCloseButtonId || id == IDCANCEL) {
      finish_installer_dialog(hwnd, *state, id == kCloseButtonId || id == IDCANCEL ? 0 : id);
      return 0;
    }
    return 0;
  }
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORBTN: {
    HWND control = reinterpret_cast<HWND>(lparam);
    LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
    LONG_PTR button_type = style & BS_TYPEMASK;
    if (message == WM_CTLCOLORBTN &&
        (button_type == BS_PUSHBUTTON || button_type == BS_DEFPUSHBUTTON)) {
      return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    HDC device_context = reinterpret_cast<HDC>(wparam);
    SetBkMode(device_context, TRANSPARENT);
    SetTextColor(device_context, GetSysColor(COLOR_WINDOWTEXT));
    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
  }
  case WM_CLOSE:
    if (state != nullptr) {
      finish_installer_dialog(hwnd, *state, 0);
    } else {
      DestroyWindow(hwnd);
    }
    return 0;
  case WM_DESTROY:
    if (state != nullptr) {
      state->done = true;
    }
    return 0;
  default:
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

void center_installer_dialog(HWND hwnd, HWND owner) {
  RECT dialog_rect = {};
  if (GetWindowRect(hwnd, &dialog_rect) == 0) {
    spdlog::error("Failed to read installer dialog bounds, error={}", GetLastError());
    return;
  }

  HMONITOR monitor = nullptr;
  if (owner != nullptr) {
    monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
  } else {
    POINT origin = {0, 0};
    monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  }
  if (monitor == nullptr) {
    spdlog::error("Failed to resolve monitor for installer dialog");
    return;
  }

  MONITORINFO monitor_info = {};
  monitor_info.cbSize = sizeof(monitor_info);
  if (GetMonitorInfoW(monitor, &monitor_info) == 0) {
    spdlog::error("Failed to read monitor work area for installer dialog, error={}",
                  GetLastError());
    return;
  }
  const RECT& work_area = monitor_info.rcWork;
  int x = work_area.left + ((work_area.right - work_area.left) -
                            (dialog_rect.right - dialog_rect.left)) /
                               2;
  int y = work_area.top + ((work_area.bottom - work_area.top) -
                           (dialog_rect.bottom - dialog_rect.top)) /
                              2;
  if (SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE) == 0) {
    spdlog::error("Failed to center installer dialog, error={}", GetLastError());
  }
}

tl::expected<int, std::string> show_installer_options_dialog(HWND owner,
                                                             InstallerDialogState& state) {
  constexpr wchar_t kInstallerDialogClassName[] = L"win-tiler-installer-dialog";
  HINSTANCE instance = GetModuleHandleW(nullptr);

  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = installer_dialog_window_proc;
  window_class.hInstance = instance;
  window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
  window_class.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kInstallerDialogClassName;

  ATOM registered = RegisterClassExW(&window_class);
  if (registered == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return tl::unexpected("Failed to register installer dialog window");
  }

  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  DWORD ex_style = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE;
  InstallerDialogLayout layout = calculate_installer_dialog_layout(state.content);
  RECT window_rect = {0, 0, scale_for_window(owner, layout.client_width),
                      scale_for_window(owner, layout.client_height)};
  if (AdjustWindowRectEx(&window_rect, style, FALSE, ex_style) == 0) {
    return tl::unexpected("Failed to size installer dialog window");
  }

  HWND hwnd = CreateWindowExW(ex_style, kInstallerDialogClassName,
                              L"Install win-tiler for this user", style, CW_USEDEFAULT,
                              CW_USEDEFAULT, window_rect.right - window_rect.left,
                              window_rect.bottom - window_rect.top, owner, nullptr, instance,
                              &state);
  if (hwnd == nullptr) {
    return tl::unexpected("Failed to show installer dialog");
  }
  if (SetWindowTextW(hwnd, L"Install win-tiler for this user") == 0) {
    spdlog::error("Failed to set installer dialog title, error={}", GetLastError());
  }

  BOOL owner_was_enabled = FALSE;
  if (owner != nullptr && IsWindowEnabled(owner) != 0) {
    owner_was_enabled = TRUE;
    EnableWindow(owner, FALSE);
  }

  center_installer_dialog(hwnd, owner);
  ShowWindow(hwnd, SW_SHOWNORMAL);
  if (UpdateWindow(hwnd) == 0) {
    spdlog::error("Failed to update installer dialog, error={}", GetLastError());
  }

  MSG message = {};
  while (!state.done) {
    BOOL get_message_result = GetMessageW(&message, nullptr, 0, 0);
    if (get_message_result == -1) {
      if (owner != nullptr && owner_was_enabled == TRUE) {
        EnableWindow(owner, TRUE);
      }
      return tl::unexpected("Failed while reading installer dialog messages");
    }
    if (get_message_result == 0) {
      state.done = true;
      break;
    }
    if (IsWindow(hwnd) == 0 || IsDialogMessageW(hwnd, &message) == 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  if (IsWindow(hwnd) != 0) {
    DestroyWindow(hwnd);
  }
  if (owner != nullptr && owner_was_enabled == TRUE) {
    EnableWindow(owner, TRUE);
    if (SetForegroundWindow(owner) == 0) {
      spdlog::debug("Failed to foreground installer dialog owner, error={}", GetLastError());
    }
  }

  return state.pressed_button;
}

} // namespace

#ifndef DOCTEST_CONFIG_DISABLE
InstallerDialogLayoutForTest get_installer_dialog_layout_for_test(std::wstring_view content) {
  InstallerDialogLayout layout = calculate_installer_dialog_layout(content);
  return InstallerDialogLayoutForTest{layout.client_height, layout.content_y, layout.content_height,
                                      layout.options_group_y, layout.start_menu_checkbox_y,
                                      layout.button_y};
}
#endif // !DOCTEST_CONFIG_DISABLE

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

std::filesystem::path
get_start_menu_shortcut_path(const std::filesystem::path& programs_directory) {
  return programs_directory / "win-tiler.lnk";
}

tl::expected<std::filesystem::path, std::string> get_default_start_menu_shortcut_path() {
  PWSTR programs_directory = nullptr;
  HRESULT result =
      SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &programs_directory);
  if (FAILED(result)) {
    return tl::unexpected("Failed to resolve Start Menu programs folder");
  }

  std::filesystem::path shortcut_path = get_start_menu_shortcut_path(programs_directory);
  CoTaskMemFree(programs_directory);
  return shortcut_path;
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

bool should_enable_installer_apply_button(bool installed, const InstallerOptions& current_options,
                                          const InstallerOptions& original_options) {
  return installed && !installer_options_equal(current_options, original_options);
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
apply_start_menu_shortcut_option(const std::filesystem::path& shortcut_path,
                                 const std::filesystem::path& executable_path,
                                 bool add_start_menu_shortcut) {
  if (!add_start_menu_shortcut) {
    std::error_code ec;
    if (!std::filesystem::exists(shortcut_path, ec)) {
      if (ec) {
        return tl::unexpected("Failed to check Start Menu shortcut: " + ec.message());
      }
      return {};
    }

    std::filesystem::remove(shortcut_path, ec);
    if (ec) {
      return tl::unexpected("Failed to remove Start Menu shortcut: " + ec.message());
    }
    return {};
  }

  std::error_code ec;
  auto shortcut_directory = shortcut_path.parent_path();
  if (!shortcut_directory.empty()) {
    std::filesystem::create_directories(shortcut_directory, ec);
    if (ec) {
      return tl::unexpected("Failed to create Start Menu shortcut directory: " + ec.message());
    }
  }

  HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool uninitialize_com = SUCCEEDED(initialize_result);
  if (FAILED(initialize_result) && initialize_result != RPC_E_CHANGED_MODE) {
    return tl::unexpected("Failed to initialize COM for Start Menu shortcut");
  }

  auto uninitialize = [&]() {
    if (uninitialize_com) {
      CoUninitialize();
      uninitialize_com = false;
    }
  };

  ComObject<IShellLinkW> shell_link;
  HRESULT create_result =
      CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                       reinterpret_cast<void**>(shell_link.put()));
  if (FAILED(create_result)) {
    uninitialize();
    return tl::unexpected("Failed to create Start Menu shortcut object");
  }

  HRESULT path_result = shell_link->SetPath(executable_path.wstring().c_str());
  if (FAILED(path_result)) {
    shell_link.reset();
    uninitialize();
    return tl::unexpected("Failed to set Start Menu shortcut target");
  }

  auto working_directory = executable_path.parent_path();
  if (!working_directory.empty()) {
    HRESULT working_directory_result =
        shell_link->SetWorkingDirectory(working_directory.wstring().c_str());
    if (FAILED(working_directory_result)) {
      shell_link.reset();
      uninitialize();
      return tl::unexpected("Failed to set Start Menu shortcut working directory");
    }
  }

  HRESULT icon_result = shell_link->SetIconLocation(executable_path.wstring().c_str(), 0);
  if (FAILED(icon_result)) {
    shell_link.reset();
    uninitialize();
    return tl::unexpected("Failed to set Start Menu shortcut icon");
  }

  HRESULT description_result = shell_link->SetDescription(L"Start win-tiler");
  if (FAILED(description_result)) {
    shell_link.reset();
    uninitialize();
    return tl::unexpected("Failed to set Start Menu shortcut description");
  }

  ComObject<IPersistFile> persist_file;
  HRESULT persist_result =
      shell_link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(persist_file.put()));
  if (FAILED(persist_result)) {
    shell_link.reset();
    uninitialize();
    return tl::unexpected("Failed to prepare Start Menu shortcut file");
  }

  HRESULT save_result = persist_file->Save(shortcut_path.wstring().c_str(), TRUE);
  persist_file.reset();
  shell_link.reset();
  uninitialize();
  if (FAILED(save_result)) {
    return tl::unexpected("Failed to save Start Menu shortcut");
  }

  return {};
}

tl::expected<void, std::string>
apply_installation_options_for_installation(const std::filesystem::path& install_dir,
                                            const InstallerOptions& options) {
  auto startup_result = apply_startup_option_for_installation(install_dir, options.auto_start);
  if (!startup_result.has_value()) {
    return tl::unexpected(startup_result.error());
  }

  auto shortcut_path = get_default_start_menu_shortcut_path();
  if (!shortcut_path.has_value()) {
    return tl::unexpected(shortcut_path.error());
  }

  auto shortcut_result = apply_start_menu_shortcut_option(
      *shortcut_path, get_installed_executable_path(install_dir), options.start_menu_shortcut);
  if (!shortcut_result.has_value()) {
    return tl::unexpected(shortcut_result.error());
  }

  return {};
}

tl::expected<void, std::string>
install_current_executable(const std::filesystem::path& current_executable,
                           const InstallerOptions& options) {
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

  auto options_result = apply_installation_options_for_installation(*install_dir, options);
  if (!options_result.has_value()) {
    return tl::unexpected(options_result.error());
  }

  auto registry_result = register_uninstall_entry(*install_dir, installed_executable);
  if (!registry_result.has_value()) {
    return tl::unexpected(registry_result.error());
  }

  return {};
}

tl::expected<void, std::string>
install_current_executable(const std::filesystem::path& current_executable, bool auto_start) {
  return install_current_executable(current_executable, InstallerOptions{auto_start, false});
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

  auto shortcut_path = get_default_start_menu_shortcut_path();
  if (!shortcut_path.has_value()) {
    return tl::unexpected(shortcut_path.error());
  }
  auto shortcut_result =
      apply_start_menu_shortcut_option(*shortcut_path, get_installed_executable_path(install_dir),
                                       false);
  if (!shortcut_result.has_value()) {
    return tl::unexpected(shortcut_result.error());
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
  InstallerOptions selected_options;
  if (installed && startup_status.has_value() && startup_status->enabled &&
      startup_status->command_line.has_value() &&
      startup_command_targets_executable(*startup_status->command_line, installed_executable)) {
    selected_options.auto_start = true;
  }
  auto shortcut_path = get_default_start_menu_shortcut_path();
  if (installed && shortcut_path.has_value()) {
    std::error_code ec;
    selected_options.start_menu_shortcut = std::filesystem::is_regular_file(*shortcut_path, ec);
    if (ec) {
      selected_options.start_menu_shortcut = false;
    }
  }
  InstallerDialogState dialog_state;
  dialog_state.installed = installed;
  dialog_state.options = selected_options;
  dialog_state.original_options = selected_options;
  dialog_state.content = content;

  auto dialog_result =
      show_installer_options_dialog(static_cast<HWND>(owner_window), dialog_state);
  if (!dialog_result.has_value()) {
    return tl::unexpected(dialog_result.error());
  }
  int pressed_button = *dialog_result;
  selected_options = dialog_state.options;

  HWND owner = static_cast<HWND>(owner_window);
  if (pressed_button == kInstallButtonId) {
    auto install_result = install_current_executable(current_executable, selected_options);
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
    auto apply_result = apply_installation_options_for_installation(*install_dir, selected_options);
    if (!apply_result.has_value()) {
      show_result_message(owner, L"win-tiler installer", widen_ascii(apply_result.error()),
                          MB_ICONERROR);
      return tl::unexpected(apply_result.error());
    }

    show_result_message(owner, L"win-tiler installer", L"Installation options were updated.",
                        MB_ICONINFORMATION);
    return InstallerDialogResult::Closed;
  }

  return InstallerDialogResult::Closed;
}

} // namespace wintiler
