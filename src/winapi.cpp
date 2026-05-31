#include "winapi.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <psapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <spdlog/spdlog.h>
#include <windows.h>
#include <wtsapi32.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include "installer.h"
#include "resource.h"
#include "version.h"

// Link with Psapi.lib
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Wtsapi32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(linker,                                                                            \
                "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "     \
                "version='6.0.0.0' processorArchitecture='*' "                                     \
                "publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace winapi {

// Helper function for case-insensitive string comparison
static bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  return std::equal(a.begin(), a.end(), b.begin(),
                    [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

bool should_ignore_owned_dialog_window(bool has_owner, const std::string& class_name) {
  return has_owner && class_name == "#32770";
}

bool should_invalidate_monitor_cache_for_message(unsigned int message) {
  return message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE;
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor,
                              LPARAM dwData) {
  std::vector<MonitorInfo>* monitors = reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
  MONITORINFOEXA mi;
  mi.cbSize = sizeof(MONITORINFOEXA);
  if (GetMonitorInfoA(hMonitor, &mi)) {
    MonitorInfo info;
    info.handle = (HMONITOR_T)hMonitor;
    info.deviceName = mi.szDevice;
    info.rect = {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom};
    info.workArea = {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom};
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY);
    monitors->push_back(info);
  }
  return TRUE;
}

namespace {
std::mutex g_monitor_cache_mutex;
std::vector<MonitorInfo> g_monitor_cache;
bool g_monitor_cache_dirty = true;

void fill_monitors_uncached(std::vector<MonitorInfo>& monitors) {
  monitors.clear();
  EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&monitors);
}
} // namespace

void fill_monitors(std::vector<MonitorInfo>& monitors) {
  std::scoped_lock lock(g_monitor_cache_mutex);
  if (g_monitor_cache_dirty) {
    fill_monitors_uncached(g_monitor_cache);
    g_monitor_cache_dirty = false;
  }
  monitors = g_monitor_cache;
}

void invalidate_monitor_cache() {
  std::scoped_lock lock(g_monitor_cache_mutex);
  g_monitor_cache_dirty = true;
}

#ifndef DOCTEST_CONFIG_DISABLE
void set_monitor_cache_for_test(const std::vector<MonitorInfo>& monitors) {
  std::scoped_lock lock(g_monitor_cache_mutex);
  g_monitor_cache = monitors;
  g_monitor_cache_dirty = false;
}

bool is_monitor_cache_dirty_for_test() {
  std::scoped_lock lock(g_monitor_cache_mutex);
  return g_monitor_cache_dirty;
}
#endif // !DOCTEST_CONFIG_DISABLE

static bool visible_size_differs_from_target(const WindowPosition& target_visible_position,
                                             const WindowPosition& actual_visible_position) {
  constexpr int kSizeTolerance = 2;

  int width_delta = actual_visible_position.width - target_visible_position.width;
  int height_delta = actual_visible_position.height - target_visible_position.height;

  return (actual_visible_position.width > 0 &&
          (width_delta < -kSizeTolerance || width_delta > kSizeTolerance)) ||
         (actual_visible_position.height > 0 &&
          (height_delta < -kSizeTolerance || height_delta > kSizeTolerance));
}

static std::string format_window_position(const WindowPosition& position) {
  std::ostringstream stream;
  stream << "x=" << position.x << ", y=" << position.y << ", w=" << position.width
         << ", h=" << position.height;
  return stream.str();
}

std::string format_window_minmax_info(const WindowMinMaxInfo& info) {
  std::ostringstream stream;
  stream << "max_size=" << info.max_width << "x" << info.max_height << ", max_position=("
         << info.max_x << "," << info.max_y << "), min_track=" << info.min_track_width << "x"
         << info.min_track_height << ", max_track=" << info.max_track_width << "x"
         << info.max_track_height;
  return stream.str();
}

std::optional<WindowMinMaxInfo> get_window_minmax_info(HWND_T hwnd) {
  HWND win = reinterpret_cast<HWND>(hwnd);
  if (!IsWindow(win)) {
    return std::nullopt;
  }

  MINMAXINFO raw_info{};
  DWORD_PTR message_result = 0;
  SetLastError(ERROR_SUCCESS);
  auto send_result =
      SendMessageTimeoutW(win, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&raw_info),
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &message_result);
  if (send_result == 0) {
    spdlog::debug("Failed to retrieve WM_GETMINMAXINFO for hwnd={}, error={}",
                  static_cast<void*>(win), GetLastError());
    return std::nullopt;
  }

  return WindowMinMaxInfo{
      static_cast<int>(raw_info.ptMaxSize.x),      static_cast<int>(raw_info.ptMaxSize.y),
      static_cast<int>(raw_info.ptMaxPosition.x),  static_cast<int>(raw_info.ptMaxPosition.y),
      static_cast<int>(raw_info.ptMinTrackSize.x), static_cast<int>(raw_info.ptMinTrackSize.y),
      static_cast<int>(raw_info.ptMaxTrackSize.x), static_cast<int>(raw_info.ptMaxTrackSize.y)};
}

static void log_failed_placement_correction(const TileInfo& tile_info,
                                            const WindowPosition& actual_visible_position) {
  WindowInfo window_info = get_window_info(tile_info.handle);
  std::string minmax_text = "unavailable";
  auto minmax_info = get_window_minmax_info(tile_info.handle);
  if (minmax_info.has_value()) {
    minmax_text = format_window_minmax_info(*minmax_info);
  }

  spdlog::error("Failed to resize window during placement correction: hwnd={}, title=\"{}\", "
                "process=\"{}\", class=\"{}\", target=[{}], actual=[{}], WM_GETMINMAXINFO=[{}]",
                tile_info.handle, window_info.title, window_info.processName, window_info.className,
                format_window_position(tile_info.window_position),
                format_window_position(actual_visible_position), minmax_text);
}

static void maybe_log_failed_placement_correction(
    const TileInfo& tile_info, const std::optional<WindowPosition>& actual_visible_position) {
  if (tile_info.placement_kind != TilePlacementKind::PlacementCorrection ||
      !actual_visible_position.has_value() ||
      !visible_size_differs_from_target(tile_info.window_position, *actual_visible_position)) {
    return;
  }

  log_failed_placement_correction(tile_info, *actual_visible_position);
}

void log_monitors(const std::vector<MonitorInfo>& monitors) {
  spdlog::info("=== Monitor Info ({} monitors) ===", monitors.size());
  for (size_t i = 0; i < monitors.size(); ++i) {
    const auto& m = monitors[i];
    long rectW = m.rect.right - m.rect.left;
    long rectH = m.rect.bottom - m.rect.top;
    long workW = m.workArea.right - m.workArea.left;
    long workH = m.workArea.bottom - m.workArea.top;
    spdlog::info("Monitor {}: handle={}, device={}, primary={}", i, m.handle, m.deviceName,
                 m.isPrimary);
    spdlog::info("  rect: [{}, {}, {}, {}] ({}x{})", m.rect.left, m.rect.top, m.rect.right,
                 m.rect.bottom, rectW, rectH);
    spdlog::info("  workArea: [{}, {}, {}, {}] ({}x{})", m.workArea.left, m.workArea.top,
                 m.workArea.right, m.workArea.bottom, workW, workH);
  }
}

bool monitors_equal(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const auto& ma = a[i];
    const auto& mb = b[i];
    // Compare rect
    if (ma.rect.left != mb.rect.left || ma.rect.top != mb.rect.top ||
        ma.rect.right != mb.rect.right || ma.rect.bottom != mb.rect.bottom) {
      return false;
    }
    // Compare workArea
    if (ma.workArea.left != mb.workArea.left || ma.workArea.top != mb.workArea.top ||
        ma.workArea.right != mb.workArea.right || ma.workArea.bottom != mb.workArea.bottom) {
      return false;
    }
    // Compare isPrimary
    if (ma.isPrimary != mb.isPrimary) {
      return false;
    }
    if (ma.deviceName != mb.deviceName) {
      return false;
    }
    // Note: handle is not compared as it may change between enumerations
  }
  return true;
}

static std::optional<DWORD_T> get_window_pid(HWND_T hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId((HWND)hwnd, &pid);
  if (pid != 0) {
    return pid;
  }
  return std::nullopt;
}

struct CachedProcessName {
  HANDLE process_handle = nullptr;
  std::string process_name;
  size_t last_seen_generation = 0;
};

struct CachedWindowClassName {
  std::optional<DWORD_T> pid;
  ULONG_PTR class_atom = 0;
  std::string class_name;
  size_t last_seen_generation = 0;
};

static std::unordered_map<DWORD_T, CachedProcessName> g_process_name_cache;
static std::unordered_map<HWND_T, CachedWindowClassName> g_window_class_name_cache;
static size_t g_metadata_cache_generation = 0;

static void close_cached_process_handle(HANDLE process_handle, DWORD_T pid) {
  if (process_handle == nullptr) {
    return;
  }
  if (CloseHandle(process_handle) == 0) {
    spdlog::debug("Failed to close cached process handle for pid {}, error={}", pid,
                  GetLastError());
  }
}

static bool cached_process_handle_is_alive(HANDLE process_handle, DWORD_T pid) {
  if (process_handle == nullptr) {
    return false;
  }

  DWORD wait_result = WaitForSingleObject(process_handle, 0);
  if (wait_result == WAIT_TIMEOUT) {
    return true;
  }

  if (wait_result == WAIT_FAILED) {
    spdlog::debug("Failed to query cached process handle for pid {}, error={}", pid,
                  GetLastError());
  }

  return false;
}

static void mark_seen_generation(size_t& last_seen_generation,
                                 std::optional<size_t> seen_generation) {
  if (seen_generation.has_value()) {
    last_seen_generation = *seen_generation;
  }
}

static std::string get_process_name_from_pid(DWORD_T pid,
                                             std::optional<size_t> seen_generation = std::nullopt) {
  auto cached = g_process_name_cache.find(pid);
  if (cached != g_process_name_cache.end()) {
    if (cached_process_handle_is_alive(cached->second.process_handle, pid)) {
      mark_seen_generation(cached->second.last_seen_generation, seen_generation);
      return cached->second.process_name;
    }

    close_cached_process_handle(cached->second.process_handle, pid);
    g_process_name_cache.erase(cached);
  }

  std::string processName;
  HANDLE hProcess =
      OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
  bool can_cache_process_handle = hProcess != nullptr;
  if (hProcess == nullptr) {
    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  }

  if (hProcess) {
    char buffer[MAX_PATH];
    if (GetModuleBaseNameA(hProcess, NULL, buffer, MAX_PATH)) {
      processName = buffer;
    }

    if (can_cache_process_handle && !processName.empty()) {
      CachedProcessName entry;
      entry.process_handle = hProcess;
      entry.process_name = processName;
      entry.last_seen_generation = seen_generation.value_or(0);
      g_process_name_cache[pid] = std::move(entry);
    } else {
      close_cached_process_handle(hProcess, pid);
    }
  }
  return processName;
}

static std::string
get_window_class_name_for_pid(HWND hwnd, std::optional<DWORD_T> pid,
                              std::optional<size_t> seen_generation = std::nullopt) {
  if (!IsWindow(hwnd)) {
    return {};
  }

  HWND_T cache_key = reinterpret_cast<HWND_T>(hwnd);
  ULONG_PTR class_atom = static_cast<ULONG_PTR>(GetClassLongPtrA(hwnd, GCW_ATOM));

  auto cached = g_window_class_name_cache.find(cache_key);
  if (cached != g_window_class_name_cache.end() && class_atom != 0 && cached->second.pid == pid &&
      cached->second.class_atom == class_atom) {
    mark_seen_generation(cached->second.last_seen_generation, seen_generation);
    return cached->second.class_name;
  }

  char class_name_buf[256] = {};
  if (GetClassNameA(hwnd, class_name_buf, sizeof(class_name_buf)) <= 0) {
    g_window_class_name_cache.erase(cache_key);
    return {};
  }

  CachedWindowClassName entry;
  entry.pid = pid;
  entry.class_atom = class_atom;
  entry.class_name = class_name_buf;
  entry.last_seen_generation = seen_generation.value_or(0);
  g_window_class_name_cache[cache_key] = entry;
  return entry.class_name;
}

static void prune_metadata_caches_after_enumeration(size_t seen_generation) {
  for (auto it = g_window_class_name_cache.begin(); it != g_window_class_name_cache.end();) {
    if (it->second.last_seen_generation != seen_generation || !IsWindow((HWND)it->first)) {
      it = g_window_class_name_cache.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = g_process_name_cache.begin(); it != g_process_name_cache.end();) {
    if (it->second.last_seen_generation != seen_generation ||
        !cached_process_handle_is_alive(it->second.process_handle, it->first)) {
      close_cached_process_handle(it->second.process_handle, it->first);
      it = g_process_name_cache.erase(it);
    } else {
      ++it;
    }
  }
}

static bool is_window_maximized(HWND_T hwnd) {
  return IsZoomed((HWND)hwnd);
}

static bool is_window_minimized(HWND_T hwnd) {
  return IsIconic((HWND)hwnd);
}

struct WindowEnumContext {
  std::vector<HWND_T>* handles;
  const wintiler::IgnoreOptions* ignore_options;
  size_t generation;
};

BOOL CALLBACK WindowEnumProc(HWND hwnd, LPARAM lParam) {
  auto* ctx = reinterpret_cast<WindowEnumContext*>(lParam);

  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }

  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
    return TRUE;
  }

  char title[256];
  if (GetWindowTextA(hwnd, title, sizeof(title)) == 0) {
    return TRUE;
  }

  auto pid = get_window_pid(reinterpret_cast<HWND_T>(hwnd));
  std::string class_name = get_window_class_name_for_pid(hwnd, pid, ctx->generation);

  if (class_name == "SysDragImage") {
    return TRUE;
  }

  if (class_name == "tooltips_class32") {
    return TRUE;
  }

  HWND owner = GetWindow(hwnd, GW_OWNER);
  if (should_ignore_owned_dialog_window(owner != nullptr, class_name)) {
    return TRUE;
  }

  LONG ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
  if (ex_style & WS_EX_TOOLWINDOW) {
    return TRUE;
  }
  if (ex_style & WS_EX_TOPMOST) {
    return TRUE;
  }
  if (ex_style & WS_EX_TRANSPARENT) {
    return TRUE;
  }
  if (ex_style & WS_EX_NOACTIVATE) {
    return TRUE;
  }

  if (IsHungAppWindow(hwnd)) {
    return TRUE;
  }

  std::string process_name;
  if (pid.has_value()) {
    process_name = get_process_name_from_pid(*pid, ctx->generation);
  }

  if (process_name.empty()) {
    return TRUE;
  }

  const auto& options = *ctx->ignore_options;

  for (const auto& proc : options.ignored_processes) {
    if (process_name == proc) {
      return TRUE;
    }
  }

  for (const auto& ignored_title : options.ignored_window_titles) {
    if (title == ignored_title) {
      return TRUE;
    }
  }

  for (const auto& pair : options.ignored_process_title_pairs) {
    if (iequals(process_name, pair.first) && iequals(title, pair.second)) {
      return TRUE;
    }
  }

  if (options.small_window_barrier.has_value()) {
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
      int width = rect.right - rect.left;
      int height = rect.bottom - rect.top;
      if (width < options.small_window_barrier->width ||
          height < options.small_window_barrier->height) {
        return TRUE;
      }
    }
  }

  if (!options.ignore_children_of_processes.empty()) {
    HWND owner = GetWindow(hwnd, GW_OWNER);
    HWND parent = GetParent(hwnd);

    if (owner != nullptr || parent != nullptr) {
      for (const auto& proc : options.ignore_children_of_processes) {
        if (iequals(process_name, proc)) {
          return TRUE;
        }
      }
    }
  }

  ctx->handles->push_back(reinterpret_cast<HWND_T>(hwnd));
  return TRUE;
}

static void fill_windows_list(const wintiler::IgnoreOptions& ignore_options,
                              std::vector<HWND_T>& handles) {
  handles.clear();
  size_t generation = ++g_metadata_cache_generation;
  WindowEnumContext ctx{&handles, &ignore_options, generation};
  EnumWindows(WindowEnumProc, (LPARAM)&ctx);
  prune_metadata_caches_after_enumeration(generation);
}

static void gather_raw_window_data_into(const wintiler::IgnoreOptions& ignore_options,
                                        std::vector<HWND_T>& handles) {
  fill_windows_list(ignore_options, handles);

  std::sort(handles.begin(), handles.end(), [](HWND_T lhs, HWND_T rhs) {
    return reinterpret_cast<uintptr_t>(lhs) < reinterpret_cast<uintptr_t>(rhs);
  });
}

static bool is_window_fullscreen(HWND_T hwnd);

struct WindowManagementState {
  HWND_T handle = nullptr;
  std::string title;
  std::string class_name;
  std::optional<DWORD_T> pid;
  std::string process_name;
  std::optional<Rect> rect;
  long ex_style = 0;
  bool is_visible = false;
  bool is_cloaked = false;
  bool has_title = false;
  bool is_sys_drag_image = false;
  bool is_tooltip = false;
  bool is_tool_window = false;
  bool is_topmost = false;
  bool is_transparent = false;
  bool is_no_activate = false;
  bool is_hung = false;
  bool has_owner = false;
  bool has_parent = false;
  bool is_child_or_owned = false;
  bool is_owned_dialog = false;
  bool process_name_missing = false;
  bool matches_ignored_process = false;
  bool matches_ignored_title = false;
  bool matches_ignored_process_title_pair = false;
  bool is_below_small_window_barrier = false;
  bool is_ignored_child_of_process = false;
  bool is_maximized = false;
  bool is_fullscreen = false;
  bool passes_filters = false;
  bool is_currently_managed = false;
  std::optional<size_t> monitor_index;
  std::vector<std::string> reasons;
};

struct DumpWindowEnumContext {
  std::vector<WindowManagementState>* windows;
  const wintiler::IgnoreOptions* ignore_options;
  const std::vector<MonitorInfo>* monitors;
};

static std::optional<Rect> get_raw_window_rect(HWND hwnd) {
  RECT rect;
  if (GetWindowRect(hwnd, &rect) == 0) {
    return std::nullopt;
  }

  return Rect{rect.left, rect.top, rect.right, rect.bottom};
}

static std::optional<size_t>
get_monitor_index_for_window(HWND hwnd, const std::vector<MonitorInfo>& monitors) {
  HMONITOR window_monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
  if (window_monitor == nullptr) {
    return std::nullopt;
  }

  for (size_t i = 0; i < monitors.size(); ++i) {
    if (window_monitor == reinterpret_cast<HMONITOR>(monitors[i].handle)) {
      return i;
    }
  }

  return std::nullopt;
}

static void add_reason_if(std::vector<std::string>& reasons, bool condition, const char* reason) {
  if (condition) {
    reasons.emplace_back(reason);
  }
}

static std::string join_reasons(const std::vector<std::string>& reasons) {
  if (reasons.empty()) {
    return "none";
  }

  std::string joined;
  for (size_t i = 0; i < reasons.size(); ++i) {
    if (i > 0) {
      joined += ", ";
    }
    joined += reasons[i];
  }
  return joined;
}

static WindowManagementState
inspect_window_management_state(HWND hwnd, const wintiler::IgnoreOptions& options,
                                const std::vector<MonitorInfo>& monitors) {
  WindowManagementState state;
  state.handle = reinterpret_cast<HWND_T>(hwnd);
  state.is_visible = IsWindowVisible(hwnd) != 0;

  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
    state.is_cloaked = cloaked != FALSE;
  }

  char title[256] = {};
  int title_length = GetWindowTextA(hwnd, title, sizeof(title));
  if (title_length > 0) {
    state.title = title;
  }
  state.has_title = title_length > 0;

  state.pid = get_window_pid(state.handle);
  state.class_name = get_window_class_name_for_pid(hwnd, state.pid);

  state.is_sys_drag_image = state.class_name == "SysDragImage";
  state.is_tooltip = state.class_name == "tooltips_class32";

  state.ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
  state.is_tool_window = (state.ex_style & WS_EX_TOOLWINDOW) != 0;
  state.is_topmost = (state.ex_style & WS_EX_TOPMOST) != 0;
  state.is_transparent = (state.ex_style & WS_EX_TRANSPARENT) != 0;
  state.is_no_activate = (state.ex_style & WS_EX_NOACTIVATE) != 0;
  state.is_hung = IsHungAppWindow(hwnd) != 0;

  if (state.pid.has_value()) {
    state.process_name = get_process_name_from_pid(*state.pid);
  }
  state.process_name_missing = state.process_name.empty();

  state.rect = get_raw_window_rect(hwnd);

  HWND owner = GetWindow(hwnd, GW_OWNER);
  HWND parent = GetParent(hwnd);
  state.has_owner = owner != nullptr;
  state.has_parent = parent != nullptr;
  state.is_child_or_owned = state.has_owner || state.has_parent;
  state.is_owned_dialog = should_ignore_owned_dialog_window(state.has_owner, state.class_name);

  for (const auto& proc : options.ignored_processes) {
    if (state.process_name == proc) {
      state.matches_ignored_process = true;
      break;
    }
  }

  for (const auto& ignored_title : options.ignored_window_titles) {
    if (state.title == ignored_title) {
      state.matches_ignored_title = true;
      break;
    }
  }

  for (const auto& pair : options.ignored_process_title_pairs) {
    if (iequals(state.process_name, pair.first) && iequals(state.title, pair.second)) {
      state.matches_ignored_process_title_pair = true;
      break;
    }
  }

  if (options.small_window_barrier.has_value() && state.rect.has_value()) {
    int width = static_cast<int>(state.rect->right - state.rect->left);
    int height = static_cast<int>(state.rect->bottom - state.rect->top);
    state.is_below_small_window_barrier = width < options.small_window_barrier->width ||
                                          height < options.small_window_barrier->height;
  }

  if (state.is_child_or_owned && !options.ignore_children_of_processes.empty()) {
    for (const auto& proc : options.ignore_children_of_processes) {
      if (iequals(state.process_name, proc)) {
        state.is_ignored_child_of_process = true;
        break;
      }
    }
  }

  state.is_maximized = is_window_maximized(state.handle);
  state.is_fullscreen = is_window_fullscreen(state.handle);
  state.monitor_index = get_monitor_index_for_window(hwnd, monitors);

  add_reason_if(state.reasons, !state.is_visible, "not visible");
  add_reason_if(state.reasons, state.is_cloaked, "cloaked");
  add_reason_if(state.reasons, !state.has_title, "empty title");
  add_reason_if(state.reasons, state.is_sys_drag_image, "SysDragImage window");
  add_reason_if(state.reasons, state.is_tooltip, "tooltip window");
  add_reason_if(state.reasons, state.is_tool_window, "WS_EX_TOOLWINDOW");
  add_reason_if(state.reasons, state.is_topmost, "WS_EX_TOPMOST");
  add_reason_if(state.reasons, state.is_transparent, "WS_EX_TRANSPARENT");
  add_reason_if(state.reasons, state.is_no_activate, "WS_EX_NOACTIVATE");
  add_reason_if(state.reasons, state.is_hung, "hung window");
  add_reason_if(state.reasons, state.process_name_missing, "missing process name");
  add_reason_if(state.reasons, state.matches_ignored_process, "ignored process");
  add_reason_if(state.reasons, state.matches_ignored_title, "ignored title");
  add_reason_if(state.reasons, state.matches_ignored_process_title_pair,
                "ignored process/title pair");
  add_reason_if(state.reasons, state.is_below_small_window_barrier, "below small window barrier");
  add_reason_if(state.reasons, state.is_owned_dialog, "owned #32770 dialog");
  add_reason_if(state.reasons, state.is_ignored_child_of_process, "ignored child/owned window");

  state.passes_filters = state.reasons.empty();
  if (!state.monitor_index.has_value()) {
    state.reasons.emplace_back("not on a known monitor");
  }
  state.is_currently_managed = state.passes_filters && state.monitor_index.has_value();

  return state;
}

BOOL CALLBACK DumpWindowEnumProc(HWND hwnd, LPARAM lParam) {
  auto* ctx = reinterpret_cast<DumpWindowEnumContext*>(lParam);
  ctx->windows->push_back(
      inspect_window_management_state(hwnd, *ctx->ignore_options, *ctx->monitors));
  return TRUE;
}

static std::vector<WindowManagementState>
gather_window_management_states(const wintiler::IgnoreOptions& ignore_options,
                                const std::vector<MonitorInfo>& monitors) {
  std::vector<WindowManagementState> windows;
  DumpWindowEnumContext ctx{&windows, &ignore_options, &monitors};
  EnumWindows(DumpWindowEnumProc, (LPARAM)&ctx);

  std::sort(windows.begin(), windows.end(),
            [](const WindowManagementState& lhs, const WindowManagementState& rhs) {
              return reinterpret_cast<uintptr_t>(lhs.handle) <
                     reinterpret_cast<uintptr_t>(rhs.handle);
            });

  return windows;
}

void log_windows_per_monitor(const wintiler::IgnoreOptions& ignore_options,
                             std::optional<size_t> monitor_index) {
  std::vector<MonitorInfo> monitors;
  fill_monitors(monitors);
  std::vector<HWND_T> handles;
  gather_raw_window_data_into(ignore_options, handles);

  if (monitor_index.has_value() && *monitor_index >= monitors.size()) {
    spdlog::error("Monitor index {} is out of bounds. Available monitors: 0-{}", *monitor_index,
                  monitors.size() - 1);
    return;
  }

  for (size_t i = 0; i < monitors.size(); ++i) {
    if (monitor_index.has_value() && i != *monitor_index) {
      continue;
    }

    const auto& monitor = monitors[i];
    spdlog::debug("Monitor {} (Handle: {})", i, monitor.handle);
    spdlog::debug("  Rect: [{}, {}, {}, {}]", monitor.rect.left, monitor.rect.top,
                  monitor.rect.right, monitor.rect.bottom);

    spdlog::debug("  Windows:");
    for (const auto& hwnd : handles) {
      HMONITOR winMonitor = MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONULL);
      if (winMonitor == (HMONITOR)monitor.handle) {
        auto win = get_window_info(hwnd);
        RECT rect;
        GetWindowRect((HWND)hwnd, &rect);
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        spdlog::debug("    Handle: {}, PID: {}, Process: {}, Title: {}", hwnd,
                      win.pid.has_value() ? std::to_string(win.pid.value()) : "N/A",
                      win.processName, win.title);
        spdlog::debug("      Position: ({}, {}), Size: {}x{}", rect.left, rect.top, width, height);
      }
    }
    spdlog::debug("--------------------------------------------------");
  }
}

void dump_window_management_state(const wintiler::IgnoreOptions& ignore_options) {
  std::vector<MonitorInfo> monitors;
  fill_monitors(monitors);
  auto windows = gather_window_management_states(ignore_options, monitors);

  spdlog::info("=== Window Management Dump ({} windows, {} monitors) ===", windows.size(),
               monitors.size());
  if (ignore_options.small_window_barrier.has_value()) {
    spdlog::info("Small window barrier: {}x{}", ignore_options.small_window_barrier->width,
                 ignore_options.small_window_barrier->height);
  } else {
    spdlog::info("Small window barrier: disabled");
  }

  for (size_t i = 0; i < windows.size(); ++i) {
    const auto& window = windows[i];
    auto rect_text = std::string("N/A");
    auto size_text = std::string("N/A");
    if (window.rect.has_value()) {
      rect_text = std::to_string(window.rect->left) + ", " + std::to_string(window.rect->top) +
                  ", " + std::to_string(window.rect->right) + ", " +
                  std::to_string(window.rect->bottom);
      size_text = std::to_string(window.rect->right - window.rect->left) + "x" +
                  std::to_string(window.rect->bottom - window.rect->top);
    }

    spdlog::info("[{}] hwnd={}, managed={}, passes_filters={}, monitor_index={}, reasons={}", i,
                 window.handle, window.is_currently_managed, window.passes_filters,
                 window.monitor_index.has_value() ? std::to_string(*window.monitor_index) : "N/A",
                 join_reasons(window.reasons));
    spdlog::info("     title=\"{}\"", window.title);
    spdlog::info("     class=\"{}\"", window.class_name);
    spdlog::info("     pid={}, process=\"{}\"",
                 window.pid.has_value() ? std::to_string(*window.pid) : "N/A", window.process_name);
    spdlog::info("     rect=[{}], size={}", rect_text, size_text);
    spdlog::info("     visible={}, cloaked={}, has_title={}, hung={}", window.is_visible,
                 window.is_cloaked, window.has_title, window.is_hung);
    spdlog::info("     ex_style=0x{:08X}, tool_window={}, topmost={}, transparent={}, "
                 "no_activate={}",
                 static_cast<unsigned long>(window.ex_style), window.is_tool_window,
                 window.is_topmost, window.is_transparent, window.is_no_activate);
    spdlog::info("     owner_present={}, parent_present={}, child_or_owned={}", window.has_owner,
                 window.has_parent, window.is_child_or_owned);
    spdlog::info("     ignored_process={}, ignored_title={}, ignored_process_title_pair={}, "
                 "ignored_child_of_process={}",
                 window.matches_ignored_process, window.matches_ignored_title,
                 window.matches_ignored_process_title_pair, window.is_ignored_child_of_process);
    spdlog::info("     small_window_barrier_hit={}, maximized={}, fullscreen={}",
                 window.is_below_small_window_barrier, window.is_maximized, window.is_fullscreen);
  }

  spdlog::info("=== End Window Management Dump ===");
}

void update_window_position(const TileInfo& tile_info) {
  HWND hwnd = (HWND)tile_info.handle;

  // Restore maximized or minimized windows to normal state before repositioning
  if (IsZoomed(hwnd) || IsIconic(hwnd)) {
    ShowWindow(hwnd, SW_RESTORE);
  }

  // Get DWM frame bounds to compensate for invisible borders
  RECT windowRect, frameRect;
  GetWindowRect(hwnd, &windowRect);
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect,
                                      sizeof(frameRect)))) {
    int borderLeft = frameRect.left - windowRect.left;
    int borderTop = frameRect.top - windowRect.top;
    int borderRight = windowRect.right - frameRect.right;
    int borderBottom = windowRect.bottom - frameRect.bottom;

    int targetX = tile_info.window_position.x - borderLeft;
    int targetY = tile_info.window_position.y - borderTop;
    int targetW = tile_info.window_position.width + borderLeft + borderRight;
    int targetH = tile_info.window_position.height + borderTop + borderBottom;

    // Skip if window is already at the correct position and size
    if (windowRect.left == targetX && windowRect.top == targetY &&
        (windowRect.right - windowRect.left) == targetW &&
        (windowRect.bottom - windowRect.top) == targetH) {
      return;
    }

    SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);

    auto actual_visible_rect = get_window_rect(tile_info.handle);
    if (actual_visible_rect.has_value() &&
        visible_size_differs_from_target(tile_info.window_position, *actual_visible_rect)) {
      SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
      actual_visible_rect = get_window_rect(tile_info.handle);
    }
    maybe_log_failed_placement_correction(tile_info, actual_visible_rect);
  } else {
    // Fallback if DWM query fails
    int targetX = tile_info.window_position.x;
    int targetY = tile_info.window_position.y;
    int targetW = tile_info.window_position.width;
    int targetH = tile_info.window_position.height;

    if (windowRect.left == targetX && windowRect.top == targetY &&
        (windowRect.right - windowRect.left) == targetW &&
        (windowRect.bottom - windowRect.top) == targetH) {
      return;
    }

    SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);

    auto actual_visible_rect = get_window_rect(tile_info.handle);
    if (actual_visible_rect.has_value() &&
        visible_size_differs_from_target(tile_info.window_position, *actual_visible_rect)) {
      SetWindowPos(hwnd, NULL, targetX, targetY, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
      actual_visible_rect = get_window_rect(tile_info.handle);
    }
    maybe_log_failed_placement_correction(tile_info, actual_visible_rect);
  }
}

std::vector<HWND_T> get_hwnds_for_monitor(size_t monitor_index,
                                          const wintiler::IgnoreOptions& ignore_options) {
  std::vector<HWND_T> hwnds;
  std::vector<MonitorInfo> monitors;
  fill_monitors(monitors);

  if (monitor_index >= monitors.size()) {
    return hwnds;
  }

  std::vector<HWND_T> handles;
  gather_raw_window_data_into(ignore_options, handles);
  const auto& monitor = monitors[monitor_index];

  for (const auto& hwnd : handles) {
    HMONITOR winMonitor = MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONULL);
    if (winMonitor == (HMONITOR)monitor.handle) {
      hwnds.push_back(hwnd);
    }
  }

  return hwnds;
}

WindowInfo get_window_info(HWND_T hwnd) {
  WindowInfo info;
  info.handle = hwnd;

  char title[256];
  if (GetWindowTextA((HWND)hwnd, title, sizeof(title)) > 0) {
    info.title = title;
  }

  info.pid = get_window_pid(hwnd);
  info.className = get_window_class_name_for_pid((HWND)hwnd, info.pid);
  if (info.pid.has_value()) {
    info.processName = get_process_name_from_pid(info.pid.value());
  }

  return info;
}

bool is_window_valid(HWND_T hwnd) {
  return hwnd != nullptr && IsWindow(reinterpret_cast<HWND>(hwnd));
}

std::optional<DWORD_T> get_window_process_id(HWND_T hwnd) {
  return get_window_pid(hwnd);
}

static bool window_chain_contains(HWND hwnd, HWND root, HWND (*next_window)(HWND)) {
  HWND current = next_window(hwnd);
  for (int depth = 0; current != nullptr && depth < 64; ++depth) {
    if (current == root) {
      return true;
    }
    current = next_window(current);
  }
  return false;
}

static HWND get_owner_window(HWND hwnd) {
  return GetWindow(hwnd, GW_OWNER);
}

static HWND get_parent_window(HWND hwnd) {
  return GetParent(hwnd);
}

bool is_window_or_owned_or_parented_by(HWND_T hwnd, HWND_T root) {
  if (!is_window_valid(hwnd) || !is_window_valid(root)) {
    return false;
  }

  HWND window = reinterpret_cast<HWND>(hwnd);
  HWND root_window = reinterpret_cast<HWND>(root);
  if (window == root_window) {
    return true;
  }

  return window_chain_contains(window, root_window, get_owner_window) ||
         window_chain_contains(window, root_window, get_parent_window);
}

std::optional<WindowPosition> get_window_rect(HWND_T hwnd) {
  if (hwnd == nullptr) {
    return std::nullopt;
  }

  HWND win = reinterpret_cast<HWND>(hwnd);
  if (!IsWindow(win)) {
    return std::nullopt;
  }

  // Use DWM frame bounds for accurate visible rect (excludes invisible borders)
  RECT frameRect;
  if (SUCCEEDED(
          DwmGetWindowAttribute(win, DWMWA_EXTENDED_FRAME_BOUNDS, &frameRect, sizeof(frameRect)))) {
    return WindowPosition{frameRect.left, frameRect.top, frameRect.right - frameRect.left,
                          frameRect.bottom - frameRect.top};
  }

  // Fallback to regular window rect
  RECT windowRect;
  if (!GetWindowRect(win, &windowRect)) {
    return std::nullopt;
  }

  return WindowPosition{windowRect.left, windowRect.top, windowRect.right - windowRect.left,
                        windowRect.bottom - windowRect.top};
}

static HWND_T get_foreground_window() {
  return reinterpret_cast<HWND_T>(GetForegroundWindow());
}

static std::optional<Point> get_cursor_pos() {
  POINT pt;
  SetLastError(ERROR_SUCCESS);
  if (GetCursorPos(&pt)) {
    return Point{pt.x, pt.y};
  }

  DWORD error = GetLastError();
  if (is_session_paused() || error == ERROR_ACCESS_DENIED) {
    spdlog::debug("GetCursorPos unavailable during inactive session/input desktop, error={}",
                  error);
  } else {
    spdlog::error("GetCursorPos failed, error={}", error);
  }
  return std::nullopt;
}

bool set_cursor_pos(long x, long y) {
  SetLastError(ERROR_SUCCESS);
  if (SetCursorPos(static_cast<int>(x), static_cast<int>(y)) == 0) {
    spdlog::debug("SetCursorPos failed, error={}", GetLastError());
    return false;
  }

  // Send synthetic mouse move event to properly notify applications
  // This fixes the "busy cursor" issue after programmatic cursor movement
  // Use MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK for multi-monitor support
  int virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
  int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  INPUT input = {};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  input.mi.dx = static_cast<LONG>(((x - virtualLeft) * 65535) / virtualWidth);
  input.mi.dy = static_cast<LONG>(((y - virtualTop) * 65535) / virtualHeight);
  SendInput(1, &input, sizeof(INPUT));

  return true;
}

bool set_foreground_window(HWND_T hwnd) {
  HWND targetHwnd = reinterpret_cast<HWND>(hwnd);
  HWND foregroundHwnd = GetForegroundWindow();

  if (foregroundHwnd == targetHwnd) {
    return true; // Already foreground
  }

  DWORD foregroundThreadId = GetWindowThreadProcessId(foregroundHwnd, nullptr);
  DWORD currentThreadId = GetCurrentThreadId();

  bool attached = false;
  if (foregroundThreadId != currentThreadId) {
    attached = AttachThreadInput(currentThreadId, foregroundThreadId, TRUE) != 0;
  }

  // Simulate a "null" keyboard event — a keypress with no actual key.
  // This satisfies condition: your process becomes the one that "received the last input event,"
  // which grants it permission to call SetForegroundWindow successfully.
  keybd_event(0, 0, 0, 0);
  bool result = SetForegroundWindow(targetHwnd) != 0;

  if (attached) {
    AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);
  }

  return result;
}

std::optional<HotKeyInfo> create_hotkey(const std::string& text, int id) {
  // Split by '+' and trim/lowercase each part
  std::vector<std::string> parts;
  std::string current;

  for (char c : text) {
    if (c == '+') {
      if (!current.empty()) {
        // Trim and lowercase
        std::string trimmed;
        for (char tc : current) {
          if (!std::isspace(static_cast<unsigned char>(tc))) {
            trimmed += static_cast<char>(std::tolower(static_cast<unsigned char>(tc)));
          }
        }
        if (!trimmed.empty()) {
          parts.push_back(trimmed);
        }
        current.clear();
      }
    } else {
      current += c;
    }
  }
  // Don't forget the last part
  if (!current.empty()) {
    std::string trimmed;
    for (char tc : current) {
      if (!std::isspace(static_cast<unsigned char>(tc))) {
        trimmed += static_cast<char>(std::tolower(static_cast<unsigned char>(tc)));
      }
    }
    if (!trimmed.empty()) {
      parts.push_back(trimmed);
    }
  }

  if (parts.empty()) {
    spdlog::error("create_hotkey: Empty hotkey text '{}'", text);
    return std::nullopt;
  }

  unsigned int modifiers = 0;
  const std::string& keyStr = parts.back();

  // Parse modifiers (all parts except the last)
  for (size_t i = 0; i < parts.size() - 1; ++i) {
    const std::string& part = parts[i];
    if (part == "alt") {
      modifiers |= MOD_ALT;
    } else if (part == "ctrl") {
      modifiers |= MOD_CONTROL;
    } else if (part == "shift") {
      modifiers |= MOD_SHIFT;
    } else if (part == "super") {
      modifiers |= MOD_WIN;
    } else {
      spdlog::error("create_hotkey: Unknown modifier '{}' in '{}'", part, text);
      return std::nullopt;
    }
  }

  // Parse key - support single character keys and special keys
  unsigned int key = 0;
  if (keyStr.length() == 1) {
    char c = keyStr[0];
    // Handle special characters that need OEM virtual key codes
    if (c == ';') {
      key = VK_OEM_1; // ;:
    } else if (c == ',') {
      key = VK_OEM_COMMA;
    } else if (c == '.') {
      key = VK_OEM_PERIOD;
    } else if (c == '/') {
      key = VK_OEM_2; // /?
    } else if (c == '[') {
      key = VK_OEM_4; // [{
    } else if (c == '\\') {
      key = VK_OEM_5; // \|
    } else if (c == ']') {
      key = VK_OEM_6; // ]}
    } else if (c == '\'') {
      key = VK_OEM_7; // '"
    } else if (c == '-') {
      key = VK_OEM_MINUS;
    } else if (c == '=') {
      key = VK_OEM_PLUS;
    } else if (c == '`') {
      key = VK_OEM_3; // `~
    } else {
      key = static_cast<unsigned int>(std::toupper(static_cast<unsigned char>(c)));
    }
  } else if (keyStr == "escape" || keyStr == "esc") {
    key = VK_ESCAPE;
  } else if (keyStr == "enter" || keyStr == "return") {
    key = VK_RETURN;
  } else if (keyStr == "space") {
    key = VK_SPACE;
  } else if (keyStr == "tab") {
    key = VK_TAB;
  } else if (keyStr == "backspace") {
    key = VK_BACK;
  } else if (keyStr == "delete") {
    key = VK_DELETE;
  } else if (keyStr == "insert") {
    key = VK_INSERT;
  } else if (keyStr == "home") {
    key = VK_HOME;
  } else if (keyStr == "end") {
    key = VK_END;
  } else if (keyStr == "pageup") {
    key = VK_PRIOR;
  } else if (keyStr == "pagedown") {
    key = VK_NEXT;
  } else if (keyStr == "left") {
    key = VK_LEFT;
  } else if (keyStr == "right") {
    key = VK_RIGHT;
  } else if (keyStr == "up") {
    key = VK_UP;
  } else if (keyStr == "down") {
    key = VK_DOWN;
  } else {
    spdlog::error("create_hotkey: Unknown key '{}'", keyStr);
    return std::nullopt;
  }

  return HotKeyInfo{id, modifiers, key};
}

std::string format_register_hotkey_failure(const HotKeyInfo& hotkey, std::string_view action_name,
                                           std::string_view shortcut, DWORD_T error) {
  std::ostringstream message;
  message << "register_hotkey: Failed to register hotkey action=" << action_name << ", shortcut='"
          << shortcut << "', id=" << hotkey.id << ", key=" << hotkey.key
          << ", modifiers=" << hotkey.modifiers << ", error=" << error;
  return message.str();
}

bool register_hotkey(const HotKeyInfo& hotkey, std::string_view action_name,
                     std::string_view shortcut) {
  BOOL result = RegisterHotKey(nullptr, hotkey.id, hotkey.modifiers, hotkey.key);
  if (result == 0) {
    spdlog::error("{}",
                  format_register_hotkey_failure(hotkey, action_name, shortcut, GetLastError()));
    return false;
  }
  return true;
}

bool unregister_hotkey(int id) {
  BOOL result = UnregisterHotKey(nullptr, id);
  return result != 0;
}

std::optional<int> check_keyboard_action() {
  MSG msg;
  if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (should_defer_message_to_hotkey_poll(msg.message)) {
      return static_cast<int>(msg.wParam);
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return std::nullopt;
}

bool should_defer_message_to_hotkey_poll(unsigned int message) {
  return message == WM_HOTKEY;
}

namespace {
bool peek_non_hotkey_message(MSG& msg) {
  if (PeekMessageW(&msg, nullptr, 0, WM_HOTKEY - 1, PM_REMOVE)) {
    return true;
  }

  return PeekMessageW(&msg, nullptr, WM_HOTKEY + 1, (std::numeric_limits<UINT>::max)(), PM_REMOVE);
}
} // namespace

void process_pending_non_hotkey_messages() {
  MSG msg;
  while (peek_non_hotkey_message(msg)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

bool wait_for_messages_or_timeout(unsigned long timeout_ms) {
  DWORD result =
      MsgWaitForMultipleObjectsEx(0,       // No handles to wait on
                                  nullptr, // No handle array
                                  timeout_ms, QS_HOTKEY | QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  return result == WAIT_OBJECT_0; // Messages available
}

// Window move/resize detection using SetWinEventHook
namespace {
std::atomic<bool> g_is_moving{false};
std::atomic<HWND> g_moving_hwnd{nullptr};
std::atomic<bool> g_move_just_ended{false};
HWINEVENTHOOK g_move_start_hook = nullptr;
HWINEVENTHOOK g_move_end_hook = nullptr;

void CALLBACK move_size_hook_proc(HWINEVENTHOOK /*hWinEventHook*/, DWORD event, HWND hwnd,
                                  LONG idObject, LONG idChild, DWORD /*idEventThread*/,
                                  DWORD /*dwmsEventTime*/) {
  // Only handle window events (OBJID_WINDOW == 0 and CHILDID_SELF == 0)
  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
    return;
  }

  if (event == EVENT_SYSTEM_MOVESIZESTART) {
    g_moving_hwnd = hwnd;
    g_is_moving = true;
    g_move_just_ended = false;
    spdlog::trace("Window move/resize started: hwnd={}", static_cast<void*>(hwnd));
  } else if (event == EVENT_SYSTEM_MOVESIZEEND) {
    g_is_moving = false;
    g_move_just_ended = true;
    spdlog::trace("Window move/resize ended: hwnd={}", static_cast<void*>(g_moving_hwnd.load()));
  }
}
} // namespace

void register_move_size_hook() {
  g_move_start_hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART,
                                      nullptr, move_size_hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);
  g_move_end_hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr,
                                    move_size_hook_proc, 0, 0, WINEVENT_OUTOFCONTEXT);

  if (g_move_start_hook == nullptr || g_move_end_hook == nullptr) {
    spdlog::error("Failed to register move/size hooks");
  } else {
    spdlog::info("Registered window move/size hooks");
  }
}

void unregister_move_size_hook() {
  if (g_move_start_hook != nullptr) {
    UnhookWinEvent(g_move_start_hook);
    g_move_start_hook = nullptr;
  }
  if (g_move_end_hook != nullptr) {
    UnhookWinEvent(g_move_end_hook);
    g_move_end_hook = nullptr;
  }
  spdlog::info("Unregistered window move/size hooks");
}

static bool is_any_window_being_moved() {
  return g_is_moving.load();
}

static std::optional<DragInfo> get_drag_info() {
  HWND hwnd = g_moving_hwnd.load();
  if (hwnd == nullptr) {
    return std::nullopt;
  }
  return DragInfo{reinterpret_cast<HWND_T>(hwnd), g_move_just_ended.load()};
}

void clear_drag_ended() {
  g_move_just_ended = false;
  g_moving_hwnd = nullptr;
}

// Session/Power notification handling
namespace {
// GUID for display power state notifications
// {6FE69556-704A-47A0-8F24-C28D936FDA47}
static const GUID GUID_CONSOLE_DISPLAY_STATE_LOCAL = {
    0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

// Hidden window for receiving notifications
constexpr int NO_NOTIFICATION_AREA_HOTKEY_ACTION = -1;
HWND g_notification_hwnd = nullptr;
HPOWERNOTIFY g_power_notify_handle = nullptr;
NOTIFYICONDATAW g_notification_area_icon = {};
bool g_notification_area_icon_added = false;
NotificationAreaIconOptions g_notification_area_icon_options;
std::atomic<bool> g_notification_area_exit_requested{false};
std::atomic<int> g_notification_area_hotkey_action_requested{NO_NOTIFICATION_AREA_HOTKEY_ACTION};
std::atomic<bool> g_notification_area_manual_pause_active{false};
std::atomic<bool> g_notification_area_verbose_logging_active{false};
UINT g_taskbar_created_message = 0;

// Pause state flags (atomic for thread safety)
std::atomic<bool> g_session_locked{false};
std::atomic<bool> g_system_suspended{false};
std::atomic<bool> g_display_off{false};

// Track if we've received initial display state (to avoid spurious "resuming" on startup)
std::atomic<bool> g_display_state_initialized{false};

// Event for blocking wait
HANDLE g_resume_event = nullptr;

const wchar_t* NOTIFICATION_WINDOW_CLASS = L"WinTilerNotificationWindow";
constexpr UINT WM_WINTILER_NOTIFICATION_ICON = WM_APP + 1;
constexpr UINT_PTR NOTIFICATION_AREA_ICON_ID = 1;
constexpr UINT ID_OPEN_CONFIG = 1001;
constexpr UINT ID_SHOW_LOG = 1002;
constexpr UINT ID_TOGGLE_PAUSE = 1003;
constexpr UINT ID_RESET = 1004;
constexpr UINT ID_ABOUT = 1005;
constexpr UINT ID_INSTALLER = 1006;
constexpr UINT ID_EXIT = 1007;
constexpr UINT ID_TOGGLE_VERBOSE_LOGGING = 1008;
constexpr const wchar_t* GITHUB_REPOSITORY_URL = L"https://github.com/tuncb/win-tiler";
const GUID NOTIFICATION_AREA_ICON_GUID = {
    0x7b3f9c9c, 0xbdc7, 0x4e83, {0x90, 0xcb, 0xef, 0x64, 0x53, 0x6d, 0xae, 0xdb}};

bool is_non_empty_path(const std::optional<std::filesystem::path>& path) {
  return path.has_value() && !path->empty();
}

std::string narrow_ascii(std::wstring_view text) {
  std::string result;
  result.reserve(text.size());
  for (wchar_t character : text) {
    result.push_back(static_cast<char>(character));
  }
  return result;
}

bool shell_open_path(HWND owner, const std::filesystem::path& path, const char* label) {
  std::wstring wide_path = path.wstring();
  HINSTANCE result =
      ShellExecuteW(owner, L"open", wide_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    spdlog::error("Failed to open {} '{}', ShellExecute result={}", label, path.string(),
                  reinterpret_cast<INT_PTR>(result));
    return false;
  }

  spdlog::info("Opened {}: {}", label, path.string());
  return true;
}

bool shell_open_url(HWND owner, std::wstring_view url, const char* label) {
  std::wstring wide_url(url);
  HINSTANCE result =
      ShellExecuteW(owner, L"open", wide_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  std::string narrow_url = narrow_ascii(wide_url);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    spdlog::error("Failed to open {} '{}', ShellExecute result={}", label, narrow_url,
                  reinterpret_cast<INT_PTR>(result));
    return false;
  }

  spdlog::info("Opened {}: {}", label, narrow_url);
  return true;
}

std::filesystem::path get_module_file_path() {
  std::vector<wchar_t> buffer(MAX_PATH, L'\0');
  while (true) {
    DWORD path_length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (path_length == 0) {
      return {};
    }
    if (path_length < buffer.size()) {
      return std::filesystem::path(std::wstring(buffer.data(), path_length));
    }
    buffer.resize(buffer.size() * 2, L'\0');
  }
}

bool append_notification_menu_item(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text) {
  if (AppendMenuW(menu, flags, id, text) == 0) {
    spdlog::error("Failed to append notification area menu item, error={}", GetLastError());
    return false;
  }
  return true;
}

HRESULT CALLBACK notification_area_about_callback(HWND hwnd, UINT notification, WPARAM,
                                                  LPARAM lParam, LONG_PTR) {
  if (notification != TDN_HYPERLINK_CLICKED) {
    return S_OK;
  }

  const auto* url = reinterpret_cast<const wchar_t*>(lParam);
  if (url == nullptr) {
    return E_INVALIDARG;
  }

  if (!shell_open_url(hwnd, url, "GitHub repository")) {
    return E_FAIL;
  }
  return S_OK;
}

void show_notification_area_about_dialog(HWND hwnd) {
  const std::wstring content = get_notification_area_about_dialog_content();
  TASKDIALOGCONFIG config = {};
  config.cbSize = sizeof(config);
  config.hwndParent = hwnd;
  config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_SIZE_TO_CONTENT;
  config.dwCommonButtons = TDCBF_OK_BUTTON;
  config.pszWindowTitle = L"About win-tiler";
  config.pszMainIcon = TD_INFORMATION_ICON;
  config.pszContent = content.c_str();
  config.pfCallback = notification_area_about_callback;

  HRESULT result = TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
  if (FAILED(result)) {
    spdlog::error("Failed to show About dialog, HRESULT={}", static_cast<long>(result));
    MessageBoxW(hwnd, get_notification_area_about_message().c_str(), L"About win-tiler",
                MB_OK | MB_ICONINFORMATION);
  }
}

void handle_notification_menu_command(HWND hwnd, UINT command) {
  switch (command) {
  case ID_OPEN_CONFIG:
    if (g_notification_area_icon_options.config_path.has_value()) {
      shell_open_path(hwnd, *g_notification_area_icon_options.config_path, "config file");
    }
    return;

  case ID_SHOW_LOG:
    if (g_notification_area_icon_options.log_file_path.has_value()) {
      shell_open_path(hwnd, *g_notification_area_icon_options.log_file_path, "log file");
    }
    return;

  case ID_TOGGLE_PAUSE:
    request_notification_area_hotkey_action(wintiler::HotkeyAction::TogglePause);
    spdlog::info("Toggle pause requested from notification area menu");
    return;

  case ID_RESET:
    request_notification_area_hotkey_action(wintiler::HotkeyAction::RestartSystem);
    spdlog::info("Reset requested from notification area menu");
    return;

  case ID_TOGGLE_VERBOSE_LOGGING:
    request_notification_area_hotkey_action(wintiler::HotkeyAction::ToggleVerboseLogging);
    spdlog::info("Verbose logging toggle requested from notification area menu");
    return;

  case ID_ABOUT:
    show_notification_area_about_dialog(hwnd);
    return;

  case ID_INSTALLER: {
    auto executable_path = get_module_file_path();
    if (executable_path.empty()) {
      spdlog::error("Failed to determine executable path for installer dialog");
      return;
    }

    auto dialog_result = wintiler::show_installer_dialog(hwnd, executable_path);
    if (!dialog_result.has_value()) {
      spdlog::error("{}", dialog_result.error());
      return;
    }
    if (*dialog_result == wintiler::InstallerDialogResult::UninstallStarted ||
        *dialog_result == wintiler::InstallerDialogResult::UpdateStarted) {
      g_notification_area_exit_requested = true;
      spdlog::info("Exit requested after installer helper was started");
    }
    return;
  }

  case ID_EXIT:
    g_notification_area_exit_requested = true;
    spdlog::info("Exit requested from notification area menu");
    return;

  default:
    return;
  }
}

void show_notification_area_menu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    spdlog::error("Failed to create notification area menu, error={}", GetLastError());
    return;
  }

  auto availability = get_notification_area_menu_availability(g_notification_area_icon_options);
  UINT open_config_flags = MF_STRING;
  if (!availability.can_open_config) {
    open_config_flags |= MF_GRAYED;
  }
  UINT show_log_flags = MF_STRING;
  if (!availability.can_show_log) {
    show_log_flags |= MF_GRAYED;
  }

  bool menu_ok =
      append_notification_menu_item(menu, open_config_flags, ID_OPEN_CONFIG, L"Open config file");
  menu_ok =
      append_notification_menu_item(menu, show_log_flags, ID_SHOW_LOG, L"Open log file") && menu_ok;
  menu_ok = append_notification_menu_item(menu, MF_SEPARATOR, 0, nullptr) && menu_ok;
  const wchar_t* toggle_pause_text =
      get_notification_area_toggle_pause_menu_text(g_notification_area_manual_pause_active.load());
  menu_ok =
      append_notification_menu_item(menu, MF_STRING, ID_TOGGLE_PAUSE, toggle_pause_text) && menu_ok;
  menu_ok = append_notification_menu_item(menu, MF_STRING, ID_RESET, L"Reset") && menu_ok;
  UINT verbose_logging_flags =
      MF_STRING | (g_notification_area_verbose_logging_active.load() ? MF_CHECKED : MF_UNCHECKED);
  menu_ok = append_notification_menu_item(menu, verbose_logging_flags, ID_TOGGLE_VERBOSE_LOGGING,
                                          L"Verbose logging") &&
            menu_ok;
  menu_ok = append_notification_menu_item(menu, MF_SEPARATOR, 0, nullptr) && menu_ok;
  menu_ok = append_notification_menu_item(menu, MF_STRING, ID_INSTALLER, L"Install...") && menu_ok;
  menu_ok = append_notification_menu_item(menu, MF_STRING, ID_ABOUT, L"About...") && menu_ok;
  menu_ok = append_notification_menu_item(menu, MF_SEPARATOR, 0, nullptr) && menu_ok;
  menu_ok = append_notification_menu_item(menu, MF_STRING, ID_EXIT, L"Exit") && menu_ok;
  if (!menu_ok) {
    if (DestroyMenu(menu) == 0) {
      spdlog::error("Failed to destroy notification area menu, error={}", GetLastError());
    }
    return;
  }

  POINT cursor_pos = {};
  if (GetCursorPos(&cursor_pos) == 0) {
    spdlog::error("Failed to get cursor position for notification area menu, error={}",
                  GetLastError());
    if (DestroyMenu(menu) == 0) {
      spdlog::error("Failed to destroy notification area menu, error={}", GetLastError());
    }
    return;
  }

  if (SetForegroundWindow(hwnd) == 0) {
    spdlog::debug("Failed to foreground notification window before menu, error={}", GetLastError());
  }

  UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, cursor_pos.x,
                                cursor_pos.y, 0, hwnd, nullptr);
  if (command != 0) {
    handle_notification_menu_command(hwnd, command);
  }

  if (PostMessageW(hwnd, WM_NULL, 0, 0) == 0) {
    spdlog::debug("Failed to post notification menu cleanup message, error={}", GetLastError());
  }

  if (DestroyMenu(menu) == 0) {
    spdlog::error("Failed to destroy notification area menu, error={}", GetLastError());
  }
}

bool should_show_notification_area_menu(LPARAM lParam) {
  UINT message = LOWORD(lParam);
  return message == WM_CONTEXTMENU || message == WM_RBUTTONUP || message == NIN_SELECT ||
         message == NIN_KEYSELECT;
}

bool add_notification_area_icon() {
  if (g_notification_hwnd == nullptr) {
    spdlog::error("Cannot add notification area icon without a notification window");
    return false;
  }

  g_notification_area_icon = {};
  g_notification_area_icon.cbSize = sizeof(g_notification_area_icon);
  g_notification_area_icon.hWnd = g_notification_hwnd;
  g_notification_area_icon.uID = NOTIFICATION_AREA_ICON_ID;
  g_notification_area_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID;
  g_notification_area_icon.uCallbackMessage = WM_WINTILER_NOTIFICATION_ICON;
  g_notification_area_icon.hIcon =
      LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
  if (g_notification_area_icon.hIcon == nullptr) {
    spdlog::error("Failed to load notification area icon resource, error={}", GetLastError());
    return false;
  }
  g_notification_area_icon.guidItem = NOTIFICATION_AREA_ICON_GUID;
  wcscpy_s(g_notification_area_icon.szTip, L"win-tiler");

  if (Shell_NotifyIconW(NIM_ADD, &g_notification_area_icon) == 0) {
    spdlog::error("Failed to add notification area icon, error={}", GetLastError());
    return false;
  }

  g_notification_area_icon.uVersion = NOTIFYICON_VERSION_4;
  if (Shell_NotifyIconW(NIM_SETVERSION, &g_notification_area_icon) == 0) {
    spdlog::error("Failed to set notification area icon version, error={}", GetLastError());
    if (Shell_NotifyIconW(NIM_DELETE, &g_notification_area_icon) == 0) {
      spdlog::error("Failed to remove notification area icon after version failure, error={}",
                    GetLastError());
    }
    return false;
  }

  g_notification_area_icon_added = true;
  spdlog::info("Added notification area icon");
  return true;
}

void readd_notification_area_icon_after_taskbar_restart() {
  if (!g_notification_area_icon_added) {
    return;
  }

  g_notification_area_icon_added = false;
  if (!add_notification_area_icon()) {
    spdlog::error("Failed to re-add notification area icon after taskbar restart");
  }
}

LRESULT CALLBACK notification_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (g_taskbar_created_message != 0 && msg == g_taskbar_created_message) {
    readd_notification_area_icon_after_taskbar_restart();
    return 0;
  }

  if (should_invalidate_monitor_cache_for_message(msg)) {
    invalidate_monitor_cache();
    spdlog::info("Display or work-area configuration changed - monitor cache invalidated");
    return 0;
  }

  switch (msg) {
  case WM_WINTILER_NOTIFICATION_ICON:
    if (should_show_notification_area_menu(lParam)) {
      show_notification_area_menu(hwnd);
    }
    return 0;

  case WM_COMMAND:
    handle_notification_menu_command(hwnd, LOWORD(wParam));
    return 0;

  case WM_WTSSESSION_CHANGE:
    if (wParam == WTS_SESSION_LOCK) {
      g_session_locked = true;
      if (g_resume_event) {
        ResetEvent(g_resume_event);
      }
      spdlog::info("Session locked - pausing");
    } else if (wParam == WTS_SESSION_UNLOCK) {
      g_session_locked = false;
      if (g_resume_event && !g_system_suspended && !g_display_off) {
        SetEvent(g_resume_event);
      }
      spdlog::info("Session unlocked - resuming");
    }
    return 0;

  case WM_POWERBROADCAST:
    if (wParam == PBT_APMSUSPEND) {
      g_system_suspended = true;
      if (g_resume_event) {
        ResetEvent(g_resume_event);
      }
      spdlog::info("System suspending - pausing");
    } else if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC) {
      g_system_suspended = false;
      if (g_resume_event && !g_session_locked && !g_display_off) {
        SetEvent(g_resume_event);
      }
      spdlog::info("System resumed - resuming");
    } else if (wParam == PBT_POWERSETTINGCHANGE) {
      auto* setting = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
      if (setting && IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE_LOCAL)) {
        DWORD state = *reinterpret_cast<DWORD*>(setting->Data);
        bool was_initialized = g_display_state_initialized.exchange(true);
        if (state == 0) { // Display off
          g_display_off = true;
          if (g_resume_event) {
            ResetEvent(g_resume_event);
          }
          spdlog::info("Display off - pausing");
        } else { // Display on or dimmed
          g_display_off = false;
          if (g_resume_event && !g_session_locked && !g_system_suspended) {
            SetEvent(g_resume_event);
          }
          // Only log if this isn't the initial state notification on startup
          if (was_initialized) {
            spdlog::info("Display on - resuming");
          }
        }
      }
    }
    return TRUE; // Must return TRUE for power messages

  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

bool create_notification_window() {
  if (g_taskbar_created_message == 0) {
    g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
    if (g_taskbar_created_message == 0) {
      spdlog::error("Failed to register TaskbarCreated message, error={}", GetLastError());
    }
  }

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = notification_wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
  wc.hIconSm = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
  wc.lpszClassName = NOTIFICATION_WINDOW_CLASS;

  if (!RegisterClassExW(&wc)) {
    // Class may already be registered
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      spdlog::error("Failed to register notification window class, error={}", GetLastError());
      return false;
    }
  }

  g_notification_hwnd =
      CreateWindowExW(0, NOTIFICATION_WINDOW_CLASS, L"WinTiler Notifications", 0, 0, 0, 0, 0,
                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

  if (!g_notification_hwnd) {
    spdlog::error("Failed to create notification window, error={}", GetLastError());
    return false;
  }

  return true;
}
} // namespace

NotificationAreaMenuAvailability
get_notification_area_menu_availability(const NotificationAreaIconOptions& options) {
  return {is_non_empty_path(options.config_path), is_non_empty_path(options.log_file_path), true};
}

static std::wstring get_notification_area_about_version_line() {
  const std::string version = wintiler::get_version_string();
  std::wstring version_line = L"Win-tiler version ";
  version_line.append(version.begin(), version.end());
  return version_line;
}

std::wstring get_notification_area_about_message() {
  std::wstring message = get_notification_area_about_version_line();
  message.append(L"\n");
  message.append(GITHUB_REPOSITORY_URL);
  return message;
}

std::wstring get_notification_area_about_dialog_content() {
  std::wstring content = get_notification_area_about_version_line();
  content.append(L"\n<a href=\"");
  content.append(GITHUB_REPOSITORY_URL);
  content.append(L"\">");
  content.append(GITHUB_REPOSITORY_URL);
  content.append(L"</a>");
  return content;
}

const wchar_t* get_notification_area_toggle_pause_menu_text(bool is_paused) {
  return is_paused ? L"Unpause" : L"Pause";
}

void register_session_power_notifications() {
  // Create event for blocking wait (manual reset, initially signaled)
  g_resume_event = CreateEventW(nullptr, TRUE, TRUE, nullptr);
  if (!g_resume_event) {
    spdlog::error("Failed to create resume event, error={}", GetLastError());
    return;
  }

  // Create hidden window for notifications
  if (!create_notification_window()) {
    spdlog::error("Failed to create notification window");
    CloseHandle(g_resume_event);
    g_resume_event = nullptr;
    return;
  }

  // Register for session notifications (lock/unlock)
  if (!WTSRegisterSessionNotification(g_notification_hwnd, NOTIFY_FOR_THIS_SESSION)) {
    spdlog::error("Failed to register session notifications, error={}", GetLastError());
  }

  // Register for display power state changes
  g_power_notify_handle = RegisterPowerSettingNotification(
      g_notification_hwnd, &GUID_CONSOLE_DISPLAY_STATE_LOCAL, DEVICE_NOTIFY_WINDOW_HANDLE);
  if (!g_power_notify_handle) {
    spdlog::error("Failed to register power setting notification, error={}", GetLastError());
  }

  spdlog::info("Registered session/power notifications");
}

void register_notification_area_icon(const NotificationAreaIconOptions& options) {
  g_notification_area_icon_options = options;
  if (g_notification_area_icon_added) {
    if (Shell_NotifyIconW(NIM_DELETE, &g_notification_area_icon) == 0) {
      spdlog::error("Failed to replace notification area icon, error={}", GetLastError());
    }
    g_notification_area_icon_added = false;
  }

  if (!add_notification_area_icon()) {
    spdlog::error("Failed to register notification area icon");
  }
}

void unregister_notification_area_icon() {
  if (!g_notification_area_icon_added) {
    return;
  }

  if (Shell_NotifyIconW(NIM_DELETE, &g_notification_area_icon) == 0) {
    spdlog::error("Failed to remove notification area icon, error={}", GetLastError());
  }
  g_notification_area_icon_added = false;
  g_notification_area_icon = {};
  spdlog::info("Removed notification area icon");
}

void set_notification_area_manual_pause_active(bool is_paused) {
  g_notification_area_manual_pause_active = is_paused;
}

void set_notification_area_verbose_logging_active(bool is_enabled) {
  g_notification_area_verbose_logging_active = is_enabled;
}

void request_notification_area_hotkey_action(wintiler::HotkeyAction action) {
  g_notification_area_hotkey_action_requested = static_cast<int>(action);
}

bool consume_notification_area_exit_requested() {
  return g_notification_area_exit_requested.exchange(false);
}

std::optional<wintiler::HotkeyAction> consume_notification_area_hotkey_action() {
  int requested =
      g_notification_area_hotkey_action_requested.exchange(NO_NOTIFICATION_AREA_HOTKEY_ACTION);
  if (requested == NO_NOTIFICATION_AREA_HOTKEY_ACTION) {
    return std::nullopt;
  }

  switch (static_cast<wintiler::HotkeyAction>(requested)) {
  case wintiler::HotkeyAction::NavigateLeft:
  case wintiler::HotkeyAction::NavigateDown:
  case wintiler::HotkeyAction::NavigateUp:
  case wintiler::HotkeyAction::NavigateRight:
  case wintiler::HotkeyAction::ToggleSplit:
  case wintiler::HotkeyAction::Exit:
  case wintiler::HotkeyAction::CycleSplitMode:
  case wintiler::HotkeyAction::StoreCell:
  case wintiler::HotkeyAction::ClearStored:
  case wintiler::HotkeyAction::Exchange:
  case wintiler::HotkeyAction::Move:
  case wintiler::HotkeyAction::SplitIncrease:
  case wintiler::HotkeyAction::SplitDecrease:
  case wintiler::HotkeyAction::ExchangeSiblings:
  case wintiler::HotkeyAction::ToggleZen:
  case wintiler::HotkeyAction::ResetSplitRatio:
  case wintiler::HotkeyAction::TogglePause:
  case wintiler::HotkeyAction::DumpWindowManagement:
  case wintiler::HotkeyAction::RestartSystem:
  case wintiler::HotkeyAction::ToggleFloating:
  case wintiler::HotkeyAction::ToggleVerboseLogging:
    return static_cast<wintiler::HotkeyAction>(requested);
  }

  spdlog::error("Invalid notification area hotkey action request: {}", requested);
  return std::nullopt;
}

std::optional<DWORD_T> find_notification_area_process_id() {
  HWND hwnd = FindWindowW(NOTIFICATION_WINDOW_CLASS, nullptr);
  if (hwnd == nullptr) {
    return std::nullopt;
  }

  DWORD process_id = 0;
  GetWindowThreadProcessId(hwnd, &process_id);
  if (process_id == 0) {
    return std::nullopt;
  }

  return process_id;
}

bool request_notification_area_exit_for_running_instance() {
  HWND hwnd = FindWindowW(NOTIFICATION_WINDOW_CLASS, nullptr);
  if (hwnd == nullptr) {
    return false;
  }

  if (PostMessageW(hwnd, WM_COMMAND, ID_EXIT, 0) == 0) {
    spdlog::error("Failed to request notification area instance exit, error={}", GetLastError());
    return false;
  }

  return true;
}

void unregister_session_power_notifications() {
  unregister_notification_area_icon();

  if (g_notification_hwnd) {
    WTSUnRegisterSessionNotification(g_notification_hwnd);

    if (g_power_notify_handle) {
      UnregisterPowerSettingNotification(g_power_notify_handle);
      g_power_notify_handle = nullptr;
    }

    DestroyWindow(g_notification_hwnd);
    g_notification_hwnd = nullptr;
  }

  if (g_resume_event) {
    CloseHandle(g_resume_event);
    g_resume_event = nullptr;
  }

  // Reset state
  g_session_locked = false;
  g_system_suspended = false;
  g_display_off = false;
  g_display_state_initialized = false;

  spdlog::info("Unregistered session/power notifications");
}

void wait_for_session_active() {
  if (!g_resume_event) {
    return;
  }

  // Process messages while waiting to ensure notifications are received
  while (g_session_locked || g_system_suspended || g_display_off) {
    DWORD result = MsgWaitForMultipleObjects(1, &g_resume_event, FALSE, INFINITE, QS_ALLINPUT);

    if (result == WAIT_OBJECT_0) {
      // Event signaled - session is active
      break;
    } else if (result == WAIT_OBJECT_0 + 1) {
      // Messages available - process them
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
  }
}

bool is_session_paused() {
  return g_session_locked || g_system_suspended || g_display_off;
}

// ============================================================================
// Virtual Desktop Detection
// ============================================================================
// Uses IVirtualDesktopManager COM interface to get desktop IDs from windows.
// The desktop ID is returned in LoopInputState::desktop_id for comparison
// in the main loop.

// Global state for virtual desktop detection
static IVirtualDesktopManager* g_vdm = nullptr;
static bool g_com_initialized = false;

void register_virtual_desktop_notifications() {
  // Initialize COM on this thread
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    spdlog::error("Failed to initialize COM for virtual desktop detection, hr=0x{:08X}", hr);
    return;
  }
  g_com_initialized = true;

  // Create IVirtualDesktopManager instance
  hr = CoCreateInstance(__uuidof(VirtualDesktopManager), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&g_vdm));
  if (FAILED(hr) || !g_vdm) {
    spdlog::error("Failed to create VirtualDesktopManager, hr=0x{:08X}", hr);
    if (g_com_initialized) {
      CoUninitialize();
      g_com_initialized = false;
    }
    return;
  }

  spdlog::debug("Initialized virtual desktop manager");
}

void unregister_virtual_desktop_notifications() {
  if (g_vdm) {
    g_vdm->Release();
    g_vdm = nullptr;
  }

  if (g_com_initialized) {
    CoUninitialize();
    g_com_initialized = false;
  }

  spdlog::debug("Unregistered virtual desktop notifications");
}

static bool is_ctrl_pressed() {
  return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
}

static bool is_right_mouse_pressed() {
  return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

static bool is_window_fullscreen(HWND_T hwnd) {
  if (hwnd == nullptr) {
    return false;
  }

  HWND win = reinterpret_cast<HWND>(hwnd);

  // Skip invisible windows
  if (!IsWindowVisible(win)) {
    return false;
  }

  // Get the monitor this window is on
  HMONITOR monitor = MonitorFromWindow(win, MONITOR_DEFAULTTONULL);
  if (monitor == nullptr) {
    return false;
  }

  // Get monitor info
  MONITORINFO mi;
  mi.cbSize = sizeof(MONITORINFO);
  if (!GetMonitorInfoA(monitor, &mi)) {
    return false;
  }

  // Get window rect
  RECT windowRect;
  if (!GetWindowRect(win, &windowRect)) {
    return false;
  }

  // Check if window covers the entire monitor (use full rect, not work area)
  return windowRect.left <= mi.rcMonitor.left && windowRect.top <= mi.rcMonitor.top &&
         windowRect.right >= mi.rcMonitor.right && windowRect.bottom >= mi.rcMonitor.bottom;
}

void gather_loop_input_state_into(const wintiler::IgnoreOptions& ignore_options,
                                  LoopInputState& state, std::vector<HWND_T>& all_handles) {
  // Gather monitor and window data
  fill_monitors(state.monitors);
  state.windows_per_monitor.resize(state.monitors.size());

  gather_raw_window_data_into(ignore_options, all_handles);

  for (size_t i = 0; i < state.monitors.size(); ++i) {
    const auto& monitor = state.monitors[i];
    auto& monitor_windows = state.windows_per_monitor[i];
    monitor_windows.clear();

    for (const auto& hwnd : all_handles) {
      HMONITOR winMonitor = MonitorFromWindow((HWND)hwnd, MONITOR_DEFAULTTONULL);
      if (winMonitor == (HMONITOR)monitor.handle) {
        ManagedWindowInfo managed_info;
        managed_info.handle = hwnd;
        managed_info.is_fullscreen = is_window_fullscreen(hwnd);
        managed_info.is_maximized = is_window_maximized(hwnd);
        managed_info.is_minimized = is_window_minimized(hwnd);
        managed_info.actual_rect = get_window_rect(hwnd);
        monitor_windows.push_back(managed_info);
      }
    }
  }

  // Gather input state
  state.is_any_window_being_moved = is_any_window_being_moved();
  state.drag_info = get_drag_info();
  state.cursor_pos = get_cursor_pos();
  state.is_ctrl_pressed = is_ctrl_pressed();
  state.is_right_mouse_pressed = is_right_mouse_pressed();
  state.foreground_window = get_foreground_window();
  state.desktop_id.reset();

  // Get desktop ID from first managed window
  if (g_vdm && !all_handles.empty()) {
    GUID desktop_guid;
    HRESULT hr = g_vdm->GetWindowDesktopId((HWND)all_handles[0], &desktop_guid);
    if (SUCCEEDED(hr)) {
      // Format GUID as string: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
      char guid_str[64];
      snprintf(guid_str, sizeof(guid_str), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
               desktop_guid.Data1, desktop_guid.Data2, desktop_guid.Data3, desktop_guid.Data4[0],
               desktop_guid.Data4[1], desktop_guid.Data4[2], desktop_guid.Data4[3],
               desktop_guid.Data4[4], desktop_guid.Data4[5], desktop_guid.Data4[6],
               desktop_guid.Data4[7]);
      state.desktop_id = guid_str;
    }
  }
}

} // namespace winapi
